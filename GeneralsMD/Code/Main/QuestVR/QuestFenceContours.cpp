// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestFenceContours.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#ifndef QUEST_FENCE_CONTOUR_HOST
#ifdef QUEST_FENCE_CONTOUR_NATIVE_FIXTURE
#include "fence-native-fixture.h"
#else
#include "dx8wrapper.h"
#endif
#endif
namespace QuestXR {
namespace {
std::atomic<uint32_t> uploads{0}, uploadFailures{0}, changedPixels{0}, partialBefore{0}, partialAfter{0};
std::array<std::atomic<uint32_t>, 2> draws{}, drawFailures{}, skips{};
}
bool FenceContoursRequested() {
    const char *s = std::getenv("GX_QUEST_FENCE_CONTOURS");
    return s && std::strcmp(s, "1") == 0;
}
bool IsFenceContourName(const char *name) {
    if (!name) return false;
    const char *base = name;
    for (const char *p = name; *p; ++p) if (*p == '/' || *p == '\\') base = p + 1;
    const size_t n = std::strlen(base);
    if (n != 16) return false;
    char lower[17]{};
    for (size_t i = 0; i < n; ++i) lower[i] = base[i] >= 'A' && base[i] <= 'Z' ? base[i] + ('a'-'A') : base[i];
    return std::strcmp(lower, "atrailings01.tga") == 0 || std::strcmp(lower, "atrailings01.dds") == 0;
}
bool UseFenceContours(const char *name) { return FenceContoursRequested() && IsFenceContourName(name); }
bool PrepareFenceContours(uint8_t *const *pixels, const unsigned *pitches,
    uint32_t width, uint32_t height, uint32_t levels, bool enabled,
    std::vector<std::vector<uint8_t>> &output, FenceContourResult &result) {
    result = {}; output.clear();
    // Only the audited square railing resource, including its inherited reduction.
    // Rows contain separate packed patterns: never mix vertical neighbors or invent new levels.
    if (!enabled || !pixels || !pitches || width != height || (width != 64 && width != 128)
        || levels < 2 || levels > 8 || (width >> (levels-1)) < 4) return false;
    std::array<uintptr_t, 8> begins{}, ends{};
    for (uint32_t l = 0; l < levels; ++l) {
        const uint32_t w = width >> l;
        if (!pixels[l] || pitches[l] < w*4 || pitches[l] > 65536) return false;
        begins[l] = reinterpret_cast<uintptr_t>(pixels[l]);
        const size_t span = size_t(w-1)*pitches[l] + w*4;
        if (begins[l] > UINTPTR_MAX-span) return false;
        ends[l] = begins[l]+span;
        for (uint32_t p = 0; p < l; ++p)
            if (begins[l] < ends[p] && begins[p] < ends[l]) return false;
    }
    try {
        std::vector<std::vector<uint8_t>> prepared(levels);
        FenceContourResult stats{}; stats.levels = levels;
        for (uint32_t l = 0; l < levels; ++l) {
            const uint32_t w = width >> l;
            prepared[l].resize(size_t(w)*w*4);
            for (uint32_t y = 0; y < w; ++y) for (uint32_t x = 0; x < w; ++x) {
                const auto *original = pixels[l]+size_t(y)*pitches[l]+x*4;
                auto *dest = prepared[l].data()+(size_t(y)*w+x)*4;
                uint32_t a = 0, colors[3]{};
                for (int dx = -1; dx <= 1; ++dx) {
                    const uint32_t sx = (x+w+dx)%w, weight = dx == 0 ? 2 : 1;
                    const auto *sample = pixels[l]+size_t(y)*pitches[l]+sx*4;
                    const uint32_t wa = sample[3]*weight; a += wa;
                    for (int c = 0; c < 3; ++c) colors[c] += sample[c]*wa;
                }
                dest[3] = uint8_t((a+2)/4);
                for (int c = 0; c < 3; ++c) dest[c] = a ? uint8_t((colors[c]+a/2)/a) : original[c];
                stats.changedPixels += std::memcmp(original, dest, 4) != 0;
                stats.partialBefore += original[3] > 0 && original[3] < 255;
                stats.partialAfter += dest[3] > 0 && dest[3] < 255;
            }
        }
        output.swap(prepared); result = stats; return true; // Caller commits only after native marker allocation succeeds.
    } catch (const std::bad_alloc &) { return false; }
}
void RecordFenceContourDraw(int eye, bool success, bool skipped) {
    if (!FenceContoursRequested() || eye < 0 || eye > 1) return;
    if (skipped) {
        const uint32_t n = ++skips[eye];
        if (n <= 4) std::fprintf(stderr,"[Quest fence contours] eye=%d skipped=%u; original draw retained\n",eye,n);
        return;
    }
    const uint32_t n = success ? ++draws[eye] : ++drawFailures[eye];
    if (n <= 4) std::fprintf(stderr, "[Quest fence contours] eye=%d %s=%u; core/fringe only for tagged railing\n",
        eye, success ? "draw" : "failure", n);
}
std::string FenceContourDiagnosticsJson() {
    char json[512];
    std::snprintf(json, sizeof(json),
        "{\"schema\":1,\"requested\":%s,\"uploads\":%u,\"uploadFailures\":%u,\"changedPixels\":%u,"
        "\"lastPartialBefore\":%u,\"lastPartialAfter\":%u,\"eyeDraws\":[%u,%u],\"eyeFailures\":[%u,%u],\"eyeSkips\":[%u,%u]}",
        FenceContoursRequested() ? "true" : "false", uploads.load(), uploadFailures.load(), changedPixels.load(),
        partialBefore.load(), partialAfter.load(), draws[0].load(), draws[1].load(),
        drawFailures[0].load(), drawFailures[1].load(), skips[0].load(), skips[1].load());
    return json;
}
#ifndef QUEST_FENCE_CONTOUR_HOST
namespace {
// Resource-owned bytes prevent stale pointer identities and do not extend resource lifetime.
constexpr GUID fenceTag = {0x6b6ca19e,0xd450,0x41ec,{0x9e,0x3c,0x42,0x1f,0xf0,0x57,0xa2,0xa9}};
struct Tag { uint32_t schema, stage, width, levels; FenceContourResult result; };
bool readTag(IDirect3DBaseTexture8 *texture, Tag &tag) {
    DWORD bytes = sizeof(tag);
    return texture && SUCCEEDED(texture->GetPrivateData(fenceTag, &tag, &bytes))
        && bytes == sizeof(tag) && tag.schema == 1;
}
}
void ApplyLoadedFenceContours(IDirect3DTexture8 *texture, uint8_t *const *pixels,
    const unsigned *pitches, uint32_t width, uint32_t height, uint32_t levels, const char *name) {
    if (!UseFenceContours(name) || !texture) return;
    texture->FreePrivateData(fenceTag); // A reload cannot inherit an earlier success marker.
    D3DSURFACE_DESC desc{};
    std::vector<std::vector<uint8_t>> output; FenceContourResult result;
    if (FAILED(texture->GetLevelDesc(0, &desc)) || desc.Format != D3DFMT_A8R8G8B8
        || desc.Pool != D3DPOOL_MANAGED || desc.Width != width || desc.Height != height
        || texture->GetLevelCount() != levels
        || !PrepareFenceContours(pixels,pitches,width,height,levels,true,output,result)) {
        ++uploadFailures; return;
    }
    const Tag tag{1,1,width,levels,result};
    if (FAILED(texture->SetPrivateData(fenceTag,&tag,sizeof(tag),0))) { ++uploadFailures; return; }
    // All validation/allocation succeeded. Source assets and row padding remain untouched.
    for (uint32_t l = 0; l < levels; ++l) {
        const uint32_t w = width >> l;
        for (uint32_t y = 0; y < w; ++y)
            std::memcpy(pixels[l]+size_t(y)*pitches[l],output[l].data()+size_t(y)*w*4,w*4);
    }
}
void CompleteFenceContourUpload(IDirect3DTexture8 *texture, bool unlocked) {
    if (!FenceContoursRequested()) return;
    Tag tag{};
    if (!readTag(texture,tag) || tag.stage != 1) return;
    tag.stage = 2;
    if (!unlocked || FAILED(texture->SetPrivateData(fenceTag,&tag,sizeof(tag),0))) {
        texture->FreePrivateData(fenceTag); ++uploadFailures; return;
    }
    const uint32_t n = ++uploads; changedPixels += tag.result.changedPixels;
    partialBefore = tag.result.partialBefore; partialAfter = tag.result.partialAfter;
    if (n <= 8) std::fprintf(stderr,
        "[Quest fence contours] upload=%u base=%ux%u levels=%u changed=%u partial_alpha=%u->%u; periodic row filter, all levels unlocked\n",
        n,tag.width,tag.width,tag.levels,tag.result.changedPixels,tag.result.partialBefore,tag.result.partialAfter);
}
bool IsFilteredFenceResource(IDirect3DBaseTexture8 *texture) {
    if (!FenceContoursRequested()) return false;
    Tag tag{};
    return readTag(texture,tag) && tag.stage == 2 && (tag.width == 64 || tag.width == 128)
        && tag.levels >= 2 && tag.levels <= 8;
}
#endif
}
#endif
