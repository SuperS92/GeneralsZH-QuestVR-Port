// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestTerrainFarMips.h"
#include <limits>
#include <vector>
static void checkTerrainFarFilter() {
    using namespace QuestXR;
    _putenv_s("GX_QUEST_TERRAIN_FAR_FILTER", "");
    require(!TerrainFarFilterRequested(), "far filter default off");
    _putenv_s("GX_QUEST_TERRAIN_FAR_FILTER", "1junk");
    require(!TerrainFarFilterRequested(), "far filter strict request value");
    _putenv_s("GX_QUEST_TERRAIN_FAR_FILTER", "1");
    require(TerrainFarFilterRequested(), "far filter request");
    _putenv_s("GX_QUEST_TERRAIN_FAR_FILTER", "0");
    const TerrainFarMaterial m[]={{4,4,64,0,1},{76,4,64,1,1},{4,76,128,2,4}};
    constexpr int pitch=518, offset=3, height=32;
    std::vector<uint8_t> bytes(offset+pitch*height+7,0xab);
    auto put=[&](int x,int y,uint16_t p) { std::memcpy(bytes.data()+offset+y*pitch+2*x,&p,2); };
    auto get=[&](int x,int y) { uint16_t p;std::memcpy(&p,bytes.data()+offset+y*pitch+2*x,2);return p; };
    for(int y=0;y<height;++y) for(int x=0;x<256;++x) put(x,y,0x8c63);
    for(int y=0;y<=8;++y) for(int x=0;x<=8;++x) put(x,y,(x%8==0 && y%8==0)?0xffff:0x8000);
    for(int y=0;y<=8;++y) for(int x=9;x<=17;++x) put(x,y,0xffff);
    for(int y=0;y<=16;++y) for(int x=0;x<=16;++x) put(x,9+y,x%16<8?0x8000:0xffff);
    const auto original=bytes;
    TerrainFarFilterResult result{};
    require(FilterTerrainFarMip(bytes.data()+offset,pitch,2048,256,m,3,result),"gentle pitched unaligned LOD3 filter");
    require(result.materials==3 && result.skippedAlpha==0 && result.changedPixels>0,"actual filter result counts");
    auto color=[](int v) { return uint16_t(0x8000|v|(v<<5)|(v<<10)); };
    require(get(0,0)==color(19) && get(1,0)==color(2) && get(1,1)==color(1),"exact center/axial/diagonal gentle impulse RGB channels");
    require(get(7,0)==color(2) && get(7,7)==color(1),"periodic opposite edges participate");
    require(get(7,14)==color(4) && get(8,14)==color(27),"whole material filtering crosses internal 64px tile boundary");
    for(int y=0;y<=8;++y) for(int x=9;x<=17;++x) require(get(x,y)==0xffff,"neighbor material stays white, no atlas contamination");
    for(const auto &a:m) {
        const int x=(a.x-4)/8,y=(a.y-4)/8,n=a.size/8;
        for(int p=0;p<=n;++p) require(get(x,y+p)==get(x+n,y+p) && get(x+p,y)==get(x+p,y+n),"duplicated periodic endpoints stay exact");
    }
    for(size_t p=0;p<bytes.size();++p) {
        const int rel=int(p)-offset,y=rel/pitch,x=(rel%pitch)/2;
        const bool inPixels=rel>=0 && y<height && rel%pitch<512;
        bool inMaterial=false;
        if(inPixels) for(const auto &a:m) {
            const int ax=(a.x-4)/8,ay=(a.y-4)/8,n=a.size/8;
            if(x>=ax && x<=ax+n && y>=ay && y<=ay+n) inMaterial=true;
        }
        if(!inMaterial) require(bytes[p]==original[p],"unused atlas/pitch padding/guards byte exact");
    }
    bytes=original;
    put(9+3,3,0x7fff); // Transparent material is skipped, not recolored or thickened.
    const auto alphaOriginal=bytes;
    require(FilterTerrainFarMip(bytes.data()+offset,pitch,2048,256,m,3,result)
        && result.materials==2 && result.skippedAlpha==1,"nonopaque material skipped");
    for(int y=0;y<=8;++y) for(int x=9;x<=17;++x) {
        const size_t p=offset+y*pitch+2*x;
        require(bytes[p]==alphaOriginal[p] && bytes[p+1]==alphaOriginal[p+1],"alpha material RGB and A1 exact");
    }
    bytes=original; put(17,4,0x8000);
    require(FilterTerrainFarMip(bytes.data()+offset,pitch,2048,256,m,3,result)
        && result.materials==3 && result.unequalEndpoints==1 && result.failureReason==0,
        "unequal opaque mip endpoints no longer reject the entire filter");
    require(get(9,4)==color(22) && get(17,4)==color(22),"exact equal-weight RGB endpoint canonicalization before gentle filter");
    bytes=original; put(17,8,0x8000);
    require(FilterTerrainFarMip(bytes.data()+offset,pitch,2048,256,m,3,result)
        && get(9,0)==color(26) && get(17,0)==color(26)
        && get(9,8)==color(26) && get(17,8)==color(26),"all four differing corner representatives averaged symmetrically");
    bytes=original; put(17,4,0x7fff);const auto edgeAlpha=bytes;
    require(FilterTerrainFarMip(bytes.data()+offset,pitch,2048,256,m,3,result)
        && result.skippedAlpha==1,"transparent endpoint skips whole material before RGB canonicalization");
    for(int y=0;y<=8;++y) for(int x=9;x<=17;++x) {
        const size_t p=offset+y*pitch+2*x;
        require(bytes[p]==edgeAlpha[p] && bytes[p+1]==edgeAlpha[p+1],"transparent endpoint material exact identity");
    }
    bytes=original;
    require(!FilterTerrainFarMip(bytes.data()+offset,510,2048,256,m,3,result)
        && !FilterTerrainFarMip(bytes.data()+offset,pitch,1024,256,m,3,result)
        && !FilterTerrainFarMip(bytes.data()+offset,pitch,2048,256,m,257,result)
        && !FilterTerrainFarMip(nullptr,pitch,2048,256,m,3,result)
        && bytes==original,"bad stride/dimension/count/null preserves original fourth mip");
    TerrainFarMaterial overlap[]={m[0],m[0]};
    require(!FilterTerrainFarMip(bytes.data()+offset,pitch,2048,256,overlap,2,result)
        && bytes==original && result.failureReason==1,"overlap rejected before writes, named layout failure");
    require(!FilterTerrainFarMip(bytes.data()+offset,510,2048,256,m,3,result)
        && bytes==original && result.failureReason==2,"named buffer failure without writes");
    for(int y=0;y<=8;++y) for(int x=0;x<=8;++x) put(x,y,0x9123);
    const auto constant=bytes;
    require(FilterTerrainFarMip(bytes.data()+offset,pitch,2048,256,m,1,result)
        && bytes==constant && result.changedPixels==0,"constant material exact identity");
}
static void checkTerrainFarMips() {
    checkTerrainFarFilter();
    using namespace QuestXR;
    _putenv_s("GX_QUEST_TERRAIN_FAR_MIPS", "0");
    require(!TerrainFarMipsRequested(), "far mip default/off");
    _putenv_s("GX_QUEST_TERRAIN_FAR_MIPS", "1junk");
    require(!TerrainFarMipsRequested(), "far mip strict environment value");
    _putenv_s("GX_QUEST_TERRAIN_FAR_MIPS", "1");
    require(TerrainFarMipsRequested(), "far mip requested independent of sampler eligibility");
    _putenv_s("GX_QUEST_TERRAIN_FAR_MIPS", "0");
    TerrainFarMaterial m[] = {{4,4,64,0,1}, {76,4,64,1,1}};
    require(ValidateTerrainFarMaterials(m,2,2048,128), "adjacent periodic materials accepted");
    for (int column=0; column<28; ++column) for (int width=1; width<=28-column; ++width) {
        TerrainFarMaterial material{4+72*column,4,64*width,0,width*width};
        require(ValidateTerrainFarMaterials(&material,1,2048,2048), "all packed ordinary widths");
        const int first = (material.x - 4)/8;
        const int last = (material.x + material.size - 4)/8;
        require(first*8 >= material.x-4 && (last+1)*8 <= material.x+material.size+4,
            "LOD3 bilinear endpoint box footprints remain inside periodic gutters");
    }
    const auto original=m[1];
    m[1].x=68; require(!ValidateTerrainFarMaterials(m,2,2048,128), "bad alignment rejected");
    m[1]=m[0]; require(!ValidateTerrainFarMaterials(m,2,2048,128), "overlapping material rejected");
    m[1]=original; m[1].firstTile=0;
    require(!ValidateTerrainFarMaterials(m,2,2048,128), "ambiguous source-tile ownership rejected");
    m[1]=original; m[1].size=2048;
    require(!ValidateTerrainFarMaterials(m,2,2048,128), "out-of-atlas material rejected");
    m[1]=original;
    require(!ValidateTerrainFarMaterials(nullptr,1,2048,128)
        && !ValidateTerrainFarMaterials(m,257,2048,128)
        && !ValidateTerrainFarMaterials(m,2,1024,128)
        && !ValidateTerrainFarMaterials(m,2,2048,96), "count/dimension guard rejection");
    float uv[]={0,0, 0,-.03125f, .03125f,-.03125f, .03125f,0};
    require(ValidateTerrainFarCliff(m,2,0,uv) && ValidateTerrainFarCliff(m,2,7,uv),
        "relative cliff endpoints and quadrant indices");
    uv[0]=-.000001f; require(!ValidateTerrainFarCliff(m,2,0,uv), "cliff beyond gutter-safe material rejected");
    uv[0]=0; uv[1]=.000001f; require(!ValidateTerrainFarCliff(m,2,0,uv), "cliff above material rejected");
    uv[1]=std::numeric_limits<float>::quiet_NaN();
    require(!ValidateTerrainFarCliff(m,2,0,uv), "nonfinite cliff rejected");
    uv[1]=0; require(!ValidateTerrainFarCliff(m,2,8,uv), "missing cliff material rejected");
    float qu[]={4.f/2048,68.f/2048,68.f/2048,4.f/2048};
    float qv[]={4.f/128,4.f/128,68.f/128,68.f/128};
    require(ValidateTerrainFarQuad(m,2,128,qu,qv),"actual normalized quad bounds accepted");
    qu[1]=140.f/2048;
    require(!ValidateTerrainFarQuad(m,2,128,qu,qv),"quad crossing two materials rejected");
    qu[1]=68.f/2048;qu[0]=0;
    require(!ValidateTerrainFarQuad(m,2,128,qu,qv),"legacy stretch outside material rejected");
    qu[0]=std::numeric_limits<float>::infinity();
    require(!ValidateTerrainFarQuad(m,2,128,qu,qv),"nonfinite emitted quad rejected");
    std::array<uint8_t,64> source, target;
    source.fill(0xcd); target.fill(0xef);
    // Four contrasting opaque corners in every 2x2 block average to16/16/16.
    for(int y=0;y<4;++y) for(int x=0;x<4;++x) {
        const uint16_t p = (x+y)%2 ? 0xffff : 0x8000;
        std::memcpy(source.data()+1+y*12+x*2,&p,2);
    }
    const auto saved=source;
    require(DownsampleTerrainFarMip(source.data()+1,12,target.data()+3,10,4,4), "pitched unaligned downsample");
    const uint16_t expected=0xc210;
    for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
        uint16_t p;std::memcpy(&p,target.data()+3+y*10+x*2,2);
        require(p==expected,"exact box-average A1R5G5B5 channels/opaque alpha");
    }
    require(source==saved,"source levels byte-exact after sampling");
    for(size_t i=0;i<target.size();++i)
        if(!((i>=3 && i<7)||(i>=13 && i<17))) require(target[i]==0xef,"target pitch padding and guards unchanged");
    const auto after=target;
    require(!DownsampleTerrainFarMip(source.data(),4,target.data(),10,4,4)
        && !DownsampleTerrainFarMip(source.data(),12,target.data(),2,4,4)
        && !DownsampleTerrainFarMip(source.data(),12,target.data(),10,3,4)
        && !DownsampleTerrainFarMip(source.data(),12,source.data()+2,12,4,4)
        && !DownsampleTerrainFarMip(nullptr,12,target.data(),10,4,4),"pitch/odd/alias/null rejection");
    require(target==after,"rejection performs no writes");
    // Two reduced adjacent material blocks do not average their interior colors.
    uint16_t strip[]={0x8000,0x8000,0xffff,0xffff,0x8000,0x8000,0xffff,0xffff};
    uint16_t reduced[2]={};
    require(DownsampleTerrainFarMip(strip,8,reduced,4,4,2)
        && reduced[0]==0x8000 && reduced[1]==0xffff,"aligned material isolation");
}
