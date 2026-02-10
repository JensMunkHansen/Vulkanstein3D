#version 450

// Debug shader: Visualize subsurface scattering contribution
// Shows the back-lighting translucency term only (white = strong SSS, black = none)

layout(set = 0, binding = 0) uniform UniformBufferObject {
  mat4 view;
  mat4 proj;
  mat4 viewInverse;
  mat4 projInverse;
  vec4 lightPosition;
  vec4 lightColor;
  vec4 lightAmbient;
  vec4 viewPos;
  vec4 material;
  vec4 flags;
  vec4 ibl_params;
} ubo;

layout(set = 0, binding = 11) uniform sampler2D thicknessTexture;

layout(push_constant) uniform PushConstants {
  mat4 model;
  vec4 baseColorFactor;
  float metallicFactor;
  float roughnessFactor;
  float alphaCutoff;
  uint alphaMode;
  float iridescenceFactor;
  float iridescenceIor;
  float iridescenceThicknessMin;
  float iridescenceThicknessMax;
  float transmissionFactor;
  float thicknessFactor;
  uint attenuationColorPacked;
  float attenuationDistance;
} pc;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec2 fragTexCoord;
layout(location = 4) in mat3 fragTBN;

layout(location = 0) out vec4 outColor;

vec3 unpackColor(uint packed)
{
  return vec3(
    float(packed & 0xFFu) / 255.0,
    float((packed >> 8u) & 0xFFu) / 255.0,
    float((packed >> 16u) & 0xFFu) / 255.0
  );
}

void main()
{
  vec3 N = normalize(fragNormal);
  vec3 V = normalize(ubo.viewPos.xyz - fragPos);

  // Two-sided normal handling
  bool doubleSided = (pc.alphaMode & 4u) != 0u;
  if (doubleSided && !gl_FrontFacing) {
    N = -N;
  }

  // Light direction
  vec3 L;
  if (ubo.lightPosition.w < 0.5) {
    L = normalize(ubo.lightPosition.xyz);
  } else {
    L = normalize(ubo.lightPosition.xyz - fragPos);
  }

  // Thickness: exponential falloff so even thick areas transmit some light
  float thickness = texture(thicknessTexture, fragTexCoord).g * pc.thicknessFactor;
  float transmission = exp(-thickness * 3.0);

  // Barré-Brisebois back-lighting
  const float distortion = 0.2;
  const float power = 4.0;
  vec3 scatteredL = L + N * distortion;
  float backLight = pow(clamp(dot(V, -scatteredL), 0.0, 1.0), power);

  vec3 attColor = unpackColor(pc.attenuationColorPacked);
  vec3 sss = attColor * backLight * transmission * pc.transmissionFactor;

  // Boost for visualization (SSS contribution is subtle in PBR)
  outColor = vec4(sss * 3.0, 1.0);
}
