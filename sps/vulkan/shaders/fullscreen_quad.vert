#version 450

// Fullscreen triangle vertex shader
// Generates a fullscreen triangle without vertex buffer
// Reference: https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/

layout(location = 0) out vec2 fragTexCoord;

void main()
{
  // Generate fullscreen triangle from vertex ID
  // ID 0: (-1, -1), ID 1: (3, -1), ID 2: (-1, 3)
  fragTexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
  gl_Position = vec4(fragTexCoord * 2.0 - 1.0, 0.0, 1.0);

  // Flip Y for Vulkan coordinate system
  fragTexCoord.y = 1.0 - fragTexCoord.y;
}
