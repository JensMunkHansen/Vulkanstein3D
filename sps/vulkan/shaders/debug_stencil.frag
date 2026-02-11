#version 450

// Debug shader: Visualize SSS stencil marking
// Green = material with transmissionFactor > 0 (will write stencil=1)
// Black = no SSS (stencil=0)

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

void main()
{
  // Show which fragments belong to SSS materials
  if (pc.transmissionFactor > 0.0)
    outColor = vec4(0.0, 1.0, 0.0, 1.0);  // Green = SSS
  else
    outColor = vec4(0.1, 0.1, 0.1, 1.0);  // Dark gray = non-SSS
}
