// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestSoftCutouts.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include "VRDevice/QuestCutoutMips.h"
#include "VRDevice/QuestTreeContours.h"
#include "VRDevice/QuestFenceContours.h"
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#ifndef QUEST_SOFT_CUTOUT_HOST
#include "texture.h"
#include "dx8wrapper.h"
#include <android/log.h>
#endif
namespace QuestXR {
namespace {
constexpr size_t count = static_cast<size_t>(CutoutState::Count);
constexpr std::array<CutoutState, 6> changed = {CutoutState::AlphaRef, CutoutState::AlphaFunc,
    CutoutState::AlphaBlend, CutoutState::SrcBlend, CutoutState::DstBlend, CutoutState::DepthWrite};
thread_local int worldEye = -1;
thread_local bool treeScope = false;
std::atomic<uint32_t> treeDraws{0}, namedDraws{0}, failures{0};
// GeneralsX @bugfix Codex 07/10/2026 Identify inactive native paths without asset names or unbounded logs.
enum class SkipReason : size_t { Binding, PixelShader, Getter, Contract, InitialWrite, Count };
std::array<std::atomic<uint32_t>, static_cast<size_t>(SkipReason::Count)> skipCounts{};
std::atomic<uint32_t> treeCandidates{0}, namedCandidates{0}, contractMismatchMask{0};
std::atomic<uint32_t> lastColorWrite{0}, lastPixelShader{0}, reportedSkips{0};
void recordSkip(SkipReason reason, uint32_t mask = 0) {
    skipCounts[static_cast<size_t>(reason)].fetch_add(1, std::memory_order_relaxed);
    if (reason == SkipReason::Contract) contractMismatchMask.fetch_or(mask, std::memory_order_relaxed);
    const uint32_t n = reportedSkips.fetch_add(1, std::memory_order_relaxed);
#ifndef QUEST_SOFT_CUTOUT_HOST
    if (n < 8) {
        __android_log_print(ANDROID_LOG_INFO, "GeneralsXR",
            "Quest soft cutouts skipped: tree=%u reason=%u state_mask=0x%x color_write=%u pixel_shader=%u",
            treeScope ? 1u : 0u, static_cast<unsigned>(reason), mask,
            lastColorWrite.load(std::memory_order_relaxed), lastPixelShader.load(std::memory_order_relaxed));
    }
#else
    (void)n;
#endif
}
}
bool SoftCutoutsRequested() {
    const char *s = std::getenv("GX_QUEST_SOFT_CUTOUTS");
    return s && std::strcmp(s, "1") == 0;
}
SoftCutoutWorldScope::SoftCutoutWorldScope(int eye) : previous(worldEye) {
    worldEye = eye == 0 || eye == 1 ? eye : -1;
}
SoftCutoutWorldScope::~SoftCutoutWorldScope() { worldEye = previous; }
SoftCutoutTreeScope::SoftCutoutTreeScope() : previous(treeScope) { treeScope = true; }
SoftCutoutTreeScope::~SoftCutoutTreeScope() { treeScope = previous; }
bool SoftCutoutDrawInScope() { return worldEye >= 0; }
bool SoftCutoutTreeInScope() { return treeScope; }
uint32_t SoftCutoutTreeDraws() { return treeDraws.load(); }
uint32_t SoftCutoutNamedDraws() { return namedDraws.load(); }
uint32_t SoftCutoutFailures() { return failures.load(); }
std::string SoftCutoutDiagnosticsJson() {
    char json[512];
    std::snprintf(json, sizeof(json),
        "{\"schema\":1,\"treeCandidates\":%u,\"namedCandidates\":%u,"
        "\"bindingSkips\":%u,\"pixelShaderSkips\":%u,\"getterSkips\":%u,"
        "\"contractSkips\":%u,\"initialWriteSkips\":%u,\"contractMismatchMask\":%u,"
        "\"lastColorWrite\":%u,\"lastPixelShader\":%u}",
        treeCandidates.load(std::memory_order_relaxed), namedCandidates.load(std::memory_order_relaxed),
        skipCounts[0].load(std::memory_order_relaxed), skipCounts[1].load(std::memory_order_relaxed),
        skipCounts[2].load(std::memory_order_relaxed), skipCounts[3].load(std::memory_order_relaxed),
        skipCounts[4].load(std::memory_order_relaxed), contractMismatchMask.load(std::memory_order_relaxed),
        lastColorWrite.load(std::memory_order_relaxed), lastPixelShader.load(std::memory_order_relaxed));
    return json;
}

CutoutResult DrawSoftCutoutPasses(CutoutBackend &backend, bool enabled) {
    if (!enabled) return CutoutResult::Skipped; // Off performs no getters, setters or draws.
    std::array<uint32_t, count> original{};
    for (size_t i = 0; i < count; ++i) {
        if (!backend.read(static_cast<CutoutState>(i), original[i])) {
            recordSkip(SkipReason::Getter, 1u << i); return CutoutResult::Skipped;
        }
    }
    auto value = [&](CutoutState s) { return original[static_cast<size_t>(s)]; };
    // Only the measured opaque cutout contract. Exclude blended particles, shadows,
    // unusual depth/stencil passes, partial RGB channel masks and arbitrary alpha rules.
    // GeneralsX @bugfix Codex 07/10/2026 RGB-only inherited passes are valid; retain their alpha-write mask exactly.
    lastColorWrite.store(value(CutoutState::ColorWrite), std::memory_order_relaxed);
    uint32_t mismatch = 0;
    auto expect = [&](CutoutState state, uint32_t expected) {
        if (value(state) != expected) mismatch |= 1u << static_cast<uint32_t>(state);
    };
    expect(CutoutState::AlphaTest, 1); expect(CutoutState::AlphaRef, 96);
    expect(CutoutState::AlphaFunc, 7); expect(CutoutState::AlphaBlend, 0);
    expect(CutoutState::DepthTest, 1); expect(CutoutState::DepthWrite, 1);
    expect(CutoutState::DepthFunc, 4); expect(CutoutState::Stencil, 0);
    expect(CutoutState::BlendOp, 1);
    if (value(CutoutState::ColorWrite) != 7 && value(CutoutState::ColorWrite) != 15)
        mismatch |= 1u << static_cast<uint32_t>(CutoutState::ColorWrite);
    if (mismatch) { recordSkip(SkipReason::Contract, mismatch); return CutoutResult::Skipped; }
    auto restore = [&]() {
        bool ok = true;
        for (CutoutState s : changed) {
            const bool restored = backend.write(s, value(s));
            ok = restored && ok; // Attempt every restore even after a failure.
        }
        if (!ok) throw std::runtime_error("Quest soft cutouts: cannot restore device render state");
    };
    if (!backend.write(CutoutState::AlphaRef, 254)) {
        recordSkip(SkipReason::InitialWrite); restore(); return CutoutResult::Skipped;
    }
    // Opaque core retains depth writes. No geometry/animation/LOD preparation repeats.
    if (!backend.draw()) { restore(); return CutoutResult::Failed; }
    const std::array<uint32_t, 6> fringe = {254, 2, 1, 5, 6, 0};
    bool ok = true;
    for (size_t i = 0; i < changed.size() && ok; ++i) ok = backend.write(changed[i], fringe[i]);
    // LESS 254 complements the core. Alpha zero contributes nothing and cannot write
    // depth. LEQUAL depth test stays on; neighboring silhouettes can still overlap.
    if (ok) ok = backend.draw();
    restore();
    return ok ? CutoutResult::Drawn : CutoutResult::Failed;
}
}

#ifndef QUEST_SOFT_CUTOUT_HOST
namespace {
constexpr std::array<D3DRENDERSTATETYPE, static_cast<size_t>(QuestXR::CutoutState::Count)> states = {
    D3DRS_ALPHATESTENABLE, D3DRS_ALPHAREF, D3DRS_ALPHAFUNC, D3DRS_ALPHABLENDENABLE,
    D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC,
    D3DRS_STENCILENABLE, D3DRS_BLENDOP, D3DRS_COLORWRITEENABLE};
static_assert(D3DCMP_GREATEREQUAL == 7 && D3DCMP_LESS == 2 && D3DCMP_LESSEQUAL == 4
    && D3DBLEND_ONE == 2 && D3DBLEND_ZERO == 1 && D3DBLEND_SRCALPHA == 5
    && D3DBLEND_INVSRCALPHA == 6 && D3DBLENDOP_ADD == 1, "D3D8 cutout state values");
class DeviceBackend final : public QuestXR::CutoutBackend {
    IDirect3DDevice8 *device;
    uint32_t primitive, minVertex, vertices, startIndex, primitives;
public:
    DeviceBackend(IDirect3DDevice8 *d, uint32_t p, uint32_t m, uint32_t v, uint32_t s, uint32_t n)
        : device(d), primitive(p), minVertex(m), vertices(v), startIndex(s), primitives(n) {}
    bool read(QuestXR::CutoutState state, uint32_t &value) override {
        DWORD actual = 0;
        if (FAILED(device->GetRenderState(states[static_cast<size_t>(state)], &actual))) return false;
        value = actual; return true;
    }
    bool write(QuestXR::CutoutState state, uint32_t value) override {
        return SUCCEEDED(device->SetRenderState(states[static_cast<size_t>(state)], value));
    }
    bool draw() override {
        return SUCCEEDED(device->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(primitive),
            minVertex, vertices, startIndex, primitives));
    }
};
}
// GeneralsX @feature Codex 02/10/2026 Scoped native draws; wrapper cache restored exactly.
bool QuestXR_DrawSoftCutout(IDirect3DDevice8 *device, TextureBaseClass *const *hints,
    uint32_t hintCount, uint32_t primitive, uint32_t minVertex, uint32_t vertices,
    uint32_t startIndex, uint32_t primitives) {
    using namespace QuestXR;
    if (!device || !SoftCutoutDrawInScope() || !hints
        || !hintCount || !hints[0] || !primitives) return false;
    const bool tree = SoftCutoutTreeInScope();
    // GeneralsX @feature Codex 07/10/2026 Filtered tree alpha needs fractional blending; never extend the new mode to named/world materials.
    const bool fence = !tree && UseFenceContours(hints[0]->Get_Full_Path().str());
    if (!SoftCutoutsRequested() && !(tree && TreeContoursRequested()) && !fence) return false;
    if (!tree && !IsCutoutTextureName(hints[0]->Get_Full_Path().str())) return false;
    auto fenceSkip = [&]() { if (fence) RecordFenceContourDraw(worldEye,false,true); };
    (tree ? treeCandidates : namedCandidates).fetch_add(1, std::memory_order_relaxed);
    // Validate the hint against the actual device binding, without acquiring/loading
    // assets. Procedural tree scope needs no new texture name or atlas pixel mutation.
    IDirect3DBaseTexture8 *bound = nullptr;
    const HRESULT binding = device->GetTexture(0, &bound);
    const bool matches = SUCCEEDED(binding) && bound && hints[0]->Peek_Diagnostic_D3D_Texture() == bound;
    const bool filteredFence = !fence || IsFilteredFenceResource(bound);
    if (bound) bound->Release();
    if (!matches || !filteredFence) { recordSkip(SkipReason::Binding); fenceSkip(); return false; }
    if (fence) {
        // Only the measured periodic sampler; no second texture, shader, arbitrary atlas or UI.
        DWORD u=0,v=0;
        IDirect3DBaseTexture8 *second=nullptr;
        const HRESULT secondResult=device->GetTexture(1,&second);
        const bool standalone=SUCCEEDED(secondResult) && !second;
        if (second) second->Release();
        if (!standalone || FAILED(device->GetTextureStageState(0,D3DTSS_ADDRESSU,&u))
            || FAILED(device->GetTextureStageState(0,D3DTSS_ADDRESSV,&v))
            || u != D3DTADDRESS_WRAP || v != D3DTADDRESS_WRAP) {
            fenceSkip(); return false;
        }
    }
    DWORD pixelShader = 0;
    if (FAILED(device->GetPixelShader(&pixelShader))) { recordSkip(SkipReason::Getter); fenceSkip(); return false; }
    lastPixelShader.store(pixelShader, std::memory_order_relaxed);
    if (pixelShader != 0) { recordSkip(SkipReason::PixelShader); fenceSkip(); return false; }
    DeviceBackend backend(device, primitive, minVertex, vertices, startIndex, primitives);
    const CutoutResult result = DrawSoftCutoutPasses(backend, true);
    if (result == CutoutResult::Skipped) { fenceSkip(); return false; }
    if (fence) RecordFenceContourDraw(worldEye,result == CutoutResult::Drawn,false);
    if (result == CutoutResult::Failed) {
        const uint32_t n = failures.fetch_add(1) + 1;
        if (n <= 8) std::fprintf(stderr, "[Quest soft cutouts] failed draw=%u; device states restored\n", n);
    } else {
        const uint32_t n = (tree ? treeDraws : namedDraws).fetch_add(1) + 1;
        if (n <= 8) std::fprintf(stderr,
            "[Quest soft cutouts] %s draw=%u; opaque core + blended fringe, fringe depth-write off\n",
            tree ? "tree-atlas" : "selected-material", n);
    }
    return true; // Do not repeat a partially submitted failed two-pass draw.
}
#endif
#endif
