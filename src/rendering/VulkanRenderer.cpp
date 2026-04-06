#include "Inventor/VulkanRenderer.h"
#include "Inventor/ModernVulkanBackend.h"
#include "Inventor/VulkanStateManager.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

VulkanRenderer::VulkanRenderer(ModernVulkanBackend* backend, VulkanStateManager* stateManager)
    : backend(backend), stateManager(stateManager) {}

VulkanRenderer::~VulkanRenderer() {
    cleanup();
}

void VulkanRenderer::init(VkFormat colorFormat, VkExtent2D extent, const std::string& vertShaderPath, const std::string& fragShaderPath, VkBuffer cameraUBO) {
    targetFormat = colorFormat;
    renderExtent = extent;
    createRenderPass(colorFormat);
    createDescriptorSetLayout();
    createGraphicsPipeline(vertShaderPath, fragShaderPath);
    allocateDescriptorSet(cameraUBO);
}

void VulkanRenderer::createFramebuffers(const std::vector<VkImageView>& imageViews) {
    swapchainFramebuffers.resize(imageViews.size());
    for (size_t i = 0; i < imageViews.size(); i++) {
        VkImageView attachments[] = { imageViews[i] };
        
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = renderExtent.width;
        framebufferInfo.height = renderExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(backend->getDevice(), &framebufferInfo, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}

void VulkanRenderer::cleanup() {
    VkDevice device = backend->getDevice();
    
    if (descriptorPool) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    for (auto framebuffer : swapchainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    swapchainFramebuffers.clear();

    if (graphicsPipeline) vkDestroyPipeline(device, graphicsPipeline, nullptr);
    if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    if (descriptorSetLayout) vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    if (renderPass) vkDestroyRenderPass(device, renderPass, nullptr);
}

void VulkanRenderer::createRenderPass(VkFormat colorFormat) {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = colorFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(backend->getDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }
}



void VulkanRenderer::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding transformBinding{};
    transformBinding.binding = 0;
    transformBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    transformBinding.descriptorCount = 1;
    transformBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding materialBinding{};
    materialBinding.binding = 1;
    materialBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    materialBinding.descriptorCount = 1;
    materialBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding cameraBinding{};
    cameraBinding.binding = 2;
    cameraBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    cameraBinding.descriptorCount = 1;
    cameraBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding shapeMapBinding{};
    shapeMapBinding.binding = 3;
    shapeMapBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    shapeMapBinding.descriptorCount = 1;
    shapeMapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding lightBinding{};
    lightBinding.binding = 4;
    lightBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    lightBinding.descriptorCount = 1;
    lightBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding bindings[] = {transformBinding, materialBinding, cameraBinding, shapeMapBinding, lightBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 5;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(backend->getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create ds layout!");
    }
}

VkShaderModule VulkanRenderer::createShaderModule(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open shader file: " + path);
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = buffer.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(buffer.data());
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(backend->getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }
    return shaderModule;
}

void VulkanRenderer::createGraphicsPipeline(const std::string& vertPath, const std::string& fragPath) {
    auto vertShaderModule = createShaderModule(vertPath);
    auto fragShaderModule = createShaderModule(fragPath);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    
    // We define a positional float3 vertex
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(float) * 3;
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescription{};
    attributeDescription.binding = 0;
    attributeDescription.location = 0;
    attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescription.offset = 0;

    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)renderExtent.width;
    viewport.height = (float)renderExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = renderExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

    if (vkCreatePipelineLayout(backend->getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = nullptr; // Optional
    pipelineInfo.pColorBlendState = &colorBlending;
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional

    if (vkCreateGraphicsPipelines(backend->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline!");
    }

    vkDestroyShaderModule(backend->getDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(backend->getDevice(), vertShaderModule, nullptr);
}


void VulkanRenderer::allocateDescriptorSet(VkBuffer cameraUBO) {
    std::vector<VkDescriptorPoolSize> poolSizes(4);
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 1; // transform
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = 3; // material, shapeMap, lights
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[2].descriptorCount = 1; // camera

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(backend->getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    if (vkAllocateDescriptorSets(backend->getDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }

    VkDescriptorBufferInfo transformInfo{};
    transformInfo.buffer = stateManager->getTransformBuffer();
    transformInfo.offset = 0;
    transformInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo materialInfo{};
    materialInfo.buffer = stateManager->getMaterialBuffer();
    materialInfo.offset = 0;
    materialInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo cameraInfo{};
    cameraInfo.buffer = cameraUBO;
    cameraInfo.offset = 0;
    cameraInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo shapeMapInfo{};
    shapeMapInfo.buffer = stateManager->getShapeMaterialMapBuffer();
    shapeMapInfo.offset = 0;
    shapeMapInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo lightInfo{};
    lightInfo.buffer = stateManager->getLightUBO();
    lightInfo.offset = 0;
    lightInfo.range = VK_WHOLE_SIZE;

    std::vector<VkWriteDescriptorSet> descriptorWrites;

    VkWriteDescriptorSet transformWrite{};
    transformWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    transformWrite.dstSet = descriptorSet;
    transformWrite.dstBinding = 0;
    transformWrite.dstArrayElement = 0;
    transformWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    transformWrite.descriptorCount = 1;
    transformWrite.pBufferInfo = &transformInfo;
    descriptorWrites.push_back(transformWrite);

    VkWriteDescriptorSet materialWrite{};
    materialWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    materialWrite.dstSet = descriptorSet;
    materialWrite.dstBinding = 1;
    materialWrite.dstArrayElement = 0;
    materialWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    materialWrite.descriptorCount = 1;
    materialWrite.pBufferInfo = &materialInfo;
    descriptorWrites.push_back(materialWrite);

    VkWriteDescriptorSet cameraWrite{};
    cameraWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    cameraWrite.dstSet = descriptorSet;
    cameraWrite.dstBinding = 2;
    cameraWrite.dstArrayElement = 0;
    cameraWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    cameraWrite.descriptorCount = 1;
    cameraWrite.pBufferInfo = &cameraInfo;
    descriptorWrites.push_back(cameraWrite);

    if (shapeMapInfo.buffer != VK_NULL_HANDLE) {
        VkWriteDescriptorSet shapeMapWrite{};
        shapeMapWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        shapeMapWrite.dstSet = descriptorSet;
        shapeMapWrite.dstBinding = 3;
        shapeMapWrite.dstArrayElement = 0;
        shapeMapWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        shapeMapWrite.descriptorCount = 1;
        shapeMapWrite.pBufferInfo = &shapeMapInfo;
        descriptorWrites.push_back(shapeMapWrite);
    }
    
    if (lightInfo.buffer != VK_NULL_HANDLE) {
        VkWriteDescriptorSet lightWrite{};
        lightWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        lightWrite.dstSet = descriptorSet;
        lightWrite.dstBinding = 4;
        lightWrite.dstArrayElement = 0;
        lightWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        lightWrite.descriptorCount = 1;
        lightWrite.pBufferInfo = &lightInfo;
        descriptorWrites.push_back(lightWrite);
    }

    vkUpdateDescriptorSets(backend->getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void VulkanRenderer::bindAndDraw(VkCommandBuffer cmdBuffer, uint32_t framebufferIndex, uint32_t maxDrawCount) {
    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapchainFramebuffers[framebufferIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = renderExtent;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(renderExtent.width);
    viewport.height = static_cast<float>(renderExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = renderExtent;
    vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

    VkBuffer vertexBuffers[] = {stateManager->getGlobalVertexBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmdBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmdBuffer, stateManager->getGlobalIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexedIndirect(cmdBuffer, stateManager->getIndirectDrawBuffer(), 0, maxDrawCount, sizeof(VkDrawIndexedIndirectCommand));

    vkCmdEndRenderPass(cmdBuffer);
}
