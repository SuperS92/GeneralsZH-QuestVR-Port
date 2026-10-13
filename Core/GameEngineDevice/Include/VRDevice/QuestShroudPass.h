// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "QuestTerrainCulling.h"
#include <algorithm>
#include <cstring>
#include <new>
#include <vector>

namespace QuestXR {
// GeneralsX @performance Codex 13/10/2026 Evidence from successful GPU uploads,
// never logical visibility or a guessed fog color. Unknown texels are nonneutral.
struct ShroudRect { int left=0,top=0,right=0,bottom=0; };
class ShroudWhiteMask {
    unsigned width=0,height=0;
    std::vector<uint8_t> white;
    std::vector<uint32_t> prefix;
    bool ready=false;
public:
    bool Initialize(unsigned w,unsigned h) {
        width=height=0;white.clear();prefix.clear();ready=false;
        if (!w || !h || w>2048 || h>2048) return false;
        try { white.assign(size_t(w)*h,0);prefix.assign(size_t(w+1)*(h+1),0); }
        catch(const std::bad_alloc &) { white.clear();prefix.clear();return false; }
        width=w;height=h;return true;
    }
    void Invalidate() { std::fill(white.begin(),white.end(),0);ready=false; }
    bool Observe(const void *source,unsigned sourceWidth,unsigned sourceHeight,unsigned pitch,
        ShroudRect rectangle,int x,int y,bool success) {
        ready=false;
        if (!success || !source || !width || pitch<size_t(sourceWidth)*2 || !sourceWidth || !sourceHeight
            || rectangle.left<0 || rectangle.top<0 || rectangle.right<=rectangle.left || rectangle.bottom<=rectangle.top
            || unsigned(rectangle.right)>sourceWidth || unsigned(rectangle.bottom)>sourceHeight
            || x<0 || y<0 || uint64_t(x)+rectangle.right-rectangle.left>width
            || uint64_t(y)+rectangle.bottom-rectangle.top>height) { Invalidate();return false; }
        auto *data=static_cast<const uint8_t*>(source);
        for (int row=rectangle.top;row<rectangle.bottom;++row) for(int col=rectangle.left;col<rectangle.right;++col) {
            uint16_t pixel;std::memcpy(&pixel,data+size_t(row)*pitch+size_t(col)*2,2);
            white[size_t(y+row-rectangle.top)*width+x+col-rectangle.left]=pixel==0xffff;
        }
        return true;
    }
    void Finish() {
        if (!width) return;
        for (unsigned y=1;y<=height;++y) {
            uint32_t row=0;
            for (unsigned x=1;x<=width;++x) {
                row+=white[size_t(y-1)*width+x-1];
                prefix[size_t(y)*(width+1)+x]=row+prefix[size_t(y-1)*(width+1)+x];
            }
        }
        ready=true;
    }
    bool Neutral(ShroudRect r) const {
        if (!ready || r.left<0 || r.top<0 || r.right<=r.left || r.bottom<=r.top
            || unsigned(r.right)>width || unsigned(r.bottom)>height) return false;
        auto at=[&](int x,int y) { return prefix[size_t(y)*(width+1)+x]; };
        return uint64_t(at(r.right,r.bottom))+at(r.left,r.top)-at(r.left,r.bottom)-at(r.right,r.top)
            ==uint64_t(r.right-r.left)*(r.bottom-r.top);
    }
};
// Row-vector matrices as returned by D3D8. Compute a guarded footprint using
// the actual view and texture matrices, including camera-space cancellation.
inline bool ShroudFootprint(const TerrainBounds &bounds,const float *view,const float *texture,
    unsigned width,unsigned height,ShroudRect &out) {
    if (!bounds.valid || !view || !texture || !width || !height) return false;
    for(unsigned i=0;i<16;++i) if(!std::isfinite(view[i]) || !std::isfinite(texture[i])) return false;
    double low[2]={INFINITY,INFINITY},high[2]={-INFINITY,-INFINITY},guard[2]={2,2};
    for(unsigned corner=0;corner<8;++corner) {
        const double p[4]={corner&1?bounds.high.x:bounds.low.x,corner&2?bounds.high.y:bounds.low.y,
            corner&4?bounds.high.z:bounds.low.z,1};
        double camera[3]{},magnitude[3]{};
        for(unsigned j=0;j<3;++j) for(unsigned i=0;i<4;++i) {
            camera[j]+=p[i]*view[i*4+j];magnitude[j]+=std::fabs(p[i]*view[i*4+j]);
        }
        for(unsigned j=0;j<2;++j) {
            double uv=texture[12+j],error=std::fabs(double(texture[12+j]));
            for(unsigned i=0;i<3;++i) { uv+=camera[i]*texture[i*4+j];error+=magnitude[i]*std::fabs(double(texture[i*4+j])); }
            const double extent=j?height:width,pixel=uv*extent-0.5;
            if(!std::isfinite(pixel) || !std::isfinite(error)) return false;
            low[j]=std::fmin(low[j],pixel);high[j]=std::fmax(high[j],pixel);
            guard[j]=std::fmax(guard[j],2+0.000002*error*extent);
        }
    }
    // Do not clamp/wrap uncertainty into a neutral interior. Retain edge draws.
    if(low[0]-guard[0]<0 || low[1]-guard[1]<0 || high[0]+guard[0]+1>width || high[1]+guard[1]+1>height) return false;
    out={int(std::floor(low[0]-guard[0])),int(std::floor(low[1]-guard[1])),
        int(std::floor(high[0]+guard[0]))+2,int(std::floor(high[1]+guard[1]))+2};
    return out.right<=int(width) && out.bottom<=int(height);
}
struct ShroudRun { unsigned first=0,count=0; };
struct ShroudRuns { std::array<ShroudRun,10> ranges{}; unsigned count=0; };
inline ShroudRuns ShroudRetainedRuns(unsigned count,uint32_t skipMask) {
    ShroudRuns out;
    if (!count || count>10 || (skipMask>>count)) return out;
    for(unsigned i=0;i<count;) {
        if(skipMask&(1u<<i)) { ++i;continue; }
        const unsigned first=i;while(i<count && !(skipMask&(1u<<i))) ++i;
        out.ranges[out.count++]={first,i-first};
    }
    return out;
}
struct ShroudPassStats {
    uint64_t attempted=0,skipped=0,submittedRuns=0,contractFallback=0,unknownBounds=0,uploadFailures=0;
    uint32_t contractMask=0,lastState=0,lastExpected=0,lastActual=0;
};
}
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <d3d8.h>
namespace QuestXR {
class ShroudUploadScope {
    bool active=false;
public:
    ShroudUploadScope(IDirect3DSurface8 *source,IDirect3DSurface8 *destination,IDirect3DTexture8 *texture,
        const void *pixels,unsigned pitch,unsigned sourceWidth,unsigned sourceHeight);
    ~ShroudUploadScope();
    ShroudUploadScope(const ShroudUploadScope &)=delete;
    ShroudUploadScope &operator=(const ShroudUploadScope &)=delete;
};
class ShroudDrawScope {
    const TerrainPatchBounds *previous;
    unsigned previousCount;
    IDirect3DVertexBuffer8 *previousVertices;
public:
    ShroudDrawScope(const TerrainPatchBounds *bounds,unsigned count,IDirect3DVertexBuffer8 *vertices);
    ~ShroudDrawScope();
    ShroudDrawScope(const ShroudDrawScope &)=delete;
    ShroudDrawScope &operator=(const ShroudDrawScope &)=delete;
};
void ForgetShroudTexture(IDirect3DTexture8 *texture);
ShroudPassStats TakeShroudPassStats();
}
bool QuestXR_CopyShroudRects(IDirect3DDevice8 *device,IDirect3DSurface8 *source,const RECT *rectangles,
    unsigned count,IDirect3DSurface8 *destination,const POINT *points,HRESULT &result);
bool QuestXR_DrawNeutralShroud(IDirect3DDevice8 *device,unsigned primitive,unsigned minVertex,
    unsigned vertices,unsigned startIndex,unsigned polygons);
#endif
