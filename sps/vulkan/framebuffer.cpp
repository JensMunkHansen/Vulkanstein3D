#include <sps/vulkan/framebuffer.h>

#include <sps/vulkan/swapchain.h>

#include <iostream>
#include <vulkan/vulkan_handles.hpp>

namespace sps::vulkan
{

/**
        Make framebuffers for the swapchain

        \param inputChunk required input for creation
        \param frames the vector to be populated with the created framebuffers
        \param debug whether the system is running in debug mode.
*/
std::vector<vk::Framebuffer> make_framebuffers(framebufferInput inputChunk, //
  const Swapchain& swapchain, bool debug)
{
  std::vector<vk::Framebuffer> frameBuffers;
  frameBuffers.resize(swapchain.image_count());

  for (size_t i = 0; i < swapchain.image_count(); i++)
  {
    std::vector<vk::ImageView> attachments = { swapchain.image_views()[i] };

    // Add depth attachment if provided (shared across all framebuffers)
    if (inputChunk.depthImageView)
    {
      attachments.push_back(inputChunk.depthImageView);
    }
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
      frameBuffers[i] = inputChunk.device.createFramebuffer(framebufferInfo);

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
  return frameBuffers;
}
}
