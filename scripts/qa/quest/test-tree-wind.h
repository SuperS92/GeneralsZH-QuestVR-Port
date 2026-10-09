// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestTreeWind.h"
#include <array>
#include <cmath>
#include <limits>
static void checkTreeWind() {
    using namespace QuestXR;
    const void *owner = reinterpret_cast<const void *>(1), *atlas = reinterpret_cast<const void *>(2);
    const std::array<float, 3> left{1, 2, 3}, right{4, 5, 6};
    _putenv_s("GX_QUEST_TREE_WIND_PAIR", "0");
    const auto disabled = TreeWindDiagnosticsJson();
    {
        TreeWindFrameScope frame; TreeWindEyeScope eye(0);
        auto values = left;
        require(!ShareTreeWind(owner, atlas, 7, values.data(), values.size()) && values == left, "wind Off inputs unchanged");
    }
    require(disabled == TreeWindDiagnosticsJson(), "wind Off diagnostics inert");
    _putenv_s("GX_QUEST_TREE_WIND_PAIR", "1");
    auto values = right;
    require(!ShareTreeWind(owner, atlas, 7, values.data(), 3) && values == right, "wind outside frame inert");
    {
        TreeWindFrameScope frame;
        values = right;
        { TreeWindEyeScope eye(1); require(!ShareTreeWind(owner, atlas, 7, values.data(), 3), "right-first fallback"); }
        { TreeWindEyeScope eye(0); values = left; require(!ShareTreeWind(owner, atlas, 7, values.data(), 3) && values == left, "left capture unchanged"); }
        { TreeWindEyeScope eye(-1); values = right; require(!ShareTreeWind(owner, atlas, 7, values.data(), 3) && values == right, "menu/pointer excluded"); }
        {
            TreeWindEyeScope eye(1); values = right;
            require(!ShareTreeWind(owner, reinterpret_cast<const void *>(3), 7, values.data(), 3) && values == right, "atlas identity required");
            require(!ShareTreeWind(owner, atlas, 8, values.data(), 3) && values == right, "breeze identity required");
            require(!ShareTreeWind(reinterpret_cast<const void *>(4), atlas, 7, values.data(), 3) && values == right, "owner identity required");
            require(ShareTreeWind(owner, atlas, 7, values.data(), 3) && values == left, "right exactly reuses left bits");
            { TreeWindEyeScope nested(-1); values = right; require(!ShareTreeWind(owner, atlas, 7, values.data(), 3), "nested non-world inert"); }
            require(ShareTreeWind(owner, atlas, 7, values.data(), 3) && values == left, "eye restored after nested scope");
        }
    }
    {
        TreeWindFrameScope frame; TreeWindEyeScope eye(1); values = right;
        require(!ShareTreeWind(owner, atlas, 7, values.data(), 3) && values == right, "no stale prior-pair sample");
    }
    {
        TreeWindFrameScope frame;
        { TreeWindEyeScope eye(0); values = left; ShareTreeWind(owner, atlas, 7, values.data(), 3); values = right; ShareTreeWind(owner, atlas, 7, values.data(), 3); }
        { TreeWindEyeScope eye(1); values = right; require(!ShareTreeWind(owner, atlas, 7, values.data(), 3) && values == right, "ambiguous multiple left uploads excluded"); }
    }
    {
        TreeWindFrameScope frame; TreeWindEyeScope eye(0);
        std::array<float, 33> oversized{};
        require(!ShareTreeWind(owner, atlas, 7, oversized.data(), oversized.size()), "bounded component capacity");
        require(!ShareTreeWind(owner, atlas, 7, values.data(), 2), "complete XYZ triples only");
        require(!ShareTreeWind(nullptr, atlas, 7, values.data(), 3), "null owner fallback");
        require(!ShareTreeWind(owner, nullptr, 7, values.data(), 3), "null atlas fallback");
        require(!ShareTreeWind(owner, atlas, 7, nullptr, 3), "null input fallback");
        values = left; ShareTreeWind(owner, atlas, 7, values.data(), 3);
        values[1] = std::numeric_limits<float>::quiet_NaN();
        require(!ShareTreeWind(owner, atlas, 7, values.data(), 3) && std::isnan(values[1]), "nonfinite unchanged and invalidates earlier sample");
        { TreeWindEyeScope rightEye(1); values = right; require(!ShareTreeWind(owner, atlas, 7, values.data(), 3), "invalidated left sample not reused"); }
    }
    {
        TreeWindFrameScope frame; TreeWindEyeScope eye(0); values = left;
        for (uintptr_t n=1; n<=4; ++n) ShareTreeWind(reinterpret_cast<const void *>(n), atlas, 7, values.data(), 3);
        require(!ShareTreeWind(reinterpret_cast<const void *>(5), atlas, 7, values.data(), 3), "bounded owner count");
        TreeWindEyeScope rightEye(1); values = right;
        require(ShareTreeWind(owner, atlas, 7, values.data(), 3) && values == left, "capacity rejection keeps valid owner");
    }
    {
        // Independent integration model: both inherited state steps still execute;
        // only the right shader input becomes the left input. No simulation clock edit.
        TreeWindFrameScope frame;
        float state = 0, push = 1, sink = 10; int updates = 0;
        std::array<float, 30> saved{};
        for (int n=0; n<2; ++n) {
            state += 0.25f; push += 0.125f; sink -= 0.5f; ++updates;
            std::array<float, 30> inputs{};
            for (size_t i=0; i<inputs.size(); ++i) inputs[i] = state + float(i);
            if (!n) saved = inputs;
            TreeWindEyeScope eye(n);
            const bool reused = ShareTreeWind(owner, atlas, 9, inputs.data(), inputs.size());
            require(reused == (n==1) && inputs == saved, "whole thirty-component shader input equality");
        }
        require(state == 0.5f && push == 1.25f && sink == 9 && updates == 2, "independent inherited updates remain twice executed");
    }
    {
        TreeWindFrameScope frame;
        { TreeWindEyeScope eye(0); values.fill(std::numeric_limits<float>::max()); ShareTreeWind(owner, atlas, 7, values.data(), 3); }
        { TreeWindEyeScope eye(1); values.fill(-std::numeric_limits<float>::max()); require(ShareTreeWind(owner, atlas, 7, values.data(), 3), "finite extreme delta supported"); }
        const auto json = TreeWindDiagnosticsJson();
        require(json.find("inf") == std::string::npos && json.find("nan") == std::string::npos, "finite JSON diagnostic delta");
    }
    // Leave an ordinary successful pair as the snapshot for JSON round-trip checks.
    { TreeWindFrameScope frame;
      { TreeWindEyeScope eye(0); values = left; ShareTreeWind(owner, atlas, 7, values.data(), 3); }
      { TreeWindEyeScope eye(1); values = right; require(ShareTreeWind(owner, atlas, 7, values.data(), 3), "diagnostic fixture active"); }
    }
}
