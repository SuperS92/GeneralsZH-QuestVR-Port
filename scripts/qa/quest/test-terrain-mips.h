// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "VRDevice/QuestTerrainMipFilter.h"
#include <vector>
#include <cstring>
static void checkTerrainMips() {
    using namespace QuestXR;
    constexpr uint32_t w = 20, h = 14, pitch = w * 4 + 5;
    TerrainMipRegion r{3, 3, 8, 8, 1, true};
    for (uint32_t mode = 1; mode <= 2; ++mode) {
        std::vector<uint8_t> image(pitch * h + 3, 0xE7);
        auto *p = image.data() + 1;
        for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) {
            p[y * pitch + x * 4] = 255; p[y * pitch + x * 4 + 1] = 0;
            p[y * pitch + x * 4 + 2] = 0; p[y * pitch + x * 4 + 3] = (x + y * w) % 256;
        }
        for (uint32_t y = 0; y < r.height; ++y) for (uint32_t x = 0; x < r.width; ++x) {
            auto *pixel = p + (y + r.y) * pitch + (x + r.x) * 4;
            pixel[0] = pixel[1] = pixel[2] = (x + y) % 2 ? 255 : 0;
        }
        const auto original = image;
        require(!FilterTerrainMipRgb(p, pitch, w, h, 21, r, 0) && image == original,
            "mip smoothing off is byte exact");
        auto invalid = r; invalid.x = 0;
        require(!FilterTerrainMipRgb(p, pitch, w, h, 21, invalid, mode), "gutter overflow rejected");
        require(!FilterTerrainMipRgb(p, pitch, w, h, 24, r, mode), "mip unknown format rejected");
        require(!FilterTerrainMipRgb(p, w * 4 - 1, w, h, 21, r, mode), "mip short pitch rejected");
        require(image == original, "mip rejection is byte exact");
        require(FilterTerrainMipRgb(p, pitch, w, h, 21, r, mode), "checkerboard filtering applied");
        for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) {
            const uint32_t offset = 1 + y * pitch + x * 4;
            const bool inside = x >= 2 && x < 12 && y >= 2 && y < 12;
            require(image[offset + 3] == original[offset + 3], "A8 alpha remains exact, including gutter");
            if (inside) require(image[offset] == 128 && image[offset + 1] == 128 && image[offset + 2] == 128,
                "checkerboard averages without importing neighbouring blue material");
            else require(std::memcmp(image.data() + offset, original.data() + offset, 4) == 0,
                "other atlas material unchanged");
        }
        for (uint32_t y = 0; y < h; ++y) for (uint32_t x = w * 4; x < pitch; ++x)
            require(p[y * pitch + x] == 0xE7, "mip row padding exact");
        require(image.front() == 0xE7 && image.back() == 0xE7, "mip outer guards exact");
    }
    // Broad linear detail remains exact away from its clamped boundary.
    std::vector<uint8_t> ramp(32 * 8 * 4, 0);
    for (uint32_t y = 0; y < 8; ++y) for (uint32_t x = 0; x < 32; ++x)
        ramp[(y * 32 + x) * 4] = static_cast<uint8_t>(x * 6);
    require(FilterTerrainMipRgb(ramp.data(), 128, 32, 8, 21, {0, 0, 32, 8, 0, false}, 2), "clamped broad ramp");
    for (uint32_t y = 0; y < 8; ++y) for (uint32_t x = 2; x < 30; ++x)
        require(ramp[(y * 32 + x) * 4] == x * 6, "broad detail preserved");
    std::vector<uint8_t> pixels(256 * 4 * 2 + 2, 0);
    for (uint32_t i = 0; i < 1024; ++i) {
        uint16_t value = static_cast<uint16_t>((i % 2 ? 0x7fff : 0) | (i % 3 ? 0x8000 : 0));
        std::memcpy(pixels.data() + 1 + i * 2, &value, 2);
    }
    const auto original = pixels;
    require(FilterTerrainMipRgb(pixels.data() + 1, 512, 256, 4, 25, {0, 0, 256, 4, 0, true}, 1), "unaligned A1 filtering");
    for (uint32_t i = 0; i < 1024; ++i) {
        uint16_t value, before;
        std::memcpy(&value, pixels.data() + 1 + i * 2, 2);
        std::memcpy(&before, original.data() + 1 + i * 2, 2);
        require((value & 0x8000) == (before & 0x8000) && (value & 0x7fff) == 0x4210,
            "A1 alpha preserved and striped RGB calmed");
    }
    std::puts("PASS: mip RGB checkerboard/ramp, material isolation, gutters, A1/A8 alpha, off, pitch and guards");
}
