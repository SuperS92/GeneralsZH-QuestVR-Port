// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestWorldCoverage.h"
#include "VRDevice/QuestWorldCoverageInterop.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <d3d8.h>
#include <android/log.h>
#include <array>
#include <utility>
namespace QuestXR {
namespace {
thread_local WorldCoverage *active=nullptr;
constexpr const char *tag="GeneralsXR";
static_assert(D3DBLEND_ONE==2&&D3DBLEND_SRCALPHA==5&&D3DBLEND_INVSRCALPHA==6
    &&D3DBLEND_DESTALPHA==7&&D3DBLEND_INVDESTALPHA==8&&D3DBLENDOP_ADD==1,"Coverage policy D3D constants");
// Pinned DXVK D3D8 forwards these D3D9 render states unchanged. Save them explicitly:
// the D3D8 ALL state block does not promise to capture states beyond its own API.
constexpr std::array<D3DRENDERSTATETYPE,4> separate={
    static_cast<D3DRENDERSTATETYPE>(206),static_cast<D3DRENDERSTATETYPE>(207),
    static_cast<D3DRENDERSTATETYPE>(208),static_cast<D3DRENDERSTATETYPE>(209)};
struct SavedState {
    IDirect3DDevice8 *device;
    IDirect3DSurface8 *color=nullptr,*depth=nullptr;
    D3DVIEWPORT8 viewport{};
    DWORD token=0,alpha[4]{};
    bool valid=false;
    explicit SavedState(IDirect3DDevice8 *d):device(d) {
        if(FAILED(d->GetRenderTarget(&color))||FAILED(d->GetDepthStencilSurface(&depth))
            ||FAILED(d->GetViewport(&viewport)))return;
        for(unsigned i=0;i<4;++i)if(FAILED(d->GetRenderState(separate[i],&alpha[i])))return;
        valid=SUCCEEDED(d->CreateStateBlock(D3DSBT_ALL,&token));
    }
    bool restore() {
        if(!valid)return false;
        bool ok=SUCCEEDED(device->SetRenderTarget(color,depth));
        ok=SUCCEEDED(device->ApplyStateBlock(token))&&ok;
        ok=SUCCEEDED(device->SetViewport(&viewport))&&ok;
        for(unsigned i=0;i<4;++i)ok=SUCCEEDED(device->SetRenderState(separate[i],alpha[i]))&&ok;
        valid=false;return ok;
    }
    ~SavedState() {
        if(valid)restore();
        if(token)device->DeleteStateBlock(token);
        if(color)color->Release();if(depth)depth->Release();
    }
};
bool fixedQuadState(IDirect3DDevice8 *d) {
    const std::array<std::pair<D3DRENDERSTATETYPE,DWORD>,13> states={{{D3DRS_COLORWRITEENABLE,8},
        {D3DRS_ALPHABLENDENABLE,FALSE},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_ZWRITEENABLE,FALSE},
        {D3DRS_STENCILENABLE,FALSE},{D3DRS_FOGENABLE,FALSE},{D3DRS_LIGHTING,FALSE},
        {D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_FILLMODE,D3DFILL_SOLID},{D3DRS_CLIPPLANEENABLE,0},
        {D3DRS_TEXTUREFACTOR,0xffffffff},{D3DRS_ZBIAS,0},{D3DRS_ZENABLE,FALSE}}};
    for(const auto &s:states)if(FAILED(d->SetRenderState(s.first,s.second)))return false;
    if(FAILED(d->SetVertexShader(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1))||FAILED(d->SetPixelShader(0)))return false;
    const std::array<std::pair<D3DTEXTURESTAGESTATETYPE,DWORD>,13> texture={{{D3DTSS_COLOROP,D3DTOP_SELECTARG1},
        {D3DTSS_COLORARG1,D3DTA_DIFFUSE},{D3DTSS_ALPHAOP,D3DTOP_SELECTARG1},{D3DTSS_ALPHAARG1,D3DTA_TEXTURE},
        {D3DTSS_TEXCOORDINDEX,0},{D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE},{D3DTSS_ADDRESSU,D3DTADDRESS_CLAMP},
        {D3DTSS_ADDRESSV,D3DTADDRESS_CLAMP},{D3DTSS_MINFILTER,D3DTEXF_POINT},{D3DTSS_MAGFILTER,D3DTEXF_POINT},
        {D3DTSS_MIPFILTER,D3DTEXF_NONE},{D3DTSS_MAXMIPLEVEL,0},{D3DTSS_RESULTARG,D3DTA_CURRENT}}};
    for(const auto &s:texture)if(FAILED(d->SetTextureStageState(0,s.first,s.second)))return false;
    return SUCCEEDED(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE))
        &&SUCCEEDED(d->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_DISABLE));
}
bool drawQuad(IDirect3DDevice8 *d,unsigned width,unsigned height) {
    struct Vertex{float x,y,z,rhw;DWORD color;float u,v;};
    const float right=float(width)-.5f,bottom=float(height)-.5f;
    // D3D8/DXVK pixel-center convention; no GL-style UV flip or extra bias.
    const Vertex vertices[4]={{-.5f,-.5f,1,1,0xffffffff,0,0},{right,-.5f,1,1,0xffffffff,1,0},
        {-.5f,bottom,1,1,0xffffffff,0,1},{right,bottom,1,1,0xffffffff,1,1}};
    return SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)));
}
}
// GeneralsX @performance Codex 17/10/2026 Independent MRT output; no scene/particle replay.
namespace {
constexpr GUID bridgeId={0xd3d9d3d8,0x42a9,0x4c1e,{0xaa,0x97,0xbe,0xef,0xca,0xfe,0x20,0x00}};
constexpr GUID coverageId={0x33b37f02,0xb8dc,0x4966,{0xb8,0x0e,0x6a,0x49,0xe2,0x2f,0xb6,0x28}};
constexpr GUID textureId={0xd56344f5,0x8d35,0x46fd,{0x80,0x6d,0x94,0xc3,0x51,0xb4,0x72,0xc1}};

}
bool WorldCoverage::begin(IDirect3DDevice8 *d,IDirect3DSurface8 *eye,IDirect3DSurface8 *depth,unsigned index) {
 if(m_failed||active||!d||!eye||!depth||index>=2)return false;
 m_ready[index]=false;D3DSURFACE_DESC color{},z{};
 if(FAILED(eye->GetDesc(&color))||FAILED(depth->GetDesc(&z))||color.MultiSampleType!=D3DMULTISAMPLE_NONE
    ||z.MultiSampleType!=D3DMULTISAMPLE_NONE||color.Width!=z.Width||color.Height!=z.Height)return false;
 if(m_mask&&(m_device!=d||m_width!=color.Width||m_height!=color.Height))destroy();
 if(!m_interop) {
  IUnknown *bridge=nullptr;
  if(FAILED(d->QueryInterface(bridgeId,reinterpret_cast<void**>(&bridge)))||!bridge){m_failed=true;return false;}
  const auto hr=bridge->QueryInterface(coverageId,reinterpret_cast<void**>(&m_interop));bridge->Release();
  if(FAILED(hr)||!m_interop){m_failed=true;return false;}
 }
 if(!m_mask) {
  IDirect3DTexture8 *texture=nullptr;IDirect3DSurface8 *surface=nullptr;
  if(FAILED(d->CreateTexture(color.Width,color.Height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture))){m_failed=true;return false;}
  if(FAILED(texture->GetSurfaceLevel(0,&surface))){texture->Release();m_failed=true;return false;}
  m_texture=texture;m_mask=surface;m_width=color.Width;m_height=color.Height;
 }
 m_device=d;m_eye=eye;m_depth=depth;m_eyeIndex=index;
 IDirect3DSurface8 *bound=nullptr,*boundDepth=nullptr;D3DVIEWPORT8 vp{};
 bool ok=SUCCEEDED(d->GetRenderTarget(&bound))&&SUCCEEDED(d->GetDepthStencilSurface(&boundDepth))
    &&SUCCEEDED(d->GetViewport(&vp))&&bound==eye&&boundDepth==depth;
 if(ok) {
  ok=SUCCEEDED(d->SetRenderTarget(m_mask,depth));
  const D3DVIEWPORT8 full{0,0,m_width,m_height,0,1};
  if(ok)ok=SUCCEEDED(d->SetViewport(&full))&&SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
  ok=SUCCEEDED(d->SetRenderTarget(bound,boundDepth))&&ok;
  ok=SUCCEEDED(d->SetViewport(&vp))&&ok;
 }
 if(bound)bound->Release();if(boundDepth)boundDepth->Release();
 IUnknown *eyeTexture=nullptr,*maskTexture=nullptr;
 if(ok)ok=SUCCEEDED(eye->QueryInterface(textureId,reinterpret_cast<void**>(&eyeTexture)))&&eyeTexture
    &&SUCCEEDED(m_mask->QueryInterface(textureId,reinterpret_cast<void**>(&maskTexture)))&&maskTexture;
 if(ok)ok=SUCCEEDED(static_cast<WorldCoverageInterop*>(m_interop)->BeginWorldCoverage(eyeTexture,maskTexture));
 if(eyeTexture)eyeTexture->Release();if(maskTexture)maskTexture->Release();
 m_valid=ok;m_failed=!ok;if(ok)active=this;
 return ok;
}
void WorldCoverage::finish(IDirect3DDevice8 *d,bool presenting) {
 if(active!=this)return;
 active=nullptr;
 const bool ended=m_interop&&SUCCEEDED(static_cast<WorldCoverageInterop*>(m_interop)->EndWorldCoverage());
 bool ok=ended&&m_valid&&!presenting&&d==m_device;
 if(ok) {
  SavedState saved(d);ok=saved.valid&&saved.color==m_eye&&saved.depth==m_depth;
  const D3DVIEWPORT8 full{0,0,m_width,m_height,0,1};
  if(ok)ok=SUCCEEDED(d->SetViewport(&full))&&fixedQuadState(d)
      &&SUCCEEDED(d->SetTexture(0,m_texture))&&drawQuad(d,m_width,m_height);
  if(saved.valid)ok=saved.restore()&&ok;
 }
 if(m_eyeIndex<2)m_ready[m_eyeIndex]=ok;
 m_valid=false;m_failed=!ok;
 if(++m_frames%240==1)__android_log_print(ANDROID_LOG_INFO,tag,
  "Quest world coverage MRT: eye=%u complete=%d replay_draws=0 mask_bytes=%llu; original material alpha retained",
  m_eyeIndex,ok,static_cast<unsigned long long>(m_width)*m_height*4);
}
void WorldCoverage::resetFrame(){
 if(active==this){active=nullptr;if(m_interop)static_cast<WorldCoverageInterop*>(m_interop)->EndWorldCoverage();}
 m_valid=false;m_ready[0]=m_ready[1]=false;
}
void WorldCoverage::destroy(){
 resetFrame();if(m_mask)m_mask->Release();if(m_texture)m_texture->Release();if(m_interop)m_interop->Release();
 m_interop=nullptr;m_mask=nullptr;m_texture=nullptr;m_eye=m_depth=nullptr;m_device=nullptr;m_width=m_height=0;m_failed=false;
}
void FinishWorldCoverage(IDirect3DDevice8 *d,bool presenting){if(active)active->finish(d,presenting);}
}
#endif
