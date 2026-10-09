// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestFenceContours.h"
#include <array>
#include <limits>
static void checkFenceContours() {
    using namespace QuestXR;
    _putenv_s("GX_QUEST_FENCE_CONTOURS","0");
    const auto dormant = FenceContourDiagnosticsJson();
    require(IsFenceContourName("dir/ATrailings01.TGA") && IsFenceContourName("dir\\ATrailings01.dds")
        && !IsFenceContourName("atrailings02.tga") && !IsFenceContourName("atrailings01.tga.bak")
        && !IsFenceContourName("pmtree01.tga") && !IsFenceContourName(nullptr),"exact fence basename only");
    require(!UseFenceContours("atrailings01.tga"),"fence Off name gate inert");
    std::array<std::vector<uint8_t>,5> bytes;
    std::array<uint8_t*,5> pixels{};
    std::array<unsigned,5> pitches{};
    for (unsigned l=0;l<5;++l) {
        const unsigned w=64>>l; pitches[l]=w*4+11;
        bytes[l].assign(size_t(pitches[l])*w+7,117);pixels[l]=bytes[l].data()+3;
        for (unsigned y=0;y<w;++y) for (unsigned x=0;x<w;++x) {
            auto *p=pixels[l]+size_t(y)*pitches[l]+x*4;
            p[0]=7;p[1]=249;p[2]=81;p[3]=0;
        }
        // At left periodic boundary. Adjacent row is a different packed pattern.
        auto *p=pixels[l]+pitches[l];p[0]=200;p[1]=100;p[2]=50;p[3]=255;
        for (unsigned x=0;x<w;++x) {
            auto *q=pixels[l]+size_t(2)*pitches[l]+x*4;q[0]=5;q[1]=210;q[2]=70;q[3]=127;
        }
    }
    const auto original=bytes;
    std::vector<std::vector<uint8_t>> output; FenceContourResult result;
    require(!PrepareFenceContours(pixels.data(),pitches.data(),64,64,5,false,output,result)
        && bytes==original && output.empty(),"fence Off reads/writes no pixels");
    RecordFenceContourDraw(0,true,false);RecordFenceContourDraw(1,false,true);
    require(FenceContourDiagnosticsJson()==dormant,"fence Off counters inert");
    require(PrepareFenceContours(pixels.data(),pitches.data(),64,64,5,true,output,result),"audited resource accepted");
    require(bytes==original && result.levels==5 && result.changedPixels==15
        && result.partialAfter==result.partialBefore+15,"prepared filter transactional with unchanged inputs/guards");
    for (unsigned l=0;l<5;++l) {
        const unsigned w=64>>l;
        for (unsigned x=0;x<w;++x) {
            const auto *p=output[l].data()+(w+x)*4;
            const unsigned a=x==0?128:(x==1||x==w-1)?64:0;
            require(p[3]==a,"periodic 1-2-1 impulse at every existing mip");
            if(a) require(p[0]==200&&p[1]==100&&p[2]==50,"alpha-weighted RGB excludes transparent garbage");
            const auto *q=output[l].data()+(2*w+x)*4;
            require(q[0]==5&&q[1]==210&&q[2]==70&&q[3]==127,"packed neighbor row fractional uniform identity");
            require(output[l][x*4+3]==0,"no vertical taps between packed patterns");
        }
    }
    auto invalid=pixels;invalid[4]=nullptr;
    require(!PrepareFenceContours(invalid.data(),pitches.data(),64,64,5,true,output,result)
        && output.empty() && bytes==original && !result.levels,"invalid last buffer commits nothing");
    invalid=pixels;invalid[4]=pixels[0]+1;
    require(!PrepareFenceContours(invalid.data(),pitches.data(),64,64,5,true,output,result),"overlapping level rejected");
    invalid=pixels;invalid[4]=reinterpret_cast<uint8_t*>(UINTPTR_MAX-1);
    require(!PrepareFenceContours(invalid.data(),pitches.data(),64,64,5,true,output,result),"address overflow rejected before dereference");
    auto badPitch=pitches;badPitch[4]=15;
    require(!PrepareFenceContours(pixels.data(),badPitch.data(),64,64,5,true,output,result),"late short pitch rejected");
    badPitch=pitches;badPitch[4]=65537;
    require(!PrepareFenceContours(pixels.data(),badPitch.data(),64,64,5,true,output,result),"oversized pitch rejected");
    for(unsigned dim:{0u,32u,256u}) require(!PrepareFenceContours(pixels.data(),pitches.data(),dim,dim,5,true,output,result),"unknown layout rejected");
    require(!PrepareFenceContours(pixels.data(),pitches.data(),64,128,5,true,output,result)
        && !PrepareFenceContours(pixels.data(),pitches.data(),64,64,1,true,output,result)
        && !PrepareFenceContours(pixels.data(),pitches.data(),64,64,6,true,output,result)
        && !PrepareFenceContours(nullptr,pitches.data(),64,64,5,true,output,result),"shape/level count/null rejected");
    std::array<std::vector<uint8_t>,6> full;
    std::array<uint8_t*,6> fullPixels{};std::array<unsigned,6> fullPitches{};
    for(unsigned l=0;l<6;++l) {
        const unsigned w=128>>l;fullPitches[l]=w*4;
        full[l].assign(size_t(w)*w*4,255);fullPixels[l]=full[l].data();
    }
    require(PrepareFenceContours(fullPixels.data(),fullPitches.data(),128,128,6,true,output,result)
        && !result.changedPixels && result.levels==6,"unreduced opaque uniform resource identity");
    for(unsigned l=0;l<6;++l)require(output[l]==full[l],"all supported full-size mip levels preserved for uniform input");
    _putenv_s("GX_QUEST_FENCE_CONTOURS","true");require(!FenceContoursRequested(),"strict environment gate");
    _putenv_s("GX_QUEST_FENCE_CONTOURS","1");require(UseFenceContours("atrailings01.tga"),"On selects exact fence");
    RecordFenceContourDraw(0,true,false);RecordFenceContourDraw(1,false,false);RecordFenceContourDraw(1,false,true);
    RecordFenceContourDraw(-1,true,false);RecordFenceContourDraw(2,true,false);
    require(FenceContourDiagnosticsJson().find("\"eyeDraws\":[1,0]")!=std::string::npos
        && FenceContourDiagnosticsJson().find("\"eyeFailures\":[0,1]")!=std::string::npos
        && FenceContourDiagnosticsJson().find("\"eyeSkips\":[0,1]")!=std::string::npos,"independent bounded eye counters");
}
