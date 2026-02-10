#version 450
#extension GL_GOOGLE_include_directive : require

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
  vec4 ibl_params;      // x = useIBL, y = iblIntensity, z = tonemapMode, w = reserved
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

// Iridescence textures
layout(set = 0, binding = 9) uniform sampler2D iridescenceTexture;           // R=factor mask
layout(set = 0, binding = 10) uniform sampler2D iridescenceThicknessTexture; // G=thickness

// Subsurface scattering textures
layout(set = 0, binding = 11) uniform sampler2D thicknessTexture;            // G=thickness (KHR_materials_volume)

// Push constant for per-draw material properties
layout(push_constant) uniform PushConstants {
  mat4 model;                  // 64 bytes (vertex stage)
  vec4 baseColorFactor;        // 16 bytes
  float metallicFactor;        //  4 bytes
  float roughnessFactor;       //  4 bytes
  float alphaCutoff;           //  4 bytes
  uint alphaMode;              //  4 bytes  bits[1:0]=0 OPAQUE,1 MASK,2 BLEND; bit[2]=doubleSided
  float iridescenceFactor;     //  4 bytes
  float iridescenceIor;        //  4 bytes
  float iridescenceThicknessMin; // 4 bytes
  float iridescenceThicknessMax; // 4 bytes
  float transmissionFactor;      // 4 bytes
  float thicknessFactor;         // 4 bytes
  uint attenuationColorPacked;   // 4 bytes  R8G8B8 packed unorm
  float attenuationDistance;     // 4 bytes  (total: 128)
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

#include "tonemap.glsl"
#include "iridescence.glsl"

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
// Subsurface Scattering (Barré-Brisebois & Bouchard, GDC 2011)
// ============================================================================

// Unpack R8G8B8 color from uint32
vec3 unpackColor(uint packed)
{
  return vec3(
    float(packed & 0xFFu) / 255.0,
    float((packed >> 8u) & 0xFFu) / 255.0,
    float((packed >> 16u) & 0xFFu) / 255.0
  );
}



// ============================================================================
// IBL Functions (matching glTF-Sample-Viewer)
// Reference: https://github.com/KhronosGroup/glTF-Sample-Viewer
// ============================================================================

// Get diffuse irradiance from environment map
// Our irradiance map stores E(n) = PI * avg(L) from cosine-weighted sampling,
// Lambertian BRDF = albedo/PI, so we divide by PI here
vec3 getIBLDiffuseLight(vec3 N)
{
  return texture(irradianceMap, N).rgb / PI;
}

// Get specular radiance from prefiltered environment map
vec3 getIBLRadianceGGX(vec3 N, vec3 V, float perceptualRoughness)
{
  vec3 R = reflect(-V, N);
  const float MAX_REFLECTION_LOD = 4.0;
  float lod = perceptualRoughness * MAX_REFLECTION_LOD;
  return textureLod(prefilterMap, R, lod).rgb;
}

// Roughness-dependent Fresnel with multi-scattering correction
// Reference: Fdez-Aguera, https://bruop.github.io/ibl/#single_scattering_results
vec3 getIBLGGXFresnel(vec3 N, vec3 V, float roughness, vec3 F0, float specularWeight)
{
  float NdotV = clamp(dot(N, V), 0.0, 1.0);
  vec2 brdfSamplePoint = clamp(vec2(NdotV, roughness), vec2(0.0), vec2(1.0));
  vec2 f_ab = texture(brdfLUT, brdfSamplePoint).rg;

  // Roughness-dependent Fresnel (Fdez-Aguera)
  vec3 Fr = max(vec3(1.0 - roughness), F0) - F0;
  vec3 k_S = F0 + Fr * pow(1.0 - NdotV, 5.0);
  vec3 FssEss = specularWeight * (k_S * f_ab.x + f_ab.y);

  // Multi-scattering correction (Fdez-Aguera)
  float Ems = 1.0 - (f_ab.x + f_ab.y);
  vec3 F_avg = specularWeight * (F0 + (1.0 - F0) / 21.0);
  vec3 FmsEms = Ems * FssEss * F_avg / (1.0 - F_avg * Ems);

  return FssEss + FmsEms;
}

void main()
{
  // Determine normal based on normal mapping toggle
  // Reference: https://learnopengl.com/Advanced-Lighting/Normal-Mapping
  vec3 N;

  if (ubo.flags.x > 0.5) {
    // Normal mapping enabled - sample and transform
    // Normal texture uses UNORM format (no sRGB conversion by GPU)
    vec3 normalMap = texture(normalTexture, fragTexCoord).rgb;
    normalMap = normalMap * 2.0 - 1.0;  // Decode from [0,1] to [-1,1]
    N = normalize(fragTBN * normalMap);
  } else {
    // Normal mapping disabled - use vertex normal
    N = normalize(fragNormal);
  }

  // Sample base color texture (sRGB format — GPU converts to linear automatically)
  vec4 texColor = texture(baseColorTexture, fragTexCoord);
  vec4 baseColor = texColor * pc.baseColorFactor;
  vec3 albedo = baseColor.rgb;

  // Use texture color if available (non-white), otherwise use vertex color
  if (texColor.r > 0.99 && texColor.g > 0.99 && texColor.b > 0.99 &&
      pc.baseColorFactor.r > 0.99 && pc.baseColorFactor.g > 0.99 && pc.baseColorFactor.b > 0.99) {
    // Default white texture with white factor - use vertex color
    albedo = fragColor;
  }

  // Sample metallic/roughness texture (glTF format: G=roughness, B=metallic)
  // Multiply by material factors per glTF spec
  vec4 mrSample = texture(metallicRoughnessTexture, fragTexCoord);
  float perceptualRoughness = mrSample.g * pc.roughnessFactor;
  float metallic = mrSample.b * pc.metallicFactor;

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

  // Two-sided normal handling (Khronos glTF-Sample-Viewer approach)
  bool doubleSided = (pc.alphaMode & 4u) != 0u;
  if (doubleSided && !gl_FrontFacing) {
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

  // Iridescence: evaluate thin-film Fresnel if factor > 0
  float iridescenceFac = pc.iridescenceFactor * texture(iridescenceTexture, fragTexCoord).r;
  float iridescenceThickness = mix(pc.iridescenceThicknessMin, pc.iridescenceThicknessMax,
    texture(iridescenceThicknessTexture, fragTexCoord).g);
  if (iridescenceThickness == 0.0) iridescenceFac = 0.0;

  vec3 iridescenceFresnel_dielectric = vec3(0.0);
  vec3 iridescenceFresnel_metallic = vec3(0.0);
  if (iridescenceFac > 0.0) {
    iridescenceFresnel_dielectric = evalIridescence(1.0, pc.iridescenceIor, NdotV,
      iridescenceThickness, f0_dielectric);
    iridescenceFresnel_metallic = evalIridescence(1.0, pc.iridescenceIor, NdotV,
      iridescenceThickness, albedo);
  }

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

  // Apply iridescence to direct lighting BRDF
  if (iridescenceFac > 0.0) {
    metal_brdf = mix(metal_brdf, vec3(specularBRDF) * iridescenceFresnel_metallic, iridescenceFac);
    dielectric_brdf = mix(dielectric_brdf,
      rgb_mix(diffuseBRDF, vec3(specularBRDF), iridescenceFresnel_dielectric), iridescenceFac);
  }

  vec3 brdf = mix(dielectric_brdf, metal_brdf, metallic);

  // Final direct lighting contribution
  vec3 Lo = brdf * radiance * NdotL;

  // Subsurface translucency (Barré-Brisebois back-lighting)
  bool useSSS = ubo.ibl_params.w > 0.5;
  if (useSSS && pc.transmissionFactor > 0.0) {
    // Thickness texture is in [0,1], thicknessFactor scales to world units
    float thickness = texture(thicknessTexture, fragTexCoord).g * pc.thicknessFactor;
    // Exponential falloff: even thick areas transmit some light
    float transmission = exp(-thickness * 3.0);
    vec3 attColor = unpackColor(pc.attenuationColorPacked);

    // Barré-Brisebois wrap lighting
    const float distortion = 0.2;
    const float power = 4.0;
    vec3 scatteredL = L + N * distortion;
    float backLight = pow(clamp(dot(V, -scatteredL), 0.0, 1.0), power);

    // Attenuation color tints the scattered light
    vec3 sss = attColor * backLight * transmission;
    Lo += sss * albedo * radiance * pc.transmissionFactor;
  }

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
    // Energy-conserving IBL (matching glTF-Sample-Viewer)
    // Reference: https://github.com/KhronosGroup/glTF-Sample-Viewer
    vec3 f_diffuse_ibl = getIBLDiffuseLight(N) * albedo;
    vec3 f_specular_ibl = getIBLRadianceGGX(N, V, perceptualRoughness);

    // Metal path: specular only, F0 = baseColor (tinted reflections)
    vec3 f_metal_fresnel = getIBLGGXFresnel(N, V, perceptualRoughness, albedo, 1.0);
    vec3 f_metal_brdf = f_metal_fresnel * f_specular_ibl;

    // Dielectric path: energy-conserving mix of diffuse and specular
    vec3 f_dielectric_fresnel = getIBLGGXFresnel(N, V, perceptualRoughness, f0_dielectric, 1.0);
    vec3 f_dielectric_brdf = mix(f_diffuse_ibl, f_specular_ibl, f_dielectric_fresnel);

    // Apply iridescence to IBL
    if (iridescenceFac > 0.0) {
      f_metal_brdf = mix(f_metal_brdf, f_specular_ibl * iridescenceFresnel_metallic, iridescenceFac);
      f_dielectric_brdf = mix(f_dielectric_brdf,
        rgb_mix(f_diffuse_ibl, f_specular_ibl, iridescenceFresnel_dielectric), iridescenceFac);
    }

    // Final: blend between dielectric and metal by metallic factor
    ambient = mix(f_dielectric_brdf, f_metal_brdf, metallic) * iblIntensity;
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
    color += emissive;  // Emissive texture is sRGB format — GPU converts to linear on sample
  }

  // Apply exposure
  float exposure = ubo.flags.w;
  color *= exposure;

  // HDR tone mapping (selectable via ibl_params.z)
  color = applyToneMap(color, int(ubo.ibl_params.z));

  // Gamma correction (linear to sRGB)
  color = linearToSRGB(color);

  // Alpha mode handling (late discard per Khronos reference)
  uint alphaModeValue = pc.alphaMode & 3u;  // Mask off doubleSided bit
  if (alphaModeValue == 1u) {
    // MASK: discard fragments below cutoff
    if (baseColor.a < pc.alphaCutoff) discard;
    outColor = vec4(color, 1.0);
  } else if (alphaModeValue == 2u) {
    // BLEND: output with alpha for blending
    outColor = vec4(color, baseColor.a);
  } else {
    // OPAQUE (default)
    outColor = vec4(color, 1.0);
  }
}
