#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 hitValue;
hitAttributeEXT vec2 attribs;

layout(set = 0, binding = 2) uniform CameraUBO {
  mat4 view;          // Raster: view matrix (not used in RT)
  mat4 proj;          // Raster: proj matrix (not used in RT)
  mat4 viewInverse;   // RT: camera transform
  mat4 projInverse;   // RT: unproject rays
  vec4 lightPosition;
  vec4 lightColor;
  vec4 lightAmbient;
  vec4 viewPos;
  vec4 material;
  vec4 flags;
  vec4 ibl_params;    // x = useIBL, y = iblIntensity, z = tonemapMode, w = reserved
} ubo;

// Vertex structure: position(3), normal(3), color(3) = 9 floats
struct Vertex {
  vec3 position;
  vec3 normal;
  vec3 color;
};

layout(set = 0, binding = 3) readonly buffer VertexBuffer { float vertices[]; };
layout(set = 0, binding = 4) readonly buffer IndexBuffer { uint indices[]; };

Vertex getVertex(uint index) {
  uint offset = index * 9;  // 9 floats per vertex
  Vertex v;
  v.position = vec3(vertices[offset + 0], vertices[offset + 1], vertices[offset + 2]);
  v.normal = vec3(vertices[offset + 3], vertices[offset + 4], vertices[offset + 5]);
  v.color = vec3(vertices[offset + 6], vertices[offset + 7], vertices[offset + 8]);
  return v;
}

void main()
{
  // Get triangle indices
  uint i0 = indices[gl_PrimitiveID * 3 + 0];
  uint i1 = indices[gl_PrimitiveID * 3 + 1];
  uint i2 = indices[gl_PrimitiveID * 3 + 2];

  // Get vertices
  Vertex v0 = getVertex(i0);
  Vertex v1 = getVertex(i1);
  Vertex v2 = getVertex(i2);

  // Barycentric interpolation
  vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

  // Interpolated attributes
  vec3 worldPos = v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z;
  vec3 normal = normalize(v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z);
  vec3 color = v0.color * barycentrics.x + v1.color * barycentrics.y + v2.color * barycentrics.z;

  // Simple Blinn-Phong lighting
  vec3 lightDir = normalize(ubo.lightPosition.xyz);
  vec3 viewDir = normalize(ubo.viewPos.xyz - worldPos);

  // Two-sided lighting
  if (dot(normal, viewDir) < 0.0) {
    normal = -normal;
  }

  // Ambient
  vec3 ambient = ubo.lightAmbient.rgb * color;

  // Diffuse
  float diff = max(dot(normal, lightDir), 0.0);
  vec3 diffuse = diff * ubo.lightColor.rgb * ubo.lightColor.a * color;

  // Specular (Blinn-Phong)
  vec3 halfwayDir = normalize(lightDir + viewDir);
  float spec = pow(max(dot(normal, halfwayDir), 0.0), ubo.material.x);
  vec3 specular = spec * ubo.lightColor.rgb * ubo.material.y;

  hitValue = ambient + diffuse + specular;
}
