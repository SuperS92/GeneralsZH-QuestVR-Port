// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <cstdint>
#include <string>
struct IDirect3DDevice8;
class TextureBaseClass;
namespace QuestXR {
// GeneralsX @feature Codex 02/10/2026 Optional two-pass cutout edges, isolated to world draws.
enum class CutoutState : uint32_t {
    AlphaTest, AlphaRef, AlphaFunc, AlphaBlend, SrcBlend, DstBlend,
    DepthTest, DepthWrite, DepthFunc, Stencil, BlendOp, ColorWrite, Count
};
class CutoutBackend {
public:
    virtual ~CutoutBackend() = default;
    virtual bool read(CutoutState state, uint32_t &value) = 0;
    virtual bool write(CutoutState state, uint32_t value) = 0;
    virtual bool draw() = 0;
};
enum class CutoutResult { Skipped, Drawn, Failed };
CutoutResult DrawSoftCutoutPasses(CutoutBackend &backend, bool enabled);
bool SoftCutoutsRequested();
uint32_t SoftCutoutTreeDraws();
uint32_t SoftCutoutNamedDraws();
uint32_t SoftCutoutFailures();
// GeneralsX @bugfix Codex 07/10/2026 Bounded rejection evidence; serialize only for requested eye captures.
std::string SoftCutoutDiagnosticsJson();
class SoftCutoutWorldScope {
    int previous;
public:
    explicit SoftCutoutWorldScope(int eye);
    ~SoftCutoutWorldScope();
    SoftCutoutWorldScope(const SoftCutoutWorldScope &) = delete;
    SoftCutoutWorldScope &operator=(const SoftCutoutWorldScope &) = delete;
};
class SoftCutoutTreeScope {
    bool previous;
public:
    SoftCutoutTreeScope();
    ~SoftCutoutTreeScope();
    SoftCutoutTreeScope(const SoftCutoutTreeScope &) = delete;
    SoftCutoutTreeScope &operator=(const SoftCutoutTreeScope &) = delete;
};
bool SoftCutoutDrawInScope();
bool SoftCutoutTreeInScope();
}
bool QuestXR_DrawSoftCutout(IDirect3DDevice8 *device, TextureBaseClass *const *hints,
    uint32_t hintCount, uint32_t primitive, uint32_t minVertex,
    uint32_t vertices, uint32_t startIndex, uint32_t primitives);
#endif
