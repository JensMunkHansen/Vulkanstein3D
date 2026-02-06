#version 450

// PBR shader with Cook-Torrance BRDF
// References:
// - https://learnopengl.com/PBR/Theory
// - https://learnopengl.com/PBR/Lighting
// - https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#metallic-roughness-material

// Light and camera uniforms (std140 layout)
// Must match C++ UniformBufferObject struct exactly
layout(set = 0, binding = 0) uniform UniformBufferObject {
  mat4 view;
  mat4 proj;
  mat4 viewInverse;     // RT: camera transform (not used in raster)
  mat4 projInverse;     // RT: unproject rays (not used in raster)
  vec4 lightPosition;   // xyz = position/direction, w = 0 directional, 1 point
  vec4 lightColor;      // rgb = color, a = intensity
  vec4 lightAmbient;    // rgb = ambient color
  vec4 viewPos;         // xyz = camera position
  vec4 material;        // x = shininess, y = specStrength, z = metallicAmbient, w = aoStrength
  vec4 flags;           // x = useNormalMap, y = useEmissive, z = useAO, w = exposure
  vec4 ibl_params;      // x = useIBL, y = iblIntensity, z = reserved, w = reserved
} ubo;

// Textures
layout(set = 0, binding = 1) uniform sampler2D baseColorTexture;
layout(set = 0, binding = 2) uniform sampler2D normalTexture;
layout(set = 0, binding = 3) uniform sampler2D metallicRoughnessTexture;  // G=roughness, B=metallic (glTF spec)
layout(set = 0, binding = 4) uniform sampler2D emissiveTexture;           // RGB emissive color
layout(set = 0, binding = 5) uniform sampler2D aoTexture;                 // R=ambient occlusion (glTF spec)

// IBL textures
layout(set = 0, binding = 6) uniform sampler2D brdfLUT;           // BRDF integration LUT
layout(set = 0, binding = 7) uniform samplerCube irradianceMap;   // Diffuse IBL
layout(set = 0, binding = 8) uniform samplerCube prefilterMap;    // Specular IBL (mips = roughness)

// Push constant for per-draw material properties
layout(push_constant) uniform PushConstants {
  mat4 model;            // 64 bytes (vertex stage)
  vec4 baseColorFactor;  // 16 bytes
  float alphaCutoff;     //  4 bytes
  uint alphaMode;        //  4 bytes  0=OPAQUE, 1=MASK, 2=BLEND
} pc;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec2 fragTexCoord;
layout(location = 4) in mat3 fragTBN;  // Tangent-Bitangent-Normal matrix

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;
const float GAMMA = 2.2;
const float INV_GAMMA = 1.0 / GAMMA;

// sRGB to linear conversion
// Reference: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_color_space
vec3 sRGBToLinear(vec3 srgb)
{
  return pow(srgb, vec3(GAMMA));
}

// Linear to sRGB
vec3 linearToSRGB(vec3 color)
{
  return pow(color, vec3(INV_GAMMA));
}

// ============================================================================
// Tone Mapping Functions (from Khronos glTF-Sample-Viewer)
// ============================================================================

// Reinhard tone mapping (simple)
vec3 toneMapReinhard(vec3 color)
{
  return color / (color + vec3(1.0));
}

// ACES tone map (faster approximation)
// see: https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
vec3 toneMapACES_Narkowicz(vec3 color)
{
  const float A = 2.51;
  const float B = 0.03;
  const float C = 2.43;
  const float D = 0.59;
  const float E = 0.14;
  return clamp((color * (A * color + B)) / (color * (C * color + D) + E), 0.0, 1.0);
}

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
const mat3 ACESInputMat = mat3(
  0.59719, 0.07600, 0.02840,
  0.35458, 0.90834, 0.13383,
  0.04823, 0.01566, 0.83777
);

// ODT_SAT => XYZ => D60_2_D65 => sRGB
const mat3 ACESOutputMat = mat3(
   1.60475, -0.10208, -0.00327,
  -0.53108,  1.10813, -0.07276,
  -0.07367, -0.00605,  1.07602
);

// ACES filmic tone map approximation (more accurate)
// see https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
vec3 RRTAndODTFit(vec3 color)
{
  vec3 a = color * (color + 0.0245786) - 0.000090537;
  vec3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
  return a / b;
}

vec3 toneMapACES_Hill(vec3 color)
{
  color = ACESInputMat * color;
  color = RRTAndODTFit(color);
  color = ACESOutputMat * color;
  return clamp(color, 0.0, 1.0);
}

// Khronos PBR Neutral tone mapping
// Reference: https://github.com/KhronosGroup/ToneMapping
vec3 toneMapKhronosPbrNeutral(vec3 color)
{
  const float startCompression = 0.8 - 0.04;
  const float desaturation = 0.15;

  float x = min(color.r, min(color.g, color.b));
  float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
  color -= offset;

  float peak = max(color.r, max(color.g, color.b));
  if (peak < startCompression) return color;

  const float d = 1.0 - startCompression;
  float newPeak = 1.0 - d * d / (peak + d - startCompression);
  color *= newPeak / peak;

  float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
  return mix(color, vec3(newPeak), g);
}

// ============================================================================
// BRDF Functions (matching glTF-Sample-Viewer)
// Reference: https://github.com/KhronosGroup/glTF-Sample-Viewer
// ============================================================================

// Fresnel-Schlick with f90 parameter
// Reference: http://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html
vec3 F_Schlick(vec3 f0, vec3 f90, float VdotH)
{
  float x = clamp(1.0 - VdotH, 0.0, 1.0);
  float x2 = x * x;
  float x5 = x * x2 * x2;
  return f0 + (f90 - f0) * x5;
}

// GGX/Trowbridge-Reitz Normal Distribution Function
// Reference: Trowbridge and Reitz, "Average Irregularity Representation of a Roughened Surface"
float D_GGX(float NdotH, float alphaRoughness)
{
  float alphaSq = alphaRoughness * alphaRoughness;
  float f = (NdotH * NdotH) * (alphaSq - 1.0) + 1.0;
  return alphaSq / (PI * f * f);
}

// Smith Height-Correlated Visibility Function (G / (4 * NdotL * NdotV))
// Reference: Heitz, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
// Reference: https://google.github.io/filament/Filament.md.html
float V_GGX(float NdotL, float NdotV, float alphaRoughness)
{
  float alphaSq = alphaRoughness * alphaRoughness;
  float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - alphaSq) + alphaSq);
  float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - alphaSq) + alphaSq);
  float GGX = GGXV + GGXL;
  return GGX > 0.0 ? 0.5 / GGX : 0.0;
}

// Lambertian diffuse BRDF
vec3 BRDF_lambertian(vec3 diffuseColor)
{
  return diffuseColor / PI;
}

// Full specular GGX BRDF (returns Vis * D, fresnel applied separately)
float BRDF_specularGGX(float alphaRoughness, float NdotL, float NdotV, float NdotH)
{
  float V = V_GGX(NdotL, NdotV, alphaRoughness);
  float D = D_GGX(NdotH, alphaRoughness);
  return V * D;
}

// ============================================================================
// IBL Functions
// ============================================================================

vec3 getIBLDiffuse(vec3 N, vec3 albedo, float metallic)
{
  vec3 irradiance = texture(irradianceMap, N).rgb;
  vec3 diffuse = irradiance * albedo * (1.0 - metallic);
  return diffuse / PI;
}

vec3 getIBLSpecular(vec3 N, vec3 V, vec3 F0, float perceptualRoughness, float NdotV)
{
  vec3 R = reflect(-V, N);
  const float MAX_REFLECTION_LOD = 4.0;
  float lod = perceptualRoughness * MAX_REFLECTION_LOD;
  vec3 prefilteredColor = textureLod(prefilterMap, R, lod).rgb;
  vec2 brdf = texture(brdfLUT, vec2(NdotV, perceptualRoughness)).rg;
  return prefilteredColor * (F0 * brdf.x + brdf.y);
}

void main()
{
  // Determine normal based on normal mapping toggle
  // Reference: https://learnopengl.com/Advanced-Lighting/Normal-Mapping
  vec3 N;

  if (ubo.flags.x > 0.5) {
    // Normal mapping enabled - sample and transform
    vec3 normalMap = texture(normalTexture, fragTexCoord).rgb;
    normalMap = normalMap * 2.0 - 1.0;  // Decode from [0,1] to [-1,1]

    // Check if we have a valid normal map (not default flat blue)
    if (abs(normalMap.x) < 0.01 && abs(normalMap.y) < 0.01 && normalMap.z > 0.98) {
      // Flat normal map - use vertex normal
      N = normalize(fragNormal);
    } else {
      // Transform normal from tangent space to world space
      N = normalize(fragTBN * normalMap);
    }
  } else {
    // Normal mapping disabled - use vertex normal
    N = normalize(fragNormal);
  }

  // Sample base color texture, apply material factor, convert from sRGB to linear
  vec4 texColor = texture(baseColorTexture, fragTexCoord);
  vec4 baseColor = texColor * pc.baseColorFactor;
  vec3 albedo = sRGBToLinear(baseColor.rgb);

  // Use texture color if available (non-white), otherwise use vertex color
  if (texColor.r > 0.99 && texColor.g > 0.99 && texColor.b > 0.99 &&
      pc.baseColorFactor.r > 0.99 && pc.baseColorFactor.g > 0.99 && pc.baseColorFactor.b > 0.99) {
    // Default white texture with white factor - use vertex color
    albedo = fragColor;
  }

  // Sample metallic/roughness texture (glTF format: G=roughness, B=metallic)
  vec4 mrSample = texture(metallicRoughnessTexture, fragTexCoord);
  float perceptualRoughness = mrSample.g;  // Green channel = perceptual roughness
  float metallic = mrSample.b;              // Blue channel = metallic

  // Sample ambient occlusion (glTF stores AO in R channel)
  float ao = texture(aoTexture, fragTexCoord).r;

  // Sample emissive texture
  vec3 emissive = texture(emissiveTexture, fragTexCoord).rgb;

  // Clamp roughness and metallic to valid ranges
  perceptualRoughness = clamp(perceptualRoughness, 0.0, 1.0);
  metallic = clamp(metallic, 0.0, 1.0);

  // Convert perceptual roughness to alpha roughness (squared per glTF spec)
  // Reference: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#metallic-roughness-material
  float alphaRoughness = perceptualRoughness * perceptualRoughness;

  // View direction
  vec3 V = normalize(ubo.viewPos.xyz - fragPos);

  // Two-sided lighting (flip normal if facing away from viewer)
  if (dot(N, V) < 0.0) {
    N = -N;
  }

  // Light direction and attenuation
  vec3 L;
  float attenuation = 1.0;

  if (ubo.lightPosition.w < 0.5) {
    // Directional light - no attenuation
    L = normalize(ubo.lightPosition.xyz);
  } else {
    // Point light with inverse square falloff
    vec3 lightVec = ubo.lightPosition.xyz - fragPos;
    float distance = length(lightVec);
    L = lightVec / distance;

    // Inverse square falloff (physically correct)
    // Add small constant to prevent division by zero at very close range
    attenuation = 1.0 / (distance * distance + 0.0001);
  }

  // Halfway vector
  vec3 H = normalize(V + L);

  // Light radiance with attenuation
  vec3 radiance = ubo.lightColor.rgb * ubo.lightColor.a * attenuation;

  // Calculate F0 (base reflectivity) and F90
  // Dielectrics have F0 ~0.04, metals use albedo color
  // F90 = 1.0 for all materials (grazing angle reflectance)
  vec3 f0_dielectric = vec3(0.04);
  vec3 F0 = mix(f0_dielectric, albedo, metallic);
  vec3 F90 = vec3(1.0);

  // Precompute dot products
  float NdotL = clamp(dot(N, L), 0.0, 1.0);
  float NdotV = clamp(dot(N, V), 0.0, 1.0);
  float NdotH = clamp(dot(N, H), 0.0, 1.0);
  float VdotH = clamp(dot(V, H), 0.0, 1.0);

  // Fresnel term
  vec3 F = F_Schlick(F0, F90, VdotH);

  // Specular BRDF (V * D term, fresnel applied below)
  float specularBRDF = BRDF_specularGGX(alphaRoughness, NdotL, NdotV, NdotH);

  // Diffuse BRDF (Lambertian)
  vec3 diffuseBRDF = BRDF_lambertian(albedo);

  // Combine using metallic workflow (matching glTF-Sample-Viewer)
  // Metals: only specular with albedo-tinted fresnel
  // Dielectrics: diffuse + specular with 0.04 fresnel
  vec3 dielectric_brdf = mix(diffuseBRDF, vec3(specularBRDF), F);
  vec3 metal_brdf = F * specularBRDF;

  vec3 brdf = mix(dielectric_brdf, metal_brdf, metallic);

  // Final direct lighting contribution
  vec3 Lo = brdf * radiance * NdotL;

  // Get material parameters from uniforms
  float metallicAmbient = ubo.material.z;  // Fake IBL strength for metals
  float aoStrength = ubo.material.w;       // AO influence
  bool useEmissive = ubo.flags.y > 0.5;
  bool useAO = ubo.flags.z > 0.5;
  bool useIBL = ubo.ibl_params.x > 0.5;
  float iblIntensity = ubo.ibl_params.y;

  // Ambient/IBL lighting
  vec3 ambient;

  if (useIBL) {
    // Real IBL using environment maps
    vec3 iblDiffuse = getIBLDiffuse(N, albedo, metallic);
    vec3 iblSpecular = getIBLSpecular(N, V, F0, perceptualRoughness, NdotV);
    ambient = (iblDiffuse + iblSpecular) * iblIntensity;
  } else {
    // Fallback: fake IBL using ambient color
    // For dielectrics: use albedo for ambient diffuse
    // For metals: use F0 (reflectance) as fake environment reflection
    vec3 ambientDiffuse = ubo.lightAmbient.rgb * albedo * (1.0 - metallic);
    vec3 ambientSpecular = ubo.lightAmbient.rgb * F0 * metallic * metallicAmbient * (1.0 - perceptualRoughness * 0.5);
    ambient = ambientDiffuse + ambientSpecular;
  }

  // Combine ambient and direct lighting
  vec3 color = ambient + Lo;

  // Apply AO to entire color (matching glTF-Sample-Viewer)
  // Formula: color = color * (1.0 + strength * (ao - 1.0))
  if (useAO) {
    color = color * (1.0 + aoStrength * (ao - 1.0));
  }

  // Add emissive (before tone mapping for HDR glow effect)
  // Reference: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#reference-material
  if (useEmissive) {
    color += sRGBToLinear(emissive);  // Emissive is also in sRGB
  }

  // Apply exposure
  float exposure = ubo.flags.w;
  color *= exposure;

  // HDR tone mapping (selectable via ibl_params.z)
  // 0 = None, 1 = Reinhard, 2 = ACES Narkowicz, 3 = ACES Hill, 4 = ACES Hill + Boost, 5 = Khronos PBR Neutral
  int tonemapMode = int(ubo.ibl_params.z);
  if (tonemapMode == 1) {
    color = toneMapReinhard(color);
  } else if (tonemapMode == 2) {
    color = toneMapACES_Narkowicz(color);
  } else if (tonemapMode == 3) {
    color = toneMapACES_Hill(color);
  } else if (tonemapMode == 4) {
    color = toneMapACES_Hill(color / 0.6);  // Exposure boost
  } else if (tonemapMode == 5) {
    color = toneMapKhronosPbrNeutral(color);
  }
  // else mode 0: no tone mapping, just gamma

  // Gamma correction (linear to sRGB)
  color = linearToSRGB(color);

  // Alpha mode handling (late discard per Khronos reference)
  if (pc.alphaMode == 1u) {
    // MASK: discard fragments below cutoff
    if (baseColor.a < pc.alphaCutoff) discard;
    outColor = vec4(color, 1.0);
  } else if (pc.alphaMode == 2u) {
    // BLEND: output with alpha for blending
    outColor = vec4(color, baseColor.a);
  } else {
    // OPAQUE (default)
    outColor = vec4(color, 1.0);
  }
}
