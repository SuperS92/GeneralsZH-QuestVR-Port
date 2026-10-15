// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace QuestXR {
struct ShadowCoverageRect { float left=0, top=0, right=0, bottom=0; };
// Integer device viewport bounds; do not infer them from the flat UI canvas.
inline bool PlanShadowCoverage(uint32_t x,uint32_t y,uint32_t width,uint32_t height,
    uint32_t surfaceWidth,uint32_t surfaceHeight,ShadowCoverageRect &result) {
    if (!width || !height || surfaceWidth>16384 || surfaceHeight>16384
        || x>surfaceWidth || y>surfaceHeight || width>surfaceWidth-x || height>surfaceHeight-y) return false;
    result={float(x),float(y),float(x+width),float(y+height)};
    return true;
}
}
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
struct IDirect3DDevice8;
namespace QuestXR {
bool GetEyeShadowCoverage(IDirect3DDevice8 *device, ShadowCoverageRect &result);
}
#endif
