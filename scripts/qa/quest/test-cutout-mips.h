// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "VRDevice/QuestCutoutMips.h"
static void checkCutoutMips() {
    using namespace QuestXR;
    require(IsCutoutTextureName("Data/Textures/ATRAILINGS01.TGA"), "cutout case/path");
    require(IsCutoutTextureName("pmbush01.dds") && IsCutoutTextureName("ptpine01.tga"), "vegetation families");
    for (const char *name : {"", "treeatlas", "mainatlas", "trgravelroads.tga", "button.tga", "pmbush01.exe", "atrailings.tga"})
        require(!IsCutoutTextureName(name), "no arbitrary UI/atlas/road/extension override");
    require(!IsCutoutTextureName(nullptr), "null name");
    std::array<std::vector<uint8_t>, 5> buffers;
    std::array<uint8_t*, 5> pointers{}; std::array<unsigned, 5> pitches{};
    uint32_t w = 16;
    for (uint32_t level = 0; level < 5; ++level) {
        pitches[level] = w * 4 + 7; buffers[level].assign(pitches[level] * w + 2, 0xcd);
        pointers[level] = buffers[level].data() + 1; // deliberately unaligned and padded
        w /= 2;
    }
    auto untouched = buffers;
    require(!RebuildCutoutAlpha(pointers.data(), pitches.data(), 16, 16, 5, false)
        && buffers == untouched, "off exact");
    auto badPitches = pitches; badPitches[2] = 1;
    require(!RebuildCutoutAlpha(pointers.data(), badPitches.data(), 16, 16, 5, true)
        && buffers == untouched, "invalid late pitch no partial mutation");
    auto overlapping = pointers; overlapping[2] = pointers[0];
    require(!RebuildCutoutAlpha(overlapping.data(), pitches.data(), 16, 16, 5, true)
        && buffers == untouched, "overlap no base mutation");
    require(!RebuildCutoutAlpha(pointers.data(), pitches.data(), 15, 16, 5, true)
        && !RebuildCutoutAlpha(pointers.data(), pitches.data(), 1, 1, 2, true)
        && !RebuildCutoutAlpha(pointers.data(), pitches.data(), 4096, 16, 5, true), "extent/chain bounded");
    for (uint32_t y = 0; y < 16; ++y) for (uint32_t x = 0; x < 16; ++x)
        pointers[0][y * pitches[0] + x * 4 + 3] = x < 8 ? 255 : 0;
    const auto original = buffers;
    require(RebuildCutoutAlpha(pointers.data(), pitches.data(), 16, 16, 5, true), "cutout chain accepted");
    require(buffers[0] == original[0], "base exact including alpha/padding");
    w = 8;
    for (uint32_t level = 1; level < 5; ++level) {
        uint32_t covered = 0;
        for (uint32_t y = 0; y < w; ++y) for (uint32_t x = 0; x < w; ++x) {
            const size_t offset = 1 + y * pitches[level] + x * 4;
            require(std::equal(buffers[level].begin()+offset, buffers[level].begin()+offset+3,
                original[level].begin()+offset), "reduced RGB exact");
            covered += pointers[level][y*pitches[level]+x*4+3] >= 96;
        }
        if (w > 1) require(covered == w*w/2, "representable half-coverage exact");
        for (size_t i = 0; i < buffers[level].size(); ++i) {
            const size_t row = i ? (i-1)/pitches[level] : w;
            const size_t col = i ? (i-1)%pitches[level] : 0;
            if (row >= w || col >= w*4 || col%4 != 3)
                require(buffers[level][i] == original[level][i], "RGB/guards/padding unchanged");
        }
        w /= 2;
    }
    for (uint8_t value : {uint8_t(0), uint8_t(255)}) {
        for (uint32_t y = 0; y < 16; ++y) for (uint32_t x = 0; x < 16; ++x)
            pointers[0][y*pitches[0]+x*4+3] = value;
        require(RebuildCutoutAlpha(pointers.data(), pitches.data(), 16, 16, 5, true), "alpha endpoint chain");
        w = 8;
        for (uint32_t level = 1; level < 5; ++level) {
            for (uint32_t y = 0; y < w; ++y) for (uint32_t x = 0; x < w; ++x)
                require(pointers[level][y*pitches[level]+x*4+3] == value, "transparent/opaque endpoints preserved");
            w /= 2;
        }
    }
    // Uniform filtered alpha cannot represent fractional binary coverage: avoid promising exactness.
    for (uint32_t y = 0; y < 16; ++y) for (uint32_t x = 0; x < 16; ++x)
        pointers[0][y*pitches[0]+x*4+3] = x%2 == 0 && y%2 == 0 ? 255 : 0;
    require(RebuildCutoutAlpha(pointers.data(), pitches.data(), 16, 16, 5, true), "uniform tie bounded");
    for (uint32_t y = 0; y < 8; ++y) for (uint32_t x = 0; x < 8; ++x)
        require(pointers[1][y*pitches[1]+x*4+3] == 64, "closest feasible coverage prefers identity scale");
    std::puts("PASS: selected names, cutout alpha mips/coverage, base/RGB/guards, endpoints, off/rejection and infeasible coverage");
}
