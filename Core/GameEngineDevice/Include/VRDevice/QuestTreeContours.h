// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <cstdint>
#include <string>
namespace QuestXR {
// GeneralsX @feature Codex 07/10/2026 Independent, bounded tree-atlas filtering; no source asset mutation.
struct TreeContourRegion { uint32_t x, y, width, height; };
struct TreeContourResult { uint32_t regions = 0, changedPixels = 0, partialBefore = 0, partialAfter = 0; };
bool TreeContoursRequested();
// A8R8G8B8 byte layout, isolated whole material rectangles, premultiplied 3x3 binomial kernel.
// Validates and allocates all scratch before any write; Original is byte-exact.
bool SmoothTreeContours(uint8_t *pixels, uint32_t pitch, uint32_t width, uint32_t height,
    const TreeContourRegion *regions, uint32_t count, bool enabled, TreeContourResult &result);
void RecordTreeContourUpload(bool success, const TreeContourResult &result);
std::string TreeContourDiagnosticsJson();
}
#endif
