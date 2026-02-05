#version 450

// Debug shader: Visualize ambient occlusion
// Grayscale: white = no occlusion, black = full occlusion

layout(set = 0, binding = 5) uniform sampler2D aoTexture;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec2 fragTexCoord;
layout(location = 4) in mat3 fragTBN;

layout(location = 0) out vec4 outColor;

void main()
{
  // glTF stores AO in R channel
  float ao = texture(aoTexture, fragTexCoord).r;

  outColor = vec4(ao, ao, ao, 1.0);
}
