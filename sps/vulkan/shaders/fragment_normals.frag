#version 450

// Normal visualization shader - fragment
// Displays world-space normals as RGB colors

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
  vec4 flags;           // not used in normal vis
  vec4 ibl_params;      // not used in normal vis
} ubo;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main()
{
  // Map normal from [-1, 1] to [0, 1] for visualization
  vec3 normal = normalize(fragNormal);
  vec3 color = normal * 0.5 + 0.5;

  outColor = vec4(color, 1.0);
}
