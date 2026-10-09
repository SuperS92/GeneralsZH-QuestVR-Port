// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <array>
#include <cstdint>
#include <string>
struct IDirect3DDevice8;
class TextureBaseClass;
namespace QuestXR {
// GeneralsX @feature Codex 02/10/2026 Read-only, bounded draw-state observation.
struct DiagnosticTexture {
    uint64_t identity = 0;
    uint32_t type = 0, levels = 0, managedLod = 0, format = 0;
    std::array<uint32_t, 16> widths{}, heights{};
    uint32_t levelValidMask = 0, samplerValidMask = 0;
    std::array<uint32_t, 9> sampler{};
    bool bindingValid = false;
    std::array<char, 160> name{};
};
struct DiagnosticDraw {
    std::array<DiagnosticTexture, 8> textures{};
    std::array<uint32_t, 9> render{};
    uint32_t renderValidMask = 0;
};
void BeginDrawCapture(bool enabled);
void SetDrawCaptureEye(int eye); // -1 ends world scope; UI/pointer are excluded.
bool DrawCaptureEnabled();
void RecordDrawData(const DiagnosticDraw &draw);
std::string DrawCaptureJson();
std::string DrawCaptureConfigJson();
}
void QuestXR_RecordDiagnosticDraw(IDirect3DDevice8 *device,
    TextureBaseClass *const *nameHints, uint32_t hintCount);
#endif
