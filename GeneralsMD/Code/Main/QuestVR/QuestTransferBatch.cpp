// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestTransferBatch.h"
#include "VRDevice/QuestColorCopy.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <limits>
namespace QuestXR {
namespace {
bool sourceLayout(VkImageLayout layout) {
    return layout == VK_IMAGE_LAYOUT_GENERAL || layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        || layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
}
bool extent(uint32_t value) { return value && value <= uint32_t(std::numeric_limits<int32_t>::max()); }
VkImageMemoryBarrier transition(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
    VkAccessFlags before, VkAccessFlags after, uint32_t layers) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.image=image; b.oldLayout=oldLayout; b.newLayout=newLayout;
    b.srcAccessMask=before; b.dstAccessMask=after;
    b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,layers};
    return b;
}
}
bool CanBatchColorTransfers(const ColorTransferBatch &b) {
    if (!extent(b.eyeWidth) || !extent(b.eyeHeight) || !sourceLayout(b.layouts[0]) || !sourceLayout(b.layouts[1])) return false;
    const VkImage handles[]={b.eyes[0],b.eyes[1],b.eyeDestination,b.menu,b.menuDestination};
    const unsigned count=b.copyMenu?5:3;
    for (unsigned i=0;i<count;++i) {
        if (!handles[i]) return false;
        for (unsigned j=0;j<i;++j) if (handles[i]==handles[j]) return false;
    }
    if (!b.copyMenu) return true;
    return extent(b.menuWidth) && extent(b.menuHeight) && sourceLayout(b.menuLayout)
        && extent(b.hud.destinationHeight) && b.hud.sourceTop <= b.menuHeight
        && b.hud.destinationHeight == b.menuHeight-b.hud.sourceTop
        && b.hud.visibleHeight && b.hud.subImageTop <= b.hud.destinationHeight
        && b.hud.visibleHeight == b.hud.destinationHeight-b.hud.subImageTop;
}
// GeneralsX @performance Codex 15/10/2026 Consolidate dependencies without narrowing their scope.
bool RecordColorTransferBatch(VkCommandBuffer command, const ColorTransferBatch &b) {
    if (!command || !CanBatchColorTransfers(b)) return false;
    constexpr VkAccessFlags eyeWrites=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
    std::array<VkImageMemoryBarrier,5> before{},after{};
    unsigned count=0;
    for (unsigned eye=0;eye<2;++eye) {
        before[count]=transition(b.eyes[eye],b.layouts[eye],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            eyeWrites,VK_ACCESS_TRANSFER_READ_BIT,1);
        after[count]=transition(b.eyes[eye],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,b.layouts[eye],
            VK_ACCESS_TRANSFER_READ_BIT,eyeWrites,1);
        ++count;
    }
    before[count]=transition(b.eyeDestination,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0,VK_ACCESS_TRANSFER_WRITE_BIT,2);
    after[count]=transition(b.eyeDestination,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,2);
    ++count;
    if (b.copyMenu) {
        before[count]=transition(b.menu,b.menuLayout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            eyeWrites,VK_ACCESS_TRANSFER_READ_BIT,1);
        after[count]=transition(b.menu,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,b.menuLayout,
            VK_ACCESS_TRANSFER_READ_BIT,eyeWrites,1);
        ++count;
        before[count]=transition(b.menuDestination,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0,VK_ACCESS_TRANSFER_WRITE_BIT,1);
        after[count]=transition(b.menuDestination,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,1);
        ++count;
    }
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,0,nullptr,0,nullptr,count,before.data());
    for (unsigned eye=0;eye<2;++eye)
        CopyEyeColorImage(command,b.eyes[eye],b.eyeDestination,b.eyeWidth,b.eyeHeight,eye,b.convertFormat);
    if (b.copyMenu) CopyHudColorImage(command,b.menu,b.menuDestination,b.menuWidth,b.hud,b.convertFormat);
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0,0,nullptr,0,nullptr,count,after.data());
    return true;
}
}
#endif
