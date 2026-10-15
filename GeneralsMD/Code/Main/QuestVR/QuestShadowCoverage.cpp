// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestShadowCoverage.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include "VRDevice/QuestPerformance.h"
#include "VRDevice/QuestXRInterface.h"
#include "d3d8.h"
#include <android/log.h>
namespace QuestXR {
// GeneralsX @bugfix Codex 15/10/2026 Match the final stencil tint to the actual owned eye viewport.
bool GetEyeShadowCoverage(IDirect3DDevice8 *device, ShadowCoverageRect &result) {
    const int eye=PerformanceEye();
    if (!device || eye<0 || eye>1 || !PerformanceRequested(PerfOption::ShadowCoverage)) return false;
    IDirect3DSurface8 *bound=nullptr;
    if (FAILED(device->GetRenderTarget(&bound)) || !bound) return false;
    D3DSURFACE_DESC surface{};
    D3DVIEWPORT8 viewport{};
    const bool valid=bound==QuestXR_GetEyeSurface(unsigned(eye))
        && SUCCEEDED(bound->GetDesc(&surface)) && SUCCEEDED(device->GetViewport(&viewport))
        && PlanShadowCoverage(viewport.X,viewport.Y,viewport.Width,viewport.Height,
            surface.Width,surface.Height,result);
    bound->Release();
    static bool logged[2]{};
    if (valid && PerformanceMetricsRequested() && !logged[eye]) {
        logged[eye]=true;
        __android_log_print(ANDROID_LOG_INFO,"GeneralsXR",
            "Quest shadow coverage: eye=%d viewport=%u,%u %ux%u surface=%ux%u (final tint only)",
            eye,viewport.X,viewport.Y,viewport.Width,viewport.Height,surface.Width,surface.Height);
    }
    return valid;
}
}
#endif
