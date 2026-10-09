// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "VRDevice/QuestTerrainDetailTest.h"
#include <vector>
#include <cstring>
// GeneralsX @feature Codex 02/10/2026 Exhaustive alpha/off and upload-bound tests.
static void checkTerrainDetail() {
    using namespace QuestXR;
    constexpr unsigned width = 257, height = 256, pitch = width * 2 + 7;
    std::vector<uint8_t> bytes(pitch * height + 4, 0xD7);
    auto *pixels = bytes.data() + 1; // Deliberately unaligned, with outer guards.
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
        uint16_t value = static_cast<uint16_t>(y * width + x);
        std::memcpy(pixels + y * pitch + x * 2, &value, 2);
    }
    const auto original = bytes;
    require(!NeutralizeTerrainRgb(pixels, pitch, width, height, 25, false), "terrain test off");
    require(bytes == original, "off is byte exact");
    require(!NeutralizeTerrainRgb(pixels, pitch, width, height, 24, true), "unknown format rejected");
    require(!NeutralizeTerrainRgb(pixels, width * 2 - 1, width, height, 25, true), "short pitch rejected");
    require(!NeutralizeTerrainRgb(pixels, -1, width, height, 25, true), "negative pitch rejected");
    require(!NeutralizeTerrainRgb(pixels, pitch, 0, height, 25, true), "zero extent rejected");
    require(!NeutralizeTerrainRgb(pixels, pitch, width, 4097, 25, true), "oversize rejected");
    require(!NeutralizeTerrainRgb(nullptr, pitch, width, height, 25, true), "null rejected");
    require(bytes == original, "all rejected inputs remain byte exact");
    require(NeutralizeTerrainRgb(pixels, pitch, width, height, 25, true), "A1 conversion");
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            uint16_t value;
            std::memcpy(&value, pixels + y * pitch + x * 2, 2);
            require(value == ((static_cast<uint16_t>(y * width + x) & 0x8000u) | 0x4210u),
                "all original 16-bit values preserve alpha and become neutral RGB");
        }
        for (unsigned x = width * 2; x < pitch; ++x)
            require(pixels[y * pitch + x] == 0xD7, "A1 row padding untouched");
    }
    require(bytes.front() == 0xD7 && bytes.back() == 0xD7, "A1 outer guards untouched");
    std::vector<uint8_t> edges(256 * 7 + 3, 0xE3);
    for (unsigned y = 0; y < 256; ++y) edges[1 + y * 7 + 3] = static_cast<uint8_t>(y);
    require(NeutralizeTerrainRgb(edges.data() + 1, 7, 1, 256, 21, true), "A8 conversion");
    for (unsigned y = 0; y < 256; ++y) {
        const auto *row = edges.data() + 1 + y * 7;
        require(row[0] == 132 && row[1] == 132 && row[2] == 132 && row[3] == y,
            "all A8 alpha values exact");
        require(row[4] == 0xE3 && row[5] == 0xE3 && row[6] == 0xE3, "A8 padding exact");
    }
    require(edges.front() == 0xE3 && edges.back() == 0xE3, "A8 outer guards untouched");
    require(TerrainDetailTestUploads() == 0, "pure fixture does not alter production counter");
    std::puts("PASS: terrain RGB isolation, all alpha values, off, pitch, guards and rejects");
}
