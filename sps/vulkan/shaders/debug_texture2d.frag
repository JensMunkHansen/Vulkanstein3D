#version 450

// 2D Texture viewer - displays a texture fullscreen
// Use with fullscreen_quad.vert

layout(set = 0, binding = 0) uniform UniformBufferObject {
  mat4 view;
  mat4 proj;
  mat4 viewInverse;
  mat4 projInverse;
  vec4 lightPosition;
  vec4 lightColor;
  vec4 lightAmbient;
  vec4 viewPos;         // xy = pan offset, z = zoom level, w = unused
  vec4 material;        // z = textureIndex (0=baseColor, 1=normal, 2=metalRough, 3=emissive, 4=ao)
  vec4 flags;           // x = channel mode (0=RGB, 1=R, 2=G, 3=B, 4=A)
  vec4 ibl_params;      // x = useIBL, y = iblIntensity, z = tonemapMode, w = reserved
} ubo;

// All textures bound
layout(set = 0, binding = 1) uniform sampler2D baseColorTexture;
layout(set = 0, binding = 2) uniform sampler2D normalTexture;
layout(set = 0, binding = 3) uniform sampler2D metallicRoughnessTexture;
layout(set = 0, binding = 4) uniform sampler2D emissiveTexture;
layout(set = 0, binding = 5) uniform sampler2D aoTexture;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main()
{
  int textureIndex = int(ubo.material.z);
  int channelMode = int(ubo.flags.x);

  // Apply zoom and pan: uv = (fragTexCoord - 0.5) / zoom + 0.5 + pan
  float zoom = ubo.viewPos.z;
  vec2 pan = ubo.viewPos.xy;
  vec2 uv = (fragTexCoord - 0.5) / zoom + 0.5 + pan;

  vec4 texColor;

  // Select texture (using zoomed/panned UV)
  if (textureIndex == 0) {
    texColor = texture(baseColorTexture, uv);
  } else if (textureIndex == 1) {
    texColor = texture(normalTexture, uv);
  } else if (textureIndex == 2) {
    texColor = texture(metallicRoughnessTexture, uv);
  } else if (textureIndex == 3) {
    texColor = texture(emissiveTexture, uv);
  } else if (textureIndex == 4) {
    texColor = texture(aoTexture, uv);
  } else {
    texColor = vec4(1.0, 0.0, 1.0, 1.0);  // Magenta for invalid
  }

  // Select channel display mode
  vec3 result;
  if (channelMode == 0) {
    result = texColor.rgb;  // RGB
  } else if (channelMode == 1) {
    result = vec3(texColor.r);  // R only
  } else if (channelMode == 2) {
    result = vec3(texColor.g);  // G only
  } else if (channelMode == 3) {
    result = vec3(texColor.b);  // B only
  } else if (channelMode == 4) {
    result = vec3(texColor.a);  // A only
  } else {
    result = texColor.rgb;
  }

  outColor = vec4(result, 1.0);
}
