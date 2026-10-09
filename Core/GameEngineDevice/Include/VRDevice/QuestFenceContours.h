// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <cstdint>
#include <string>
#include <vector>
struct IDirect3DTexture8;
struct IDirect3DBaseTexture8;
namespace QuestXR {
// GeneralsX @feature Codex 07/10/2026 Separate, default-off fence resource filter and native draw evidence.
struct FenceContourResult { uint32_t levels = 0, changedPixels = 0, partialBefore = 0, partialAfter = 0; };
bool FenceContoursRequested();
bool IsFenceContourName(const char *name);
bool UseFenceContours(const char *name);
bool PrepareFenceContours(uint8_t *const *pixels, const unsigned *pitches,
    uint32_t width, uint32_t height, uint32_t levels, bool enabled,
    std::vector<std::vector<uint8_t>> &output, FenceContourResult &result);
void ApplyLoadedFenceContours(IDirect3DTexture8 *texture, uint8_t *const *pixels,
    const unsigned *pitches, uint32_t width, uint32_t height, uint32_t levels, const char *name);
void CompleteFenceContourUpload(IDirect3DTexture8 *texture, bool unlocked);
bool IsFilteredFenceResource(IDirect3DBaseTexture8 *texture);
void RecordFenceContourDraw(int eye, bool success, bool skipped);
std::string FenceContourDiagnosticsJson();
}
#endif
