// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace QuestXR {
enum class CoverageDraw { Skip, Over, Solid, Unsupported };
// GeneralsX @feature Codex 17/10/2026 Independent visibility; never reinterpret material destination alpha.
inline CoverageDraw ClassifyCoverage(uint32_t blend,uint32_t src,uint32_t dst,uint32_t op,uint32_t mask) {
    if (!(mask&7) || !blend || (src==1&&dst==2)) return CoverageDraw::Skip;
    if (op!=1 || (mask&7)!=7) return CoverageDraw::Unsupported;
    if ((src==2||src==5)&&dst==1) return CoverageDraw::Solid;
    if ((src==2||src==5)&&dst==6) return CoverageDraw::Over;
    // Additive light already has premultiplied RGB; it must not darken the room.
    if ((src==2||src==5)&&dst==2) return CoverageDraw::Skip;
    if ((src==1&&dst==3)||(src==9&&dst==1)) return CoverageDraw::Skip;
    // Soft-water edges read the legacy destination alpha. Keep the water surface solid in MR.
    if (src==7&&(dst==8||dst==2)) return CoverageDraw::Solid;
    return CoverageDraw::Unsupported;
}
}

#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
struct IDirect3DDevice8;
struct IDirect3DTexture8;
struct IDirect3DSurface8;
namespace QuestXR {
class WorldCoverage final {
public:
    WorldCoverage()=default;
    WorldCoverage(const WorldCoverage &)=delete;
    WorldCoverage &operator=(const WorldCoverage &)=delete;
    bool begin(IDirect3DDevice8 *,IDirect3DSurface8 *,IDirect3DSurface8 *,unsigned eye);
    void capture(IDirect3DDevice8 *,uint32_t primitive,uint32_t first,uint32_t vertices,uint32_t start,uint32_t count,bool indexed=true);
    void finish(IDirect3DDevice8 *,bool presenting);
    void resetFrame();
    void destroy();
    bool stereoReady() const {return m_ready[0]&&m_ready[1];}
    bool available() const {return !m_failed;}
private:
    IDirect3DDevice8 *m_device=nullptr;
    IDirect3DTexture8 *m_texture=nullptr;
    IDirect3DSurface8 *m_mask=nullptr,*m_eye=nullptr,*m_depth=nullptr;
    uint32_t m_width=0,m_height=0,m_eyeIndex=2,m_frames=0,m_replays=0;
    bool m_ready[2]{},m_valid=false,m_failed=false;
};
// Called only after a real native GPU draw, never by world/update traversal.
void CaptureWorldCoverageDraw(IDirect3DDevice8 *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
void CaptureWorldCoveragePrimitive(IDirect3DDevice8 *,uint32_t,uint32_t,uint32_t);
void FinishWorldCoverage(IDirect3DDevice8 *,bool presenting);
}
#endif
