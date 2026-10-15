// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include "VRDevice/QuestHudTransfer.h"
#include <array>
namespace QuestXR {
struct ColorTransferBatch {
    std::array<VkImage, 2> eyes{};
    std::array<VkImageLayout, 2> layouts{};
    VkImage eyeDestination = VK_NULL_HANDLE;
    uint32_t eyeWidth = 0, eyeHeight = 0;
    bool convertFormat = false;
    bool copyMenu = false;
    VkImage menu = VK_NULL_HANDLE, menuDestination = VK_NULL_HANDLE;
    VkImageLayout menuLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t menuWidth = 0, menuHeight = 0;
    HudTransferPlan hud{};
};
// Single queue, acquired destination images and completed original DXVK flush
// remain the caller's responsibility. Invalid plans record no commands.
bool CanBatchColorTransfers(const ColorTransferBatch &batch);
bool RecordColorTransferBatch(VkCommandBuffer command, const ColorTransferBatch &batch);
}
#endif
