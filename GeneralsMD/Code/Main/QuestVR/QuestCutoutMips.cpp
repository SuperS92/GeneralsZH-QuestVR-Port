// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestCutoutMips.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <new>
#include <vector>
namespace QuestXR {
namespace {
std::atomic<uint32_t> uploads{0}, failures{0};
constexpr uint32_t reference = 96, unit = 4096, maxScale = 4 * unit;
uint32_t scaleAlpha(uint32_t alpha, uint32_t scale) {
    return std::min(255u, (alpha * scale + unit / 2) / unit);
}
uint32_t chooseScale(const std::vector<uint8_t> &alpha, uint32_t target) {
    std::array<uint32_t, 256> histogram{};
    for (uint8_t a : alpha) ++histogram[a];
    uint32_t best = unit, bestError = 0xffffffffu, bestDistance = 0xffffffffu;
    auto consider = [&](uint32_t q) {
        if (q > maxScale) return;
        uint32_t covered = 0;
        for (uint32_t a = 1; a < 256; ++a)
            if (scaleAlpha(a, q) >= reference) covered += histogram[a];
        const uint32_t error = covered > target ? covered - target : target - covered;
        const uint32_t distance = q > unit ? q - unit : unit - q;
        if (error < bestError || (error == bestError && distance < bestDistance)) {
            best = q; bestError = error; bestDistance = distance;
        }
    };
    consider(unit); consider(0); consider(maxScale);
    for (uint32_t a = 1; a < 256; ++a) if (histogram[a]) {
        const uint32_t boundary = (reference * unit - unit / 2 + a - 1) / a;
        consider(boundary);
        if (boundary) consider(boundary - 1);
    }
    return best;
}
}
bool CutoutMipFixRequested() {
    const char *s = std::getenv("GX_QUEST_CUTOUT_MIPS");
    return s && std::strcmp(s, "1") == 0;
}
bool IsCutoutTextureName(const char *name) {
    if (!name) return false;
    const char *base = name;
    for (const char *p = name; *p; ++p) if (*p == '/' || *p == '\\') base = p + 1;
    const size_t length = std::strlen(base);
    if (!length || length >= 96) return false;
    char lower[96]{};
    for (size_t i = 0; i < length; ++i)
        lower[i] = base[i] >= 'A' && base[i] <= 'Z' ? base[i] + ('a' - 'A') : base[i];
    const char *prefixes[] = {"pmbush", "ptbush", "pmtree", "pttree", "pmpine", "ptpine",
        "pmpalm", "ptpalm", "atrailings"};
    if (length < 5 || (std::strcmp(lower + length - 4, ".tga") != 0
        && std::strcmp(lower + length - 4, ".dds") != 0)) return false;
    for (const char *prefix : prefixes) {
        const size_t n = std::strlen(prefix);
        if (length > n + 4 && std::strncmp(lower, prefix, n) == 0) return true;
    }
    return false; // No terrain/tree atlas, arbitrary UI/particles, or mod-wide alpha override.
}
bool UseCutoutMipFix(const char *name) { return CutoutMipFixRequested() && IsCutoutTextureName(name); }
uint32_t CutoutMipUploads() { return uploads.load(); }
uint32_t CutoutMipFailures() { return failures.load(); }
bool RebuildCutoutAlpha(uint8_t *const *pixels, const unsigned *pitches,
    uint32_t width, uint32_t height, uint32_t levels, bool enabled) {
    if (!enabled || !pixels || !pitches || !width || !height || width > 2048 || height > 2048
        || (width & (width - 1)) || (height & (height - 1)) || levels < 2 || levels > 12) return false;
    uint32_t w = width, h = height, maxLevels = 1;
    while (w > 1 || h > 1) { w = std::max(1u, w / 2); h = std::max(1u, h / 2); ++maxLevels; }
    if (levels > maxLevels) return false;
    w = width; h = height;
    // Validate all buffers, dimensions and overlap before allocation or mutation.
    std::array<uintptr_t, 12> begin{}, end{};
    for (uint32_t level = 0; level < levels; ++level) {
        if (!pixels[level] || pitches[level] < w * 4 || pitches[level] > 65536) return false;
        begin[level] = reinterpret_cast<uintptr_t>(pixels[level]);
        const size_t span = size_t(h - 1) * pitches[level] + w * 4;
        if (begin[level] > UINTPTR_MAX - span) return false;
        end[level] = begin[level] + span;
        for (uint32_t prior = 0; prior < level; ++prior)
            if (begin[level] < end[prior] && begin[prior] < end[level]) return false;
        w = std::max(1u, w / 2); h = std::max(1u, h / 2);
    }
    try {
        std::vector<std::vector<uint8_t>> alpha(levels);
        alpha[0].resize(size_t(width) * height);
        uint32_t baseCovered = 0;
        for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
            const uint8_t a = pixels[0][size_t(y) * pitches[0] + x * 4 + 3];
            alpha[0][size_t(y) * width + x] = a;
            baseCovered += a >= reference;
        }
        w = width; h = height;
        // Generate all raw alpha mips first. Never propagate coverage scaling into the next mip.
        for (uint32_t level = 1; level < levels; ++level) {
            const uint32_t nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
            alpha[level].resize(size_t(nw) * nh);
            for (uint32_t y = 0; y < nh; ++y) for (uint32_t x = 0; x < nw; ++x) {
                uint32_t sum = 0;
                for (uint32_t dy = 0; dy < 2; ++dy) for (uint32_t dx = 0; dx < 2; ++dx)
                    sum += alpha[level-1][size_t(std::min(h-1, y*2+dy)) * w + std::min(w-1, x*2+dx)];
                alpha[level][size_t(y) * nw + x] = static_cast<uint8_t>((sum + 2) / 4);
            }
            w = nw; h = nh;
        }
        for (uint32_t level = 1; level < levels; ++level) {
            const uint32_t target = static_cast<uint32_t>((uint64_t(baseCovered) * alpha[level].size()
                + uint64_t(width) * height / 2) / (uint64_t(width) * height));
            const uint32_t scale = chooseScale(alpha[level], target);
            for (uint8_t &a : alpha[level]) a = static_cast<uint8_t>(scaleAlpha(a, scale));
        }
        // No further allocation/fallible work. Only reduced-level A8 changes; RGB/base/padding exact.
        w = width; h = height;
        for (uint32_t level = 1; level < levels; ++level) {
            w = std::max(1u, w / 2); h = std::max(1u, h / 2);
            for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x)
                pixels[level][size_t(y) * pitches[level] + x * 4 + 3] = alpha[level][size_t(y) * w + x];
        }
        return true;
    } catch (const std::bad_alloc &) { return false; }
}
void ApplyLoadedCutoutAlpha(uint8_t *const *pixels, const unsigned *pitches,
    uint32_t width, uint32_t height, uint32_t levels, const char *name) {
    if (!UseCutoutMipFix(name) || levels < 2) return;
    if (!RebuildCutoutAlpha(pixels, pitches, width, height, levels, true)) { ++failures; return; }
    const uint32_t count = uploads.fetch_add(1) + 1;
    if (count <= 8) std::fprintf(stderr,
        "[Quest cutout mips] upload=%u base=%ux%u levels=%u alphaReference=96; level-zero/RGB preserved\n",
        count, width, height, levels);
}
}
#endif
