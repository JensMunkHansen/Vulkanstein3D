#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 hitValue;

layout(set = 0, binding = 2) uniform CameraUBO {
  mat4 view;
  mat4 proj;
  mat4 viewInverse;
  mat4 projInverse;
  vec4 lightPosition;
  vec4 lightColor;
  vec4 lightAmbient;
  vec4 viewPos;
  vec4 material;
  vec4 flags;
  vec4 ibl_params;
  vec4 clear_color;
} ubo;

layout(set = 0, binding = 7) uniform samplerCube environmentMap;

void main()
{
  if (ubo.ibl_params.x > 0.5) {
    // Sample HDR environment cubemap
    hitValue = texture(environmentMap, gl_WorldRayDirectionEXT).rgb * ubo.ibl_params.y;
  } else {
    hitValue = ubo.clear_color.rgb;
  }
}
