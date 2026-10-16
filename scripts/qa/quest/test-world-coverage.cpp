// SPDX-License-Identifier: GPL-3.0-or-later
#include "coverage-stubs.h"
#include "VRDevice/QuestWorldCoverage.h"
#include "VRDevice/QuestWorldCoverageInterop.h"
#include "d3d9_quest_coverage.h"
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
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void **p)override{AddRef();*p=static_cast<IDirect3DSurface8*>(this);return S_OK;}
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
struct Interop:QuestXR::WorldCoverageInterop {
 Surface *eye=nullptr,*mask=nullptr;unsigned refs=1,begins=0,ends=0;bool failed=false,reject=false;
 ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
 ULONG STDMETHODCALLTYPE Release()override{assert(refs);return --refs;}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**p)override{AddRef();*p=static_cast<QuestXR::WorldCoverageInterop*>(this);return S_OK;}
 void STDMETHODCALLTYPE GetVulkanHandles(void*,void*,void*)override{}
 void STDMETHODCALLTYPE GetSubmissionQueue(void*,void*,void*)override{}
 void STDMETHODCALLTYPE TransitionTextureLayout(void*,const void*,int,int)override{}
 void STDMETHODCALLTYPE FlushRenderingCommands()override{}
 void STDMETHODCALLTYPE LockSubmissionQueue()override{}
 void STDMETHODCALLTYPE ReleaseSubmissionQueue()override{}
 void STDMETHODCALLTYPE LockDevice()override{}
 void STDMETHODCALLTYPE UnlockDevice()override{}
 bool STDMETHODCALLTYPE WaitForResource(void*,DWORD)override{return true;}
 HRESULT STDMETHODCALLTYPE CreateImage(const void*,void**)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE DiscardCompletedEyeDepth(void*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE BeginWorldCoverage(void*e,void*m)override{
  if(reject||eye||!e||!m)return E_FAIL;++begins;eye=static_cast<Surface*>(static_cast<IDirect3DSurface8*>(e));
  mask=static_cast<Surface*>(static_cast<IDirect3DSurface8*>(m));failed=false;return S_OK;
 }
 HRESULT STDMETHODCALLTYPE EndWorldCoverage()override{++ends;bool ok=eye&&!failed;eye=mask=nullptr;return ok?S_OK:E_FAIL;}
 void fragment(unsigned pixel,unsigned policy,float alpha){
  if(policy==3){failed=true;return;}if(!policy||failed)return;
  const float a=policy==1?1:alpha;mask->rgba[pixel][3]=q(a+mask->rgba[pixel][3]*(1-a));
 }
};
struct Device:StubDirect3DDevice8 {
    Interop interop;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**p)override{interop.AddRef();*p=static_cast<QuestXR::WorldCoverageInterop*>(&interop);return S_OK;}
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
 // Exercise production begin/finish repeatedly: a single reusable mask, no per-object replay.
 for(unsigned trial=0;trial<128;++trial){
  Device d;QuestXR::WorldCoverage coverage;
  for(unsigned frame=0;frame<8;++frame){
   coverage.resetFrame();
   for(unsigned eye=0;eye<2;++eye){
    d.target=&d.eyes[eye];const auto original=d.target->rgba;const auto depth=d.depths;const auto stencil=d.stencil;
    const State state=d.state;assert(coverage.begin(&d,d.target,&d.depth,eye));assert(d.state==state&&d.blocks.empty());
    std::array<float,pixels> expected{};
    for(unsigned layer=0;layer<4;++layer)for(unsigned i=0;i<pixels;++i){
     const float alpha=q(float((trial*17+i*29+eye*31+layer*47)%256)/255);
     const unsigned mode=(i+layer)%3;
     if(depth[i]>=.5f){d.interop.fragment(i,mode,alpha);if(mode)expected[i]=q((mode==1?1:alpha)+expected[i]*(1-(mode==1?1:alpha)));}
    }
    QuestXR::FinishWorldCoverage(&d,false);assert(d.state==state&&d.target==&d.eyes[eye]);
    rgbSame(d.target->rgba,original);for(unsigned i=0;i<pixels;++i)assert(std::fabs(d.target->rgba[i][3]-expected[i])<1e-6);
    assert(d.depths==depth&&d.stencil==stencil&&d.blocks.empty()&&d.indexed==0);
    assert(coverage.stereoReady()==(eye==1));++cases;
   }
   assert(d.textures.size()==1&&d.interop.begins==d.interop.ends);++cases;
  }
  coverage.destroy();assert(d.interop.refs==1&&d.textures[0]->refs==0&&d.eyes[0].refs==1&&d.depth.refs==1);++cases;
 }
 for(unsigned failure=0;failure<7;++failure){
  Device d,other;QuestXR::WorldCoverage c;
  if(failure==0)d.failTexture=true;
  if(failure==1)d.interop.reject=true;
  const auto original=d.target->rgba;const State state=d.state;
  const bool began=c.begin(&d,d.target,&d.depth,0);
  if(failure<2)assert(!began);else{
   assert(began);
   if(failure==2)d.interop.failed=true;
   if(failure==3)d.failBlock=true;
   if(failure==4)d.failQuad=true;
   QuestXR::FinishWorldCoverage(failure==6?&other:&d,failure==5);
  }
  assert(!c.available()&&!c.stereoReady()&&d.state==state);rgbSame(d.target->rgba,original);
  c.resetFrame();assert(!c.begin(&d,d.target,&d.depth,0));c.destroy();assert(c.available()&&d.interop.refs==1);++cases;
 }
 {Device d;QuestXR::WorldCoverage c;const State state=d.state;assert(c.begin(&d,d.target,&d.depth,0));c.resetFrame();
  assert(d.interop.eye==nullptr&&d.state==state);c.destroy();++cases;}
 {Device d;const auto getters=d.getters;QuestXR::FinishWorldCoverage(&d,false);assert(d.getters==getters&&!d.quads);++cases;}
 // Execute the policy header extracted from the actual versioned backend patch.
 for(unsigned src=1;src<18;++src)for(unsigned dst=1;dst<18;++dst)for(unsigned op=1;op<=5;++op)for(unsigned mask=0;mask<16;++mask){
  const auto actual=dxvk::QuestCoveragePolicy(1,src,dst,op,mask);
  unsigned expected=3;
  if(!(mask&7))expected=0;
  else if((mask&7)!=7)expected=3;
  else if(op!=1)expected=3;
  else if((src==2||src==5)&&dst==1)expected=1;
  else if((src==2||src==5)&&dst==6)expected=2;
  else if((src==1&&dst==2)||((src==2||src==5)&&dst==2)||(src==1&&dst==3)||(src==9&&dst==1))expected=0;
  else if(src==7&&(dst==8||dst==2))expected=1;
  assert(actual==expected);++cases;
 }
 std::printf("PASS %u coverage cases: production begin/finalize, independent blend policy, bounded mask reuse, exact state/RGB/depth/stencil and failure cleanup; software model, not GPU evidence\n",cases);
}
