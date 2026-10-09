// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestTreeWind.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace QuestXR {
namespace {
constexpr size_t maxComponents = 30, maxOwners = 4;
struct Sample {
    const void *owner = nullptr, *atlas = nullptr;
    int breezeVersion = 0;
    size_t components = 0;
    bool ambiguous = false;
    std::array<float, maxComponents> xyz{};
};
thread_local bool active = false;
thread_local int eye = -1;
thread_local std::array<Sample, maxOwners> samples{};
thread_local uint64_t pairs = 0, leftSamples = 0, rightReuses = 0, differentRightInputs = 0, fallbacks = 0;
thread_local uint32_t pairSamples = 0, pairReuses = 0;
thread_local double pairMaxOriginalDelta = 0;
void clearSamples() { samples = {}; }
}
bool TreeWindRequested() {
    const char *value = std::getenv("GX_QUEST_TREE_WIND_PAIR");
    return value && std::strcmp(value, "1") == 0;
}
TreeWindFrameScope::TreeWindFrameScope() : previous(active) {
    if (previous) clearSamples();
    active = TreeWindRequested();
    if (active) clearSamples();
    if (active) { ++pairs; pairSamples = pairReuses = 0; pairMaxOriginalDelta = 0; }
}
TreeWindFrameScope::~TreeWindFrameScope() { if (active) clearSamples(); active = previous; }
TreeWindEyeScope::TreeWindEyeScope(int value) : previous(eye) {
    eye = value == 0 || value == 1 ? value : -1;
}
TreeWindEyeScope::~TreeWindEyeScope() { eye = previous; }
bool ShareTreeWind(const void *owner, const void *atlas, int breezeVersion, float *xyz, size_t components) {
    if (!active || !TreeWindRequested() || (eye != 0 && eye != 1)) return false;
    if (!owner || !atlas || !xyz || !components || components > maxComponents || components % 3) {
        ++fallbacks; return false;
    }
    if (!std::all_of(xyz, xyz + components, [](float value) { return std::isfinite(value); })) {
        // Invalidate an earlier left sample too; never mix invalid/current/previous inputs.
        for (auto &sample : samples) if (sample.owner == owner) sample.ambiguous = true;
        ++fallbacks; return false;
    }
    auto found = std::find_if(samples.begin(), samples.end(), [&](const Sample &sample) { return sample.owner == owner; });
    if (eye == 0) {
        if (found != samples.end()) { found->ambiguous = true; ++fallbacks; return false; }
        found = std::find_if(samples.begin(), samples.end(), [](const Sample &sample) { return !sample.owner; });
        if (found == samples.end()) { ++fallbacks; return false; }
        found->owner = owner; found->atlas = atlas; found->breezeVersion = breezeVersion;
        found->components = components; std::copy_n(xyz, components, found->xyz.begin());
        ++leftSamples; ++pairSamples; return false;
    }
    if (found == samples.end() || found->ambiguous || found->atlas != atlas
        || found->breezeVersion != breezeVersion || found->components != components) {
        ++fallbacks; return false;
    }
    double delta = 0;
    for (size_t i = 0; i < components; ++i) delta = std::max(delta, std::fabs(double(xyz[i]) - double(found->xyz[i])));
    if (delta > 0) ++differentRightInputs;
    pairMaxOriginalDelta = std::max(pairMaxOriginalDelta, delta);
    std::copy_n(found->xyz.begin(), components, xyz);
    ++rightReuses; ++pairReuses;
    if (rightReuses <= 8) std::fprintf(stderr,
        "[Quest tree wind] shared right upload=%llu original_max_delta=%g; updates unchanged\n",
        static_cast<unsigned long long>(rightReuses), double(delta));
    return true;
}
std::string TreeWindDiagnosticsJson() {
    char json[512];
    std::snprintf(json, sizeof(json),
        "{\"schema\":1,\"requested\":%s,\"pairs\":%llu,\"leftSamples\":%llu,"
        "\"rightReuses\":%llu,\"differentRightInputs\":%llu,\"fallbacks\":%llu,"
        "\"lastPairSamples\":%u,\"lastPairReuses\":%u,\"lastPairMaxOriginalDelta\":%.9g}",
        TreeWindRequested() ? "true" : "false", static_cast<unsigned long long>(pairs),
        static_cast<unsigned long long>(leftSamples), static_cast<unsigned long long>(rightReuses),
        static_cast<unsigned long long>(differentRightInputs), static_cast<unsigned long long>(fallbacks),
        pairSamples, pairReuses, double(pairMaxOriginalDelta));
    return json;
}
}
#endif
