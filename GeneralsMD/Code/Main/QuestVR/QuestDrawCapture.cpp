// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestDrawCapture.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <cstdio>
#include <cstring>
#include <sstream>
#include <fstream>
#include <cstdlib>
#ifndef QUEST_DRAW_CAPTURE_HOST
#include "texture.h"
#include "dx8wrapper.h"
#endif
namespace QuestXR {
namespace {
constexpr uint32_t kRecords = 256, kDraws = 4096;
struct Record { DiagnosticDraw draw; uint32_t count = 0; };
struct Eye {
    std::array<Record, kRecords> records{};
    uint32_t draws = 0, used = 0, omitted = 0;
    bool entered = false;
};
std::array<Eye, 2> eyes;
bool active = false;
int currentEye = -1;
bool same(const DiagnosticTexture &a, const DiagnosticTexture &b) {
    return a.identity == b.identity && a.type == b.type && a.levels == b.levels &&
        a.managedLod == b.managedLod && a.format == b.format && a.widths == b.widths &&
        a.heights == b.heights && a.levelValidMask == b.levelValidMask &&
        a.samplerValidMask == b.samplerValidMask && a.sampler == b.sampler &&
        a.bindingValid == b.bindingValid && a.name == b.name;
}
bool same(const DiagnosticDraw &a, const DiagnosticDraw &b) {
    if (a.render != b.render || a.renderValidMask != b.renderValidMask) return false;
    for (uint32_t i = 0; i < 8; ++i) if (!same(a.textures[i], b.textures[i])) return false;
    return true;
}
void quoted(std::ostream &out, const char *value) {
    out << '"';
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(value); *p; ++p) {
        if (*p == '"' || *p == '\\') out << '\\' << static_cast<char>(*p);
        else if (*p < 32 || *p >= 127) {
            char escaped[7]; std::snprintf(escaped, sizeof(escaped), "\\u%04x", *p); out << escaped;
        } else out << static_cast<char>(*p);
    }
    out << '"';
}
template<size_t N> void values(std::ostream &out, const std::array<uint32_t, N> &a) {
    out << '[';
    for (size_t i = 0; i < N; ++i) { if (i) out << ','; out << a[i]; }
    out << ']';
}
}
void BeginDrawCapture(bool enabled) {
    active = enabled; currentEye = -1;
    // Counts reset without clearing the large bounded allocation every frame.
    for (auto &eye : eyes) { eye.draws = eye.used = eye.omitted = 0; eye.entered = false; }
}
void SetDrawCaptureEye(int eye) {
    currentEye = active && eye >= 0 && eye < 2 ? eye : -1;
    if (currentEye >= 0) eyes[currentEye].entered = true;
}
bool DrawCaptureEnabled() {
    if (!active || currentEye < 0) return false;
    auto &eye = eyes[currentEye];
    if (eye.draws >= kDraws) { ++eye.draws; ++eye.omitted; return false; }
    return true;
}
void RecordDrawData(const DiagnosticDraw &draw) {
    if (!active || currentEye < 0) return;
    auto &eye = eyes[currentEye];
    if (++eye.draws > kDraws) { ++eye.omitted; return; }
    for (uint32_t i = 0; i < eye.used; ++i) {
        if (same(eye.records[i].draw, draw)) { ++eye.records[i].count; return; }
    }
    if (eye.used == kRecords) { ++eye.omitted; return; }
    eye.records[eye.used++] = {draw, 1};
}
std::string DrawCaptureJson() {
    if (!active) return "null";
    std::ostringstream out;
    out << "{\"schema\":1,\"source\":\"D3D8 getters immediately before wrapper world draws\","
        "\"vulkanEffectiveSampler\":\"NOT_VERIFIED\",\"sampledPixelLod\":\"NOT_VERIFIED\","
        "\"maxDistinctStatesPerEye\":256,\"maxInspectedDrawsPerEye\":4096,"
        "\"coverage\":\"wrapper indexed world draws; direct device calls excluded\","
        "\"samplerOrder\":[\"min\",\"mag\",\"mip\",\"lodBiasBits\",\"maxMip\",\"maxAnisotropy\",\"addressU\",\"addressV\",\"colorOp\"],"
        "\"renderOrder\":[\"alphaTest\",\"alphaRef\",\"alphaFunc\",\"alphaBlend\",\"srcBlend\",\"dstBlend\",\"zEnable\",\"zWrite\",\"zFunc\"],\"eyes\":[";
    for (uint32_t i = 0; i < 2; ++i) {
        if (i) out << ',';
        const auto &eye = eyes[i];
        out << "{\"entered\":" << (eye.entered ? "true" : "false") << ",\"draws\":" << eye.draws
            << ",\"omitted\":" << eye.omitted << ",\"truncated\":" << (eye.omitted ? "true" : "false") << ",\"states\":[";
        for (uint32_t r = 0; r < eye.used; ++r) {
            if (r) out << ',';
            const auto &record = eye.records[r];
            out << "{\"count\":" << record.count << ",\"renderValidMask\":" << record.draw.renderValidMask << ",\"render\":";
            values(out, record.draw.render); out << ",\"textures\":[";
            for (uint32_t t = 0; t < 8; ++t) {
                if (t) out << ',';
                const auto &texture = record.draw.textures[t];
                out << "{\"id\":" << texture.identity << ",\"bindingValid\":" << (texture.bindingValid ? "true" : "false")
                    << ",\"nameHint\":"; quoted(out, texture.name.data());
                out << ",\"type\":" << texture.type << ",\"levels\":" << texture.levels
                    << ",\"managedLod\":" << texture.managedLod << ",\"format\":" << texture.format
                    << ",\"levelValidMask\":" << texture.levelValidMask << ",\"widths\":";
                values(out, texture.widths); out << ",\"heights\":"; values(out, texture.heights);
                out << ",\"samplerValidMask\":" << texture.samplerValidMask << ",\"sampler\":";
                values(out, texture.sampler); out << '}';
            }
            out << "]}";
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}
std::string DrawCaptureConfigJson() {
    // Only graphics keys, bounded reads; never serialize account/game configuration.
    const char *keys[] = {"d3d9.samplerLodBias", "d3d9.samplerAnisotropy",
        "d3d9.clampNegativeLodBias", "d3d9.forceSamplerTypeSpecConstants"};
    const char *fileName = std::getenv("DXVK_CONFIG_FILE");
    if (!fileName || !*fileName) fileName = "dxvk.conf";
    std::ifstream file(fileName, std::ios::binary);
    std::array<char, 65537> bytes{};
    if (file) file.read(bytes.data(), bytes.size() - 1);
    const auto count = file.gcount();
    const char *environment = std::getenv("DXVK_CONFIG");
    std::string env = environment ? std::string(environment).substr(0, 65536) : "";
    for (char &c : env) if (c == ';') c = '\n';
    std::ostringstream out;
    out << "{\"schema\":1,\"inputsOnlyNoRuntimeConfirmation\":true,\"configFileReadable\":"
        << (file.is_open() ? "true" : "false") << ",\"fileReadLimit\":65536,\"fileAtReadLimit\":"
        << (count == 65536 ? "true" : "false") << ",\"values\":[";
    bool first = true;
    for (uint32_t source = 0; source < 2; ++source) {
        std::istringstream lines(source ? env : std::string(bytes.data(), static_cast<size_t>(count)));
        std::string line;
        while (std::getline(lines, line)) {
            const auto equal = line.find('=');
            if (equal == std::string::npos) continue;
            auto key = line.substr(0, equal);
            const auto start = key.find_first_not_of(" \t\r");
            if (start == std::string::npos) continue;
            key = key.substr(start, key.find_last_not_of(" \t\r") - start + 1);
            bool allowed = false;
            for (const auto *candidate : keys) if (key == candidate) allowed = true;
            if (!allowed) continue;
            if (!first) out << ',';
            first = false; out << "{\"source\":"; quoted(out, source ? "DXVK_CONFIG" : "config file");
            out << ",\"key\":"; quoted(out, key.c_str());
            out << ",\"valueText\":"; quoted(out, line.substr(equal + 1, 128).c_str()); out << '}';
        }
    }
    out << "]}"; return out.str();
}
}
#ifndef QUEST_DRAW_CAPTURE_HOST
// GeneralsX @feature Codex 02/10/2026 Getters only; GetTexture's owned reference is released.
void QuestXR_RecordDiagnosticDraw(IDirect3DDevice8 *device, TextureBaseClass *const *hints, uint32_t hintCount) {
    if (!QuestXR::DrawCaptureEnabled()) return;
    QuestXR::DiagnosticDraw draw{};
    const D3DRENDERSTATETYPE renderStates[] = {D3DRS_ALPHATESTENABLE, D3DRS_ALPHAREF,
        D3DRS_ALPHAFUNC, D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND,
        D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC};
    const D3DTEXTURESTAGESTATETYPE samplerStates[] = {D3DTSS_MINFILTER, D3DTSS_MAGFILTER,
        D3DTSS_MIPFILTER, D3DTSS_MIPMAPLODBIAS, D3DTSS_MAXMIPLEVEL, D3DTSS_MAXANISOTROPY,
        D3DTSS_ADDRESSU, D3DTSS_ADDRESSV, D3DTSS_COLOROP};
    for (uint32_t i = 0; i < 9; ++i) {
        DWORD value = 0;
        if (SUCCEEDED(device->GetRenderState(renderStates[i], &value))) {
            draw.render[i] = value; draw.renderValidMask |= 1u << i;
        }
    }
    for (uint32_t stage = 0; stage < 8; ++stage) {
        auto &texture = draw.textures[stage];
        for (uint32_t i = 0; i < 9; ++i) {
            DWORD value = 0;
            if (SUCCEEDED(device->GetTextureStageState(stage, samplerStates[i], &value))) {
                texture.sampler[i] = value; texture.samplerValidMask |= 1u << i;
            }
        }
        IDirect3DBaseTexture8 *bound = nullptr;
        texture.bindingValid = SUCCEEDED(device->GetTexture(stage, &bound));
        if (!bound) continue;
        texture.identity = reinterpret_cast<uintptr_t>(bound);
        texture.type = bound->GetType(); texture.levels = bound->GetLevelCount(); texture.managedLod = bound->GetLOD();
        if (stage < hintCount && hints[stage] && hints[stage]->Peek_Diagnostic_D3D_Texture() == bound) {
            const char *name = hints[stage]->Get_Full_Path().str();
            // Store a basename only, never private install paths.
            for (const char *p = name; *p; ++p) if (*p == '/' || *p == '\\') name = p + 1;
            std::snprintf(texture.name.data(), texture.name.size(), "%s", name);
        }
        if (texture.type == D3DRTYPE_TEXTURE) {
            auto *image = static_cast<IDirect3DTexture8 *>(bound);
            for (uint32_t level = 0; level < texture.levels && level < 16; ++level) {
                D3DSURFACE_DESC desc{};
                if (SUCCEEDED(image->GetLevelDesc(level, &desc))) {
                    texture.widths[level] = desc.Width; texture.heights[level] = desc.Height;
                    texture.levelValidMask |= 1u << level;
                    if (!level) texture.format = desc.Format;
                }
            }
        }
        bound->Release();
    }
    QuestXR::RecordDrawData(draw);
}
#endif
#endif
