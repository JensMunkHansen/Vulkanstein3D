#pragma once

#include <sps/vulkan/config.h>

#include <string>
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
