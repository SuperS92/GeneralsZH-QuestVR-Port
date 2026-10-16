// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
namespace QuestXR {
struct MaskPoint { double x=0,y=0; };
struct MaskRect { int32_t left=0,top=0,right=0,bottom=0; };
struct MaskFov { float left=0,right=0,up=0,down=0; };
struct MaskPlan { std::vector<MaskRect> rectangles; uint64_t pixels=0; bool valid=false; };
constexpr unsigned kMaskBlock=32,kMaskGuard=16,kMaskMaxRects=512;
inline bool ValidMaskFov(const MaskFov &f) {
    for(float v:{f.left,f.right,f.up,f.down})if(!std::isfinite(v)||std::abs(v)>=1.55f)return false;
    return f.left<f.right&&f.down<f.up&&std::tan(f.right)-std::tan(f.left)>1e-5
        &&std::tan(f.up)-std::tan(f.down)>1e-5;
}
// OpenXR mask vertices are in the z=-1 view plane. The pinned DXVK viewport
// adds half a pixel, as independently checked by the temporal projection oracle.
inline MaskPoint ProjectMaskPoint(MaskPoint p,const MaskFov &f,unsigned width,unsigned height) {
    const double l=std::tan(f.left),r=std::tan(f.right),u=std::tan(f.up),d=std::tan(f.down);
    return {(p.x-l)/(r-l)*width+.5,(u-p.y)/(u-d)*height+.5};
}
inline bool MaskProjectionMatches(const float *m,const MaskFov &f) {
    if(!m||!ValidMaskFov(f))return false;
    for(unsigned n=0;n<16;++n)if(!std::isfinite(m[n]))return false;
    const double l=std::tan(f.left),r=std::tan(f.right),u=std::tan(f.up),d=std::tan(f.down);
    for(unsigned n=0;n<4;++n) {
        const double x=(n&1)?r:l,y=(n&2)?u:d;
        const double w=x*m[3]+y*m[7]-m[11]+m[15];
        const double px=x*m[0]+y*m[4]-m[8]+m[12];
        const double py=x*m[1]+y*m[5]-m[9]+m[13];
        if(!(w>1e-6)||std::abs(px/w-((n&1)?1:-1))>1e-4||std::abs(py/w-((n&2)?1:-1))>1e-4)return false;
    }
    return true;
}
inline double MaskCross(MaskPoint a,MaskPoint b,MaskPoint p) {
    return (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x);
}
inline bool MaskInside(MaskPoint p,MaskPoint a,MaskPoint b,MaskPoint c) {
    const double sign=MaskCross(a,b,c);
    if(std::abs(sign)<1e-8)return false;
    const double direction=sign>0?1:-1;
    return direction*MaskCross(a,b,p)>1e-7&&direction*MaskCross(b,c,p)>1e-7&&direction*MaskCross(c,a,p)>1e-7;
}
// GeneralsX @performance Codex 16/10/2026 Only mask blocks proven inside a
// runtime hidden triangle, with an inward pixel guard. Never guess lens borders.
inline MaskPlan PlanVisibilityMask(const std::vector<MaskPoint> &vertices,const std::vector<uint32_t> &indices,
    const MaskFov &f,unsigned width,unsigned height) {
    MaskPlan result;
    if(!width||!height||width>8192||height>8192||!ValidMaskFov(f)||vertices.size()<3||vertices.size()>8192
        ||indices.empty()||indices.size()>24576||indices.size()%3)return result;
    std::vector<MaskPoint> points;points.reserve(vertices.size());
    for(auto p:vertices) {
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||std::abs(p.x)>128||std::abs(p.y)>128)return {};
        points.push_back(ProjectMaskPoint(p,f,width,height));
    }
    for(auto i:indices)if(i>=points.size())return {};
    // Keep the complete Lite history/current/gather footprint untouched. This
    // also conservatively protects the central view with temporal filtering Off.
    const int mx=int(width*15/100),my=int(height*15/100);
    const MaskRect protectedCenter{mx-4,my-4,int(width)-mx+4,int(height)-my+4};
    unsigned work=0;
    for(unsigned y=0;y<height;y+=kMaskBlock) {
        MaskRect run{};bool pending=false;
        for(unsigned x=0;x<width;x+=kMaskBlock) {
            const MaskRect cell{int(x),int(y),int(std::min(width,x+kMaskBlock)),int(std::min(height,y+kMaskBlock))};
            bool hidden=false;
            const bool center=cell.left<protectedCenter.right&&cell.right>protectedCenter.left
                &&cell.top<protectedCenter.bottom&&cell.bottom>protectedCenter.top;
            if(!center) {
                const double l=std::max(.5,double(cell.left)-kMaskGuard+.5),r=std::min(double(width)-.5,double(cell.right)+kMaskGuard-.5);
                const double t=std::max(.5,double(cell.top)-kMaskGuard+.5),b=std::min(double(height)-.5,double(cell.bottom)+kMaskGuard-.5);
                for(size_t i=0;i<indices.size()&&!hidden;i+=3) {
                    if(++work>2000000)return {}; // Bounded one-time runtime-data preparation.
                    const auto a=points[indices[i]],v=points[indices[i+1]],c=points[indices[i+2]];
                    hidden=MaskInside({l,t},a,v,c)&&MaskInside({r,t},a,v,c)&&MaskInside({l,b},a,v,c)&&MaskInside({r,b},a,v,c);
                }
            }
            if(hidden) {
                if(!pending){run=cell;pending=true;}else run.right=cell.right;
            }
            if(pending&&(!hidden||cell.right==int(width))) {
                result.pixels+=uint64_t(run.right-run.left)*unsigned(run.bottom-run.top);
                result.rectangles.push_back(run);pending=false;
                if(result.rectangles.size()>kMaskMaxRects)return {};
            }
        }
    }
    result.valid=true;return result;
}
struct MaskCache {
    MaskPlan plan;
    MaskFov fov{};
    uint64_t generation=0;
};
}
