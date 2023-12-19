#include <sps/vulkan/frame.h>

#include <memory>
#include <sps/vulkan/device.h>
#include <sps/vulkan/fence.h>
#include <sps/vulkan/semaphore.h>
#include <utility>

namespace sps::vulkan
{

Frame::Frame(Device& device, const vk::Image& image, const vk::Format& format)
  : m_device(device)
  , m_image(image)
{
  vk::ImageViewCreateInfo createInfo = {};
  // TODO: Create image views either here or inside swapchain

  m_imageAvailable = std::make_unique<Semaphore>(m_device, "Swapchain image available");
  m_renderFinished = std::make_unique<Semaphore>(m_device, "Render finished");
  m_inFlight = std::make_unique<Fence>(m_device, "In flight", true);
}

Frame::Frame(Frame&& other) noexcept
  : m_device(other.m_device)
  , m_imageAvailable(std::move(other.m_imageAvailable))
  , m_renderFinished(std::move(other.m_renderFinished))
  , m_inFlight(std::move(other.m_inFlight))
{
  m_image = std::exchange(other.m_image, nullptr);
  m_imageView = std::exchange(other.m_imageView, nullptr);
}
Frame::~Frame()
{
  m_device.device().destroyImageView(m_imageView);
  // m_device.device().destroyFramebuffer();
  // device.destroyImageView(m_imageView);
  // device.destroyFramebuffer(m_frameBuffer);
}
}
