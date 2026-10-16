/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "VRDevice/QuestVRMath.h"
#include <cmath>
#include <cstdint>

// GeneralsX @feature Codex 16/10/2026 One room-to-world comfort transform for eyes and controller rays.
namespace QuestComfort {
inline float ClampDegrees(float degrees) {
    return !std::isfinite(degrees) ? 0.0f : degrees < 0.0f ? 0.0f : degrees > 150.0f ? 150.0f : degrees;
}
inline QuestXRFloat3 Rotate(QuestXRFloat3 value,float yaw,float pitch) {
    if(pitch!=0.0f) {
        const float c=std::cos(pitch),s=std::sin(pitch);
        value={value.x,c*value.y-s*value.z,s*value.y+c*value.z};
    }
    const float c=std::cos(yaw),s=std::sin(yaw);
    return {c*value.x+s*value.z,value.y,-s*value.x+c*value.z};
}
inline QuestXRFloat3 Point(QuestXRFloat3 metres,float scale,float yaw,float pitch,
    const float *pivot,const float *offset) {
    QuestXRFloat3 value{metres.x*scale,metres.y*scale,metres.z*scale};
    if(pitch!=0.0f) {
        const QuestXRFloat3 center{pivot[0]*scale,pivot[1]*scale,pivot[2]*scale};
        value=Rotate({value.x-center.x,value.y-center.y,value.z-center.z},0.0f,pitch);
        value={value.x+center.x,value.y+center.y,value.z+center.z};
    }
    value=Rotate(value,yaw,0.0f);
    return {value.x+offset[0],value.y+offset[1],value.z+offset[2]};
}
struct Rotation { float x,y,z,w; };
inline Rotation Orientation(float yaw,float pitch) {
    const float sy=std::sin(yaw*0.5f),cy=std::cos(yaw*0.5f);
    const float sp=std::sin(pitch*0.5f),cp=std::cos(pitch*0.5f);
    return {cy*sp,sy*cp,-sy*sp,cy*cp};
}

// Menu-only discrete wheel repeats, independent of simulation/render rate.
// Reset on ray loss/keyboard ownership; never catch up with a burst after a stall.
struct MenuScroll {
    int direction=0;
    uint64_t next=0;
    int Step(float axis,bool enabled,uint64_t now) {
        const int wanted=enabled&&std::isfinite(axis) ? (axis>0.55f?1:axis< -0.55f?-1:0):0;
        if(!wanted){direction=0;next=0;return 0;}
        if(wanted!=direction){direction=wanted;next=now+350000000ULL;return wanted;}
        if(now<next)return 0;
        next=now+160000000ULL;return wanted;
    }
};

// One tilt step per deliberate stick excursion; neutral is required to rearm.
// Once a tilt gesture begins, suppress the simultaneous two-hand scale gesture.
struct TiltGesture {
    bool armed=true;
    bool active=false;
    int Step(float x,float y,bool enabled) {
        if(!enabled||!std::isfinite(x)||!std::isfinite(y)){armed=true;active=false;return 0;}
        const float ax=std::fabs(x),ay=std::fabs(y);
        if(ax<0.25f&&ay<0.25f){armed=true;return 0;}
        if(!armed)return 0;
        int result=0;
        if(ay>=0.75f&&ay>ax+0.1f)result=y>0.0f?1:-1;
        else if(x<=-0.75f&&ax>ay+0.1f)result=2;
        if(result){armed=false;active=true;}
        return result;
    }
};
}
