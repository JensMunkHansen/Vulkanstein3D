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
  vec4 flags;     // x=useNormalMap
  vec4 ibl_params;  // x = useIBL, y = iblIntensity, z = tonemapMode, w = reserved
} ubo;

// Vertex attributes
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inTangent;  // xyz=tangent, w=handedness

// Push constant for per-draw material properties
layout(push_constant) uniform PushConstants {
  mat4 model;            // 64 bytes (vertex stage)
  vec4 baseColorFactor;  // 16 bytes (fragment stage)
  float alphaCutoff;     //  4 bytes (fragment stage)
  uint alphaMode;        //  4 bytes (fragment stage) 0=OPAQUE, 1=MASK, 2=BLEND
} pc;

// Outputs to fragment shader
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragPos;
layout(location = 3) out vec2 fragTexCoord;
layout(location = 4) out mat3 fragTBN;  // Tangent-Bitangent-Normal matrix

void main()
{
  // World position via model matrix
  vec4 worldPos = pc.model * vec4(inPosition, 1.0);
  fragPos = worldPos.xyz;

  gl_Position = ubo.proj * ubo.view * worldPos;
  fragColor = inColor;
  fragTexCoord = inTexCoord;

  // Transform normal and tangent by model matrix (upper 3x3)
  mat3 normalMatrix = mat3(pc.model);
  fragNormal = normalize(normalMatrix * inNormal);

  // Compute TBN matrix for normal mapping
  // Reference: https://learnopengl.com/Advanced-Lighting/Normal-Mapping
  vec3 T = normalize(normalMatrix * inTangent.xyz);
  vec3 N = fragNormal;
  // Re-orthogonalize T with respect to N (Gram-Schmidt)
  T = normalize(T - dot(T, N) * N);
  // Bitangent: cross product with handedness from tangent.w
  vec3 B = cross(N, T) * inTangent.w;

  // TBN transforms from tangent space to world space
  fragTBN = mat3(T, B, N);
}
