// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <cstdint>
#include <string>
struct IDirect3DTexture8;
namespace QuestXR {
struct TerrainFarMaterial { int32_t x, y, size, firstTile, tileCount; };
bool TerrainFarMipsRequested();
bool TerrainFarFilterRequested();
struct TerrainFarFilterResult {
    uint32_t materials = 0, skippedAlpha = 0, changedPixels = 0, unequalEndpoints = 0, failureReason = 0;
};
// Only LOD3 logical pixels; failure leaves pixels unchanged, sets failureReason and clears counts.
bool FilterTerrainFarMip(void *pixels, int pitch, uint32_t baseWidth, uint32_t baseHeight,
    const TerrainFarMaterial *materials, uint32_t count, TerrainFarFilterResult &result);
bool ValidateTerrainFarMaterials(const TerrainFarMaterial *materials, uint32_t count,
    uint32_t width, uint32_t height);
bool ValidateTerrainFarCliff(const TerrainFarMaterial *materials, uint32_t count,
    int32_t tileIndex, const float *uv);
bool ValidateTerrainFarQuad(const TerrainFarMaterial *materials, uint32_t count,
    uint32_t height, const float *u, const float *v);
bool DownsampleTerrainFarMip(const void *source, int sourcePitch, void *target,
    int targetPitch, uint32_t width, uint32_t height);
std::string TerrainFarMipDiagnosticsJson();
}
#ifndef QUEST_DRAW_CAPTURE_HOST
#include "W3DDevice/GameClient/WorldHeightMap.h"
namespace QuestXR {
// Returns one owned reference only after every copy/validation/unlock succeeds.
IDirect3DTexture8 *CreateTerrainFarMip(IDirect3DTexture8 *source,
    const TXTextureClass *materials, int count, const TCliffInfo *cliffs, int cliffCount, WorldHeightMap *map);
}
#endif
#endif
