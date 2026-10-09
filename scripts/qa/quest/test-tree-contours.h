// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestTreeContours.h"
#include <array>
#include <limits>
static void checkTreeContours() {
    using namespace QuestXR;
    // GeneralsX @feature Codex 07/10/2026 Exact impulse and independent material/guard fixtures.
    _putenv_s("GX_QUEST_TREE_CONTOURS", "0");
    const auto dormant = TreeContourDiagnosticsJson();
    std::array<uint8_t, 103> bytes{}; bytes.fill(117);
    auto *pixels = bytes.data()+3; // Unaligned address, 3x3 with pitch padding.
    for (int y=0; y<3; ++y) for (int x=0; x<3; ++x) {
        auto *p=pixels+y*32+x*4; p[0]=9; p[1]=17; p[2]=250; p[3]=0;
    }
    auto *center=pixels+32+4; center[0]=200; center[1]=100; center[2]=50; center[3]=255;
    const auto original=bytes;
    TreeContourRegion region{0,0,3,3}; TreeContourResult result;
    require(!SmoothTreeContours(pixels,32,3,3,&region,1,false,result) && bytes==original,
        "tree contours Off is byte-exact");
    RecordTreeContourUpload(true,result); RecordTreeContourUpload(false,result);
    require(TreeContourDiagnosticsJson()==dormant, "tree contours Off counters inert");
    require(SmoothTreeContours(pixels,32,3,3,&region,1,true,result), "tree contour impulse accepted");
    const unsigned expected[3][3]={{16,32,16},{32,64,32},{16,32,16}};
    for (int y=0; y<3; ++y) for (int x=0; x<3; ++x) {
        const auto *p=pixels+y*32+x*4;
        require(p[3]==expected[y][x] && p[0]==200 && p[1]==100 && p[2]==50,
            "exact binomial alpha; no transparent garbage/dark halo in RGB");
    }
    for (size_t i=0;i<bytes.size();++i) {
        const bool pixel=i>=3 && i<3+96 && (i-3)%32<12;
        if (!pixel) require(bytes[i]==original[i], "unaligned image padding and guards untouched");
    }
    require(result.regions==1 && result.changedPixels==9 && result.partialBefore==0 && result.partialAfter==9,
        "thin opaque feature becomes nine fractional texels");
    _putenv_s("GX_QUEST_TREE_CONTOURS", "1");
    RecordTreeContourUpload(true,result);

    // Two adjacent rectangles at nonzero origins; no samples across material boundary.
    std::array<uint8_t,256> atlas{}; atlas.fill(71);
    TreeContourRegion pair[2]={{1,1,2,2},{3,1,2,2}};
    for (unsigned i=0;i<2;++i) for(unsigned y=1;y<3;++y) for(unsigned x=pair[i].x;x<pair[i].x+2;++x) {
        auto *p=atlas.data()+y*32+x*4; p[0]=i?240:20; p[1]=i?5:220; p[2]=17; p[3]=i?127:255;
    }
    const auto uniform=atlas;
    require(SmoothTreeContours(atlas.data(),32,6,4,pair,2,true,result) && atlas==uniform,
        "uniform opaque/fractional materials identity, adjacent colors isolated, gaps preserved");
    // Whole 128px material: kernel crosses the internal 64px source-tile boundary.
    std::array<uint8_t,512> strip{};
    for(unsigned x=0;x<128;++x) { strip[x*4]=x<64?0:200; strip[x*4+3]=255; }
    TreeContourRegion wide{0,0,128,1};
    require(SmoothTreeContours(strip.data(),512,128,1,&wide,1,true,result), "whole material filter");
    require(strip[63*4]==50 && strip[64*4]==150 && strip[62*4]==0 && strip[65*4]==200,
        "opaque RGB exact kernel, no artificial internal tile boundary");
    std::array<uint8_t,4> single{2,90,250,13}; const auto singleOriginal=single;
    TreeContourRegion one{0,0,1,1};
    require(SmoothTreeContours(single.data(),4,1,1,&one,1,true,result) && single==singleOriginal,
        "collapsed axes clamp without changing a 1x1 material");

    const auto intact=atlas;
    auto reject=[&](const TreeContourRegion *regions,unsigned count,unsigned pitch=32,unsigned width=6,unsigned height=4) {
        result.regions=42;
        require(!SmoothTreeContours(atlas.data(),pitch,width,height,regions,count,true,result)
            && atlas==intact && result.regions==0, "invalid request rejected before any write");
    };
    TreeContourRegion late[2]={{1,1,2,2},{5,0,2,1}}; reject(late,2);
    TreeContourRegion overlap[2]={{1,1,2,2},{2,1,2,2}}; reject(overlap,2);
    TreeContourRegion empty{1,1,0,1}; reject(&empty,1);
    TreeContourRegion huge{0,0,UINT32_MAX,1}; reject(&huge,1);
    reject(nullptr,1); reject(pair,0); reject(pair,65); reject(pair,2,23);
    reject(pair,2,65537); reject(pair,2,32,2049); reject(pair,2,32,6,0);
    auto *invalid=reinterpret_cast<uint8_t *>(UINTPTR_MAX-2);
    require(!SmoothTreeContours(invalid,4,1,1,&one,1,true,result), "address span overflow guard");
    require(!SmoothTreeContours(nullptr,4,1,1,&one,1,true,result), "null pixel guard");
    // Leave actual successful impulse counters for capture JSON round-trip, with one deliberate failure.
    RecordTreeContourUpload(false,result);
}
