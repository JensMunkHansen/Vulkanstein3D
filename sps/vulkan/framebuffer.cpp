#include <sps/vulkan/framebuffer.h>

#include <sps/vulkan/swapchain.h>

#include <iostream>

namespace sps::vulkan
{

/**
        Make framebuffers for the swapchain

        \param inputChunk required input for creation
        \param frames the vector to be populated with the created framebuffers
        \param debug whether the system is running in debug mode.
*/
void make_framebuffers(framebufferInput inputChunk, //
  const Swapchain& swapchain, bool debug)
{
#if 0
  
  for (int i = 0; i < frames.size(); ++i)
  {

    std::vector<vk::ImageView> attachments = { frames[i].imageView };

    vk::FramebufferCreateInfo framebufferInfo;
    framebufferInfo.flags = vk::FramebufferCreateFlags();
    framebufferInfo.renderPass = inputChunk.renderpass;
    framebufferInfo.attachmentCount = attachments.size();
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = inputChunk.swapchainExtent.width;
    framebufferInfo.height = inputChunk.swapchainExtent.height;
    framebufferInfo.layers = 1;

    try
    {
      frames[i].framebuffer = inputChunk.device.createFramebuffer(framebufferInfo);

      if (debug)
      {
        std::cout << "Created framebuffer for frame " << i << std::endl;
      }
    }
    catch (vk::SystemError err)
    {
      if (debug)
      {
        std::cout << "Failed to create framebuffer for frame " << i << std::endl;
      }
    }
  }
#endif
}
}
