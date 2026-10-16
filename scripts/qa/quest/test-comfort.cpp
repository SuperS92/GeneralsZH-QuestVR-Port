// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestComfort.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <initializer_list>

struct D3 { double x,y,z; };
static D3 oracle(D3 v,double yaw,double pitch) {
    // Independent double-precision Rodrigues rotations about X then Y.
    D3 x{v.x,v.y*std::cos(pitch)-v.z*std::sin(pitch),v.y*std::sin(pitch)+v.z*std::cos(pitch)};
    return {x.x*std::cos(yaw)+x.z*std::sin(yaw),x.y,-x.x*std::sin(yaw)+x.z*std::cos(yaw)};
}
static bool near(float a,double b,double tolerance=0.008) { return std::fabs(a-b)<=tolerance; }
static void same(QuestXRFloat3 a,D3 b) { assert(near(a.x,b.x)&&near(a.y,b.y)&&near(a.z,b.z)); }
static D3 quat(QuestComfort::Rotation q,D3 v) {
    return {(1-2*(double(q.y)*q.y+double(q.z)*q.z))*v.x+2*(double(q.x)*q.y-double(q.z)*q.w)*v.y+2*(double(q.x)*q.z+double(q.y)*q.w)*v.z,
        2*(double(q.x)*q.y+double(q.z)*q.w)*v.x+(1-2*(double(q.x)*q.x+double(q.z)*q.z))*v.y+2*(double(q.y)*q.z-double(q.x)*q.w)*v.z,
        2*(double(q.x)*q.z-double(q.y)*q.w)*v.x+2*(double(q.y)*q.z+double(q.x)*q.w)*v.y+(1-2*(double(q.x)*q.x+double(q.y)*q.y))*v.z};
}
int main() {
    unsigned cases=0;
    const float pivot[]={0.21f,1.62f,-0.33f},offset[]={22,-84,61};
    for(float scale:{20.0f,100.0f,500.0f,1200.0f})for(float yaw:{-2.7f,-1.2f,0.0f,0.6f,2.2f})
    for(int degrees=0;degrees<=150;degrees+=15)for(int i=0;i<41;++i) {
        const float pitch=-degrees*0.017453292519943295f;
        QuestXRFloat3 point{float(i-20)*0.17f,float(i%9)*0.23f,float(i%13)*-0.19f};
        auto result=QuestComfort::Point(point,scale,yaw,pitch,pivot,offset);
        D3 relative{double(point.x)*scale-double(pivot[0])*scale,double(point.y)*scale-double(pivot[1])*scale,double(point.z)*scale-double(pivot[2])*scale};
        auto turned=oracle(relative,0,pitch);turned.x+=double(pivot[0])*scale;turned.y+=double(pivot[1])*scale;turned.z+=double(pivot[2])*scale;
        auto expected=oracle(turned,yaw,0);expected.x+=offset[0];expected.y+=offset[1];expected.z+=offset[2];same(result,expected);
        auto rotation=QuestComfort::Orientation(yaw,pitch);
        auto direct=QuestComfort::Rotate(point,yaw,pitch);same(direct,oracle({point.x,point.y,point.z},yaw,pitch));
        same(direct,quat(rotation,{point.x,point.y,point.z}));
        const auto translation=QuestComfort::Point({0,0,0},scale,yaw,pitch,pivot,offset);
        const auto renderer=quat(rotation,{point.x*scale,point.y*scale,point.z*scale});
        same(result,{renderer.x+translation.x,renderer.y+translation.y,renderer.z+translation.z});
        // The same fixed center is shared by eyes; rotation must preserve IPD.
        auto l=QuestComfort::Point({pivot[0]-.032f,pivot[1],pivot[2]},scale,yaw,pitch,pivot,offset);
        auto r=QuestComfort::Point({pivot[0]+.032f,pivot[1],pivot[2]},scale,yaw,pitch,pivot,offset);
        double distance=std::sqrt(double(r.x-l.x)*(r.x-l.x)+double(r.y-l.y)*(r.y-l.y)+double(r.z-l.z)*(r.z-l.z));
        assert(std::fabs(distance-scale*.064)<.002);
        auto center=QuestComfort::Point({pivot[0],pivot[1],pivot[2]},scale,yaw,pitch,pivot,offset);
        same(center,{(double(l.x)+r.x)/2,(double(l.y)+r.y)/2,(double(l.z)+r.z)/2});
        auto untranslated=oracle({double(pivot[0])*scale,double(pivot[1])*scale,double(pivot[2])*scale},yaw,0);
        same(center,{untranslated.x+offset[0],untranslated.y+offset[1],untranslated.z+offset[2]});
        if(degrees==0) {
            const float c=std::cos(yaw),s=std::sin(yaw);
            QuestXRFloat3 old{c*(point.x*scale)+s*(point.z*scale)+offset[0],point.y*scale+offset[1],-s*(point.x*scale)+c*(point.z*scale)+offset[2]};
            assert(std::memcmp(&old,&result,sizeof old)==0);
        }
        // Re-anchor a subsequent step at the current head without translating it.
        auto baseline=QuestComfort::Point(point,scale,yaw,0,pivot,offset);
        float nextOffset[]={offset[0]+result.x-baseline.x,offset[1]+result.y-baseline.y,offset[2]+result.z-baseline.z};
        float newPivot[]={point.x,point.y,point.z};
        auto next=QuestComfort::Point(point,scale,yaw,pitch-.1f,newPivot,nextOffset);same(next,{result.x,result.y,result.z});
        ++cases;
    }
    assert(QuestComfort::ClampDegrees(std::numeric_limits<float>::quiet_NaN())==0);
    assert(QuestComfort::ClampDegrees(std::numeric_limits<float>::infinity())==0);
    assert(QuestComfort::ClampDegrees(-10)==0&&QuestComfort::ClampDegrees(180)==150);
    QuestComfort::MenuScroll wheel;
    assert(wheel.Step(1,true,1)==1&&wheel.Step(1,true,100)==0);
    assert(wheel.Step(1,true,350000001)==1&&wheel.Step(1,true,350000002)==0);
    assert(wheel.Step(-1,true,350000003)==-1);
    assert(wheel.Step(1,false,350000004)==0&&wheel.Step(1,true,350000005)==1);
    assert(wheel.Step(std::numeric_limits<float>::quiet_NaN(),true,350000006)==0);
    assert(wheel.Step(1,true,99999999999ULL)==1&&wheel.Step(1,true,99999999999ULL)==0);
    QuestComfort::TiltGesture gesture;
    assert(gesture.Step(0,0,true)==0&&!gesture.active); // Existing pinch remains available.
    assert(gesture.Step(0,1,true)==1&&gesture.active);
    assert(gesture.Step(0,1,true)==0&&gesture.Step(0,-1,true)==0); // Must rearm.
    assert(gesture.Step(0,0,true)==0&&gesture.Step(0,-1,true)==-1);
    gesture.Step(0,0,true);assert(gesture.Step(-1,0,true)==2);
    assert(gesture.Step(0,1,false)==0&&!gesture.active);
    assert(gesture.Step(1,1,true)==0&&!gesture.active); // Ambiguous diagonal ignored.
    assert(gesture.Step(0,1,true)==1);
    std::printf("PASS %u comfort transform cases; stereo, pivot, camera/ray, identity, bounds and gesture contracts\n",cases);
}
