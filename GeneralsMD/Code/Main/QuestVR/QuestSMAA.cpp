// SPDX-License-Identifier: GPL-3.0-or-later
#include "QuestSMAA.h"
#include "VRDevice/QuestPerformance.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include "QuestSMAA.spv.h"
#include "SMAA/AreaTex.h"
#include "SMAA/SearchTex.h"
#include <android/log.h>
#include <cstring>

namespace QuestXR {
namespace {
VkImageMemoryBarrier barrier(VkImage image, VkImageLayout oldLayout,
    VkImageLayout newLayout, VkAccessFlags source, VkAccessFlags destination) {
    VkImageMemoryBarrier value{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    value.image = image;
    value.oldLayout = oldLayout; value.newLayout = newLayout;
    value.srcAccessMask = source; value.dstAccessMask = destination;
    value.srcQueueFamilyIndex = value.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    value.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return value;
}
uint32_t memoryType(VkPhysicalDevice physical, uint32_t allowed, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((allowed & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) return i;
    return UINT32_MAX;
}
}

bool Smaa::createImage(VkPhysicalDevice physical, Image &image, uint32_t width,
    uint32_t height, VkImageUsageFlags usage) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D; info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.extent = {width, height, 1}; info.mipLevels = info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT; info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(m_device, &info, nullptr, &image.image) != VK_SUCCESS) return false;
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_device, image.image, &requirements);
    const uint32_t type = memoryType(physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) return false;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = type;
    if (vkAllocateMemory(m_device, &allocation, nullptr, &image.memory) != VK_SUCCESS
        || vkBindImageMemory(m_device, image.image, image.memory, 0) != VK_SUCCESS) return false;
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = image.image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = info.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return vkCreateImageView(m_device, &view, nullptr, &image.view) == VK_SUCCESS;
}

bool Smaa::uploadTables(VkPhysicalDevice physical, VkQueue queue, uint32_t family) {
    // One-time upload, while the caller holds the DXVK submission queue guard.
    // Only the small owned upload command waits; nothing is uploaded per frame.
    constexpr uint32_t areaBytes = AREATEX_WIDTH * AREATEX_HEIGHT * 4;
    constexpr uint32_t searchBytes = SEARCHTEX_WIDTH * SEARCHTEX_HEIGHT * 4;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    auto cleanup = [&]() {
        if (fence) vkDestroyFence(m_device, fence, nullptr);
        if (pool) vkDestroyCommandPool(m_device, pool, nullptr);
        if (buffer) vkDestroyBuffer(m_device, buffer, nullptr);
        if (memory) vkFreeMemory(m_device, memory, nullptr);
    };
    auto fail = [&]() { cleanup(); return false; };
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = areaBytes + searchBytes; bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) return fail();
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(m_device, buffer, &requirements);
    const uint32_t type = memoryType(physical, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (type == UINT32_MAX) return fail();
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = type;
    if (vkAllocateMemory(m_device, &allocation, nullptr, &memory) != VK_SUCCESS
        || vkBindBufferMemory(m_device, buffer, memory, 0) != VK_SUCCESS) return fail();
    void *mapped = nullptr;
    if (vkMapMemory(m_device, memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) return fail();
    auto *bytes = static_cast<unsigned char *>(mapped);
    std::memset(bytes, 0, areaBytes + searchBytes);
    for (uint32_t i = 0; i < AREATEX_WIDTH * AREATEX_HEIGHT; ++i) {
        bytes[i * 4] = areaTexBytes[i * 2]; bytes[i * 4 + 1] = areaTexBytes[i * 2 + 1];
    }
    for (uint32_t i = 0; i < SEARCHTEX_WIDTH * SEARCHTEX_HEIGHT; ++i)
        bytes[areaBytes + i * 4] = searchTexBytes[i];
    vkUnmapMemory(m_device, memory);
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = family;
    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &pool) != VK_SUCCESS) return fail();
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = pool; commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; commandInfo.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &commandInfo, &command) != VK_SUCCESS) return fail();
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(command, &begin) != VK_SUCCESS) return fail();
    for (uint32_t i = 0; i < 2; ++i) {
        auto change = barrier(m_tables[i].image, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &change);
        VkBufferImageCopy copy{};
        copy.bufferOffset = i == 0 ? 0 : areaBytes;
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = i == 0 ? VkExtent3D{AREATEX_WIDTH, AREATEX_HEIGHT, 1}
            : VkExtent3D{SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 1};
        vkCmdCopyBufferToImage(command, buffer, m_tables[i].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        change = barrier(m_tables[i].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &change);
    }
    if (vkEndCommandBuffer(command) != VK_SUCCESS) return fail();
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(m_device, &fenceInfo, nullptr, &fence) != VK_SUCCESS) return fail();
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
    if (vkQueueSubmit(queue, 1, &submit, fence) != VK_SUCCESS) return fail();
    if (vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        vkQueueWaitIdle(queue);
        return fail();
    }
    cleanup();
    return true;
}

bool Smaa::initialize(VkPhysicalDevice physical, VkDevice device, VkQueue queue,
    uint32_t family, const std::array<VkImage, 2> &sources, VkFormat format,
    const std::array<VkImageUsageFlags, 2> &usage, uint32_t width, uint32_t height, uint32_t slots) {
    m_device = device;
    auto fail = [this]() {
        __android_log_print(ANDROID_LOG_WARN, "GeneralsXR", "SMAA unavailable; retaining unfiltered stereo");
        destroy(); return false;
    };
    if (!device || !queue || !sources[0] || !sources[1] || !width || !height || !slots || slots > 16
        || (format != VK_FORMAT_R8G8B8A8_UNORM && format != VK_FORMAT_B8G8R8A8_UNORM)
        || !(usage[0] & VK_IMAGE_USAGE_SAMPLED_BIT) || !(usage[1] & VK_IMAGE_USAGE_SAMPLED_BIT)) return fail();
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> queues(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, queues.data());
    if (family >= count || !(queues[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) return fail();
    const VkFormatFeatureFlags sampled = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(physical, format, &properties);
    if ((properties.optimalTilingFeatures & sampled) != sampled) return fail();
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R8G8B8A8_UNORM, &properties);
    if ((properties.optimalTilingFeatures & (sampled | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
        != (sampled | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) return fail();
    VkImageFormatProperties imageProperties{};
    if (vkGetPhysicalDeviceImageFormatProperties(physical, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D,
        VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        0, &imageProperties) != VK_SUCCESS || width > imageProperties.maxExtent.width || height > imageProperties.maxExtent.height) return fail();
    m_width = width; m_height = height;
    m_sharedEdges = PerformanceRequested(PerfOption::SmaaScratch);
    std::array<VkDescriptorSetLayoutBinding, 6> bindings{};
    for (uint32_t i = 0; i < 6; ++i) {
        bindings[i].binding = i; bindings[i].descriptorCount = 1;
        bindings[i].descriptorType = i == 5 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo descriptorInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptorInfo.bindingCount = bindings.size(); descriptorInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &descriptorInfo, nullptr, &m_descriptors) != VK_SUCCESS) return fail();
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4 * sizeof(float)};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1; layoutInfo.pSetLayouts = &m_descriptors;
    layoutInfo.pushConstantRangeCount = 1; layoutInfo.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_layout) != VK_SUCCESS) return fail();
    const uint32_t *codes[] = {kQuestSmaaStage0, kQuestSmaaStage1, kQuestSmaaStage2};
    const size_t sizes[] = {sizeof(kQuestSmaaStage0), sizeof(kQuestSmaaStage1), sizeof(kQuestSmaaStage2)};
    for (uint32_t stage = 0; stage < 3; ++stage) {
        VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        moduleInfo.pCode = codes[stage]; moduleInfo.codeSize = sizes[stage];
        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &moduleInfo, nullptr, &module) != VK_SUCCESS) return fail();
        VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; pipelineInfo.stage.module = module; pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = m_layout;
        const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipelines[stage]);
        vkDestroyShaderModule(device, module, nullptr);
        if (result != VK_SUCCESS) return fail();
    }
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.minFilter = samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) return fail();
    for (uint32_t eye = 0; eye < 2; ++eye) {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = sources[eye]; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device, &view, nullptr, &m_sources[eye]) != VK_SUCCESS) return fail();
    }
    if (!createImage(physical, m_tables[0], AREATEX_WIDTH, AREATEX_HEIGHT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        || !createImage(physical, m_tables[1], SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) return fail();
    const uint32_t sets = slots * 2 * 3;
    VkDescriptorPoolSize poolSizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sets * 5}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, sets}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = sets; poolInfo.poolSizeCount = 2; poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS) return fail();
    std::vector<VkDescriptorSetLayout> layouts(sets, m_descriptors);
    std::vector<VkDescriptorSet> descriptors(sets);
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = m_pool; allocate.descriptorSetCount = sets; allocate.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device, &allocate, descriptors.data()) != VK_SUCCESS) return fail();
    m_targets.resize(slots * 2);
    for (uint32_t i = 0; i < m_targets.size(); ++i) {
        Target &target = m_targets[i];
        // GeneralsX @performance Codex 05/10/2026 Share only edge scratch between sequential eyes of one fence slot.
        // Weight/final targets and different in-flight slots stay independent.
        for (uint32_t stage = 0; stage < 3; ++stage) {
            if (m_sharedEdges && stage == 0 && (i % 2) == 1) {
                target.images[0] = m_targets[i-1].images[0];
                continue;
            }
            if (!createImage(physical, target.images[stage], width, height,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) return fail();
        }
        for (uint32_t stage = 0; stage < 3; ++stage) {
            target.descriptors[stage] = descriptors[i * 3 + stage];
            VkDescriptorImageInfo images[] = {
                {m_sampler, m_sources[i % 2], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {m_sampler, target.images[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {m_sampler, target.images[1].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {m_sampler, m_tables[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {m_sampler, m_tables[1].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {VK_NULL_HANDLE, target.images[stage].view, VK_IMAGE_LAYOUT_GENERAL}};
            VkWriteDescriptorSet writes[6] = {};
            for (uint32_t binding = 0; binding < 6; ++binding) {
                writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[binding].dstSet = target.descriptors[stage]; writes[binding].dstBinding = binding;
                writes[binding].descriptorCount = 1; writes[binding].descriptorType = bindings[binding].descriptorType;
                writes[binding].pImageInfo = &images[binding];
            }
            vkUpdateDescriptorSets(device, 6, writes, 0, nullptr);
        }
    }
    if (!uploadTables(physical, queue, family)) return fail();
    __android_log_print(ANDROID_LOG_INFO, "GeneralsXR", "SMAA 1x HIGH ready: %ux%u, %u separate eye/slot targets, three passes; HUD excluded",
        width, height, slots * 2);
    __android_log_print(ANDROID_LOG_INFO, "GeneralsXR", "SMAA edge scratch: shared_per_slot=%d (same shader/format; no slot sharing)", m_sharedEdges);
    return true;
}

VkImage Smaa::record(VkCommandBuffer command, uint32_t slot, uint32_t eye, VkImage source, VkImageLayout layout) const {
    const Target &target = m_targets[slot * 2 + eye];
    auto sourceToRead = barrier(source, layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &sourceToRead);
    const float metrics[] = {1.0f / m_width, 1.0f / m_height, float(m_width), float(m_height)};
    vkCmdPushConstants(command, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(metrics), metrics);
    for (uint32_t stage = 0; stage < 3; ++stage) {
        // Completed slot fence covers previous frames. ALL_COMMANDS also orders the
        // first eye's edge reads before second-eye writes (WAR), even across command buffers.
        auto toWrite = barrier(target.images[stage].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            m_sharedEdges && stage == 0 ? VK_ACCESS_SHADER_READ_BIT : 0, VK_ACCESS_SHADER_WRITE_BIT);
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toWrite);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelines[stage]);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, 1, &target.descriptors[stage], 0, nullptr);
        vkCmdDispatch(command, (m_width + 7) / 8, (m_height + 7) / 8, 1);
        auto toRead = barrier(target.images[stage].image, VK_IMAGE_LAYOUT_GENERAL,
            stage == 2 ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_SHADER_WRITE_BIT, stage == 2 ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_SHADER_READ_BIT);
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            stage == 2 ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toRead);
    }
    auto restore = barrier(source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, layout,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 0, nullptr, 0, nullptr, 1, &restore);
    return target.images[2].image;
}

void Smaa::destroy() {
    if (!m_device) return;
    if (m_pool) vkDestroyDescriptorPool(m_device, m_pool, nullptr);
    auto freeImage = [&](const Image &image) {
        if (image.view) vkDestroyImageView(m_device, image.view, nullptr);
        if (image.image) vkDestroyImage(m_device, image.image, nullptr);
        if (image.memory) vkFreeMemory(m_device, image.memory, nullptr);
    };
    for (size_t i=0; i<m_targets.size(); ++i) for (size_t stage=0; stage<3; ++stage) {
        if (m_sharedEdges && stage == 0 && (i % 2) == 1) continue; // Alias owned by the first eye.
        freeImage(m_targets[i].images[stage]);
    }
    for (const Image &image : m_tables) freeImage(image);
    for (VkImageView view : m_sources) if (view) vkDestroyImageView(m_device, view, nullptr);
    if (m_sampler) vkDestroySampler(m_device, m_sampler, nullptr);
    for (VkPipeline pipeline : m_pipelines) if (pipeline) vkDestroyPipeline(m_device, pipeline, nullptr);
    if (m_layout) vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    if (m_descriptors) vkDestroyDescriptorSetLayout(m_device, m_descriptors, nullptr);
    m_targets.clear(); m_tables = {}; m_sources = {}; m_pipelines = {};
    m_sampler = VK_NULL_HANDLE; m_layout = VK_NULL_HANDLE; m_descriptors = VK_NULL_HANDLE;
    m_pool = VK_NULL_HANDLE; m_device = VK_NULL_HANDLE; m_width = m_height = 0; m_sharedEdges = false;
}
}
#endif
