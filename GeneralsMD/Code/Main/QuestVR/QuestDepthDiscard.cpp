// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestDepthDiscard.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include "VRDevice/QuestPerformance.h"
#include <android/log.h>
bool QuestXR_QueueEyeDepthDiscard(IDirect3DDevice8 *device, IDirect3DSurface8 *depth, unsigned eye);
namespace QuestXR {
namespace {
thread_local EyeDepthTicket *active = nullptr;
thread_local uint64_t reports = 0, queued = 0, rejected = 0;
}
EyeDepthDiscardScope::EyeDepthDiscardScope(IDirect3DDevice8 *device, IDirect3DSurface8 *depth, unsigned eye)
    :ticket{device, depth, eye, PerformanceRequested(PerfOption::DepthDiscard), false}, previous(active) {
    active = &ticket;
}
EyeDepthDiscardScope::~EyeDepthDiscardScope() { active = previous; }
void DiscardCompletedEyeDepth(IDirect3DDevice8 *device, bool presenting) {
    if (!active || active->consumed) return;
    const bool eligible = active->Consume(device, presenting);
    const bool success = eligible && QuestXR_QueueEyeDepthDiscard(device,
        static_cast<IDirect3DSurface8 *>(const_cast<void *>(active->depth)), active->eye);
    if (!PerformanceMetricsRequested()) return;
    if (active->enabled) { if (success) ++queued; else ++rejected; }
    if (++reports == 240) {
        __android_log_print(ANDROID_LOG_INFO, "GeneralsXR",
            "Quest eye depth discard: enabled=%d queued=%llu rejected=%llu (240 completed-eye scopes; queued is not a GPU store-op measurement)",
            active->enabled, (unsigned long long)queued, (unsigned long long)rejected);
        reports = queued = rejected = 0;
    }
}
}
#endif
