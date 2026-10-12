// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestPerformance.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <android/log.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <map>
#include <utility>
namespace QuestXR {
namespace {
constexpr const char *optionNames[] = {"GX_QUEST_PERF_AUDIO", "GX_QUEST_PERF_TRACKS",
    "GX_QUEST_PERF_TERRAIN", "GX_QUEST_PERF_TREES", "GX_QUEST_PERF_SHADOWS", "GX_QUEST_PERF_SMAA", "GX_QUEST_TERRAIN_BATCHING", "GX_QUEST_TRACK_STREAMING", "GX_QUEST_COLOR_TRANSFER"};
constexpr const char *stageNames[] = {"audio", "tracks", "terrain_blend", "trees", "shadows", "terrain_submit",
    "track_pack", "track_lock", "track_copy", "track_unlock", "track_submit"};
static_assert(sizeof(optionNames) / sizeof(optionNames[0]) == size_t(PerfOption::Count), "Option names must match");
static_assert(sizeof(stageNames) / sizeof(stageNames[0]) == size_t(PerfStage::Count), "Stage names must match");
bool frameActive = false;
int worldEye = -1;
struct Upload {
    const void *owner = nullptr, *resource = nullptr;
    std::vector<uint8_t> bytes;
} track;
struct LightValue { std::vector<uint8_t> inputs; int diffuse = 0; uint64_t generation = 0; };
const void *lightOwner = nullptr, *lightMap = nullptr;
std::map<std::pair<int, int>, LightValue> lighting;
uint64_t lightGeneration = 0;
const void *shadowOwner = nullptr, *shadowVertices = nullptr, *shadowIndices = nullptr;
int shadowEye = -1;
std::array<double, size_t(PerfStage::Count)> stageMs{};
std::array<uint64_t, size_t(PerfStage::Count)> stageCalls{};
std::array<double, 120> intervals{};
uint32_t frames = 0, audioReports = 0;
uint64_t trackHits = 0, trackUploads = 0, trackSavedBytes = 0;
uint64_t lightHits = 0, lightMisses = 0, shadowAppends = 0, shadowResets = 0;
uint64_t treeOriginalBytes = 0, treeWrittenBytes = 0;
uint64_t terrainDraws[2][2]{}, terrainPatches[2][2]{};
bool strictSwitch(const char *name) {
    const char *s = std::getenv(name);
    return s && std::strcmp(s, "1") == 0;
}
bool inWorld() { return frameActive && (worldEye == 0 || worldEye == 1); }
void clearFrameCaches() {
    track.owner = track.resource = nullptr; track.bytes.clear();
    // Retain bounded allocations, but never reuse a color from an earlier pair.
    if (++lightGeneration == 0) { lighting.clear(); ++lightGeneration; }
    InvalidateShadowBuffer();
}
}
bool PerformanceRequested(PerfOption option) {
    const size_t i = size_t(option);
    return i < size_t(PerfOption::Count) && strictSwitch(optionNames[i]);
}
bool PerformanceMetricsRequested() { return strictSwitch("GX_QUEST_PERF_METRICS"); }
int PerformanceEye() { return inWorld() ? worldEye : -1; }
PerformanceFrameScope::PerformanceFrameScope() : previous(frameActive) {
    clearFrameCaches(); frameActive = true;
    static bool logged=false;
    if (!logged) {
        logged=true;
        __android_log_print(ANDROID_LOG_INFO, "GeneralsXR",
            "Quest perf choices: audio=%d tracks=%d terrain=%d trees=%d shadows=%d smaa_scratch=%d metrics=%d track_streaming=%d (full restart)",
            PerformanceRequested(PerfOption::AudioGrowth), PerformanceRequested(PerfOption::TrackUpload),
            PerformanceRequested(PerfOption::TerrainLighting), PerformanceRequested(PerfOption::TreeRange),
            PerformanceRequested(PerfOption::ShadowAppend), PerformanceRequested(PerfOption::SmaaScratch), PerformanceMetricsRequested(), PerformanceRequested(PerfOption::TrackStreaming));
    }
}
PerformanceFrameScope::~PerformanceFrameScope() { clearFrameCaches(); frameActive = previous; }
PerformanceEyeScope::PerformanceEyeScope(int eye) : previous(worldEye) { worldEye = eye; }
PerformanceEyeScope::~PerformanceEyeScope() { worldEye = previous; }
// GeneralsX @performance Codex 12/10/2026 Split CPU buffer waits from packing and submission.
PerformanceStageScope::PerformanceStageScope(PerfStage value, bool startNow)
    : stage(value), enabled(false) {
    if (startNow) Start();
}
void PerformanceStageScope::Start() {
    if (!enabled && PerformanceMetricsRequested()) {
        start = std::chrono::steady_clock::now();
        enabled = true;
    }
}
void PerformanceStageScope::Stop() {
    if (enabled) {
        stageMs[size_t(stage)] += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        ++stageCalls[size_t(stage)];
        enabled = false;
    }
}
PerformanceStageScope::~PerformanceStageScope() { Stop(); }
void RecordPerformanceFrameInterval(double milliseconds) {
    if (!PerformanceMetricsRequested() || !(milliseconds > 0) || !std::isfinite(milliseconds)) return;
    intervals[frames++] = milliseconds;
    if (frames != intervals.size()) return;
    auto sorted = intervals; std::sort(sorted.begin(), sorted.end());
    __android_log_print(ANDROID_LOG_INFO, "GeneralsXR",
        "Quest perf interval: n=120 p50=%.3f p95=%.3f p99=%.3f max=%.3f ms (wall; includes pacing)",
        sorted[59], sorted[113], sorted[118], sorted[119]);
    for (size_t i = 0; i < stageMs.size(); ++i) {
        __android_log_print(ANDROID_LOG_INFO, "GeneralsXR", "Quest perf stage: %s %.3f ms/frame calls=%llu",
            stageNames[i], stageMs[i] / 120, static_cast<unsigned long long>(stageCalls[i]));
    }
    __android_log_print(ANDROID_LOG_INFO, "GeneralsXR",
        "Quest perf reuse: tracks hits=%llu uploads=%llu saved_bytes=%llu terrain hits=%llu misses=%llu shadows append=%llu reset=%llu tree_bytes=%llu/%llu",
        (unsigned long long)trackHits, (unsigned long long)trackUploads, (unsigned long long)trackSavedBytes,
        (unsigned long long)lightHits, (unsigned long long)lightMisses, (unsigned long long)shadowAppends,
        (unsigned long long)shadowResets, (unsigned long long)treeWrittenBytes, (unsigned long long)treeOriginalBytes);
    for (int eye = 0; eye < 2; ++eye) for (int pass = 0; pass < 2; ++pass)
        __android_log_print(ANDROID_LOG_INFO, "GeneralsXR",
            "Quest terrain submit: batching=%d eye=%d shroud=%d draws=%llu patches=%llu (120 frames)",
            PerformanceRequested(PerfOption::TerrainBatching), eye, pass,
            (unsigned long long)terrainDraws[eye][pass], (unsigned long long)terrainPatches[eye][pass]);
    for (int eye = 0; eye < 2; ++eye) for (int pass = 0; pass < 2; ++pass)
        terrainDraws[eye][pass] = terrainPatches[eye][pass] = 0;
    frames = 0; stageMs = {}; stageCalls = {};
    trackHits = trackUploads = trackSavedBytes = lightHits = lightMisses = 0;
    shadowAppends = shadowResets = treeOriginalBytes = treeWrittenBytes = 0;
}
void RecordAudioAssembly(size_t bytes, size_t blocks, size_t growths) {
    if (PerformanceMetricsRequested() && audioReports++ < 12)
        __android_log_print(ANDROID_LOG_INFO, "GeneralsXR", "Quest perf audio: geometric=%d bytes=%zu blocks=%zu growth_blocks=%zu",
            PerformanceRequested(PerfOption::AudioGrowth), bytes, blocks, growths);
}
bool TrackUploadMatches(const void *owner, const void *resource, const void *data, size_t bytes) {
    const bool hit = inWorld() && worldEye == 1 && owner && resource && data && bytes
        && owner == track.owner && resource == track.resource && bytes == track.bytes.size()
        && std::memcmp(data, track.bytes.data(), bytes) == 0;
    if (hit && PerformanceMetricsRequested()) { ++trackHits; trackSavedBytes += bytes; }
    return hit;
}
void RememberTrackUpload(const void *owner, const void *resource, const void *data, size_t bytes) {
    if (!inWorld()) return;
    if (PerformanceMetricsRequested()) ++trackUploads;
    track.owner = owner; track.resource = resource;
    const auto *p = static_cast<const uint8_t *>(data);
    track.bytes.assign(p, p + bytes);
}
void InvalidateTrackUpload() { track.owner = track.resource = nullptr; track.bytes.clear(); }
bool TerrainLightLookup(const void *owner, const void *map, int x, int y,
    const std::vector<uint8_t> &inputs, int &diffuse) {
    if (!inWorld() || !owner || !map) return false;
    if (owner != lightOwner || map != lightMap) {
        lighting.clear(); lightOwner = owner; lightMap = map;
    }
    const auto it = lighting.find({x, y});
    if (it != lighting.end() && it->second.generation == lightGeneration && it->second.inputs == inputs) {
        diffuse = it->second.diffuse;
        if (PerformanceMetricsRequested()) ++lightHits;
        return true;
    }
    if (PerformanceMetricsRequested()) ++lightMisses;
    return false;
}
void RememberTerrainLight(const void *owner, const void *map, int x, int y,
    const std::vector<uint8_t> &inputs, int diffuse) {
    if (inWorld() && owner == lightOwner && map == lightMap) {
        auto it = lighting.find({x, y});
        if (it == lighting.end()) {
            if (lighting.size() >= 40000) return;
            it = lighting.emplace(std::make_pair(x, y), LightValue{}).first;
        }
        it->second.inputs = inputs; it->second.diffuse = diffuse;
        it->second.generation = lightGeneration;
    }
}
bool ContinueShadowBuffer(const void *owner, const void *vertices, const void *indices) {
    const int eye = PerformanceEye();
    const bool append = eye == 1 && shadowEye == 0 && owner && vertices && indices
        && shadowOwner == owner && shadowVertices == vertices && shadowIndices == indices;
    shadowOwner = owner; shadowVertices = vertices; shadowIndices = indices; shadowEye = eye;
    if (PerformanceMetricsRequested()) { if (append) ++shadowAppends; else ++shadowResets; }
    return append;
}
void InvalidateShadowBuffer() {
    shadowOwner = shadowVertices = shadowIndices = nullptr; shadowEye = -1;
}
void RecordTerrainSubmission(unsigned patches, bool shroud) {
    const int eye = PerformanceEye();
    if (!PerformanceMetricsRequested() || eye < 0 || !patches) return;
    ++terrainDraws[eye][shroud ? 1 : 0];
    terrainPatches[eye][shroud ? 1 : 0] += patches;
}
void RecordTreeRange(size_t originalBytes, size_t writtenBytes) {
    if (PerformanceMetricsRequested()) { treeOriginalBytes += originalBytes; treeWrittenBytes += writtenBytes; }
}
}
#endif
