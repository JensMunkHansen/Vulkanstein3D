#include "exception.h"
#include "representation.h"

namespace sps::vulkan
{

VulkanException::VulkanException(std::string message, const VkResult result)
  : SpsException(message.append(" (")
                   .append(sps::vulkan::utils::as_string(result))
                   .append(": ")
                   .append(sps::vulkan::utils::result_to_description(result))
                   .append(")"))
{
}

VulkanException::VulkanException(std::string message, const vk::Result result)
  : SpsException(message.append(" (")
                   .append(vk::to_string(result))
                   .append(": ")
                   .append(sps::vulkan::utils::result_to_description(static_cast<VkResult>(result)))
                   .append(")"))
{
}

} // namespace sps::vulkan
