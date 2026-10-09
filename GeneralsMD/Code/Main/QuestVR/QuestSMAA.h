// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <vulkan/vulkan.h>
#include <array>
#include <vector>
namespace QuestXR {
// GeneralsX @feature Codex 01/10/2026 Original SMAA 1x on finished eye colors.
// Caller owns queue synchronization, completed slot fences, and teardown wait.
class Smaa final {
public:
    bool initialize(VkPhysicalDevice physical, VkDevice device, VkQueue queue,
        uint32_t family, const std::array<VkImage, 2> &sources, VkFormat format,
        const std::array<VkImageUsageFlags, 2> &usage,
        uint32_t width, uint32_t height, uint32_t slots);
    VkImage record(VkCommandBuffer command, uint32_t slot, uint32_t eye,
        VkImage source, VkImageLayout layout) const;
    void destroy();
    bool ready() const { return m_pipelines[2] != VK_NULL_HANDLE; }
private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    struct Target {
        std::array<Image, 3> images = {}; // edges, weights, final color
        std::array<VkDescriptorSet, 3> descriptors = {};
    };
    bool createImage(VkPhysicalDevice physical, Image &image, uint32_t width,
        uint32_t height, VkImageUsageFlags usage);
    bool uploadTables(VkPhysicalDevice physical, VkQueue queue, uint32_t family);
    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptors = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    std::array<VkPipeline, 3> m_pipelines = {};
    VkSampler m_sampler = VK_NULL_HANDLE;
    std::array<VkImageView, 2> m_sources = {};
    std::array<Image, 2> m_tables = {};
    std::vector<Target> m_targets;
    uint32_t m_width = 0, m_height = 0;
    bool m_sharedEdges = false;
};
}
#endif
