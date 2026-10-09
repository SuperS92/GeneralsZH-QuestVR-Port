// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <vulkan/vulkan.h>
#include <array>
#include <vector>
#include "QuestSMAA.h"

namespace QuestXR {
// GeneralsX @feature Codex 30/09/2026 Optional spatial AA after each eye draw.
// All resources are separate from the D3D targets and excluded from the HUD.
class SpatialAA final {
public:
    bool initialize(VkPhysicalDevice physical, VkDevice device, uint32_t queueFamily,
        const std::array<VkImage, 2> &sources, VkFormat format,
        const std::array<VkImageUsageFlags, 2> &usage,
        uint32_t width, uint32_t height, uint32_t slots,
        uint32_t mode = 1, VkQueue queue = VK_NULL_HANDLE);
    VkImage record(VkCommandBuffer command, uint32_t slot, uint32_t eye,
        VkImage source, VkImageLayout sourceLayout) const;
    void destroy(); // Caller waits for the bound queue before destruction.
    bool ready() const { return m_pipeline != VK_NULL_HANDLE || m_smaa.ready(); }
    bool matches(const std::array<VkImage, 2> &sources) const { return m_sourceImages == sources; }
private:
    Smaa m_smaa;
    struct Target {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
    };
    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptors = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    std::array<VkImageView, 2> m_sources = {};
    std::array<VkImage, 2> m_sourceImages = {};
    std::vector<Target> m_targets;
    uint32_t m_width = 0, m_height = 0;
};
}
#endif
