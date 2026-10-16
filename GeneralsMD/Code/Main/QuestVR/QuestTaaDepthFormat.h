// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <vulkan/vulkan.h>
namespace QuestXR {
// GeneralsX @performance Codex 16/10/2026 Accept only the original single-sample stencil-capable eye format.
inline bool IsSampledTaaEyeDepth(const VkImageCreateInfo &info,VkImageLayout layout,uint32_t width,uint32_t height) {
    const auto required=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    return info.format==VK_FORMAT_D24_UNORM_S8_UINT&&info.imageType==VK_IMAGE_TYPE_2D
        &&info.extent.width==width&&info.extent.height==height&&info.extent.depth==1
        &&info.samples==VK_SAMPLE_COUNT_1_BIT&&info.arrayLayers==1&&info.mipLevels==1
        &&info.tiling==VK_IMAGE_TILING_OPTIMAL&&(info.usage&required)==required
        &&(layout==VK_IMAGE_LAYOUT_GENERAL||layout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
            ||layout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}
}
