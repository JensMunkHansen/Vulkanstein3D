#include <sps/vulkan/render_graph.h>
#include <sps/vulkan/device.h>

#include <spdlog/spdlog.h>

#include <array>

namespace sps::vulkan
{

void RenderGraph::record(const FrameContext& ctx)
{
  // Phase 1: Execute pre-pass stages (uses_render_pass() == false)
  for (auto& stage : m_stages)
  {
    if (!stage->uses_render_pass() && stage->is_enabled())
    {
      stage->record(ctx);
    }
  }

  // Phase 2: Check if any render-pass stage is enabled
  bool any_rp_stage = false;
  for (auto& stage : m_stages)
  {
    if (stage->uses_render_pass() && stage->is_enabled())
    {
      any_rp_stage = true;
      break;
    }
  }

  if (any_rp_stage)
  {
    // Begin render pass
    vk::RenderPassBeginInfo renderPassInfo{};
    renderPassInfo.renderPass = ctx.render_pass;
    renderPassInfo.framebuffer = ctx.framebuffer;
    renderPassInfo.renderArea.offset.x = 0;
    renderPassInfo.renderArea.offset.y = 0;
    renderPassInfo.renderArea.extent = ctx.extent;

    // 3 clear values: color, depth, resolve (extra values are ignored when not using MSAA)
    std::array<vk::ClearValue, 3> clearValues{};
    clearValues[0].color = vk::ClearColorValue{ std::array<float, 4>{ ctx.clear_color.r, ctx.clear_color.g, ctx.clear_color.b, 1.0f } };
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{ 1.0f, 0 };
    clearValues[2].color = vk::ClearColorValue{ std::array<float, 4>{ ctx.clear_color.r, ctx.clear_color.g, ctx.clear_color.b, 1.0f } };
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    ctx.command_buffer.beginRenderPass(&renderPassInfo, vk::SubpassContents::eInline);

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

    // Phase 3: Execute render-pass stages in order
    for (auto& stage : m_stages)
    {
      if (stage->uses_render_pass() && stage->is_enabled())
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
