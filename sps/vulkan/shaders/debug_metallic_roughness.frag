#version 450

// Debug shader: Visualize metallic/roughness
// Red = Metallic, Green = Roughness, Blue = 0

layout(set = 0, binding = 3) uniform sampler2D metallicRoughnessTexture;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec2 fragTexCoord;
layout(location = 4) in mat3 fragTBN;

layout(location = 0) out vec4 outColor;

void main()
{
  vec4 mrSample = texture(metallicRoughnessTexture, fragTexCoord);

  // glTF format: G=roughness, B=metallic
  float roughness = mrSample.g;
  float metallic = mrSample.b;

  // Visualize: Red = Metallic, Green = Roughness
  outColor = vec4(metallic, roughness, 0.0, 1.0);
}
