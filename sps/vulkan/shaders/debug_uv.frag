#version 450

// Debug shader: Visualize UV coordinates
// Red = U, Green = V, Blue = 0

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec2 fragTexCoord;
layout(location = 4) in mat3 fragTBN;

layout(location = 0) out vec4 outColor;

void main()
{
  // UV as color: U -> Red, V -> Green
  outColor = vec4(fragTexCoord.x, fragTexCoord.y, 0.0, 1.0);
}
