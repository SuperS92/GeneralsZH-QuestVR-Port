// SPDX-License-Identifier: GPL-3.0-or-later
#include "QuestColorTransfer.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include "QuestColorTransfer.spv.h"
#include <android/log.h>

namespace QuestXR {
bool ColorTransfer::initialize(VkPhysicalDevice physical, VkDevice device,
    VkFormat destinationFormat, const std::array<Source, 3> &sources)
{
    destroy();
    if (!physical || !device || destinationFormat != VK_FORMAT_R8G8B8A8_UNORM) return false;
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(physical, destinationFormat, &properties);
    if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) return false;
    for (const auto &source : sources) {
        if (!source.image || source.format != VK_FORMAT_B8G8R8A8_UNORM
            || !(source.usage & VK_IMAGE_USAGE_SAMPLED_BIT) || !source.width || !source.height
            || source.destinations.empty()) return false;
        for (VkImage image : source.destinations) if (!image) return false;
        vkGetPhysicalDeviceFormatProperties(physical, source.format, &properties);
        if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) return false;
    }
    m_device = device;
    const auto fail = [this]() { destroy(); return false; };
    VkDescriptorSetLayoutBinding binding{};
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptors.bindingCount = 1; descriptors.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device, &descriptors, nullptr, &m_descriptors) != VK_SUCCESS) return fail();
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1; layout.pSetLayouts = &m_descriptors;
    if (vkCreatePipelineLayout(device, &layout, nullptr, &m_layout) != VK_SUCCESS) return fail();
    VkAttachmentDescription attachment{};
    attachment.format = destinationFormat;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &reference;
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    pass.attachmentCount = 1; pass.pAttachments = &attachment;
    pass.subpassCount = 1; pass.pSubpasses = &subpass;
    if (vkCreateRenderPass(device, &pass, nullptr, &m_pass) != VK_SUCCESS) return fail();
    VkShaderModule modules[2] = {};
    const uint32_t *code[] = {kQuestColorTransferVertex, kQuestColorTransferFragment};
    const size_t sizes[] = {sizeof(kQuestColorTransferVertex), sizeof(kQuestColorTransferFragment)};
    for (unsigned i = 0; i < 2; ++i) {
        VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module.codeSize = sizes[i]; module.pCode = code[i];
        if (vkCreateShaderModule(device, &module, nullptr, &modules[i]) != VK_SUCCESS) {
            if (modules[0]) vkDestroyShaderModule(device, modules[0], nullptr);
            return fail();
        }
    }
    VkPipelineShaderStageCreateInfo stages[2] = {};
    for (unsigned i = 0; i < 2; ++i) {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[i].module = modules[i]; stages[i].pName = "main";
    }
    VkPipelineVertexInputStateCreateInfo vertices{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE; raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState colors{};
    colors.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1; blend.pAttachments = &colors;
    const VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = states;
    VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline.stageCount = 2; pipeline.pStages = stages;
    pipeline.pVertexInputState = &vertices; pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &viewport; pipeline.pRasterizationState = &raster;
    pipeline.pMultisampleState = &samples; pipeline.pColorBlendState = &blend;
    pipeline.pDynamicState = &dynamic; pipeline.layout = m_layout; pipeline.renderPass = m_pass;
    const VkResult created = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &m_pipeline);
    for (VkShaderModule module : modules) vkDestroyShaderModule(device, module, nullptr);
    if (created != VK_SUCCESS) return fail();
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &sampler, nullptr, &m_sampler) != VK_SUCCESS) return fail();
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 3; pool.poolSizeCount = 1; pool.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device, &pool, nullptr, &m_pool) != VK_SUCCESS) return fail();
    const VkDescriptorSetLayout layouts[] = {m_descriptors, m_descriptors, m_descriptors};
    VkDescriptorSet sets[3] = {};
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = m_pool; allocate.descriptorSetCount = 3; allocate.pSetLayouts = layouts;
    if (vkAllocateDescriptorSets(device, &allocate, sets) != VK_SUCCESS) return fail();
    for (unsigned i = 0; i < 3; ++i) {
        Channel &channel = m_channels[i]; channel.source = sources[i]; channel.descriptor = sets[i];
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = sources[i].image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = sources[i].format;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = view.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &view, nullptr, &channel.view) != VK_SUCCESS) return fail();
        VkDescriptorImageInfo image{m_sampler, channel.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = channel.descriptor; write.descriptorCount = 1;
        write.descriptorType = binding.descriptorType; write.pImageInfo = &image;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        channel.destinations.resize(sources[i].destinations.size());
        channel.framebuffers.resize(sources[i].destinations.size());
        view.format = destinationFormat; view.subresourceRange.baseArrayLayer = sources[i].destinationLayer;
        for (unsigned slot = 0; slot < channel.destinations.size(); ++slot) {
            view.image = sources[i].destinations[slot];
            if (vkCreateImageView(device, &view, nullptr, &channel.destinations[slot]) != VK_SUCCESS) return fail();
            VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            framebuffer.renderPass = m_pass; framebuffer.attachmentCount = 1;
            framebuffer.pAttachments = &channel.destinations[slot];
            framebuffer.width = sources[i].width; framebuffer.height = sources[i].height; framebuffer.layers = 1;
            if (vkCreateFramebuffer(device, &framebuffer, nullptr, &channel.framebuffers[slot]) != VK_SUCCESS) return fail();
        }
    }
    return true;
}

bool ColorTransfer::matches(uint32_t channel, VkImage source, uint32_t destination) const {
    return m_pipeline && channel < m_channels.size() && source == m_channels[channel].source.image
        && destination < m_channels[channel].framebuffers.size();
}

void ColorTransfer::record(VkCommandBuffer command, uint32_t index, uint32_t destination,
    VkImageLayout sourceLayout) const
{
    const Channel &channel = m_channels[index];
    VkImageMemoryBarrier barriers[2] = {};
    for (auto &barrier : barriers) {
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = barrier.subresourceRange.layerCount = 1;
    }
    barriers[0].image = channel.source.image;
    barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].oldLayout = sourceLayout; barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    // Acquired destination is fully overwritten, independently for each eye/layer.
    barriers[1].image = channel.source.destinations[destination];
    barriers[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[1].subresourceRange.baseArrayLayer = channel.source.destinationLayer;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, nullptr, 0, nullptr, 2, barriers);
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = m_pass; pass.framebuffer = channel.framebuffers[destination];
    pass.renderArea.extent = {channel.source.width, channel.source.height};
    vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0, 1, &channel.descriptor, 0, nullptr);
    VkViewport viewport{0, 0, float(channel.source.width), float(channel.source.height), 0, 1};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &pass.renderArea);
    vkCmdDraw(command, 3, 1, 0, 0);
    vkCmdEndRenderPass(command);
    barriers[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; barriers[0].newLayout = sourceLayout;
    barriers[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    barriers[1].oldLayout = barriers[1].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);
}

void ColorTransfer::destroy() {
    if (!m_device) return;
    for (auto &channel : m_channels) {
        for (VkFramebuffer framebuffer : channel.framebuffers) if (framebuffer) vkDestroyFramebuffer(m_device, framebuffer, nullptr);
        for (VkImageView view : channel.destinations) if (view) vkDestroyImageView(m_device, view, nullptr);
        if (channel.view) vkDestroyImageView(m_device, channel.view, nullptr);
        channel = {};
    }
    if (m_pool) vkDestroyDescriptorPool(m_device, m_pool, nullptr);
    if (m_pipeline) vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_pass) vkDestroyRenderPass(m_device, m_pass, nullptr);
    if (m_sampler) vkDestroySampler(m_device, m_sampler, nullptr);
    if (m_layout) vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    if (m_descriptors) vkDestroyDescriptorSetLayout(m_device, m_descriptors, nullptr);
    m_device = VK_NULL_HANDLE; m_pool = VK_NULL_HANDLE; m_pipeline = VK_NULL_HANDLE;
    m_pass = VK_NULL_HANDLE; m_sampler = VK_NULL_HANDLE; m_layout = VK_NULL_HANDLE; m_descriptors = VK_NULL_HANDLE;
}
}
#endif
