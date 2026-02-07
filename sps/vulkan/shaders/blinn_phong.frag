#version 450
#extension GL_GOOGLE_include_directive : require

// Blinn-Phong lighting shader
// Reference: https://learnopengl.com/Advanced-Lighting/Advanced-Lighting

layout(set = 0, binding = 0) uniform UniformBufferObject {
  mat4 view;
  mat4 proj;
  mat4 viewInverse;
  mat4 projInverse;
  vec4 lightPosition;   // xyz = position/direction, w = 0 directional, 1 point
  vec4 lightColor;      // rgb = color, a = intensity
  vec4 lightAmbient;    // rgb = ambient color
  vec4 viewPos;         // xyz = camera position
  vec4 material;        // x = shininess, y = specStrength
  vec4 flags;           // x = useNormalMap, w = exposure
  vec4 ibl_params;      // x = useIBL, y = iblIntensity, z = tonemapMode, w = reserved
} ubo;

const float GAMMA = 2.2;

// sRGB to linear
vec3 sRGBToLinear(vec3 srgb)
{
  return pow(srgb, vec3(GAMMA));
}

// Linear to sRGB
vec3 linearToSRGB(vec3 color)
{
  return pow(color, vec3(1.0 / GAMMA));
}

#include "tonemap.glsl"

// Textures
layout(set = 0, binding = 1) uniform sampler2D baseColorTexture;
layout(set = 0, binding = 2) uniform sampler2D normalTexture;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec2 fragTexCoord;
layout(location = 4) in mat3 fragTBN;

layout(location = 0) out vec4 outColor;

void main()
{
  // Determine normal based on normal mapping toggle
  vec3 normal;

  if (ubo.flags.x > 0.5) {
    // Normal mapping enabled
    vec3 normalMap = texture(normalTexture, fragTexCoord).rgb;
    normalMap = normalMap * 2.0 - 1.0;

    if (abs(normalMap.x) < 0.01 && abs(normalMap.y) < 0.01 && normalMap.z > 0.98) {
      normal = normalize(fragNormal);
    } else {
      normal = normalize(fragTBN * normalMap);
    }
  } else {
    normal = normalize(fragNormal);
  }

  // Sample base color texture and convert from sRGB to linear
  vec4 texColor = texture(baseColorTexture, fragTexCoord);
  vec3 albedo = sRGBToLinear(texColor.rgb);

  // Use texture color if available, otherwise vertex color
  if (texColor.r > 0.99 && texColor.g > 0.99 && texColor.b > 0.99) {
    albedo = fragColor;
  }

  // Material properties
  float shininess = ubo.material.x;
  float specStrength = ubo.material.y;

  // Light properties
  vec3 lightColor = ubo.lightColor.rgb * ubo.lightColor.a;
  vec3 ambientColor = ubo.lightAmbient.rgb;

  // View direction
  vec3 viewDir = normalize(ubo.viewPos.xyz - fragPos);

  // Two-sided lighting
  if (dot(normal, viewDir) < 0.0) {
    normal = -normal;
  }

  // Light direction and attenuation
  vec3 lightDir;
  float attenuation = 1.0;

  if (ubo.lightPosition.w < 0.5) {
    lightDir = normalize(ubo.lightPosition.xyz);  // Directional
  } else {
    // Point light with inverse square falloff
    vec3 lightVec = ubo.lightPosition.xyz - fragPos;
    float distance = length(lightVec);
    lightDir = lightVec / distance;
    attenuation = 1.0 / (distance * distance + 0.0001);
  }

  // Apply attenuation to light color
  vec3 attenuatedLight = lightColor * attenuation;

  // Ambient
  vec3 ambient = ambientColor * albedo;

  // Diffuse (Lambertian)
  float diff = max(dot(normal, lightDir), 0.0);
  vec3 diffuse = diff * attenuatedLight * albedo;

  // Specular (Blinn-Phong)
  vec3 halfwayDir = normalize(lightDir + viewDir);
  float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);
  vec3 specular = spec * attenuatedLight * specStrength;

  vec3 result = ambient + diffuse + specular;

  // Apply exposure
  float exposure = ubo.flags.w;
  result *= exposure;

  // Tone mapping (selectable via ibl_params.z)
  result = applyToneMap(result, int(ubo.ibl_params.z));

  // Gamma correction (linear to sRGB)
  result = linearToSRGB(result);

  outColor = vec4(result, 1.0);
}
