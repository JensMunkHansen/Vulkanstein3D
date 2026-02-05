#version 450

// Debug shader: Visualize world-space normals
// Maps normal [-1,1] to color [0,1]

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
  vec4 flags;           // x = useNormalMap
  vec4 ibl_params;      // unused in debug shader
} ubo;

layout(set = 0, binding = 2) uniform sampler2D normalTexture;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec2 fragTexCoord;
layout(location = 4) in mat3 fragTBN;

layout(location = 0) out vec4 outColor;

void main()
{
  vec3 N;

  if (ubo.flags.x > 0.5) {
    // Normal mapping enabled
    vec3 normalMap = texture(normalTexture, fragTexCoord).rgb;
    normalMap = normalMap * 2.0 - 1.0;

    if (abs(normalMap.x) < 0.01 && abs(normalMap.y) < 0.01 && normalMap.z > 0.98) {
      N = normalize(fragNormal);
    } else {
      N = normalize(fragTBN * normalMap);
    }
  } else {
    N = normalize(fragNormal);
  }

  // Map normal from [-1,1] to [0,1] for visualization
  vec3 color = N * 0.5 + 0.5;

  outColor = vec4(color, 1.0);
}
