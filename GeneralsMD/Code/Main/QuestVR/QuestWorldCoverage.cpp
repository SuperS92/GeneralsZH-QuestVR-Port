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
 if((m_mask[0]||m_mask[1])&&(m_device!=d||m_width!=color.Width||m_height!=color.Height))destroy();
 if(!m_interop) {
  IUnknown *bridge=nullptr;
  if(FAILED(d->QueryInterface(bridgeId,reinterpret_cast<void**>(&bridge)))||!bridge){m_failed=true;return false;}
  const auto hr=bridge->QueryInterface(coverageId,reinterpret_cast<void**>(&m_interop));bridge->Release();
  if(FAILED(hr)||!m_interop){m_failed=true;return false;}
 }
 if(!m_mask[index]) {
  IDirect3DTexture8 *texture=nullptr;IDirect3DSurface8 *surface=nullptr;
  if(FAILED(d->CreateTexture(color.Width,color.Height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8,D3DPOOL_DEFAULT,&texture))){m_failed=true;return false;}
  if(FAILED(texture->GetSurfaceLevel(0,&surface))){texture->Release();m_failed=true;return false;}
  m_texture[index]=texture;m_mask[index]=surface;m_width=color.Width;m_height=color.Height;
 }
 m_device=d;m_eye=eye;m_depth=depth;m_eyeIndex=index;
 IDirect3DSurface8 *bound=nullptr,*boundDepth=nullptr;D3DVIEWPORT8 vp{};
 bool ok=SUCCEEDED(d->GetRenderTarget(&bound))&&SUCCEEDED(d->GetDepthStencilSurface(&boundDepth))
    &&SUCCEEDED(d->GetViewport(&vp))&&bound==eye&&boundDepth==depth;
 if(ok) {
  ok=SUCCEEDED(d->SetRenderTarget(m_mask[index],depth));
  const D3DVIEWPORT8 full{0,0,m_width,m_height,0,1};
  if(ok)ok=SUCCEEDED(d->SetViewport(&full))&&SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
  ok=SUCCEEDED(d->SetRenderTarget(bound,boundDepth))&&ok;
  ok=SUCCEEDED(d->SetViewport(&vp))&&ok;
 }
 if(bound)bound->Release();if(boundDepth)boundDepth->Release();
 IUnknown *eyeTexture=nullptr,*maskTexture=nullptr;
 if(ok)ok=SUCCEEDED(eye->QueryInterface(textureId,reinterpret_cast<void**>(&eyeTexture)))&&eyeTexture
    &&SUCCEEDED(m_mask[index]->QueryInterface(textureId,reinterpret_cast<void**>(&maskTexture)))&&maskTexture;
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
 if(m_eyeIndex<2)m_ready[m_eyeIndex]=ok;
 m_valid=false;m_failed=!ok;
 if(++m_frames%240==1)__android_log_print(ANDROID_LOG_INFO,tag,
  "Quest world coverage MRT: eye=%u complete=%d replay_draws=0 mask_bytes_per_eye=%llu final_alpha_draws=0; original material alpha retained",
  m_eyeIndex,ok,static_cast<unsigned long long>(m_width)*m_height);
}
void WorldCoverage::resetFrame(){
 if(active==this){active=nullptr;if(m_interop)static_cast<WorldCoverageInterop*>(m_interop)->EndWorldCoverage();}
 m_valid=false;m_ready[0]=m_ready[1]=false;
}
void WorldCoverage::destroy(){
 resetFrame();for(unsigned eye=0;eye<2;++eye){if(m_mask[eye])m_mask[eye]->Release();if(m_texture[eye])m_texture[eye]->Release();m_mask[eye]=nullptr;m_texture[eye]=nullptr;}if(m_interop)m_interop->Release();
 m_interop=nullptr;m_eye=m_depth=nullptr;m_device=nullptr;m_width=m_height=0;m_failed=false;
}
void FinishWorldCoverage(IDirect3DDevice8 *d,bool presenting){if(active)active->finish(d,presenting);}
}
#endif
