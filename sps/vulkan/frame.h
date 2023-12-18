#pragma once

#include <vulkan/vulkan.hpp>

#include <memory>

namespace sps::vulkan
{
class Semaphore;
class Fence;
class Device;
class Frame
{
private:
  const Device& m_device;
  vk::Image m_image;
  vk::ImageView m_imageView;

  std::unique_ptr<Semaphore> m_imageAvailable;
  std::unique_ptr<Semaphore> m_renderFinished;
  std::unique_ptr<Fence> m_inFlight;

public:
  Frame() = delete;
  Frame(Device& device, const vk::Image& image, const vk::Format& format);
  Frame(const Frame&) = delete;
  Frame(Frame&&) noexcept;
  ~Frame();

  [[nodiscard]] vk::Image image() const { return m_image; }
  [[nodiscard]] vk::ImageView imageView() const { return m_imageView; }

  Frame& operator=(const Frame&) = delete;
  Frame& operator=(Frame&&) = delete;
};
}
