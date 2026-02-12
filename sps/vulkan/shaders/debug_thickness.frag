#version 450

// Debug shader: Visualize volume thickness
// Shows thickness texture (G channel) * thicknessFactor as grayscale
// White = thick, black = thin

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

void main()
{
  float thickness = texture(thicknessTexture, fragTexCoord).g * pc.thicknessFactor;

  // Heat map: zero=black, thin=blue, medium=green, thick=red
  // Black for no thickness data (models without KHR_materials_volume have thicknessFactor=0)
  vec3 color;
  if (thickness <= 0.0) {
    color = vec3(0.0);
  } else if (thickness < 0.5) {
    color = mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), thickness * 2.0);
  } else {
    color = mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (thickness - 0.5) * 2.0);
  }

  outColor = vec4(color, 1.0);
}
