#pragma once

namespace sps::vulkan {

/// @brief A small helper function that return vulkan create infos with sType already set
/// @code{.cpp}
/// auto render_pass_ci = make_info<VkRenderPassCreateInfo>();
/// @endcode
/// @note Also zeros the returned struct
template <typename T>
[[nodiscard]] T make_info(T = {});

} // namespace sps::vulkan
