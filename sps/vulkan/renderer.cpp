#include <GLFW/glfw3.h>
#include <sps/vulkan/config.h>

#include <spdlog/spdlog.h>
#include <sps/vulkan/renderer.h>

namespace sps::vulkan
{
VulkanRenderer::~VulkanRenderer()
{
  spdlog::trace("Shutting down vulkan renderer");
  if (m_device == VK_NULL_HANDLE)
  {
    return;
  }
  m_device->wait_idle();

#if 0
  if (!m_debug_report_callback_initialised) {
    return;
  }

  // TODO(): Is there a better way to do this? Maybe add a helper function to wrapper::Instance?
  auto vk_destroy_debug_report_callback = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(									vkGetInstanceProcAddr(m_instance->instance(), "vkDestroyDebugReportCallbackEXT"));
  if (vk_destroy_debug_report_callback != nullptr) {
    vk_destroy_debug_report_callback(m_instance->instance(), m_debug_report_callback, nullptr);
  }
#endif
}

void VulkanRenderer::recreate_swapchain()
{
  //  m_window->wait_for_focus();
  m_device->wait_idle();
  // Query frame buffer size
  int windowWidth = 0;
  int windowHeight = 0;
  glfwGetFramebufferSize(m_window->get(), &windowWidth, &windowHeight);

  // Reset render graph (not needed)

  // Recreate swapchain

  // Create new render graph

  // Setup render graph

  // Create camera

  // Update imgui overlay

  // Compile render graph
}
} // namespace sps::vulkan
