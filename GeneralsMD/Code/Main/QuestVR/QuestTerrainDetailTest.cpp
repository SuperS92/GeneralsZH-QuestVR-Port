// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestTerrainDetailTest.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cstdio>
namespace QuestXR {
namespace { std::atomic<uint32_t> uploads{0}; }
bool TerrainDetailTestRequested() {
    const char *choice = std::getenv("GX_QUEST_TERRAIN_DETAIL_TEST");
    return choice && std::strcmp(choice, "1") == 0;
}
uint32_t TerrainDetailTestUploads() { return uploads.load(); }
bool NeutralizeTerrainRgb(void *pixels, int pitch, uint32_t width, uint32_t height,
    uint32_t format, bool enabled) {
    const uint32_t bytes = format == 25 ? 2 : format == 21 ? 4 : 0;
    if (!enabled || !pixels || !bytes || !width || !height || width > 4096 || height > 4096
        || pitch < static_cast<int>(width * bytes) || pitch > 65536) return false;
    auto *base = static_cast<uint8_t *>(pixels);
    for (uint32_t y = 0; y < height; ++y) {
        auto *row = base + static_cast<size_t>(y) * static_cast<size_t>(pitch);
        for (uint32_t x = 0; x < width; ++x) {
            if (bytes == 2) {
                uint16_t value;
                std::memcpy(&value, row + x * 2, 2);
                value = (value & 0x8000u) | 0x4210u;
                std::memcpy(row + x * 2, &value, 2);
            } else {
                // Preserve A8 byte exactly, independent of alignment/endian integer access.
                row[x * 4] = row[x * 4 + 1] = row[x * 4 + 2] = 132;
            }
        }
    }
    return true;
}
void ApplyTerrainDetailTest(void *pixels, int pitch, uint32_t width,
    uint32_t height, uint32_t format) {
    if (NeutralizeTerrainRgb(pixels, pitch, width, height, format, TerrainDetailTestRequested())) {
        const uint32_t count = uploads.fetch_add(1) + 1;
        if (count <= 8) std::fprintf(stderr,
            "[Quest terrain detail test] neutral RGB upload %u: %ux%u format=%u; alpha preserved\n",
            count, width, height, format);
    }
}
}
#endif
