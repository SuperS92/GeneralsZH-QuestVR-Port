// SPDX-License-Identifier: GPL-3.0-or-later
#include "QuestSpatialAA.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include "QuestSpatialAA.spv.h"
#include <android/log.h>

namespace QuestXR {
bool SpatialAA::initialize(VkPhysicalDevice physical, VkDevice device, uint32_t queueFamily,
    const std::array<VkImage, 2> &sources, VkFormat format,
    const std::array<VkImageUsageFlags, 2> &usage,
    uint32_t width, uint32_t height, uint32_t slots, uint32_t mode, VkQueue queue)
{
    m_device = device;
    auto fail = [this]() {
        __android_log_print(ANDROID_LOG_WARN, "GeneralsXR", "Spatial AA unavailable; retaining unfiltered stereo");
        destroy();
        return false;
    };
    // GeneralsX @feature Codex 01/10/2026 Allocate only the selected AA mode.
    if (mode == 2) {
        if (!m_smaa.initialize(physical, device, queue, queueFamily, sources, format,
            usage, width, height, slots)) return fail();
        m_sourceImages = sources;
        return true;
    }
    if (mode != 1) return fail();
    if (width == 0 || height == 0 || slots == 0 || slots > 16
        || (format != VK_FORMAT_B8G8R8A8_UNORM && format != VK_FORMAT_R8G8B8A8_UNORM)
        || (usage[0] & VK_IMAGE_USAGE_SAMPLED_BIT) == 0
        || (usage[1] & VK_IMAGE_USAGE_SAMPLED_BIT) == 0) return fail();
    uint32_t queueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &queueCount, nullptr);
    std::vector<VkQueueFamilyProperties> queues(queueCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &queueCount, queues.data());
    if (queueFamily >= queues.size() || !(queues[queueFamily].queueFlags & VK_QUEUE_COMPUTE_BIT)) return fail();
    VkFormatProperties inputProperties{}, outputProperties{};
    vkGetPhysicalDeviceFormatProperties(physical, format, &inputProperties);
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R8G8B8A8_UNORM, &outputProperties);
    const VkFormatFeatureFlags sampled = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((inputProperties.optimalTilingFeatures & sampled) != sampled
        || !(outputProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) return fail();
    VkImageFormatProperties imageProperties{};
    if (vkGetPhysicalDeviceImageFormatProperties(physical, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        0, &imageProperties) != VK_SUCCESS || width > imageProperties.maxExtent.width
        || height > imageProperties.maxExtent.height) return fail();
    m_width = width;
    m_height = height;
    m_sourceImages = sources;
    VkDescriptorSetLayoutBinding bindings[2] = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo descriptorInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptorInfo.bindingCount = 2;
    descriptorInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device, &descriptorInfo, nullptr, &m_descriptors) != VK_SUCCESS) return fail();
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_descriptors;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_layout) != VK_SUCCESS) return fail();
    VkShaderModule module = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    moduleInfo.codeSize = sizeof(kQuestSpatialAaShader);
    moduleInfo.pCode = kQuestSpatialAaShader;
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &module) != VK_SUCCESS) return fail();
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = module;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = m_layout;
    const VkResult pipelineResult = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);
    vkDestroyShaderModule(device, module, nullptr);
    if (pipelineResult != VK_SUCCESS) return fail();
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) return fail();
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = viewInfo.subresourceRange.layerCount = 1;
    for (uint32_t eye = 0; eye < 2; ++eye) {
        viewInfo.image = sources[eye];
        if (vkCreateImageView(device, &viewInfo, nullptr, &m_sources[eye]) != VK_SUCCESS) return fail();
    }
    const uint32_t targetCount = slots * 2;
    VkDescriptorPoolSize poolSizes[2] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, targetCount}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, targetCount}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = targetCount;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS) return fail();
    std::vector<VkDescriptorSetLayout> layouts(targetCount, m_descriptors);
    std::vector<VkDescriptorSet> descriptors(targetCount);
    VkDescriptorSetAllocateInfo allocateDescriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocateDescriptors.descriptorPool = m_pool;
    allocateDescriptors.descriptorSetCount = targetCount;
    allocateDescriptors.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device, &allocateDescriptors, descriptors.data()) != VK_SUCCESS) return fail();
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);
    m_targets.resize(targetCount);
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    for (uint32_t index = 0; index < targetCount; ++index) {
        Target &target = m_targets[index];
        if (vkCreateImage(device, &imageInfo, nullptr, &target.image) != VK_SUCCESS) return fail();
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, target.image, &requirements);
        uint32_t memoryType = memoryProperties.memoryTypeCount;
        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
            if ((requirements.memoryTypeBits & (1u << i)) &&
                (memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                memoryType = i;
                break;
            }
        }
        if (memoryType == memoryProperties.memoryTypeCount) return fail();
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType;
        if (vkAllocateMemory(device, &allocation, nullptr, &target.memory) != VK_SUCCESS
            || vkBindImageMemory(device, target.image, target.memory, 0) != VK_SUCCESS) return fail();
        viewInfo.image = target.image;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        if (vkCreateImageView(device, &viewInfo, nullptr, &target.view) != VK_SUCCESS) return fail();
        target.descriptor = descriptors[index];
        VkDescriptorImageInfo images[2] = {
            {m_sampler, m_sources[index % 2], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, target.view, VK_IMAGE_LAYOUT_GENERAL}};
        VkWriteDescriptorSet writes[2] = {};
        for (uint32_t binding = 0; binding < 2; ++binding) {
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = target.descriptor;
            writes[binding].dstBinding = binding;
            writes[binding].descriptorCount = 1;
            writes[binding].descriptorType = bindings[binding].descriptorType;
            writes[binding].pImageInfo = &images[binding];
        }
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    }
    __android_log_print(ANDROID_LOG_INFO, "GeneralsXR", "Spatial AA ready: %ux%u, %u independent eye/slot targets; HUD excluded", width, height, targetCount);
    return true;
}

VkImage SpatialAA::record(VkCommandBuffer command, uint32_t slot, uint32_t eye,
    VkImage source, VkImageLayout sourceLayout) const
{
    if (m_smaa.ready()) return m_smaa.record(command, slot, eye, source, sourceLayout);
    const Target &target = m_targets[slot * 2 + eye];
    VkImageMemoryBarrier barriers[2] = {};
    for (auto &barrier : barriers) {
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = barrier.subresourceRange.layerCount = 1;
    }
    barriers[0].image = source;
    barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].oldLayout = sourceLayout;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    // This target belongs to the slot whose fence was waited by the caller.
    // Discard its previous pixels after that completed transfer, not in flight.
    barriers[1].image = target.image;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, 1,
        &target.descriptor, 0, nullptr);
    vkCmdDispatch(command, (m_width + 7) / 8, (m_height + 7) / 8, 1);
    barriers[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].newLayout = sourceLayout;
    barriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);
    return target.image;
}

void SpatialAA::destroy()
{
    m_smaa.destroy();
    if (m_device == VK_NULL_HANDLE) return;
    if (m_pool) vkDestroyDescriptorPool(m_device, m_pool, nullptr);
    for (const Target &target : m_targets) {
        if (target.view) vkDestroyImageView(m_device, target.view, nullptr);
        if (target.image) vkDestroyImage(m_device, target.image, nullptr);
        if (target.memory) vkFreeMemory(m_device, target.memory, nullptr);
    }
    for (VkImageView view : m_sources) if (view) vkDestroyImageView(m_device, view, nullptr);
    if (m_sampler) vkDestroySampler(m_device, m_sampler, nullptr);
    if (m_pipeline) vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_layout) vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    if (m_descriptors) vkDestroyDescriptorSetLayout(m_device, m_descriptors, nullptr);
    m_targets.clear();
    m_sources = {};
    m_sourceImages = {};
    m_sampler = VK_NULL_HANDLE;
    m_pipeline = VK_NULL_HANDLE;
    m_layout = VK_NULL_HANDLE;
    m_descriptors = VK_NULL_HANDLE;
    m_pool = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
    m_width = m_height = 0;
}
}
#endif
