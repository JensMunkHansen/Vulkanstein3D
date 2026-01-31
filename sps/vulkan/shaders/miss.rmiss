#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 hitValue;

void main()
{
  // Sky gradient - blue at top, lighter at horizon
  vec3 direction = normalize(gl_WorldRayDirectionEXT);
  float t = 0.5 * (direction.y + 1.0);
  hitValue = mix(vec3(1.0, 1.0, 1.0), vec3(0.5, 0.7, 1.0), t);
}
