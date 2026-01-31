#version 450

// Light and camera uniforms (std140 layout)
// Must match C++ UniformBufferObject struct exactly
layout(set = 0, binding = 0) uniform UniformBufferObject {
  mat4 view;
  mat4 proj;
  mat4 viewInverse;     // RT: camera transform (not used in raster)
  mat4 projInverse;     // RT: unproject rays (not used in raster)
  vec4 lightPosition;   // xyz = position/direction, w = 0 directional, 1 point
  vec4 lightColor;      // rgb = color, a = intensity
  vec4 lightAmbient;    // rgb = ambient color
  vec4 viewPos;         // xyz = camera position
  vec4 material;        // x = shininess, y = specStrength
} ubo;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;

layout(location = 0) out vec4 outColor;

void main()
{
  vec3 normal = normalize(fragNormal);

  // Light parameters from uniform buffer
  vec3 lightColor = ubo.lightColor.rgb * ubo.lightColor.a;
  vec3 ambientColor = ubo.lightAmbient.rgb;
  float shininess = ubo.material.x;
  float specStrength = ubo.material.y;

  // Light direction
  vec3 lightDir;
  if (ubo.lightPosition.w < 0.5) {
    // Directional light
    lightDir = normalize(ubo.lightPosition.xyz);
  } else {
    // Point light
    lightDir = normalize(ubo.lightPosition.xyz - fragPos);
  }

  // View direction from uniform buffer
  vec3 viewDir = normalize(ubo.viewPos.xyz - fragPos);

  // Two-sided lighting
  if (dot(normal, viewDir) < 0.0) {
    normal = -normal;
  }

  // Ambient
  vec3 ambient = ambientColor * fragColor;

  // Diffuse
  float diff = max(dot(normal, lightDir), 0.0);
  vec3 diffuse = diff * lightColor * fragColor;

  // Specular (Blinn-Phong)
  vec3 halfwayDir = normalize(lightDir + viewDir);
  float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);
  vec3 specular = spec * lightColor * specStrength;

  vec3 result = ambient + diffuse + specular;

  outColor = vec4(result, 1.0);
}
