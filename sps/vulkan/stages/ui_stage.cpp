#include <sps/vulkan/stages/ui_stage.h>

namespace sps::vulkan
{

void UIStage::record(const FrameContext& ctx)
{
  (*m_callback)(ctx.command_buffer);
}

bool UIStage::is_enabled() const
{
  return m_callback && *m_callback;
}

} // namespace sps::vulkan
