#include "exception.h"
#include "representation.h"

namespace sps::vulkan
{

VulkanException::VulkanException(std::string message, const VkResult result)
  : SpsException(message.append(" (")
                   .append(sps::vulkan::as_string(result))
                   .append(": ")
                   .append(sps::vulkan::result_to_description(result))
                   .append(")"))
{
}

} // namespace sps::vulkan
