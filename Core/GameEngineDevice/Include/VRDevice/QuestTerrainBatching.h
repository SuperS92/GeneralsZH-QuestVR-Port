// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
namespace QuestXR {
// GeneralsX @performance Codex 09/10/2026 Independent terrain submission layout; no vertex or lighting math.
constexpr unsigned TerrainPatchVertices = 4096;
constexpr unsigned TerrainPatchIndices = 6144;
constexpr unsigned TerrainMaxGroup = 10; // Both VB and IB counts are unsigned short in W3D.
struct TerrainPatchSpan {
    unsigned firstPatch = 0, patches = 0, vertexOffset = 0;
};
inline bool TerrainSpan(unsigned patch, unsigned total, unsigned group, TerrainPatchSpan &out) {
    if (!group || group > TerrainMaxGroup || patch >= total) return false;
    const unsigned first = patch - patch % group;
    out = {first, total - first < group ? total - first : group,
           (patch - first) * TerrainPatchVertices};
    return true;
}
inline bool FillTerrainGroupIndices(uint16_t *out, size_t count, unsigned group) {
    if (!out || !group || group > TerrainMaxGroup || count != group * TerrainPatchIndices) return false;
    // Preserve the original two triangles for every four consecutive vertices,
    // including padded border vertices; append patches in the original Y/X order.
    for (unsigned quad = 0; quad < group * TerrainPatchVertices / 4; ++quad) {
        const unsigned vertex = quad * 4;
        const unsigned index = quad * 6;
        out[index] = vertex; out[index + 1] = vertex + 2; out[index + 2] = vertex + 3;
        out[index + 3] = vertex; out[index + 4] = vertex + 1; out[index + 5] = vertex + 2;
    }
    return true;
}
}
