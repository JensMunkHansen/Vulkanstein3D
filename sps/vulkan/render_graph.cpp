#include <sps/vulkan/render_graph.h>
#include <sps/vulkan/device.h>

#include <spdlog/spdlog.h>

#include <array>

namespace sps::vulkan
{

namespace
{

void begin_render_pass(const FrameContext& ctx, vk::RenderPass renderPass,
  vk::Framebuffer framebuffer, uint32_t clearCount,
  const vk::ClearValue* clearValues)
{
  vk::RenderPassBeginInfo rpInfo{};
  rpInfo.renderPass = renderPass;
  rpInfo.framebuffer = framebuffer;
  rpInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
  rpInfo.renderArea.extent = ctx.extent;
  rpInfo.clearValueCount = clearCount;
  rpInfo.pClearValues = clearValues;

  ctx.command_buffer.beginRenderPass(&rpInfo, vk::SubpassContents::eInline);

  // Set dynamic viewport
  vk::Viewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(ctx.extent.width);
  viewport.height = static_cast<float>(ctx.extent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  ctx.command_buffer.setViewport(0, 1, &viewport);

  // Set dynamic scissor
  vk::Rect2D scissor{};
  scissor.offset = vk::Offset2D{ 0, 0 };
  scissor.extent = ctx.extent;
  ctx.command_buffer.setScissor(0, 1, &scissor);
}

} // anonymous namespace

void RenderGraph::record(const FrameContext& ctx)
{
  // Phase 1: PrePass stages (outside render pass)
  for (auto& stage : m_stages)
  {
    if (stage->phase() == Phase::PrePass && stage->is_enabled())
    {
      stage->record(ctx);
    }
  }

  // Phase 2: Scene render pass (HDR target)
  bool any_scene_stage = false;
  for (auto& stage : m_stages)
  {
    if (stage->phase() == Phase::ScenePass && stage->is_enabled())
    {
      any_scene_stage = true;
      break;
    }
  }

  if (any_scene_stage)
  {
    // 3 clear values: color, depth, resolve (extra values ignored when not using MSAA)
    std::array<vk::ClearValue, 3> clearValues{};
    clearValues[0].color = vk::ClearColorValue{
      std::array<float, 4>{ ctx.clear_color.r, ctx.clear_color.g, ctx.clear_color.b, 1.0f } };
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{ 1.0f, 0 };
    clearValues[2].color = vk::ClearColorValue{
      std::array<float, 4>{ ctx.clear_color.r, ctx.clear_color.g, ctx.clear_color.b, 1.0f } };

    begin_render_pass(ctx, ctx.scene_render_pass, ctx.scene_framebuffer,
      static_cast<uint32_t>(clearValues.size()), clearValues.data());

    for (auto& stage : m_stages)
    {
      if (stage->phase() == Phase::ScenePass && stage->is_enabled())
      {
        stage->record(ctx);
      }
    }

    ctx.command_buffer.endRenderPass();
  }

  // Phase 3: Intermediate stages (outside render pass, e.g. compute blur)
  for (auto& stage : m_stages)
  {
    if (stage->phase() == Phase::Intermediate && stage->is_enabled())
    {
      stage->record(ctx);
    }
  }

  // Phase 4: Composite render pass (swapchain target)
  bool any_composite_stage = false;
  for (auto& stage : m_stages)
  {
    if (stage->phase() == Phase::CompositePass && stage->is_enabled())
    {
      any_composite_stage = true;
      break;
    }
  }

  if (any_composite_stage)
  {
    // Single clear value: swapchain color
    std::array<vk::ClearValue, 1> clearValues{};
    clearValues[0].color = vk::ClearColorValue{
      std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f } };

    begin_render_pass(ctx, ctx.composite_render_pass, ctx.composite_framebuffer,
      static_cast<uint32_t>(clearValues.size()), clearValues.data());

    for (auto& stage : m_stages)
    {
      if (stage->phase() == Phase::CompositePass && stage->is_enabled())
      {
        stage->record(ctx);
      }
    }

    ctx.command_buffer.endRenderPass();
  }
}

void RenderGraph::on_swapchain_resize(const Device& device, vk::Extent2D extent)
{
  for (auto& stage : m_stages)
  {
    stage->on_swapchain_resize(device, extent);
  }
}

} // namespace sps::vulkan
