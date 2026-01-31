#pragma once

#include <vulkan/vulkan.hpp>

#include <glm/glm.hpp>

#include <array>

namespace sps::vulkan
{

/// @brief Vertex structure for mesh rendering.
///
/// Matches the vertex shader input layout:
///   layout(location = 0) in vec3 inPosition;
///   layout(location = 1) in vec3 inNormal;
///   layout(location = 2) in vec3 inColor;
struct Vertex
{
  glm::vec3 position{ 0.0f };
  glm::vec3 normal{ 0.0f, 0.0f, 1.0f };
  glm::vec3 color{ 1.0f };

  /// @brief Get the vertex binding description.
  /// Describes how to read vertex data from the buffer.
  static vk::VertexInputBindingDescription binding_description()
  {
    vk::VertexInputBindingDescription description{};
    description.binding = 0;
    description.stride = sizeof(Vertex);
    description.inputRate = vk::VertexInputRate::eVertex;
    return description;
  }

  /// @brief Get the vertex attribute descriptions.
  /// Describes the layout of each vertex attribute.
  static std::array<vk::VertexInputAttributeDescription, 3> attribute_descriptions()
  {
    std::array<vk::VertexInputAttributeDescription, 3> descriptions{};

    // Position at location 0
    descriptions[0].binding = 0;
    descriptions[0].location = 0;
    descriptions[0].format = vk::Format::eR32G32B32Sfloat;
    descriptions[0].offset = offsetof(Vertex, position);

    // Normal at location 1
    descriptions[1].binding = 0;
    descriptions[1].location = 1;
    descriptions[1].format = vk::Format::eR32G32B32Sfloat;
    descriptions[1].offset = offsetof(Vertex, normal);

    // Color at location 2
    descriptions[2].binding = 0;
    descriptions[2].location = 2;
    descriptions[2].format = vk::Format::eR32G32B32Sfloat;
    descriptions[2].offset = offsetof(Vertex, color);

    return descriptions;
  }
};

} // namespace sps::vulkan
