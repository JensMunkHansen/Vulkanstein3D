#pragma once

#include <sps/vulkan/config.h>

#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace sps::vulkan
{
/**
        holds the data structures used to create a pipeline
*/
struct GraphicsPipelineInBundle
{
  vk::Device device;
  std::string vertexFilepath;
  std::string fragmentFilepath;
  vk::Extent2D swapchainExtent;
  vk::Format swapchainImageFormat;
  vk::DescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE }; // Optional

  // Vertex input (optional - if empty, no vertex buffers used)
  std::vector<vk::VertexInputBindingDescription> vertexBindings;
  std::vector<vk::VertexInputAttributeDescription> vertexAttributes;

  // Rasterizer options
  bool backfaceCulling{ true };

  // Depth testing
  bool depthTestEnabled{ false };
  vk::Format depthFormat{ vk::Format::eD32Sfloat };

  // Optional: use existing render pass instead of creating new one
  vk::RenderPass existingRenderPass{ VK_NULL_HANDLE };

  // Optional: use existing pipeline layout instead of creating new one
  vk::PipelineLayout existingPipelineLayout{ VK_NULL_HANDLE };
};

/**
        Used for returning the pipeline, along with associated data structures,
        after creation.
*/
struct GraphicsPipelineOutBundle
{
  vk::PipelineLayout layout;
  vk::RenderPass renderpass;
  vk::Pipeline pipeline;
};

vk::PipelineLayout make_pipeline_layout(vk::Device device, bool debug);

vk::RenderPass make_renderpass(vk::Device device, vk::Format swapchainImageFormat, bool debug);

GraphicsPipelineOutBundle create_graphics_pipeline(
  GraphicsPipelineInBundle& specification, bool debug);

class Pipeline
{
};
}
