#version 450
#extension GL_GOOGLE_include_directive : require

// Composite pass: sample HDR buffer, apply exposure + tone mapping + gamma correction
// Outputs to swapchain (UNORM format)

layout(set = 0, binding = 0) uniform sampler2D hdrBuffer;

layout(push_constant) uniform PushConstants {
  float exposure;
  int tonemapMode;
} pc;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

const float GAMMA = 2.2;
const float INV_GAMMA = 1.0 / GAMMA;

vec3 linearToSRGB(vec3 color)
{
  return pow(color, vec3(INV_GAMMA));
}

#include "tonemap.glsl"

void main()
{
  vec3 color = texture(hdrBuffer, fragTexCoord).rgb;

  // Apply exposure
  color *= pc.exposure;

  // HDR tone mapping
  color = applyToneMap(color, pc.tonemapMode);

  // Gamma correction (linear to sRGB)
  color = linearToSRGB(color);

  outColor = vec4(color, 1.0);
}
