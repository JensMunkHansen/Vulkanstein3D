#include "sps/vulkan/swapchain.h"
#include <sps/vulkan/config.h>

#include <vulkan/vulkan.hpp>

namespace sps::vulkan
{
class Swapchain;
/**
        Data structures involved in making framebuffers for the
        swapchain.
*/
struct framebufferInput
{
  vk::Device device;
  vk::RenderPass renderpass;
  vk::Extent2D swapchainExtent;
};

/**
        Make framebuffers for the swapchain

        \param inputChunk required input for creation
        \param frames the vector to be populated with the created framebuffers
        \param debug whether the system is running in debug mode.
*/
std::vector<vk::Framebuffer> make_framebuffers(
  framebufferInput inputChunk, const Swapchain& swapchaine, bool debug);

}
