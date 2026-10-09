// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestTreeContours.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>
namespace QuestXR {
namespace {
std::atomic<uint32_t> uploads{0}, failures{0}, regionCount{0}, changedCount{0}, partialBefore{0}, partialAfter{0};
}
bool TreeContoursRequested() {
    const char *s = std::getenv("GX_QUEST_TREE_CONTOURS");
    return s && std::strcmp(s, "1") == 0;
}
bool SmoothTreeContours(uint8_t *pixels, uint32_t pitch, uint32_t width, uint32_t height,
    const TreeContourRegion *regions, uint32_t count, bool enabled, TreeContourResult &result) {
    result = {};
    if (!enabled || !pixels || !regions || !count || count > 64 || !width || !height
        || width > 2048 || height > 2048 || pitch < width*4 || pitch > 65536) return false;
    const uintptr_t begin = reinterpret_cast<uintptr_t>(pixels);
    const size_t span = size_t(height-1)*pitch+width*4;
    if (begin > UINTPTR_MAX-span) return false;
    for (uint32_t i=0; i<count; ++i) {
        const auto &r = regions[i];
        if (!r.width || !r.height || r.x >= width || r.y >= height
            || r.width > width-r.x || r.height > height-r.y) return false;
        for (uint32_t j=0; j<i; ++j) {
            const auto &b = regions[j];
            if (r.x < b.x+b.width && b.x < r.x+r.width && r.y < b.y+b.height && b.y < r.y+r.height) return false;
        }
    }
    try {
        std::vector<std::vector<uint8_t>> output(count);
        for (uint32_t i=0; i<count; ++i) output[i].resize(size_t(regions[i].width)*regions[i].height*4);
        TreeContourResult prepared{}; prepared.regions = count;
        const unsigned weights[3] = {1, 2, 1};
        for (uint32_t i=0; i<count; ++i) {
            const auto &r = regions[i];
            for (uint32_t y=0; y<r.height; ++y) for (uint32_t x=0; x<r.width; ++x) {
                const auto *original = pixels+size_t(r.y+y)*pitch+(r.x+x)*4;
                auto *dest = output[i].data()+(size_t(y)*r.width+x)*4;
                uint32_t alphaSum = 0, colorSum[3]{};
                for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
                    const uint32_t sx = uint32_t(std::max(0, std::min(int(r.width)-1, int(x)+dx)));
                    const uint32_t sy = uint32_t(std::max(0, std::min(int(r.height)-1, int(y)+dy)));
                    const auto *sample = pixels+size_t(r.y+sy)*pitch+(r.x+sx)*4;
                    const uint32_t weight = weights[dy+1]*weights[dx+1], a = sample[3]*weight;
                    alphaSum += a;
                    for (int c=0; c<3; ++c) colorSum[c] += sample[c]*a;
                }
                dest[3] = uint8_t((alphaSum+8)/16);
                // Do not pull RGB from transparent atlas texels into a visible contour.
                for (int c=0; c<3; ++c) dest[c] = alphaSum ? uint8_t((colorSum[c]+alphaSum/2)/alphaSum) : original[c];
                prepared.changedPixels += std::memcmp(original, dest, 4) != 0;
                prepared.partialBefore += original[3] > 0 && original[3] < 255;
                prepared.partialAfter += dest[3] > 0 && dest[3] < 255;
            }
        }
        // No fallible work remains; write only each material rectangle, never gaps or pitch padding.
        for (uint32_t i=0; i<count; ++i) for (uint32_t y=0; y<regions[i].height; ++y)
            std::memcpy(pixels+size_t(regions[i].y+y)*pitch+regions[i].x*4,
                output[i].data()+size_t(y)*regions[i].width*4, regions[i].width*4);
        result = prepared; return true;
    } catch (const std::bad_alloc &) { return false; }
}
void RecordTreeContourUpload(bool success, const TreeContourResult &result) {
    if (!TreeContoursRequested()) return;
    if (!success) { failures.fetch_add(1); return; }
    const uint32_t n = uploads.fetch_add(1)+1;
    regionCount.fetch_add(result.regions); changedCount.fetch_add(result.changedPixels);
    partialBefore.store(result.partialBefore); partialAfter.store(result.partialAfter);
    if (n <= 8) std::fprintf(stderr,
        "[Quest tree contours] upload=%u regions=%u changed=%u partial_alpha=%u->%u; isolated premultiplied 3x3\n",
        n, result.regions, result.changedPixels, result.partialBefore, result.partialAfter);
}
std::string TreeContourDiagnosticsJson() {
    char json[384];
    std::snprintf(json, sizeof(json),
        "{\"schema\":1,\"requested\":%s,\"uploads\":%u,\"failures\":%u,\"regions\":%u,"
        "\"changedPixels\":%u,\"lastPartialBefore\":%u,\"lastPartialAfter\":%u}",
        TreeContoursRequested() ? "true" : "false", uploads.load(), failures.load(), regionCount.load(),
        changedCount.load(), partialBefore.load(), partialAfter.load());
    return json;
}
}
#endif
