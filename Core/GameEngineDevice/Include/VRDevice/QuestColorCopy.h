// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace QuestXR {
// Only the verified DXVK D3D9 format extension, never a retail texture format change.
constexpr uint32_t kLegacyEyeColorFormat = 21; // D3DFMT_A8R8G8B8 -> Vulkan BGRA8
constexpr uint32_t kRgbaEyeColorFormat = 32;   // D3D9 A8B8G8R8 -> Vulkan RGBA8
constexpr uint32_t ChooseEyeColorFormat(bool requested,bool aaOff,bool runtimeRgba,
    bool nativeRenderTransfer,bool multisampled) {
    return requested && aaOff && runtimeRgba && nativeRenderTransfer && !multisampled
        ? kRgbaEyeColorFormat : kLegacyEyeColorFormat;
}
}
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <vulkan/vulkan.h>
namespace QuestXR {
// Caller preserves its original layouts, extent, queue ownership and acquire/fence lifetime.
void CopyEyeColorImage(VkCommandBuffer command,VkImage source,VkImage destination,
    uint32_t width,uint32_t height,uint32_t eyeLayer,bool convertFormat);
}
#endif
