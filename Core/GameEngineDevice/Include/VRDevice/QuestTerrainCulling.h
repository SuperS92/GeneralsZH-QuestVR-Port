// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace QuestXR {
// GeneralsX @performance Codex 13/10/2026 Conservative bounds from actual terrain writes.
struct TerrainPoint { double x, y, z; };
struct TerrainBounds {
    TerrainPoint low{}, high{};
    bool valid = false;
    bool Include(TerrainPoint p) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
        if (!valid) { low = high = p; valid = true; return true; }
        low = {std::fmin(low.x,p.x), std::fmin(low.y,p.y), std::fmin(low.z,p.z)};
        high = {std::fmax(high.x,p.x), std::fmax(high.y,p.y), std::fmax(high.z,p.z)};
        return true;
    }
};
struct TerrainPatchBounds {
    static constexpr unsigned Cells = 1024;
    TerrainBounds bounds;
    std::array<uint64_t,Cells/64> written{};
    unsigned cells = 0;
    bool unsafe = false;
    void Observe(unsigned cell, const std::array<TerrainPoint,4> &vertices) {
        if (cell >= Cells) { unsafe = true; return; }
        for (auto p : vertices) if (!bounds.Include(p)) unsafe = true;
        const uint64_t bit = uint64_t(1) << (cell % 64);
        if (!(written[cell/64] & bit)) { written[cell/64] |= bit; ++cells; }
    }
    bool Complete() const { return !unsafe && bounds.valid && cells == Cells; }
};
inline bool TerrainGroupBounds(const TerrainPatchBounds *patches, size_t total,
    size_t first, size_t count, TerrainBounds &out) {
    out = {};
    if (!patches || !count || first >= total || count > total-first) return false;
    for (size_t i=first; i<first+count; ++i) {
        if (!patches[i].Complete()) return false;
        out.Include(patches[i].bounds.low); out.Include(patches[i].bounds.high);
    }
    return out.valid;
}
struct TerrainPlane { TerrainPoint normal; double distance; };
struct TerrainFrustum {
    std::array<TerrainPlane,6> planes{};
    bool valid = false;
};
inline double TerrainPlaneTolerance(const TerrainPlane &p, TerrainPoint point) {
    // W3D planes/camera matrices originate in floats. Retain a generous guard,
    // including large coordinates and the very distant XR far plane.
    return 0.05 * (std::fabs(p.normal.x)+std::fabs(p.normal.y)+std::fabs(p.normal.z))
        + 0.00002 * (std::fabs(p.normal.x*point.x)+std::fabs(p.normal.y*point.y)
            +std::fabs(p.normal.z*point.z)+std::fabs(p.distance)+1.0);
}
inline TerrainFrustum PrepareTerrainFrustum(const std::array<TerrainPlane,6> &planes,
    const std::array<TerrainPoint,8> &corners, bool eligible) {
    TerrainFrustum out; out.planes = planes;
    if (!eligible) return out;
    TerrainPoint center{};
    for (auto c : corners) {
        if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z)) return out;
        center.x += c.x/8; center.y += c.y/8; center.z += c.z/8;
    }
    for (auto p : planes) {
        const double length = p.normal.x*p.normal.x+p.normal.y*p.normal.y+p.normal.z*p.normal.z;
        if (!std::isfinite(length) || !(length>0) || !std::isfinite(p.distance)) return out;
        // W3D normals point outwards. Reject malformed or oppositely oriented
        // planes rather than removing geometry using an uncertain convention.
        for (auto c : corners) {
            const double side=p.normal.x*c.x+p.normal.y*c.y+p.normal.z*c.z-p.distance;
            if (!std::isfinite(side) || side>TerrainPlaneTolerance(p,c)) return out;
        }
        const double side=p.normal.x*center.x+p.normal.y*center.y+p.normal.z*center.z-p.distance;
        if (!std::isfinite(side) || side>0) return out;
    }
    out.valid = true;
    return out;
}
inline bool TerrainOutside(const TerrainBounds &box, const TerrainFrustum &frustum) {
    if (!box.valid || !frustum.valid) return false;
    for (auto p : frustum.planes) {
        // Minimum signed distance: reject only when the entire box is outside
        // one plane. A partly visible group is always retained for this eye.
        const TerrainPoint closest{p.normal.x<0?box.high.x:box.low.x,
            p.normal.y<0?box.high.y:box.low.y, p.normal.z<0?box.high.z:box.low.z};
        const double side=p.normal.x*closest.x+p.normal.y*closest.y+p.normal.z*closest.z-p.distance;
        if (std::isfinite(side) && side>TerrainPlaneTolerance(p,closest)) return true;
    }
    return false;
}
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
class TerrainCullDrawScope {
    bool previous;
public:
    explicit TerrainCullDrawScope(bool suppress);
    ~TerrainCullDrawScope();
    TerrainCullDrawScope(const TerrainCullDrawScope &) = delete;
    TerrainCullDrawScope &operator=(const TerrainCullDrawScope &) = delete;
};
#endif
}
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
extern "C" bool QuestXR_SuppressTerrainDraw();
#endif
