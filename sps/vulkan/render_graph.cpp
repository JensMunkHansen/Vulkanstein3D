#include <sps/vulkan/render_graph.h>
#include <sps/vulkan/device.h>
#include <sps/vulkan/renderer.h>

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

RenderGraph::~RenderGraph()
{
  destroy_scene_framebuffers();
}

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
    auto scene_rp = m_render_passes[static_cast<int>(Phase::ScenePass)];
    auto scene_fb = m_scene_framebuffers[ctx.image_index];

    // 3 clear values: color, depth, resolve (extra values ignored when not using MSAA)
    std::array<vk::ClearValue, 3> clearValues{};
    // Alpha=0: background pixels have no SSS blur (blur shader reads alpha as blur scale)
    clearValues[0].color = vk::ClearColorValue{
      std::array<float, 4>{ ctx.clear_color.r, ctx.clear_color.g, ctx.clear_color.b, 0.0f } };
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{ 1.0f, 0 };
    clearValues[2].color = vk::ClearColorValue{
      std::array<float, 4>{ ctx.clear_color.r, ctx.clear_color.g, ctx.clear_color.b, 0.0f } };

    begin_render_pass(ctx, scene_rp, scene_fb,
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

    begin_render_pass(ctx, m_render_passes[static_cast<int>(Phase::CompositePass)], ctx.composite_framebuffer,
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

void RenderGraph::set_render_pass(Phase phase, vk::RenderPass rp)
{
  m_render_passes[static_cast<int>(phase)] = rp;
}

vk::RenderPass RenderGraph::render_pass(Phase phase) const
{
  return m_render_passes[static_cast<int>(phase)];
}

void RenderGraph::create_scene_framebuffers()
{
  vk::Extent2D extent = m_renderer->swapchain().extent();
  uint32_t count = m_renderer->swapchain().image_count();
  auto scene_rp = m_render_passes[static_cast<int>(Phase::ScenePass)];

  m_scene_framebuffers.resize(count);
  for (uint32_t i = 0; i < count; i++)
  {
    std::vector<vk::ImageView> attachments;
    if (m_renderer->msaa_samples() != vk::SampleCountFlagBits::e1)
    {
      // MSAA: [hdrMsaa, depth, hdrResolve]
      attachments = {
        m_renderer->hdr_msaa_image_view(),
        m_renderer->depth_stencil().combined_view(),
        m_renderer->hdr_image_view()
      };
    }
    else
    {
      // No MSAA: [hdr, depth]
      attachments = {
        m_renderer->hdr_image_view(),
        m_renderer->depth_stencil().combined_view()
      };
    }

    vk::FramebufferCreateInfo fbInfo{};
    fbInfo.renderPass = scene_rp;
    fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    fbInfo.pAttachments = attachments.data();
    fbInfo.width = extent.width;
    fbInfo.height = extent.height;
    fbInfo.layers = 1;
    m_scene_framebuffers[i] = m_renderer->device().device().createFramebuffer(fbInfo);
  }
}

void RenderGraph::destroy_scene_framebuffers()
{
  if (!m_renderer || m_scene_framebuffers.empty())
    return;

  for (auto fb : m_scene_framebuffers)
    m_renderer->device().device().destroyFramebuffer(fb);
  m_scene_framebuffers.clear();
}

void RenderGraph::recreate_scene_framebuffers()
{
  destroy_scene_framebuffers();
  create_scene_framebuffers();
}

} // namespace sps::vulkan
