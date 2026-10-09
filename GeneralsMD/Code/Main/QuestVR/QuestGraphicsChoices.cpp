// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestGraphicsChoices.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <algorithm>
#include <cstdlib>
#include <cstring>
namespace QuestXR {
// GeneralsX @feature Codex 09/10/2026 Explicit default-off shadow drawing.
bool ShadowsRequested() {
    const char *s = std::getenv("GX_QUEST_SHADOWS");
    return s && std::strcmp(s, "1") == 0;
}
bool RoadMipFixRequested() {
    const char *s = std::getenv("GX_QUEST_ROAD_MIPS");
    return s && std::strcmp(s, "1") == 0;
}
uint32_t TextureFilterChoice() {
    const char *s = std::getenv("GX_QUEST_TEXTURE_FILTER");
    return s && std::strcmp(s, "1") == 0 ? 1 : s && std::strcmp(s, "2") == 0 ? 2 : 0;
}
uint32_t RenderScaleChoice() {
    const char *s = std::getenv("GX_QUEST_RENDER_SCALE");
    return s && std::strcmp(s, "1250") == 0 ? 1250 : s && std::strcmp(s, "1500") == 0 ? 1500 : 1000;
}
bool ChooseEyeExtent(uint32_t width, uint32_t height, uint32_t scale,
    uint32_t maxWidth, uint32_t maxHeight, uint32_t &outWidth, uint32_t &outHeight) {
    outWidth = outHeight = 0;
    maxWidth = std::min(4096u, maxWidth); maxHeight = std::min(4096u, maxHeight);
    if (!width || !height || width > maxWidth || height > maxHeight) return false;
    if (scale != 1250 && scale != 1500) scale = 1000;
    const uint32_t permitted = std::min(scale, static_cast<uint32_t>(std::min(
        uint64_t(maxWidth) * 1000 / width, uint64_t(maxHeight) * 1000 / height)));
    outWidth = static_cast<uint32_t>(uint64_t(width) * permitted / 1000);
    outHeight = static_cast<uint32_t>(uint64_t(height) * permitted / 1000);
    return outWidth && outHeight;
}
}
#endif
