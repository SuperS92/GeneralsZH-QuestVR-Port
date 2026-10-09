// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestTerrainMipFilter.h"
#include "VRDevice/QuestTerrainDetailTest.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>
#include <new>
#ifndef QUEST_DRAW_CAPTURE_HOST
#include <d3d8.h>
#endif
namespace QuestXR {
namespace {
std::atomic<uint32_t> regions{0}, failures{0};
uint32_t readPixel(const uint8_t *p, uint32_t bytes) {
    uint32_t value = 0;
    std::memcpy(&value, p, bytes);
    return value;
}
void writeRgb(uint8_t *p, uint32_t bytes, uint32_t rgb) {
    const uint32_t alphaMask = bytes == 2 ? 0x8000u : 0xff000000u;
    const uint32_t value = (readPixel(p, bytes) & alphaMask) | rgb;
    std::memcpy(p, &value, bytes);
}
int coordinate(int value, int extent, bool repeat) {
    if (!repeat) return std::max(0, std::min(extent - 1, value));
    const int wrapped = value % extent;
    return wrapped < 0 ? wrapped + extent : wrapped;
}
}
uint32_t TerrainMipFilterMode() {
    if (TerrainDetailTestRequested()) return 0; // Neutral test takes precedence.
    const char *choice = std::getenv("GX_QUEST_TERRAIN_MIP_FILTER");
    return choice && std::strcmp(choice, "1") == 0 ? 1
        : choice && std::strcmp(choice, "2") == 0 ? 2 : 0;
}
uint32_t TerrainMipFilterRegions() { return regions.load(); }
uint32_t TerrainMipFilterFailures() { return failures.load(); }
bool FilterTerrainMipRgb(void *pixels, int pitch, uint32_t sw, uint32_t sh,
    uint32_t format, const TerrainMipRegion &r, uint32_t mode) {
    const uint32_t bytes = format == 25 ? 2 : format == 21 ? 4 : 0;
    if (mode < 1 || mode > 2 || !pixels || !bytes || !sw || !sh || sw > 4096 || sh > 4096
        || pitch < static_cast<int>(sw * bytes) || pitch > 65536 || !r.width || !r.height
        || r.x > sw || r.y > sh || r.width > sw - r.x || r.height > sh - r.y
        || r.border > r.x || r.border > r.y || r.border > sw - r.x - r.width
        || r.border > sh - r.y - r.height) return false;
    std::vector<uint32_t> rgb;
    try { rgb.resize(static_cast<size_t>(r.width) * r.height); }
    catch (const std::bad_alloc &) { return false; } // Nothing modified.
    auto *base = static_cast<uint8_t *>(pixels);
    const int radius = mode == 1 ? 1 : 2;
    const uint32_t weights[5] = {1, 4, 6, 4, 1};
    const uint32_t divisor = mode == 1 ? 16 : 256;
    const uint32_t mask = bytes == 2 ? 31 : 255;
    const uint32_t shift = bytes == 2 ? 5 : 8;
    for (uint32_t y = 0; y < r.height; ++y) for (uint32_t x = 0; x < r.width; ++x) {
        uint32_t sums[3] = {};
        for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx) {
            const uint32_t wx = mode == 1 ? (dx == 0 ? 2 : 1) : weights[dx + 2];
            const uint32_t wy = mode == 1 ? (dy == 0 ? 2 : 1) : weights[dy + 2];
            const uint32_t px = r.x + coordinate(static_cast<int>(x) + dx, r.width, r.repeat);
            const uint32_t py = r.y + coordinate(static_cast<int>(y) + dy, r.height, r.repeat);
            const uint32_t value = readPixel(base + static_cast<size_t>(py) * pitch + px * bytes, bytes);
            for (uint32_t c = 0; c < 3; ++c) sums[c] += ((value >> (c * shift)) & mask) * wx * wy;
        }
        uint32_t value = 0;
        for (uint32_t c = 0; c < 3; ++c) value |= ((sums[c] + divisor / 2) / divisor) << (c * shift);
        rgb[static_cast<size_t>(y) * r.width + x] = value;
    }
    // Only after all sampling/allocation succeeded: write RGB, then regenerate
    // the existing gutter RGB from this material. Never sample a neighbouring class.
    for (int y = -static_cast<int>(r.border); y < static_cast<int>(r.height + r.border); ++y)
        for (int x = -static_cast<int>(r.border); x < static_cast<int>(r.width + r.border); ++x) {
            const uint32_t sx = coordinate(x, r.width, r.repeat), sy = coordinate(y, r.height, r.repeat);
            auto *p = base + static_cast<size_t>(static_cast<int>(r.y) + y) * pitch
                + (static_cast<int>(r.x) + x) * bytes;
            writeRgb(p, bytes, rgb[static_cast<size_t>(sy) * r.width + sx]);
        }
    return true;
}
void ApplyTerrainMipFilter(IDirect3DTexture8 *texture, const TerrainMipRegion &baseRegion) {
#ifndef QUEST_DRAW_CAPTURE_HOST
    const uint32_t mode = TerrainMipFilterMode();
    if (!mode || !texture) return;
    // Preserve level zero, dimensions, count, LOD and sampler state. The atlas
    // has valid integral material origins/gutters through existing levels 1/2.
    const uint32_t levels = std::min(3u, static_cast<uint32_t>(texture->GetLevelCount()));
    for (uint32_t level = 1; level < levels; ++level) {
        const uint32_t scale = 1u << level;
        if (baseRegion.x % scale || baseRegion.y % scale || baseRegion.width % scale
            || baseRegion.height % scale || baseRegion.border % scale) { ++failures; continue; }
        IDirect3DSurface8 *surface = nullptr;
        D3DSURFACE_DESC desc{}; D3DLOCKED_RECT lock{};
        if (FAILED(texture->GetSurfaceLevel(level, &surface)) || !surface) { ++failures; continue; }
        if (FAILED(surface->GetDesc(&desc)) || FAILED(surface->LockRect(&lock, nullptr, 0))) {
            surface->Release(); ++failures; continue;
        }
        const TerrainMipRegion r{baseRegion.x / scale, baseRegion.y / scale,
            baseRegion.width / scale, baseRegion.height / scale, baseRegion.border / scale, baseRegion.repeat};
        const bool applied = FilterTerrainMipRgb(lock.pBits, lock.Pitch, desc.Width, desc.Height, desc.Format, r, mode);
        const bool unlocked = SUCCEEDED(surface->UnlockRect());
        surface->Release();
        if (!applied || !unlocked) { ++failures; continue; }
        const uint32_t count = regions.fetch_add(1) + 1;
        if (count <= 8) std::fprintf(stderr,
            "[Quest terrain mip filter] mode=%u region=%u level=%u rect=%u,%u %ux%u; RGB only, level zero preserved\n",
            mode, count, level, r.x, r.y, r.width, r.height);
    }
#else
    // Host tests exercise pixel filtering; native device wrapper is hardware-gated.
    (void)texture; (void)baseRegion;
#endif
}
}
#endif
