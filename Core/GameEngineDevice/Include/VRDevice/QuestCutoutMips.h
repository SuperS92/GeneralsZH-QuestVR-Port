// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <cstdint>
namespace QuestXR {
// GeneralsX @feature Codex 02/10/2026 Local opt-in cutout mip coverage; no draw/game state.
bool CutoutMipFixRequested();
bool IsCutoutTextureName(const char *name);
bool UseCutoutMipFix(const char *name);
bool RebuildCutoutAlpha(uint8_t *const *pixels, const unsigned *pitches,
    uint32_t width, uint32_t height, uint32_t levels, bool enabled);
void ApplyLoadedCutoutAlpha(uint8_t *const *pixels, const unsigned *pitches,
    uint32_t width, uint32_t height, uint32_t levels, const char *name);
uint32_t CutoutMipUploads();
uint32_t CutoutMipFailures();
}
#endif
