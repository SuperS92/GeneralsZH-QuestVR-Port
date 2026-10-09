// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <cstddef>
#include <cstdint>
#include <string>
namespace QuestXR {
// GeneralsX @feature Codex 07/10/2026 Isolate eye-phase comparison to shader inputs, not tree updates.
bool TreeWindRequested();
class TreeWindFrameScope {
    bool previous;
public:
    TreeWindFrameScope();
    ~TreeWindFrameScope();
    TreeWindFrameScope(const TreeWindFrameScope &) = delete;
    TreeWindFrameScope &operator=(const TreeWindFrameScope &) = delete;
};
class TreeWindEyeScope {
    int previous;
public:
    explicit TreeWindEyeScope(int eye);
    ~TreeWindEyeScope();
    TreeWindEyeScope(const TreeWindEyeScope &) = delete;
    TreeWindEyeScope &operator=(const TreeWindEyeScope &) = delete;
};
// First eligible left-eye upload is retained only for this pair/owner/atlas/breeze.
// Every inherited phase/RNG/topple/push update must still execute before this call.
// Changes only caller's packed shader XYZ inputs when a matching right upload exists.
bool ShareTreeWind(const void *owner, const void *atlas, int breezeVersion,
    float *xyz, size_t components);
std::string TreeWindDiagnosticsJson();
}
#endif
