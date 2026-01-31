#version 450

// Camera uniform buffer (std140 layout)
// Must match C++ UniformBufferObject struct exactly
layout(set = 0, binding = 0) uniform UniformBufferObject {
  mat4 view;
  mat4 proj;
  mat4 viewInverse;     // RT: not used in raster
  mat4 projInverse;     // RT: not used in raster
  vec4 lightPosition;
  vec4 lightColor;
  vec4 lightAmbient;
  vec4 viewPos;
  vec4 material;  // x=shininess, y=specStrength
} ubo;

// Vertex attributes
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

// Outputs to fragment shader
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragPos;

void main()
{
  // World position (no model matrix for now, vertices are in world space)
  fragPos = inPosition;

  gl_Position = ubo.proj * ubo.view * vec4(inPosition, 1.0);
  fragColor = inColor;
  fragNormal = inNormal;
}
