#pragma once

#include <array>
#include <cstdint>

namespace sps::vulkan {

/// The following data will be replaced by CMake setup.
constexpr const char *APP_NAME{"Vulkan renderer example"};
constexpr std::array<std::uint32_t, 3> APP_VERSION{0, 1, 0};
constexpr const char *APP_VERSION_STR{"0.1.0"};
constexpr const char* ENGINE_NAME{"Inexor Engine"};
constexpr std::array<std::uint32_t, 3> ENGINE_VERSION{0, 1, 0};
constexpr const char *ENGINE_VERSION_STR{"0.1.0"};
constexpr const char *BUILD_GIT = "50378ec";
constexpr const char *BUILD_TYPE = "";

} // namespace inexor::vulkan_renderer
