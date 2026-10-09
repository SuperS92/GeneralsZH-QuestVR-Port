// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

// GeneralsX @feature Codex 01/10/2026 Read-only effective-camera diagnostics.
struct QuestXRDiagnosticCamera {
    float cameraToWorld[12]{}; // row-major 3x4, game world units
    float worldUnitsPerMeter = 0;
    bool valid = false;
};
extern "C" void QuestXR_RecordDiagnosticCamera(std::uint32_t eye,
    const QuestXRDiagnosticCamera *camera);
