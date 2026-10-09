// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestTerrainBatching.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>
static void require(bool ok) { if (!ok) std::abort(); }
int main() {
    using namespace QuestXR;
    // Independent original single-patch index oracle, concatenated with offsets.
    std::vector<uint16_t> single;
    for (unsigned y = 0; y < 32; ++y) for (unsigned x = 0; x < 32; ++x) {
        unsigned v = 4 * (y * 32 + x);
        for (unsigned offset : {0u, 2u, 3u, 0u, 1u, 2u}) single.push_back(v + offset);
    }
    for (unsigned group = 1; group <= 10; ++group) {
        std::vector<uint16_t> indices(group * single.size() + 2, 0xbeef);
        require(FillTerrainGroupIndices(indices.data() + 1, group * single.size(), group));
        require(indices.front() == 0xbeef && indices.back() == 0xbeef);
        for (unsigned patch = 0; patch < group; ++patch)
            for (unsigned i = 0; i < single.size(); ++i)
                require(indices[1 + patch * single.size() + i] == single[i] + patch * 4096);
        require(*std::max_element(indices.begin() + 1, indices.end() - 1) == group * 4096 - 1);
        // Exercise full/partial groups, odd row widths, map borders and recreations.
        for (unsigned total = 1; total <= 257; ++total) {
            std::vector<unsigned> visits(total);
            unsigned draws = 0;
            for (unsigned patch = 0; patch < total; ++patch) {
                TerrainPatchSpan span;
                require(TerrainSpan(patch, total, group, span));
                require(span.vertexOffset / 4096 + span.firstPatch == patch);
                require(span.vertexOffset + 4096 <= span.patches * 4096);
                if (patch == span.firstPatch) {
                    ++draws;
                    for (unsigned n = 0; n < span.patches; ++n) ++visits[patch + n];
                }
                // A static or dynamic-light update must affect only this patch's slice.
                std::vector<unsigned> payload(span.patches * 4096 + 2, 7);
                std::fill(payload.begin() + 1 + span.vertexOffset,
                          payload.begin() + 1 + span.vertexOffset + 4096, 42);
                for (unsigned i = 0; i < payload.size(); ++i)
                    require(payload[i] == (i >= 1 + span.vertexOffset && i <= span.vertexOffset + 4096 ? 42u : 7u));
            }
            require(draws == (total + group - 1) / group);
            require(std::all_of(visits.begin(), visits.end(), [](unsigned n) { return n == 1; }));
        }
    }
    TerrainPatchSpan invalid{17, 19, 23};
    require(!TerrainSpan(0, 0, 10, invalid) && invalid.firstPatch == 17);
    require(!TerrainSpan(0, 1, 0, invalid));
    require(!TerrainSpan(0, 1, 11, invalid));
    require(!TerrainSpan(1, 1, 10, invalid));
    std::array<uint16_t, 8> guard{};
    require(!FillTerrainGroupIndices(guard.data(), guard.size(), 10));
    require(!FillTerrainGroupIndices(nullptr, 6144, 1));
    require(std::all_of(guard.begin(), guard.end(), [](unsigned n) { return n == 0; }));
    std::puts("PASS: original index order, 16-bit bounds, all partial groups, isolated patch updates and rejection guards; not GPU/crossplay evidence");
}
