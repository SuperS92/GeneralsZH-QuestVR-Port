// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace QuestXR {
// GeneralsX @performance Codex 05/10/2026 Independent, default-off presentation experiments.
enum class PerfOption { AudioGrowth, TrackUpload, TerrainLighting, TreeRange, ShadowAppend, SmaaScratch, TerrainBatching, TrackStreaming, HudTransfer, TerrainCulling, Count };
enum class PerfStage { Audio, Tracks, TerrainBlend, Trees, Shadows, TerrainSubmit, TrackPack, TrackLock, TrackCopy, TrackUnlock, TrackSubmit, Count };
bool PerformanceRequested(PerfOption option);
bool PerformanceMetricsRequested();
int PerformanceEye();
class PerformanceFrameScope {
    bool previous;
public:
    PerformanceFrameScope();
    ~PerformanceFrameScope();
    PerformanceFrameScope(const PerformanceFrameScope &) = delete;
    PerformanceFrameScope &operator=(const PerformanceFrameScope &) = delete;
};
class PerformanceEyeScope {
    int previous;
public:
    explicit PerformanceEyeScope(int eye);
    ~PerformanceEyeScope();
    PerformanceEyeScope(const PerformanceEyeScope &) = delete;
    PerformanceEyeScope &operator=(const PerformanceEyeScope &) = delete;
};
class PerformanceStageScope {
    PerfStage stage;
    bool enabled;
    std::chrono::steady_clock::time_point start;
public:
    explicit PerformanceStageScope(PerfStage stage, bool startNow = true);
    void Start();
    void Stop();
    ~PerformanceStageScope();
};
void RecordPerformanceFrameInterval(double milliseconds);
void RecordAudioAssembly(size_t bytes, size_t blocks, size_t growths);
// Compare actual packed vertices, including visibility/order/fade; never a hash-only match.
bool TrackUploadMatches(const void *owner, const void *resource, const void *data, size_t bytes);
void RememberTrackUpload(const void *owner, const void *resource, const void *data, size_t bytes);
void InvalidateTrackUpload();
// Inputs include all scalar lighting/normal/depth inputs; caller excludes scene lights.
bool TerrainLightLookup(const void *owner, const void *map, int x, int y,
    const std::vector<uint8_t> &inputs, int &diffuse);
void RememberTerrainLight(const void *owner, const void *map, int x, int y,
    const std::vector<uint8_t> &inputs, int diffuse);
bool ContinueShadowBuffer(const void *owner, const void *vertices, const void *indices);
void InvalidateShadowBuffer();
void RecordTreeRange(size_t originalBytes, size_t writtenBytes);
void RecordTerrainSubmission(unsigned patches, bool shroud);
void RecordTerrainCull(unsigned patches, bool shroud, bool culled, bool eligible);
void RecordHudTransfer(unsigned width, unsigned fullHeight, unsigned copiedHeight, bool compact);
template<class T> void AppendPerfInput(std::vector<uint8_t> &bytes, const T &value) {
    const auto *p = reinterpret_cast<const uint8_t *>(&value);
    bytes.insert(bytes.end(), p, p + sizeof(value));
}
template<class T> void PrepareAudioAppend(std::vector<T> &data, size_t count, bool geometric) {
    // Keep the inherited allocation path exactly when this experiment is off.
    if (!geometric) data.reserve(data.size() + count);
}
}
#endif
