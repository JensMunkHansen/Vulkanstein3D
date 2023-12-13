#include <sps/vulkan/config.h>

#include <spdlog/spdlog.h>
#include <sps/vulkan/renderer.h>

namespace sps::vulkan
{
VulkanRenderer::~VulkanRenderer()
{
  spdlog::trace("Shutting down vulkan renderer");
}
} // namespace sps::vulkan
