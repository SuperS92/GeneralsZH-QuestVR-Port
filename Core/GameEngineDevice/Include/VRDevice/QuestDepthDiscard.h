// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace QuestXR {
// GeneralsX @performance Codex 14/10/2026 A single explicit completed-eye ticket.
struct EyeDepthTicket {
    const void *device = nullptr, *depth = nullptr;
    unsigned eye = 2;
    bool enabled = false, consumed = false;
    bool Consume(const void *currentDevice, bool presenting) {
        if (consumed) return false;
        consumed = true;
        return enabled && !presenting && device && depth && eye < 2 && currentDevice == device;
    }
};
}
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
struct IDirect3DDevice8;
struct IDirect3DSurface8;
namespace QuestXR {
class EyeDepthDiscardScope {
    EyeDepthTicket ticket;
    EyeDepthTicket *previous;
public:
    EyeDepthDiscardScope(IDirect3DDevice8 *device, IDirect3DSurface8 *depth, unsigned eye);
    ~EyeDepthDiscardScope();
    EyeDepthDiscardScope(const EyeDepthDiscardScope &) = delete;
    EyeDepthDiscardScope &operator=(const EyeDepthDiscardScope &) = delete;
};
void DiscardCompletedEyeDepth(IDirect3DDevice8 *device, bool presenting);
}
#endif
