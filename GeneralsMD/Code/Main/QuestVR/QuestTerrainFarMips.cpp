// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestTerrainFarMips.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include "VRDevice/QuestGraphicsChoices.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#ifndef QUEST_DRAW_CAPTURE_HOST
#include "WW3D2/dx8wrapper.h"
#include "W3DDevice/GameClient/TerrainTex.h"
static_assert(TEXTURE_WIDTH == 2048 && TILE_PIXEL_EXTENT == 64 && TILE_OFFSET == 8,
    "Revalidate terrain far mip packing after upstream layout changes");
#endif
namespace QuestXR {
namespace {
std::atomic<uint32_t> attempts{0}, uploads{0}, rejected{0}, failures{0};
std::atomic<uint32_t> lastReject{0}, cellsChecked{0};
std::atomic<uint32_t> filterUploads{0}, filterFailures{0}, filterMaterials{0}, filterSkippedAlpha{0}, filterChangedPixels{0};
std::atomic<uint32_t> filterUnequalEndpoints{0}, filterLastFailure{0};
IDirect3DTexture8 *reject(uint32_t reason) {
    lastReject = reason;
    if (rejected.fetch_add(1) < 8) std::fprintf(stderr,
        "[Quest terrain far mip] kept original: reason=%u (1=request/resource,2=material,3=layout,4=stored cliff,5=map/UV)\n", reason);
    return nullptr;
}
bool buffer(const void *p, int pitch, uint32_t width, uint32_t height) {
    if (!p || !width || !height || width > 2048 || height > 2048 || pitch < 0
        || pitch > 65536 || static_cast<uint32_t>(pitch) < width * 2) return false;
    const uintptr_t bytes = uintptr_t(height - 1) * pitch + width * 2;
    return reinterpret_cast<uintptr_t>(p) <= std::numeric_limits<uintptr_t>::max() - bytes;
}
uint16_t read(const uint8_t *p) { uint16_t value; std::memcpy(&value, p, 2); return value; }
}
// GeneralsX @feature Codex 07/10/2026 One additional terrain mip under the accepted edge-guard sampler.
bool TerrainFarMipsRequested() {
    const char *s = std::getenv("GX_QUEST_TERRAIN_FAR_MIPS");
    return s && std::strcmp(s, "1") == 0;
}
bool TerrainFarFilterRequested() {
    const char *s = std::getenv("GX_QUEST_TERRAIN_FAR_FILTER");
    return s && std::strcmp(s, "1") == 0;
}
bool ValidateTerrainFarMaterials(const TerrainFarMaterial *m, uint32_t count, uint32_t w, uint32_t h) {
    if (!m || !count || count > 256 || w != 2048 || h < 64 || h > 2048 || (h & (h - 1))) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const auto &r = m[i];
        if (r.x < 4 || r.y < 4 || (r.x - 4) % 72 || (r.y - 4) % 72
            || r.size <= 0 || r.size > 1792 || r.size % 64 || r.firstTile < 0
            || r.tileCount <= 0 || r.firstTile >= 1024 || r.tileCount > 1024 - r.firstTile
            || r.x > int(w) - r.size - 4 || r.y > int(h) - r.size - 4) return false;
        for (uint32_t j = 0; j < i; ++j) {
            const auto &s = m[j];
            if (r.x - 4 < s.x + s.size + 4 && s.x - 4 < r.x + r.size + 4
                && r.y - 4 < s.y + s.size + 4 && s.y - 4 < r.y + r.size + 4) return false;
            if (r.firstTile < s.firstTile + s.tileCount && s.firstTile < r.firstTile + r.tileCount) return false;
        }
    }
    return true;
}
// GeneralsX @feature Codex 07/10/2026 Gentle RGB-only filtering within each complete periodic material at LOD3.
bool FilterTerrainFarMip(void *pixels, int pitch, uint32_t baseWidth, uint32_t baseHeight,
    const TerrainFarMaterial *materials, uint32_t count, TerrainFarFilterResult &result) {
    result = {};
    auto fail = [&](uint32_t reason) { result.failureReason = reason; return false; };
    if (!ValidateTerrainFarMaterials(materials, count, baseWidth, baseHeight)) return fail(1);
    const uint32_t w = baseWidth/8, h = baseHeight/8;
    if (!buffer(pixels, pitch, w, h)) return fail(2);
    std::unique_ptr<uint16_t[]> filtered(new (std::nothrow) uint16_t[size_t(w)*h]);
    if (!filtered) return fail(3);
    const auto *src = static_cast<const uint8_t *>(pixels);
    for (uint32_t y = 0; y < h; ++y) std::memcpy(filtered.get()+y*w, src+y*pitch, w*2);
    TerrainFarFilterResult completed{};
    for (uint32_t i = 0; i < count; ++i) {
        const auto &m = materials[i];
        const uint32_t ox = (m.x-4)/8, oy = (m.y-4)/8, period = m.size/8;
        if (ox+period >= w || oy+period >= h) return fail(4);
        auto pixel = [&](uint32_t x, uint32_t y) { return read(src+(oy+y)*pitch+2*(ox+x)); };
        bool opaque = true;
        for (uint32_t y = 0; y <= period && opaque; ++y)
            for (uint32_t x = 0; x <= period; ++x)
                if (!(pixel(x,y)&0x8000)) { opaque = false; break; }
        if (!opaque) { ++completed.skippedAlpha; continue; }
        // GeneralsX @bugfix Codex 07/10/2026 Inherited mip resampling does not guarantee identical endpoints.
        // Give both representatives equal weight, including all four corners.
        // Bounds/ownership checks still precede reads; never sample adjacent materials.
        for (uint32_t p = 0; p <= period; ++p) {
            if (pixel(0,p) != pixel(period,p)) ++completed.unequalEndpoints;
            if (pixel(p,0) != pixel(p,period)) ++completed.unequalEndpoints;
        }
        auto channel = [&](uint32_t x, uint32_t y, uint32_t shift) {
            const uint32_t nx = x == 0 ? 2 : 1, ny = y == 0 ? 2 : 1;
            uint32_t sum = 0;
            for (uint32_t j = 0; j < ny; ++j) for (uint32_t k = 0; k < nx; ++k)
                sum += (pixel(k ? period : x, j ? period : y) >> shift) & 31;
            const uint32_t samples = nx*ny;
            return (sum + samples/2)/samples;
        };
        // Half original + half 3x3 binomial kernel: center20, axial2, diagonal1 /32.
        // Wrap at the complete material boundary, never at its internal 64px tiles.
        for (uint32_t y = 0; y <= period; ++y) for (uint32_t x = 0; x <= period; ++x) {
            const uint32_t px=x%period, py=y%period;
            uint16_t output = 0x8000;
            for (uint32_t shift : {0u,5u,10u}) {
                uint32_t sum = 0;
                for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
                    const uint32_t sx=(int(px)+dx+int(period))%period, sy=(int(py)+dy+int(period))%period;
                    const uint32_t weight = !dx && !dy ? 20 : !dx || !dy ? 2 : 1;
                    sum += channel(sx,sy,shift)*weight;
                }
                output |= uint16_t(((sum+16)/32)<<shift);
            }
            filtered[(oy+y)*w+ox+x] = output;
            if (output != pixel(x,y)) ++completed.changedPixels;
        }
        ++completed.materials;
    }
    // All layouts and the allocation have passed. Padding stays untouched.
    auto *dst = static_cast<uint8_t *>(pixels);
    for (uint32_t y = 0; y < h; ++y) std::memcpy(dst+y*pitch, filtered.get()+y*w, w*2);
    result = completed;
    return true;
}
bool ValidateTerrainFarCliff(const TerrainFarMaterial *m, uint32_t count, int32_t tile, const float *uv) {
    if (!m || !count || count > 256 || !uv || tile < 0 || tile >= 4096) return false;
    const int32_t index = tile >> 2;
    for (uint32_t i = 0; i < count; ++i) {
        if (index < m[i].firstTile || index >= m[i].firstTile + m[i].tileCount) continue;
        // Stored cliff U is relative to the material's left; V to its bottom.
        const float extent = float(m[i].size) / 2048.0f;
        for (uint32_t p = 0; p < 4; ++p)
            if (!std::isfinite(uv[2*p]) || !std::isfinite(uv[2*p+1])
                || uv[2*p] < 0 || uv[2*p] > extent || uv[2*p+1] < -extent || uv[2*p+1] > 0) return false;
        return true;
    }
    return false;
}
bool DownsampleTerrainFarMip(const void *source, int sp, void *target, int tp, uint32_t w, uint32_t h) {
    if (w < 2 || h < 2 || (w & 1) || (h & 1) || !buffer(source, sp, w, h)
        || !buffer(target, tp, w/2, h/2)) return false;
    const uintptr_t sa = reinterpret_cast<uintptr_t>(source), ta = reinterpret_cast<uintptr_t>(target);
    const uintptr_t se = sa + uintptr_t(h - 1) * sp + w * 2;
    const uintptr_t te = ta + uintptr_t(h/2 - 1) * tp + w;
    if (sa < te && ta < se) return false;
    const auto *s = static_cast<const uint8_t *>(source);
    auto *t = static_cast<uint8_t *>(target);
    for (uint32_t y = 0; y < h/2; ++y) for (uint32_t x = 0; x < w/2; ++x) {
        const uint16_t v[] = {read(s + 2*y*sp + 4*x), read(s + 2*y*sp + 4*x + 2),
            read(s + (2*y+1)*sp + 4*x), read(s + (2*y+1)*sp + 4*x + 2)};
        uint16_t result = 0;
        for (uint32_t shift : {0u, 5u, 10u}) {
            const uint32_t sum = ((v[0] >> shift) & 31) + ((v[1] >> shift) & 31)
                + ((v[2] >> shift) & 31) + ((v[3] >> shift) & 31);
            result |= uint16_t(((sum + 2) / 4) << shift);
        }
        const uint32_t alpha = (v[0] >> 15) + (v[1] >> 15) + (v[2] >> 15) + (v[3] >> 15);
        if (alpha >= 2) result |= 0x8000;
        std::memcpy(t + y*tp + 2*x, &result, 2);
    }
    return true;
}
bool ValidateTerrainFarQuad(const TerrainFarMaterial *m, uint32_t count, uint32_t h, const float *u, const float *v) {
    if (!m || !count || count > 256 || !h || h > 2048 || !u || !v) return false;
    for (uint32_t p = 0; p < 4; ++p) if (!std::isfinite(u[p]) || !std::isfinite(v[p])) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const float loU=float(m[i].x)/2048, hiU=float(m[i].x+m[i].size)/2048;
        const float loV=float(m[i].y)/h, hiV=float(m[i].y+m[i].size)/h;
        bool contained=true;
        for (uint32_t p = 0; p < 4; ++p)
            if (u[p]<loU || u[p]>hiU || v[p]<loV || v[p]>hiV) { contained=false; break; }
        if (contained) return true;
    }
    return false;
}
std::string TerrainFarMipDiagnosticsJson() {
    char text[768];
    std::snprintf(text, sizeof(text), "{\"schema\":3,\"requested\":%s,\"edgeGuard\":%s,\"attempts\":%u,\"uploads\":%u,\"rejected\":%u,\"failures\":%u,\"lastReject\":%u,\"lastCellsChecked\":%u,\"filterRequested\":%s,\"filterUploads\":%u,\"filterFailures\":%u,\"filterMaterials\":%u,\"filterSkippedAlpha\":%u,\"filterChangedPixels\":%u,\"filterUnequalEndpoints\":%u,\"filterLastFailure\":%u}",
        TerrainFarMipsRequested() ? "true" : "false", TextureFilterChoice() == 2 ? "true" : "false",
        attempts.load(), uploads.load(), rejected.load(), failures.load(), lastReject.load(), cellsChecked.load(),
        TerrainFarFilterRequested() ? "true" : "false", filterUploads.load(), filterFailures.load(),
        filterMaterials.load(), filterSkippedAlpha.load(), filterChangedPixels.load(),
        filterUnequalEndpoints.load(), filterLastFailure.load());
    return text;
}
#ifndef QUEST_DRAW_CAPTURE_HOST
namespace {
template<class T> struct Ref {
    T *p = nullptr;
    ~Ref() { if (p) p->Release(); }
    Ref() = default;
    Ref(const Ref &) = delete;
    Ref &operator=(const Ref &) = delete;
};
struct Lock {
    IDirect3DSurface8 *surface;
    D3DLOCKED_RECT rect{};
    bool held = false;
    explicit Lock(IDirect3DSurface8 *s) : surface(s) {}
    bool open(DWORD flags) { held = SUCCEEDED(surface->LockRect(&rect, nullptr, flags)); return held; }
    bool close() { if (!held) return false; held = false; return SUCCEEDED(surface->UnlockRect()); }
    ~Lock() { if (held) surface->UnlockRect(); }
};
bool copyLevel(IDirect3DTexture8 *source, IDirect3DTexture8 *target, uint32_t level, uint32_t w, uint32_t h) {
    Ref<IDirect3DSurface8> s, t;
    if (FAILED(source->GetSurfaceLevel(level, &s.p)) || !s.p
        || FAILED(target->GetSurfaceLevel(level, &t.p)) || !t.p) return false;
    D3DSURFACE_DESC sd{}, td{};
    if (FAILED(s.p->GetDesc(&sd)) || FAILED(t.p->GetDesc(&td))
        || sd.Width != w || td.Width != w || sd.Height != h || td.Height != h
        || sd.Format != D3DFMT_A1R5G5B5 || td.Format != sd.Format) return false;
    Lock sl(s.p), tl(t.p);
    if (!sl.open(D3DLOCK_READONLY) || !tl.open(0)
        || !buffer(sl.rect.pBits, sl.rect.Pitch, w, h) || !buffer(tl.rect.pBits, tl.rect.Pitch, w, h)) return false;
    for (uint32_t y = 0; y < h; ++y) std::memcpy(static_cast<uint8_t *>(tl.rect.pBits) + y*tl.rect.Pitch,
        static_cast<const uint8_t *>(sl.rect.pBits) + y*sl.rect.Pitch, w*2);
    const bool a = tl.close(), b = sl.close();
    return a && b;
}
}
IDirect3DTexture8 *CreateTerrainFarMip(IDirect3DTexture8 *source, const TXTextureClass *classes,
    int count, const TCliffInfo *cliffs, int cliffCount, WorldHeightMap *map) {
    if (!TerrainFarMipsRequested()) return nullptr;
    ++attempts;
    if (TextureFilterChoice() != 2 || !source || !classes || count <= 0 || count > 256
        || !cliffs || cliffCount < 1 || cliffCount > NUM_CLIFF_INFO || source->GetLevelCount() != 3) {
        return reject(1);
    }
    std::array<TerrainFarMaterial, 256> materials{};
    for (int i = 0; i < count; ++i) {
        if (classes[i].width <= 0 || classes[i].width > 28) return reject(2);
        materials[i] = {classes[i].positionInTexture.x, classes[i].positionInTexture.y,
            classes[i].width * 64, classes[i].firstTile, classes[i].numTiles};
    }
    D3DSURFACE_DESC desc{};
    if (FAILED(source->GetLevelDesc(0, &desc))) { ++failures; return nullptr; }
    if (desc.Format != D3DFMT_A1R5G5B5 || desc.Pool != D3DPOOL_MANAGED || desc.Usage
        || !ValidateTerrainFarMaterials(materials.data(), count, desc.Width, desc.Height)) {
        return reject(3);
    }
    for (int i = 1; i < cliffCount; ++i) {
        const auto &c = cliffs[i];
        const float uv[] = {c.u0,c.v0,c.u1,c.v1,c.u2,c.v2,c.u3,c.v3};
        if (!ValidateTerrainFarCliff(materials.data(), count, c.tileIndex, uv)) return reject(4);
    }
    // Read the inherited base/blend UV queries only after the factory sets the
    // original atlas height. Include legacy height-based stretching, not just
    // stored cliff records. Queries do not mutate map data or triangle flags.
    if (!map || map->getXExtent()<2 || map->getYExtent()<2
        || map->getXExtent()>1024 || map->getYExtent()>1024) return reject(5);
    cellsChecked=0;
    for (int y=0; y<map->getYExtent()-1; ++y) for (int x=0; x<map->getXExtent()-1; ++x) {
        float u[4],v[4];
        for (uint32_t p=0;p<4;++p) u[p]=v[p]=std::numeric_limits<float>::quiet_NaN();
        const int qx=x-map->getDrawOrgX(),qy=y-map->getDrawOrgY();
        map->getUVData(qx,qy,u,v);
        if (!ValidateTerrainFarQuad(materials.data(),count,desc.Height,u,v)) return reject(5);
        UnsignedByte alpha[4]{};Bool flip=false,cliff=false;
        map->getAlphaUVData(qx,qy,u,v,alpha,&flip);
        if ((alpha[0]||alpha[1]||alpha[2]||alpha[3])
            && !ValidateTerrainFarQuad(materials.data(),count,desc.Height,u,v)) return reject(5);
        if (map->getExtraAlphaUVData(x,y,u,v,alpha,&flip,&cliff)
            && (alpha[0]||alpha[1]||alpha[2]||alpha[3])
            && !ValidateTerrainFarQuad(materials.data(),count,desc.Height,u,v)) return reject(5);
        ++cellsChecked;
    }
    Ref<IDirect3DDevice8> device;
    Ref<IDirect3DTexture8> target;
    if (FAILED(source->GetDevice(&device.p)) || !device.p
        || FAILED(device.p->CreateTexture(desc.Width, desc.Height, 4, 0, desc.Format, desc.Pool, &target.p)) || !target.p) {
        ++failures; return nullptr;
    }
    for (uint32_t level = 0; level < 3; ++level)
        if (!copyLevel(source, target.p, level, desc.Width >> level, desc.Height >> level)) { ++failures; return nullptr; }
    Ref<IDirect3DSurface8> s, t;
    if (target.p->GetLevelCount() != 4 || FAILED(target.p->GetSurfaceLevel(2, &s.p)) || !s.p
        || FAILED(target.p->GetSurfaceLevel(3, &t.p)) || !t.p) { ++failures; return nullptr; }
    Lock sl(s.p), tl(t.p);
    if (!sl.open(D3DLOCK_READONLY) || !tl.open(0)
        || !DownsampleTerrainFarMip(sl.rect.pBits, sl.rect.Pitch, tl.rect.pBits, tl.rect.Pitch, desc.Width/4, desc.Height/4)) {
        ++failures; return nullptr;
    }
    TerrainFarFilterResult filtered{};
    bool filterApplied = false;
    if (TerrainFarFilterRequested()) {
        filterApplied = FilterTerrainFarMip(tl.rect.pBits, tl.rect.Pitch, desc.Width, desc.Height,
            materials.data(), count, filtered);
        if (!filterApplied) {
            ++filterFailures; filterLastFailure = filtered.failureReason;
            std::fprintf(stderr, "[Quest terrain far filter] kept accepted fourth mip: reason=%u (1=layout,2=buffer,3=allocation,4=bounds)\n",
                filtered.failureReason);
        }
    }
    const bool a = tl.close(), b = sl.close();
    if (!a || !b) { ++failures; return nullptr; }
    target.p->SetLOD(source->GetLOD());
    ++uploads;
    if (filterApplied) {
        ++filterUploads; filterMaterials += filtered.materials;
        filterSkippedAlpha += filtered.skippedAlpha; filterChangedPixels += filtered.changedPixels;
        filterUnequalEndpoints += filtered.unequalEndpoints;
        std::fprintf(stderr, "[Quest terrain far filter] gentle LOD3 only: materials=%u skippedAlpha=%u changedPixels=%u unequalEndpoints=%u\n",
            filtered.materials, filtered.skippedAlpha, filtered.changedPixels, filtered.unequalEndpoints);
    }
    std::fprintf(stderr, "[Quest terrain far mip] four levels %ux%u -> %ux%u; original three levels copied exactly, cliff UV/layout checked\n",
        desc.Width, desc.Height, desc.Width/8, desc.Height/8);
    auto *result = target.p; target.p = nullptr;
    return result;
}
#endif
}
#endif
