#version 450

// Debug shader: Visualize emissive texture
// Shows raw emissive color (no tone mapping)

layout(set = 0, binding = 4) uniform sampler2D emissiveTexture;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec2 fragTexCoord;
layout(location = 4) in mat3 fragTBN;

layout(location = 0) out vec4 outColor;

void main()
{
  vec3 emissive = texture(emissiveTexture, fragTexCoord).rgb;

  outColor = vec4(emissive, 1.0);
}
