// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <cstdint>
struct IDirect3DTexture8;
namespace QuestXR {
// GeneralsX @feature Codex 02/10/2026 Opt-in atlas-safe reduced terrain RGB filtering.
struct TerrainMipRegion { uint32_t x, y, width, height, border; bool repeat; };
uint32_t TerrainMipFilterMode();
uint32_t TerrainMipFilterRegions();
uint32_t TerrainMipFilterFailures();
bool FilterTerrainMipRgb(void *pixels, int pitch, uint32_t surfaceWidth,
    uint32_t surfaceHeight, uint32_t format, const TerrainMipRegion &region, uint32_t mode);
void ApplyTerrainMipFilter(IDirect3DTexture8 *texture, const TerrainMipRegion &baseRegion);
}
#endif
