// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestHudTransfer.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
namespace QuestXR {
void CopyHudColorImage(VkCommandBuffer command, VkImage source, VkImage destination,
    uint32_t width, const HudTransferPlan &plan, bool convertFormat)
{
    if (convertFormat) {
        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.layerCount = blit.dstSubresource.layerCount = 1;
        blit.srcOffsets[0] = {0, int32_t(plan.sourceTop), 0};
        blit.srcOffsets[1] = {int32_t(width), int32_t(plan.sourceTop + plan.destinationHeight), 1};
        blit.dstOffsets[1] = {int32_t(width), int32_t(plan.destinationHeight), 1};
        vkCmdBlitImage(command, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
    } else {
        VkImageCopy copy{};
        copy.srcSubresource.aspectMask = copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.srcSubresource.layerCount = copy.dstSubresource.layerCount = 1;
        copy.srcOffset = {0, int32_t(plan.sourceTop), 0};
        copy.extent = {width, plan.destinationHeight, 1};
        vkCmdCopyImage(command, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    }
}
}
#endif
