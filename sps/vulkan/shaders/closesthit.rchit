#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

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
  vec4 clear_color;   // rgb = background color
} ubo;

// Vertex stride set from sizeof(Vertex)/sizeof(float) at pipeline creation time.
// If the CPU Vertex struct changes, the shader adapts automatically.
layout(constant_id = 0) const uint VERTEX_STRIDE = 15;

struct Vertex {
  vec3 position;
  vec3 normal;
  vec3 color;
  vec2 texCoord;
};

layout(set = 0, binding = 3) readonly buffer VertexBuffer { float vertices[]; };
layout(set = 0, binding = 4) readonly buffer IndexBuffer { uint indices[]; };
layout(set = 0, binding = 5) readonly buffer MaterialIndexBuffer { uint materialIndices[]; };
layout(set = 0, binding = 6) uniform sampler2D baseColorTextures[];

// IBL textures
layout(set = 0, binding = 7) uniform samplerCube prefilterMap;   // Specular IBL (mips = roughness)
layout(set = 0, binding = 8) uniform samplerCube irradianceMap;  // Diffuse IBL
layout(set = 0, binding = 9) uniform sampler2D brdfLUT;          // BRDF integration LUT

const float PI = 3.14159265359;

Vertex getVertex(uint index) {
  uint offset = index * VERTEX_STRIDE;
  Vertex v;
  v.position = vec3(vertices[offset + 0], vertices[offset + 1], vertices[offset + 2]);
  v.normal = vec3(vertices[offset + 3], vertices[offset + 4], vertices[offset + 5]);
  v.color = vec3(vertices[offset + 6], vertices[offset + 7], vertices[offset + 8]);
  v.texCoord = vec2(vertices[offset + 9], vertices[offset + 10]);
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
  vec3 vertexColor = v0.color * barycentrics.x + v1.color * barycentrics.y + v2.color * barycentrics.z;
  vec2 texCoord = v0.texCoord * barycentrics.x + v1.texCoord * barycentrics.y + v2.texCoord * barycentrics.z;

  // Sample base color texture (material index maps triangle -> texture)
  uint matIndex = materialIndices[gl_PrimitiveID];
  vec4 texColor = texture(baseColorTextures[nonuniformEXT(matIndex)], texCoord);
  vec3 color = texColor.rgb * vertexColor;

  // Lighting
  vec3 V = normalize(ubo.viewPos.xyz - worldPos);
  vec3 N = normal;

  // Two-sided lighting
  if (dot(N, V) < 0.0) {
    N = -N;
  }

  bool useIBL = ubo.ibl_params.x > 0.5;
  float iblIntensity = ubo.ibl_params.y;

  // Ambient / IBL
  vec3 ambient;
  if (useIBL) {
    // Diffuse IBL from irradiance map
    // Irradiance stores E(n) = PI * avg(L), Lambertian = albedo/PI, so divide by PI
    vec3 irradiance = texture(irradianceMap, N).rgb / PI;
    vec3 diffuseIBL = irradiance * color;

    // Specular IBL from prefiltered environment map
    float roughness = 0.5;  // Default roughness (no MR texture in RT yet)
    vec3 R = reflect(-V, N);
    const float MAX_REFLECTION_LOD = 4.0;
    float lod = roughness * MAX_REFLECTION_LOD;
    vec3 specularIBL = textureLod(prefilterMap, R, lod).rgb;

    // BRDF LUT split-sum approximation
    float NdotV = clamp(dot(N, V), 0.0, 1.0);
    vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    vec3 F0 = vec3(0.04);  // Dielectric default
    specularIBL *= F0 * brdf.x + brdf.y;

    ambient = (diffuseIBL + specularIBL) * iblIntensity;
  } else {
    ambient = ubo.lightAmbient.rgb * color;
  }

  // Direct lighting (Blinn-Phong)
  vec3 lightDir = normalize(ubo.lightPosition.xyz);
  float diff = max(dot(N, lightDir), 0.0);
  vec3 diffuse = diff * ubo.lightColor.rgb * ubo.lightColor.a * color;

  vec3 halfwayDir = normalize(lightDir + V);
  float spec = pow(max(dot(N, halfwayDir), 0.0), ubo.material.x);
  vec3 specular = spec * ubo.lightColor.rgb * ubo.material.y;

  hitValue = ambient + diffuse + specular;
}
