// SPDX-License-Identifier: GPL-3.0-or-later
#include "coverage-stubs.h"
#include "VRDevice/QuestWorldCoverage.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <vector>

namespace {
constexpr unsigned width=4,height=3,pixels=width*height;
using Pixel=std::array<float,4>;
float q(float x){return std::round(std::fmax(0,std::fmin(1,x))*255)/255;}
struct Surface:StubDirect3DSurface8 {
    D3DSURFACE_DESC desc{};std::array<Pixel,pixels> rgba{};unsigned refs=1;
    Surface(){desc.Width=width;desc.Height=height;desc.Format=D3DFMT_A8R8G8B8;desc.MultiSampleType=D3DMULTISAMPLE_NONE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{assert(refs);return --refs;}
    HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC *p)override{*p=desc;return S_OK;}
};
struct Texture:StubDirect3DTexture8 {
    Surface surface;unsigned refs=1;
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{assert(refs);if(--refs==0)surface.Release();return refs;}
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT level,IDirect3DSurface8 **out)override{
        assert(!level);surface.AddRef();*out=&surface;return S_OK;
    }
};
struct State {
    std::array<DWORD,256> rs{};std::array<std::array<DWORD,32>,8> ts{};
    IDirect3DBaseTexture8 *texture=nullptr;DWORD vertex=192,pixel=38,stream=81;
    D3DVIEWPORT8 viewport{0,0,width,height,0,1};
    bool operator==(const State &o)const{return rs==o.rs&&ts==o.ts&&texture==o.texture&&vertex==o.vertex&&pixel==o.pixel
        &&stream==o.stream&&viewport.X==o.viewport.X&&viewport.Y==o.viewport.Y&&viewport.Width==o.viewport.Width
        &&viewport.Height==o.viewport.Height&&viewport.MinZ==o.viewport.MinZ&&viewport.MaxZ==o.viewport.MaxZ;}
};
struct Device:StubDirect3DDevice8 {
    State state;Surface eyes[2],depth,other;Surface *target=&eyes[0],*z=&depth;
    std::array<float,pixels> depths{},fragment{};std::array<unsigned,pixels> stencil{};
    std::vector<std::unique_ptr<Texture>> textures;std::map<DWORD,State> blocks;
    unsigned next=1,getters=0,indexed=0,quads=0;bool failBlock=false,failTexture=false,failGet=false,failNative=false,failQuad=false;
    Device(){
        auto &r=state.rs;r[D3DRS_ALPHABLENDENABLE]=1;r[D3DRS_SRCBLEND]=D3DBLEND_SRCALPHA;
        r[D3DRS_DESTBLEND]=D3DBLEND_INVSRCALPHA;r[D3DRS_BLENDOP]=D3DBLENDOP_ADD;r[D3DRS_COLORWRITEENABLE]=7;
        r[D3DRS_ZENABLE]=1;r[D3DRS_ZFUNC]=D3DCMP_LESSEQUAL;r[D3DRS_STENCILWRITEMASK]=0xffffffff;
        r[D3DRS_STENCILFAIL]=r[D3DRS_STENCILZFAIL]=r[D3DRS_STENCILPASS]=D3DSTENCILOP_KEEP;
        r[206]=0;r[207]=5;r[208]=6;r[209]=1;
        for(unsigned i=0;i<pixels;++i){depths[i]=i%3==0?.25f:1;fragment[i]=float(i+1)/(pixels+1);stencil[i]=i*17;
            for(unsigned e=0;e<2;++e)eyes[e].rgba[i]={q(.1f+e*.2f),q(.3f+i*.01f),q(.7f),q(.13f+i*.03f)};}
    }
    HRESULT STDMETHODCALLTYPE GetRenderTarget(IDirect3DSurface8 **p)override{++getters;target->AddRef();*p=target;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface8 **p)override{++getters;z->AddRef();*p=z;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetRenderTarget(IDirect3DSurface8 *c,IDirect3DSurface8 *d)override{
        target=static_cast<Surface*>(c);z=static_cast<Surface*>(d);state.viewport={0,0,width,height,0,1};return S_OK;}
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT8 *p)override{++getters;*p=state.viewport;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT8 *p)override{state.viewport=*p;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE r,DWORD *v)override{++getters;if(failGet)return E_FAIL;*v=state.rs.at(r);return S_OK;}
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE r,DWORD v)override{state.rs.at(r)=v;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD stage,D3DTEXTURESTAGESTATETYPE t,DWORD v)override{state.ts.at(stage).at(t)=v;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage,IDirect3DBaseTexture8 *t)override{assert(!stage);state.texture=t;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetPixelShader(DWORD v)override{state.pixel=v;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetVertexShader(DWORD v)override{state.vertex=v;return S_OK;}
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE type,DWORD *p)override{
        assert(type==D3DSBT_ALL);if(failBlock)return E_FAIL;*p=next++;blocks[*p]=state;return S_OK;}
    HRESULT STDMETHODCALLTYPE ApplyStateBlock(DWORD token)override{
        const std::array<DWORD,4> separate={state.rs[206],state.rs[207],state.rs[208],state.rs[209]};
        state=blocks.at(token);for(unsigned i=0;i<4;++i)state.rs[206+i]=separate[i];return S_OK;}
    HRESULT STDMETHODCALLTYPE DeleteStateBlock(DWORD token)override{assert(blocks.erase(token)==1);return S_OK;}
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture8 **p)override{
        assert(w==width&&h==height&&levels==1&&usage==D3DUSAGE_RENDERTARGET&&format==D3DFMT_A8R8G8B8&&pool==D3DPOOL_DEFAULT);
        if(failTexture)return E_FAIL;textures.emplace_back(new Texture);*p=textures.back().get();return S_OK;}
    HRESULT STDMETHODCALLTYPE Clear(DWORD,const D3DRECT *,DWORD flags,D3DCOLOR color,float,DWORD)override{
        assert(flags==D3DCLEAR_TARGET&&color==0&&target!=&eyes[0]&&target!=&eyes[1]);
        for(auto &p:target->rgba)p={0,0,0,0};return S_OK;}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE,UINT,UINT,UINT,UINT)override{
        if(failNative)return E_FAIL;
        ++indexed;assert(target!=&eyes[0]&&target!=&eyes[1]);
        assert(state.rs[D3DRS_COLORWRITEENABLE]==8&&state.rs[D3DRS_ZWRITEENABLE]==0&&state.rs[D3DRS_STENCILWRITEMASK]==0);
        assert(state.rs[206]&&state.rs[207]==D3DBLEND_ONE&&state.rs[208]==D3DBLEND_INVSRCALPHA&&state.rs[209]==D3DBLENDOP_ADD);
        for(unsigned i=0;i<pixels;++i){
            if(state.rs[D3DRS_ZENABLE]&&.5f>depths[i])continue;
            const float a=state.ts[0][D3DTSS_ALPHAARG1]==D3DTA_TFACTOR?1:fragment[i];
            if(state.rs[D3DRS_ALPHATESTENABLE]&&a*255<state.rs[D3DRS_ALPHAREF])continue;
            target->rgba[i][3]=q(a+target->rgba[i][3]*(1-a));
        }return S_OK;}
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE p,UINT f,UINT n)override{return DrawIndexedPrimitive(p,f,0,0,n);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE primitive,UINT count,const void *data,UINT stride)override{
        if(failQuad)return E_FAIL;
        ++quads;assert(primitive==D3DPT_TRIANGLESTRIP&&count==2&&stride==28&&(target==&eyes[0]||target==&eyes[1]));
        assert(state.rs[D3DRS_COLORWRITEENABLE]==8&&!state.rs[D3DRS_ZWRITEENABLE]&&!state.rs[D3DRS_STENCILENABLE]);
        struct Vertex{float x,y,z,rhw;DWORD color;float u,v;};auto v=static_cast<const Vertex*>(data);
        assert(v[0].x==-.5f&&v[0].y==-.5f&&v[3].x==float(width)-.5f&&v[3].y==float(height)-.5f&&v[0].z==1);
        for(unsigned i=0;i<pixels;++i){
            if(state.rs[D3DRS_ZENABLE]){assert(state.rs[D3DRS_ZFUNC]==D3DCMP_GREATER);if(depths[i]<1)target->rgba[i][3]=1;}
            else {auto texture=static_cast<Texture*>(state.texture);assert(texture);target->rgba[i][3]=texture->surface.rgba[i][3];}
        }state.stream=0;return S_OK;}
};
void rgbSame(const std::array<Pixel,pixels> &a,const std::array<Pixel,pixels> &b){for(unsigned i=0;i<pixels;++i)for(unsigned c=0;c<3;++c)assert(a[i][c]==b[i][c]);}
}
int main(){
    unsigned cases=0;
    for(unsigned trial=0;trial<64;++trial){
        Device d;QuestXR::WorldCoverage coverage;
        for(unsigned eye=0;eye<2;++eye){
            d.target=&d.eyes[eye];const auto original=d.target->rgba;const auto depth=d.depths;const auto stencil=d.stencil;
            const State state=d.state;assert(coverage.begin(&d,d.target,&d.depth,eye));assert(d.state==state);++cases;
            std::array<float,pixels> expected{};
            for(unsigned layer=0;layer<3;++layer){
                for(unsigned i=0;i<pixels;++i){d.fragment[i]=q(float((trial*17+i*29+eye*31+layer*47)%256)/255);
                    if(depth[i]>=.5f)expected[i]=q(d.fragment[i]+expected[i]*(1-d.fragment[i]));}
                QuestXR::CaptureWorldCoverageDraw(&d,D3DPT_TRIANGLELIST,0,4,0,2);
                assert(d.state==state&&d.target==&d.eyes[eye]&&d.z==&d.depth&&d.depths==depth&&d.stencil==stencil);
                assert(d.target->rgba==original);++cases;
            }
            // An offscreen water reflection must not contaminate or invalidate eye coverage.
            d.target=&d.other;d.state.rs[D3DRS_SRCBLEND]=14;
            QuestXR::CaptureWorldCoverageDraw(&d,D3DPT_TRIANGLESTRIP,0,4,0,2);d.target=&d.eyes[eye];d.state=state;
            QuestXR::FinishWorldCoverage(&d,false);assert(d.state==state);rgbSame(d.target->rgba,original);
            for(unsigned i=0;i<pixels;++i)assert(std::fabs(d.target->rgba[i][3]-(depth[i]<1?1:expected[i]))<1e-6);
            assert(d.depths==depth&&d.stencil==stencil);assert(coverage.stereoReady()==(eye==1));++cases;
        }coverage.destroy();assert(d.blocks.empty());++cases;
    }
    for(unsigned kind=0;kind<8;++kind){
        Device d;QuestXR::WorldCoverage c;const auto original=d.target->rgba;assert(c.begin(&d,d.target,&d.depth,0));
        if(kind==0){d.state.rs[D3DRS_SRCBLEND]=D3DBLEND_ONE;d.state.rs[D3DRS_DESTBLEND]=D3DBLEND_ONE;}
        if(kind==1){d.state.rs[D3DRS_SRCBLEND]=D3DBLEND_DESTALPHA;d.state.rs[D3DRS_DESTBLEND]=D3DBLEND_INVDESTALPHA;}
        if(kind==2){d.state.rs[D3DRS_ZWRITEENABLE]=1;d.state.rs[D3DRS_ALPHABLENDENABLE]=0;}
        if(kind==3){d.state.rs[D3DRS_SRCBLEND]=14;}
        if(kind==4){d.state.rs[D3DRS_STENCILENABLE]=1;d.state.rs[D3DRS_STENCILPASS]=D3DSTENCILOP_INCR;}
        if(kind==5){d.state.rs[D3DRS_COLORWRITEENABLE]=0;}
        if(kind==7){d.state.rs[D3DRS_SRCBLEND]=D3DBLEND_ONE;d.state.rs[D3DRS_DESTBLEND]=D3DBLEND_ZERO;}
        const State state=d.state;
        if(kind==6)QuestXR::CaptureWorldCoveragePrimitive(&d,D3DPT_POINTLIST,0,4);
        else QuestXR::CaptureWorldCoverageDraw(&d,D3DPT_TRIANGLELIST,0,4,0,2);
        assert(d.state==state&&d.target->rgba==original);QuestXR::FinishWorldCoverage(&d,false);assert(d.state==state);rgbSame(d.target->rgba,original);
        if(kind==3||kind==4)assert(d.target->rgba==original);
        else for(unsigned i=0;i<pixels;++i){
            const float expected=d.depths[i]<1?1:(kind==1||kind==7)?1:kind==6?q(d.fragment[i]):0;
            assert(std::fabs(d.target->rgba[i][3]-expected)<1e-6);
        }c.destroy();++cases;
    }
    for(unsigned fault=0;fault<3;++fault){
        Device d;QuestXR::WorldCoverage c;d.failTexture=fault==0;d.failBlock=fault==1;
        if(fault==2)d.depth.desc.MultiSampleType=D3DMULTISAMPLE_2_SAMPLES;
        const State state=d.state;const auto original=d.target->rgba;
        assert(!c.begin(&d,d.target,&d.depth,0));assert(d.state==state&&d.target->rgba==original&&!c.stereoReady());c.destroy();++cases;
    }
    for(unsigned fault=0;fault<5;++fault){
        Device d,other;QuestXR::WorldCoverage c;const auto original=d.target->rgba;const State state=d.state;
        assert(c.begin(&d,d.target,&d.depth,0));d.failGet=fault==0;d.failNative=fault==1;d.failQuad=fault==2;
        QuestXR::CaptureWorldCoverageDraw(&d,4,0,4,0,2);
        QuestXR::FinishWorldCoverage(fault==4?&other:&d,fault==3);
        assert(!c.stereoReady()&&!c.available()&&d.state==state);rgbSame(d.target->rgba,original);
        c.resetFrame();d.failGet=d.failNative=d.failQuad=false;
        const auto before=d.getters;assert(!c.begin(&d,d.target,&d.depth,0)&&before==d.getters);
        c.destroy();assert(c.available()&&d.blocks.empty());++cases;
    }
    {Device d;const auto getters=d.getters;QuestXR::CaptureWorldCoverageDraw(&d,4,0,4,0,2);QuestXR::FinishWorldCoverage(&d,false);
        assert(d.getters==getters&&!d.indexed&&!d.quads);++cases;}
    for(unsigned src=1;src<18;++src)for(unsigned dst=1;dst<18;++dst){
        assert(QuestXR::ClassifyCoverage(0,src,dst,1,7)==QuestXR::CoverageDraw::Skip);
        assert(QuestXR::ClassifyCoverage(1,src,dst,1,0)==QuestXR::CoverageDraw::Skip);cases+=2;
    }
    std::printf("PASS %u coverage cases: production D3D8 calls, raster model, alpha/depth/stencil isolation, state restoration and fallbacks\n",cases);
}
