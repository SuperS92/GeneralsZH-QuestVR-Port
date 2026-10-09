// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <cstdint>
namespace QuestXR {
// GeneralsX @feature Codex 02/10/2026 Presentation-only filtering and eye resolution.
bool RoadMipFixRequested();
bool ShadowsRequested();
uint32_t TextureFilterChoice();
uint32_t RenderScaleChoice();
inline uint32_t MapMipFilter(uint32_t value, uint32_t choice) {
    return choice >= 1 && choice <= 2 && value == 1 ? 2 : value;
}
bool ChooseEyeExtent(uint32_t width, uint32_t height, uint32_t scale,
    uint32_t maxWidth, uint32_t maxHeight, uint32_t &outWidth, uint32_t &outHeight);
}
#endif
