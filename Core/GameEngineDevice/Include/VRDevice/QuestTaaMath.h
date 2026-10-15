// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>
namespace QuestXR {
// Actual D3D row-vector matrices and full device viewport, captured before the world draw.
struct TaaCamera {
    float view[16]{}, projection[16]{};
    uint32_t width=0,height=0;
    bool valid=false;
};
namespace TaaMath {
using Matrix=std::array<double,16>; // column-major; column-vector algebra
inline Matrix Identity() { Matrix r{};for(unsigned i=0;i<4;++i)r[i*5]=1;return r; }
inline Matrix Multiply(const Matrix &a,const Matrix &b) {
    Matrix r{};
    for(unsigned c=0;c<4;++c)for(unsigned row=0;row<4;++row)
        for(unsigned k=0;k<4;++k)r[c*4+row]+=a[k*4+row]*b[c*4+k];
    return r;
}
inline bool Inverse(const Matrix &a,Matrix &out) {
    double work[4][8]{};
    for(unsigned row=0;row<4;++row)for(unsigned c=0;c<4;++c) {
        if(!std::isfinite(a[c*4+row]))return false;
        work[row][c]=a[c*4+row];work[row][c+4]=(row==c);
    }
    for(unsigned c=0;c<4;++c) {
        unsigned pivot=c;
        for(unsigned row=c+1;row<4;++row)if(std::abs(work[row][c])>std::abs(work[pivot][c]))pivot=row;
        if(!(std::abs(work[pivot][c])>1e-20))return false;
        if(pivot!=c)for(unsigned j=0;j<8;++j)std::swap(work[c][j],work[pivot][j]);
        const double divisor=work[c][c];for(unsigned j=0;j<8;++j)work[c][j]/=divisor;
        for(unsigned row=0;row<4;++row)if(row!=c) {
            const double factor=work[row][c];for(unsigned j=0;j<8;++j)work[row][j]-=factor*work[c][j];
        }
    }
    for(unsigned row=0;row<4;++row)for(unsigned c=0;c<4;++c) {
        if(!std::isfinite(work[row][c+4]))return false;
        out[c*4+row]=work[row][c+4];
    }
    return true;
}
inline bool Matrices(const TaaCamera &camera,Matrix &view,Matrix &projection,Matrix &clip,Matrix &inverseClip) {
    if(!camera.valid || !camera.width || !camera.height)return false;
    // D3D's row-vector arrays become column-vector matrices without moving their bytes.
    for(unsigned i=0;i<16;++i) {
        if(!std::isfinite(camera.view[i])||!std::isfinite(camera.projection[i]))return false;
        view[i]=camera.view[i];projection[i]=camera.projection[i];
    }
    Matrix framebuffer=Identity();
    framebuffer[5]=-1; // actual pinned DXVK D3D9 negative-height Vulkan viewport
    framebuffer[12]=1.0/camera.width;framebuffer[13]=1.0/camera.height;
    // DXVK's +0.5 framebuffer-pixel offset applies to both axes. Depth stays [0,1].
    clip=Multiply(framebuffer,Multiply(projection,view));
    return Inverse(clip,inverseClip);
}
struct History {
    Matrix clip{},view{},projection{};
    uint64_t frame=0,time=0;
    bool valid=false;
};
enum class Reset : unsigned { None, First, Gap, Camera, Projection, Invalid };
inline Reset Plan(const TaaCamera &camera,const History &previous,uint64_t frame,uint64_t time,
    float (&reprojection)[16],History &next) {
    Matrix inverse{};
    next={};
    if(!frame || !time || !Matrices(camera,next.view,next.projection,next.clip,inverse))return Reset::Invalid;
    next.frame=frame;next.time=time;next.valid=true;
    Reset reason=Reset::None;
    if(!previous.valid)reason=Reset::First;
    else if(frame!=previous.frame+1 || time<=previous.time || time-previous.time>100000000ull)reason=Reset::Gap;
    else {
        Matrix oldWorld{},newWorld{};
        if(!Inverse(previous.view,oldWorld)||!Inverse(next.view,newWorld))return Reset::Invalid;
        double distance=0,rotation=0;
        for(unsigned i=0;i<3;++i) {
            const double d=oldWorld[12+i]-newWorld[12+i];distance+=d*d;
            for(unsigned c=0;c<3;++c)rotation=std::max(rotation,std::abs(oldWorld[c*4+i]-newWorld[c*4+i]));
        }
        if(distance>250.0*250.0 || rotation>0.12)reason=Reset::Camera;
        for(unsigned i=0;i<16;++i)
            if(std::abs(previous.projection[i]-next.projection[i])>0.02)reason=Reset::Projection;
    }
    const Matrix matrix=reason==Reset::None?Multiply(previous.clip,inverse):Identity();
    for(unsigned i=0;i<16;++i) {
        if(!std::isfinite(matrix[i]) || std::abs(matrix[i])>1e20)return Reset::Invalid;
        reprojection[i]=float(matrix[i]);
    }
    return reason;
}
}
}
