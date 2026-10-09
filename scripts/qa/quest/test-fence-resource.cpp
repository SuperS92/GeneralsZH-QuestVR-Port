// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestFenceContours.h"
#include "fence-native-fixture.h"
#include <array>
#include <cstdlib>
#include <stdexcept>
#include <iostream>
static void check(bool ok,const char *why) { if(!ok)throw std::runtime_error(why); }
int main() {
    using namespace QuestXR;
    std::array<std::vector<uint8_t>,5> data;
    std::array<uint8_t*,5> pixels{};
    std::array<unsigned,5> pitches{};
    for(unsigned l=0;l<5;++l) {
        unsigned w=64>>l;pitches[l]=w*4;data[l].assign(size_t(w)*w*4,0);pixels[l]=data[l].data();
        pixels[l][0]=200;pixels[l][1]=100;pixels[l][2]=50;pixels[l][3]=255;
    }
    const auto original=data;
    _putenv_s("GX_QUEST_FENCE_CONTOURS","0");
    IDirect3DTexture8 off;
    ApplyLoadedFenceContours(&off,pixels.data(),pitches.data(),64,64,5,"atrailings01.tga");
    CompleteFenceContourUpload(&off,true);
    check(data==original && !off.getters&&!off.setters&&!off.frees&&!IsFilteredFenceResource(&off),"Off lifecycle inert");
    _putenv_s("GX_QUEST_FENCE_CONTOURS","1");
    for(unsigned failure=0;failure<5;++failure) {
        data=original;for(unsigned l=0;l<5;++l)pixels[l]=data[l].data();
        IDirect3DTexture8 t;
        if(failure==0)t.failDesc=true;
        if(failure==1)t.desc.Format=0;
        if(failure==2)t.desc.Pool=0;
        if(failure==3)t.levels=4;
        if(failure==4)t.failSet=1;
        ApplyLoadedFenceContours(&t,pixels.data(),pitches.data(),64,64,5,"atrailings01.tga");
        check(data==original && !IsFilteredFenceResource(&t),"descriptor/marker failure cannot mutate buffers or qualify draw");
    }
    for(unsigned failure=0;failure<3;++failure) {
        data=original;for(unsigned l=0;l<5;++l)pixels[l]=data[l].data();
        IDirect3DTexture8 t;
        ApplyLoadedFenceContours(&t,pixels.data(),pitches.data(),64,64,5,"atrailings01.tga");
        check(data!=original && !IsFilteredFenceResource(&t),"prepared resource not eligible before unlock");
        if(failure==1)t.failSet=2;
        CompleteFenceContourUpload(&t,failure!=0);
        check(IsFilteredFenceResource(&t)==(failure==2),"unlock or final marker failure prevents native draw eligibility");
        if(failure!=2)check(t.marker.empty(),"failed commit removes marker");
        if(failure==2) {
            t.failGet=true;check(!IsFilteredFenceResource(&t),"failed marker getter skips");t.failGet=false;
            const auto before=FenceContourDiagnosticsJson();CompleteFenceContourUpload(&t,true);
            check(FenceContourDiagnosticsJson()==before,"upload completion cannot double count");
            t.failDesc=true;
            ApplyLoadedFenceContours(&t,pixels.data(),pitches.data(),64,64,5,"atrailings01.dds");
            check(!IsFilteredFenceResource(&t)&&t.marker.empty(),"failed reload clears former success identity");
        }
    }
    check(FenceContourDiagnosticsJson().find("\"uploads\":1")!=std::string::npos
        && FenceContourDiagnosticsJson().find("\"uploadFailures\":8")!=std::string::npos,"success/failure accounting");
    std::cout<<"PASS: fence resource Off, descriptor/marker failures, unlock ordering, failed commit/reload and identity lifetime\n";
}
