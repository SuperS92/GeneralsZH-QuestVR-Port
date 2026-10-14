// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestColorCopy.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
namespace QuestXR {
// GeneralsX @performance Codex 14/10/2026 Original transfer commands, with a copy-compatible target trial.
void CopyEyeColorImage(VkCommandBuffer command,VkImage source,VkImage destination,
    uint32_t width,uint32_t height,uint32_t eyeLayer,bool convertFormat) {
    if (convertFormat) {
        VkImageBlit blit{};
        blit.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,eyeLayer,1};
        blit.srcOffsets[1]=blit.dstOffsets[1]={static_cast<int32_t>(width),static_cast<int32_t>(height),1};
        vkCmdBlitImage(command,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            destination,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_NEAREST);
    } else {
        VkImageCopy copy{};
        copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,eyeLayer,1};
        copy.extent={width,height,1};
        vkCmdCopyImage(command,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            destination,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    }
}
}
#endif
