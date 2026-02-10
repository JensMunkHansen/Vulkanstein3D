#pragma once

#include <sps/vulkan/render_stage.h>

#include <functional>

namespace sps::vulkan
{

/// Calls the application's UI render callback (ImGui etc.) inside the render pass.
class UIStage : public RenderStage
{
public:
  using RenderCallback = std::function<void(vk::CommandBuffer)>;

  explicit UIStage(const RenderCallback* callback)
    : RenderStage("UIStage"), m_callback(callback)
  {
  }

  void record(const FrameContext& ctx) override;
  [[nodiscard]] bool is_enabled() const override;
  [[nodiscard]] Phase phase() const override { return Phase::CompositePass; }

private:
  const RenderCallback* m_callback;
};

} // namespace sps::vulkan
