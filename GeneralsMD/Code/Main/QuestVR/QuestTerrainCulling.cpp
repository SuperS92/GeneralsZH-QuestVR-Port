// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestTerrainCulling.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
namespace { thread_local bool suppressTerrainDraw = false; }
QuestXR::TerrainCullDrawScope::TerrainCullDrawScope(bool suppress)
    : previous(suppressTerrainDraw) { suppressTerrainDraw = suppress; }
QuestXR::TerrainCullDrawScope::~TerrainCullDrawScope() { suppressTerrainDraw = previous; }
extern "C" bool QuestXR_SuppressTerrainDraw() { return suppressTerrainDraw; }
#endif
