// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "VRDevice/QuestTaaMath.h"
#include <limits>
static void testMath() {
    using namespace QuestXR;
    TaaCamera camera; camera.width=2520;camera.height=2640;camera.valid=true;
    camera.view[0]=camera.view[5]=camera.view[10]=camera.view[15]=1;
    camera.projection[0]=.8f;camera.projection[5]=1.2f;
    camera.projection[8]=.11f;camera.projection[9]=-.06f;
    camera.projection[10]=1.0001f;camera.projection[11]=1;camera.projection[14]=-.1f;
    // Independent row-vector D3D projection plus the actual half-pixel viewport.
    auto project=[](const TaaCamera &c,const double (&world)[4],double (&pixel)[3]) {
        double v[4]{},q[4]{};
        for(unsigned col=0;col<4;++col)for(unsigned row=0;row<4;++row)v[col]+=world[row]*c.view[row*4+col];
        for(unsigned col=0;col<4;++col)for(unsigned row=0;row<4;++row)q[col]+=v[row]*c.projection[row*4+col];
        pixel[0]=(q[0]/q[3]*.5+.5)*c.width+.5;
        pixel[1]=(-q[1]/q[3]*.5+.5)*c.height+.5;pixel[2]=q[2]/q[3];
    };
    TaaMath::History old,next;float matrix[16]{};
    require(TaaMath::Plan(camera,old,1,1000000000,matrix,next)==TaaMath::Reset::First,"First camera resets");
    old=next;
    auto moved=camera;moved.view[12]=-2;moved.view[13]=.5f;
    require(TaaMath::Plan(moved,old,2,1016000000,matrix,next)==TaaMath::Reset::None,"Small camera movement reprojects");
    for(unsigned i=0;i<1000;++i) {
        const double world[4]={double(int(i%21)-10)*2,double(int(i%17)-8),10.0+i*.13,1};double now[3],previous[3];
        project(moved,world,now);project(camera,world,previous);
        const double current[4]={now[0]/camera.width*2-1,now[1]/camera.height*2-1,now[2],1};double actual[4]{};
        for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)actual[row]+=matrix[col*4+row]*current[col];
        require(std::abs((actual[0]/actual[3]*.5+.5)*camera.width-previous[0])<.02,"D3D X reprojection oracle");
        require(std::abs((actual[1]/actual[3]*.5+.5)*camera.height-previous[1])<.02,"D3D Y flip and half-pixel oracle");
        require(std::abs(actual[2]/actual[3]-previous[2])<1e-5,"D3D [0,1] depth oracle");
    }
    require(TaaMath::Plan(camera,old,3,1032000000,matrix,next)==TaaMath::Reset::Gap,"Missing frame resets");
    require(TaaMath::Plan(camera,old,2,1200000000,matrix,next)==TaaMath::Reset::Gap,"Time gap resets");
    auto teleport=camera;teleport.view[12]=1000;
    require(TaaMath::Plan(teleport,old,2,1016000000,matrix,next)==TaaMath::Reset::Camera,"Camera jump resets");
    auto zoom=camera;zoom.projection[0]+=.1f;
    require(TaaMath::Plan(zoom,old,2,1016000000,matrix,next)==TaaMath::Reset::Projection,"Projection jump resets");
    auto invalid=camera;invalid.projection[0]=std::numeric_limits<float>::quiet_NaN();
    require(TaaMath::Plan(invalid,old,2,1016000000,matrix,next)==TaaMath::Reset::Invalid,"NaN rejected");
    invalid.projection[0]=0;
    require(TaaMath::Plan(invalid,old,2,1016000000,matrix,next)==TaaMath::Reset::Invalid,"Singular projection rejected");
}
