// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <limits>
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <vulkan/vulkan.h>
#endif
namespace QuestXR {
// GeneralsX @performance Codex 13/10/2026 Tail-aligned HUD output keeps pixel scale.
struct HudTransferPlan {
    uint32_t sourceTop = 0, destinationHeight = 0, subImageTop = 0, visibleHeight = 0;
    bool compact = false;
};
inline HudTransferPlan PlanHudTransfer(uint32_t height, int32_t cropTop,
    bool hud, bool compactAvailable)
{
    if (!height || height > uint32_t(std::numeric_limits<int32_t>::max())) return {};
    const uint32_t top = cropTop < 0 ? 0 : uint32_t(cropTop) >= height ? height - 1 : uint32_t(cropTop);
    HudTransferPlan result{0, height, top, height - top, false};
    const uint32_t sourceTop = height / 2;
    // Keep a defined 64-pixel source guard above the displayed HUD. Larger UI
    // always uses the full image; every chosen destination texel is written.
    if (hud && compactAvailable && height >= 128 && top >= sourceTop + 64) {
        result.sourceTop = sourceTop;
        result.destinationHeight = height - sourceTop;
        result.subImageTop = top - sourceTop;
        result.compact = true;
    }
    return result;
}
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
// Caller owns source/destination layout transitions, acquisition and fences.
void CopyHudColorImage(VkCommandBuffer command, VkImage source, VkImage destination,
    uint32_t width, const HudTransferPlan &plan, bool convertFormat);
#endif
}
