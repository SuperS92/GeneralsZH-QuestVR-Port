// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "VRDevice/QuestSoftCutouts.h"
#include <stdexcept>
struct CutoutFixture final : QuestXR::CutoutBackend {
    using State = QuestXR::CutoutState;
    // First nine fields match captured tree-like state; stencil/blend-op/color mask are host fixture inputs.
    std::array<uint32_t, 12> state = {1,96,7,0,2,1,1,1,4,0,1,15};
    std::vector<std::array<uint32_t, 12>> submissions;
    uint32_t reads = 0, writes = 0, draws = 0;
    uint32_t failRead = 0, failWrite = 0, failDraw = 0;
    bool failRestore = false;
    bool read(State s, uint32_t &value) override {
        if (++reads == failRead) return false;
        value = state[static_cast<size_t>(s)]; return true;
    }
    bool write(State s, uint32_t value) override {
        if (++writes == failWrite || (failRestore && draws == 2 && s == State::AlphaRef)) return false;
        state[static_cast<size_t>(s)] = value; return true;
    }
    bool draw() override {
        ++draws;
        if (draws == failDraw) return false;
        submissions.push_back(state); return true;
    }
};
static void checkSoftCutouts() {
    using namespace QuestXR;
    CutoutFixture off;
    const auto original = off.state;
    require(DrawSoftCutoutPasses(off, false) == CutoutResult::Skipped
        && !off.reads && !off.writes && !off.draws && off.state == original, "soft cutouts off is inert");
    for (uint32_t failed = 1; failed <= 12; ++failed) {
        CutoutFixture getter; getter.failRead = failed;
        require(DrawSoftCutoutPasses(getter, true) == CutoutResult::Skipped
            && !getter.writes && !getter.draws && getter.state == original, "failed getter never mutates");
    }
    for (size_t field : {0u,1u,2u,3u,6u,7u,8u,9u,10u,11u}) {
        CutoutFixture excluded; excluded.state[field] ^= 1;
        const auto before = excluded.state;
        require(DrawSoftCutoutPasses(excluded, true) == CutoutResult::Skipped
            && !excluded.writes && !excluded.draws && excluded.state == before, "unrelated state contract excluded");
    }
    for (uint32_t colorMask : {7u,15u}) for (bool railing : {false, true}) {
        CutoutFixture valid;
        valid.state[11] = colorMask;
        if (railing) { valid.state[4] = 5; valid.state[5] = 6; } // Captured atrailings01 inactive factors.
        const auto before = valid.state;
        require(DrawSoftCutoutPasses(valid, true) == CutoutResult::Drawn
            && valid.submissions.size() == 2 && valid.state == before, "two passes restore exact original including inactive blend factors");
        const auto &core = valid.submissions[0], &edge = valid.submissions[1];
        require(core[1] == 254 && core[2] == 7 && !core[3] && core[7] == 1,
            "core opaque and writes depth");
        require(edge[1] == 254 && edge[2] == 2 && edge[3] == 1 && edge[4] == 5
            && edge[5] == 6 && !edge[7], "fringe blend and depth writes disabled");
        for (size_t field : {0u,6u,8u,9u,10u,11u})
            require(core[field] == before[field] && edge[field] == before[field], "test/stencil/color state untouched");
        // Independent blend/depth model across every possible final fragment alpha.
        // It checks full opacity at the core, continuous fractional edges, no depth
        // occlusion by transparent pixels and at most 2/255 discontinuity at the join.
        double previous = 0;
        for (uint32_t alpha = 0; alpha < 256; ++alpha) {
            double pixel = 0; bool depthWritten = false;
            if (alpha >= core[1]) { pixel = 1; depthWritten = core[7] != 0; }
            if (alpha < edge[1]) { pixel = double(alpha) / 255; depthWritten = edge[7] != 0; }
            const double expected = alpha >= 254 ? 1.0 : double(alpha) / 255;
            require(pixel == expected && pixel >= previous && pixel - previous <= 2.01/255,
                "fractional edges continuous instead of binary threshold at 96");
            require(depthWritten == (alpha >= 254), "fringe cannot obscure later geometry through depth writes");
            previous = pixel;
        }
    }
    // Every other D3D8 channel mask is ineligible. RGB-only must never enable alpha writes.
    for (uint32_t mask = 0; mask < 16; ++mask) {
        if (mask == 7 || mask == 15) continue;
        CutoutFixture masked; masked.state[11] = mask; const auto before = masked.state;
        require(DrawSoftCutoutPasses(masked, true) == CutoutResult::Skipped
            && !masked.writes && !masked.draws && masked.state == before, "partial/alpha-only masks excluded");
    }
    const auto diagnosticBeforeOff = SoftCutoutDiagnosticsJson();
    CutoutFixture inert;
    require(DrawSoftCutoutPasses(inert, false) == CutoutResult::Skipped
        && SoftCutoutDiagnosticsJson() == diagnosticBeforeOff, "off leaves diagnostic counters unchanged");
    require(SoftCutoutDiagnosticsJson().find("\"contractSkips\":0") == std::string::npos,
        "rejections leave bounded diagnostic evidence");
    for (uint32_t failed = 1; failed <= 7; ++failed) {
        CutoutFixture setter; setter.failWrite = failed;
        const auto result = DrawSoftCutoutPasses(setter, true);
        require(result == (failed == 1 ? CutoutResult::Skipped : CutoutResult::Failed)
            && setter.state == original && setter.draws == (failed == 1 ? 0u : 1u), "failed setup or fringe setter restores all state");
    }
    for (uint32_t failed : {1u,2u}) {
        CutoutFixture draw; draw.failDraw = failed;
        require(DrawSoftCutoutPasses(draw, true) == CutoutResult::Failed && draw.state == original
            && draw.draws == failed, "failed native draw restores state without repeating submission");
    }
    CutoutFixture restore; restore.failRestore = true; bool threw = false;
    try { DrawSoftCutoutPasses(restore, true); } catch (const std::runtime_error &) { threw = true; }
    require(threw && restore.writes == 13 && restore.state[2] == 7 && restore.state[3] == 0
        && restore.state[4] == 2 && restore.state[5] == 1 && restore.state[7] == 1,
        "restore failure attempts every restore and throws instead of leaking silently");
    require(!SoftCutoutDrawInScope() && !SoftCutoutTreeInScope(), "UI outside world scope");
    for (int eye : {0,1}) {
        SoftCutoutWorldScope world(eye);
        require(SoftCutoutDrawInScope() && !SoftCutoutTreeInScope(), "both eye worlds eligible");
        {
            SoftCutoutTreeScope tree;
            require(SoftCutoutTreeInScope(), "procedural atlas tree scope");
            {
                SoftCutoutTreeScope nested;
                SoftCutoutWorldScope excluded(-1);
                require(!SoftCutoutDrawInScope(), "invalid world scope excluded");
            }
            require(SoftCutoutDrawInScope() && SoftCutoutTreeInScope(), "nested scopes restored");
        }
        require(!SoftCutoutTreeInScope(), "tree scope cannot leak into other geometry");
    }
    require(!SoftCutoutDrawInScope(), "world scope cannot leak into pointer/HUD");
    std::puts("PASS: soft cutout two-pass blend/depth, strict eligibility, off, both eyes, nested scopes, getters/setters/draw failures and exact restore");
}
