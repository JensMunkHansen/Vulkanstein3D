#include <sps/vulkan/pipeline.h>

#include <sps/vulkan/shaders.h>

#include <iostream>

namespace sps::vulkan
{
vk::PipelineLayout make_pipeline_layout(
  vk::Device device, vk::DescriptorSetLayout descriptorSetLayout, bool debug)
{
  vk::PipelineLayoutCreateInfo layoutInfo;
  layoutInfo.flags = vk::PipelineLayoutCreateFlags();
  layoutInfo.pushConstantRangeCount = 0;

  if (descriptorSetLayout)
  {
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
  }
  else
  {
    layoutInfo.setLayoutCount = 0;
  }

  try
  {
    return device.createPipelineLayout(layoutInfo);
  }
  catch (vk::SystemError err)
  {
    if (debug)
    {
      std::cout << "Failed to create pipeline layout!" << std::endl;
    }
  }
  return nullptr;
}

vk::RenderPass make_renderpass(vk::Device device, vk::Format swapchainImageFormat,
  bool depthEnabled, vk::Format depthFormat, bool debug)
{
  std::vector<vk::AttachmentDescription> attachments;

  // Color attachment
  vk::AttachmentDescription colorAttachment = {};
  colorAttachment.flags = vk::AttachmentDescriptionFlags();
  colorAttachment.format = swapchainImageFormat;
  colorAttachment.samples = vk::SampleCountFlagBits::e1;
  colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
  colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
  colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
  colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
  colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
  colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;
  attachments.push_back(colorAttachment);

  // Depth attachment (optional)
  vk::AttachmentDescription depthAttachment = {};
  if (depthEnabled)
  {
    depthAttachment.flags = vk::AttachmentDescriptionFlags();
    depthAttachment.format = depthFormat;
    depthAttachment.samples = vk::SampleCountFlagBits::e1;
    depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
    depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    attachments.push_back(depthAttachment);
  }

  // Color attachment reference
  vk::AttachmentReference colorAttachmentRef = {};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

  // Depth attachment reference
  vk::AttachmentReference depthAttachmentRef = {};
  depthAttachmentRef.attachment = 1;
  depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

  // Subpass
  vk::SubpassDescription subpass = {};
  subpass.flags = vk::SubpassDescriptionFlags();
  subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;
  if (depthEnabled)
  {
    subpass.pDepthStencilAttachment = &depthAttachmentRef;
  }

  // Subpass dependency
  vk::SubpassDependency dependency = {};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask =
    vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
  dependency.srcAccessMask = vk::AccessFlagBits::eNone;
  dependency.dstStageMask =
    vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
  dependency.dstAccessMask =
    vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

  // Create renderpass
  vk::RenderPassCreateInfo renderpassInfo = {};
  renderpassInfo.flags = vk::RenderPassCreateFlags();
  renderpassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  renderpassInfo.pAttachments = attachments.data();
  renderpassInfo.subpassCount = 1;
  renderpassInfo.pSubpasses = &subpass;
  renderpassInfo.dependencyCount = 1;
  renderpassInfo.pDependencies = &dependency;

  try
  {
    return device.createRenderPass(renderpassInfo);
  }
  catch (vk::SystemError err)
  {
    if (debug)
    {
      std::cout << "Failed to create renderpass!" << std::endl;
    }
  }
  return nullptr;
}

/**
        Make a graphics pipeline, along with renderpass and pipeline layout

        \param specification the struct holding input data, as specified at the top of the file.
        \param debug whether the system is running in debug mode
        \returns the bundle of data structures created
*/
GraphicsPipelineOutBundle create_graphics_pipeline(
  GraphicsPipelineInBundle& specification, bool debug)
{
  /*
   * Build and return a graphics pipeline based on the given info.
   */

  // The info for the graphics pipeline
  vk::GraphicsPipelineCreateInfo pipelineInfo = {};
  pipelineInfo.flags = vk::PipelineCreateFlags();

  // Shader stages, to be populated later
  std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;

  // Vertex Input
  vk::PipelineVertexInputStateCreateInfo vertexInputInfo = {};
  vertexInputInfo.flags = vk::PipelineVertexInputStateCreateFlags();
  vertexInputInfo.vertexBindingDescriptionCount =
    static_cast<uint32_t>(specification.vertexBindings.size());
  vertexInputInfo.pVertexBindingDescriptions = specification.vertexBindings.data();
  vertexInputInfo.vertexAttributeDescriptionCount =
    static_cast<uint32_t>(specification.vertexAttributes.size());
  vertexInputInfo.pVertexAttributeDescriptions = specification.vertexAttributes.data();
  pipelineInfo.pVertexInputState = &vertexInputInfo;

  // Input Assembly
  vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {};
  inputAssemblyInfo.flags = vk::PipelineInputAssemblyStateCreateFlags();
  inputAssemblyInfo.topology = vk::PrimitiveTopology::eTriangleList;
  pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;

  // Vertex Shader
  if (debug)
  {
    std::cout << "Create vertex shader module" << std::endl;
  }
  vk::ShaderModule vertexShader =
    sps::vulkan::createModule(specification.vertexFilepath, specification.device, debug);
  vk::PipelineShaderStageCreateInfo vertexShaderInfo = {};
  vertexShaderInfo.flags = vk::PipelineShaderStageCreateFlags();
  vertexShaderInfo.stage = vk::ShaderStageFlagBits::eVertex;
  vertexShaderInfo.module = vertexShader;
  vertexShaderInfo.pName = "main";
  shaderStages.push_back(vertexShaderInfo);

  // Viewport and Scissor - using dynamic state
  vk::PipelineViewportStateCreateInfo viewportState = {};
  viewportState.flags = vk::PipelineViewportStateCreateFlags();
  viewportState.viewportCount = 1;
  viewportState.pViewports = nullptr; // Dynamic state
  viewportState.scissorCount = 1;
  viewportState.pScissors = nullptr; // Dynamic state
  pipelineInfo.pViewportState = &viewportState;

  // Rasterizer
  vk::PipelineRasterizationStateCreateInfo rasterizer = {};
  rasterizer.flags = vk::PipelineRasterizationStateCreateFlags();
  rasterizer.depthClampEnable = VK_FALSE; // discard out of bounds fragments, don't clamp them
  rasterizer.rasterizerDiscardEnable = VK_FALSE; // This flag would disable fragment output
  rasterizer.polygonMode = vk::PolygonMode::eFill;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode =
    specification.backfaceCulling ? vk::CullModeFlagBits::eBack : vk::CullModeFlagBits::eNone;
  rasterizer.frontFace = vk::FrontFace::eClockwise;
  rasterizer.depthBiasEnable = VK_FALSE; // Depth bias can be useful in shadow maps.
  pipelineInfo.pRasterizationState = &rasterizer;

  // Fragment Shader
  if (debug)
  {
    std::cout << "Create fragment shader module" << std::endl;
  }
  vk::ShaderModule fragmentShader =
    sps::vulkan::createModule(specification.fragmentFilepath, specification.device, debug);
  vk::PipelineShaderStageCreateInfo fragmentShaderInfo = {};
  fragmentShaderInfo.flags = vk::PipelineShaderStageCreateFlags();
  fragmentShaderInfo.stage = vk::ShaderStageFlagBits::eFragment;
  fragmentShaderInfo.module = fragmentShader;
  fragmentShaderInfo.pName = "main";
  shaderStages.push_back(fragmentShaderInfo);
  // Now both shaders have been made, we can declare them to the pipeline info
  pipelineInfo.stageCount = shaderStages.size();
  pipelineInfo.pStages = shaderStages.data();

  // Multisampling
  vk::PipelineMultisampleStateCreateInfo multisampling = {};
  multisampling.flags = vk::PipelineMultisampleStateCreateFlags();
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
  pipelineInfo.pMultisampleState = &multisampling;

  // Depth Stencil
  vk::PipelineDepthStencilStateCreateInfo depthStencil = {};
  depthStencil.depthTestEnable = specification.depthTestEnabled ? VK_TRUE : VK_FALSE;
  depthStencil.depthWriteEnable = specification.depthTestEnabled ? VK_TRUE : VK_FALSE;
  depthStencil.depthCompareOp = vk::CompareOp::eLess;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable = VK_FALSE;
  pipelineInfo.pDepthStencilState = &depthStencil;

  // Color Blend
  vk::PipelineColorBlendAttachmentState colorBlendAttachment = {};
  colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
    vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB |
    vk::ColorComponentFlagBits::eA;
  colorBlendAttachment.blendEnable = VK_FALSE;
  vk::PipelineColorBlendStateCreateInfo colorBlending = {};
  colorBlending.flags = vk::PipelineColorBlendStateCreateFlags();
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.logicOp = vk::LogicOp::eCopy;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;
  colorBlending.blendConstants[0] = 0.0f;
  colorBlending.blendConstants[1] = 0.0f;
  colorBlending.blendConstants[2] = 0.0f;
  colorBlending.blendConstants[3] = 0.0f;
  pipelineInfo.pColorBlendState = &colorBlending;

  // Pipeline Layout - use existing if provided, otherwise create new
  vk::PipelineLayout pipelineLayout;
  if (specification.existingPipelineLayout)
  {
    if (debug)
    {
      std::cout << "Using existing Pipeline Layout" << std::endl;
    }
    pipelineLayout = specification.existingPipelineLayout;
  }
  else
  {
    if (debug)
    {
      std::cout << "Create Pipeline Layout" << std::endl;
    }
    pipelineLayout =
      make_pipeline_layout(specification.device, specification.descriptorSetLayout, debug);
  }
  pipelineInfo.layout = pipelineLayout;

  // Renderpass - use existing if provided, otherwise create new
  vk::RenderPass renderpass;
  bool ownsRenderPass = false;
  if (specification.existingRenderPass)
  {
    if (debug)
    {
      std::cout << "Using existing RenderPass" << std::endl;
    }
    renderpass = specification.existingRenderPass;
  }
  else
  {
    if (debug)
    {
      std::cout << "Create RenderPass" << std::endl;
    }
    renderpass = make_renderpass(specification.device, specification.swapchainImageFormat,
      specification.depthTestEnabled, specification.depthFormat, debug);
    ownsRenderPass = true;
  }
  pipelineInfo.renderPass = renderpass;
  pipelineInfo.subpass = 0;

  // Dynamic state for viewport and scissor
  std::vector<vk::DynamicState> dynamicStates = {
    vk::DynamicState::eViewport,
    vk::DynamicState::eScissor
  };
  vk::PipelineDynamicStateCreateInfo dynamicState = {};
  dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();
  pipelineInfo.pDynamicState = &dynamicState;

  // Extra stuff
  pipelineInfo.basePipelineHandle = nullptr;

  // Make the Pipeline
  if (debug)
  {
    std::cout << "Create Graphics Pipeline" << std::endl;
  }
  vk::Pipeline graphicsPipeline;
  try
  {
    graphicsPipeline = (specification.device.createGraphicsPipeline(nullptr, pipelineInfo)).value;
  }
  catch (vk::SystemError err)
  {
    if (debug)
    {
      std::cout << "Failed to create Pipeline" << std::endl;
    }
  }

  GraphicsPipelineOutBundle output;
  output.layout = pipelineLayout;
  output.renderpass = renderpass;
  output.pipeline = graphicsPipeline;

  // Finally clean up by destroying shader modules
  specification.device.destroyShaderModule(vertexShader);
  specification.device.destroyShaderModule(fragmentShader);

  return output;
}

}
