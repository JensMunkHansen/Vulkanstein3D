#version 450

// Debug shader: Show base color texture only (no lighting)

layout(set = 0, binding = 1) uniform sampler2D baseColorTexture;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec2 fragTexCoord;
layout(location = 4) in mat3 fragTBN;

layout(location = 0) out vec4 outColor;

void main()
{
  vec4 texColor = texture(baseColorTexture, fragTexCoord);

  // Use texture if available, otherwise vertex color
  vec3 color = texColor.rgb;
  if (texColor.r > 0.99 && texColor.g > 0.99 && texColor.b > 0.99) {
    color = fragColor;
  }

  outColor = vec4(color, 1.0);
}
