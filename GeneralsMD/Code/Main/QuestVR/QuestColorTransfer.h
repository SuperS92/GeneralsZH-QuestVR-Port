// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <vulkan/vulkan.h>
#include <array>
#include <vector>

namespace QuestXR {
// GeneralsX @performance Codex 13/10/2026 Optional exact-pixel raster transfer.
// No owned images, filtering, blending, scene traversal or descriptor rewrites.
class ColorTransfer final {
public:
    struct Source {
        VkImage image = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageUsageFlags usage = 0;
        uint32_t width = 0, height = 0, destinationLayer = 0;
        std::vector<VkImage> destinations;
    };
    bool initialize(VkPhysicalDevice physical, VkDevice device, VkFormat destinationFormat,
        const std::array<Source, 3> &sources);
    bool matches(uint32_t channel, VkImage source, uint32_t destination) const;
    void record(VkCommandBuffer command, uint32_t channel, uint32_t destination,
        VkImageLayout sourceLayout) const;
    void destroy(); // Caller must drain submissions before destroying borrowed image views.
    ColorTransfer() = default;
    ColorTransfer(const ColorTransfer &) = delete;
    ColorTransfer &operator=(const ColorTransfer &) = delete;
private:
    struct Channel {
        Source source;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
        std::vector<VkImageView> destinations;
        std::vector<VkFramebuffer> framebuffers;
    };
    VkDevice m_device = VK_NULL_HANDLE;
    VkRenderPass m_pass = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptors = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    std::array<Channel, 3> m_channels;
};
}
#endif
