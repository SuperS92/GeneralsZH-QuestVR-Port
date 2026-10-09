// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <cstdint>
namespace QuestXR {
// GeneralsX @feature Codex 02/10/2026 Default-off terrain pattern isolation test.
// Only generated, already locked upload RGB changes. Alpha and row padding stay exact.
bool NeutralizeTerrainRgb(void *pixels, int pitch, uint32_t width, uint32_t height,
    uint32_t d3dFormat, bool enabled);
bool TerrainDetailTestRequested();
uint32_t TerrainDetailTestUploads();
void ApplyTerrainDetailTest(void *pixels, int pitch, uint32_t width,
    uint32_t height, uint32_t d3dFormat);
}
#endif
