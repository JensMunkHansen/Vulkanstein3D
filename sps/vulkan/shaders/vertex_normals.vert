#version 450

// Normal visualization shader - vertex
// Transforms vertices and passes normals to fragment shader

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
  vec4 flags;       // not used in normal vis
  vec4 ibl_params;  // x = useIBL, y = iblIntensity, z = tonemapMode, w = reserved
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragPos;
layout(location = 3) out vec2 fragTexCoord;

void main()
{
  fragPos = inPosition;
  gl_Position = ubo.proj * ubo.view * vec4(inPosition, 1.0);
  fragColor = inColor;
  fragNormal = inNormal;
  fragTexCoord = inTexCoord;
}
