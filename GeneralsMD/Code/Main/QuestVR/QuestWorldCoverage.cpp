// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestWorldCoverage.h"
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
bool separateAlpha(IDirect3DDevice8 *d) {
    const DWORD values[4]={TRUE,D3DBLEND_ONE,D3DBLEND_INVSRCALPHA,D3DBLENDOP_ADD};
    for(unsigned i=0;i<4;++i)if(FAILED(d->SetRenderState(separate[i],values[i])))return false;
    return true;
}
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
bool WorldCoverage::begin(IDirect3DDevice8 *d,IDirect3DSurface8 *eye,IDirect3DSurface8 *depth,unsigned index) {
    if(m_failed||active||!d||!eye||!depth||index>=2)return false;
    m_ready[index]=false;D3DSURFACE_DESC color{},z{};
    if(FAILED(eye->GetDesc(&color))||FAILED(depth->GetDesc(&z))||color.MultiSampleType!=D3DMULTISAMPLE_NONE
        ||z.MultiSampleType!=D3DMULTISAMPLE_NONE||color.Width!=z.Width||color.Height!=z.Height)return false;
    if(m_mask&&(m_device!=d||m_width!=color.Width||m_height!=color.Height))destroy();
    if(!m_mask) {
        IDirect3DTexture8 *texture=nullptr;IDirect3DSurface8 *surface=nullptr;
        if(FAILED(d->CreateTexture(color.Width,color.Height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture))){m_failed=true;return false;}
        if(FAILED(texture->GetSurfaceLevel(0,&surface))){texture->Release();m_failed=true;return false;}
        m_texture=texture;m_mask=surface;m_width=color.Width;m_height=color.Height;
    }
    m_device=d;m_eye=eye;m_depth=depth;m_eyeIndex=index;m_replays=0;
    SavedState saved(d);
    bool ok=saved.valid&&saved.color==eye&&saved.depth==depth;
    if(ok)ok=SUCCEEDED(d->SetRenderTarget(m_mask,depth))&&SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
    if(saved.valid)ok=saved.restore()&&ok;
    m_valid=ok;m_failed=!ok;if(ok)active=this;
    return ok;
}
void WorldCoverage::capture(IDirect3DDevice8 *d,uint32_t primitive,uint32_t first,uint32_t vertices,uint32_t start,uint32_t count,bool indexed) {
    if(!m_valid||d!=m_device||!count)return;
    DWORD blend=0,zwrite=0,src=0,dst=0,op=0,mask=0;
    if(FAILED(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))||FAILED(d->GetRenderState(D3DRS_ZWRITEENABLE,&zwrite))){m_valid=false;return;}
    if(!blend&&zwrite)return; // Opaque geometry is covered by the final depth union.
    IDirect3DSurface8 *bound=nullptr;
    const HRESULT binding=d->GetRenderTarget(&bound);
    const bool eye=SUCCEEDED(binding)&&bound==m_eye;
    if(bound)bound->Release();
    if(!eye)return; // Water reflections and other original offscreen work are not eye coverage.
    if(FAILED(d->GetRenderState(D3DRS_SRCBLEND,&src))
        ||FAILED(d->GetRenderState(D3DRS_DESTBLEND,&dst))||FAILED(d->GetRenderState(D3DRS_BLENDOP,&op))
        ||FAILED(d->GetRenderState(D3DRS_COLORWRITEENABLE,&mask))){m_valid=false;return;}
    const auto kind=!blend&&(mask&7)?CoverageDraw::Solid:ClassifyCoverage(blend,src,dst,op,mask);
    if(kind==CoverageDraw::Skip)return;
    if(kind==CoverageDraw::Unsupported) {
        static unsigned reports=0;
        if(reports++<8)__android_log_print(ANDROID_LOG_WARN,tag,"Quest world passthrough: opaque fallback for blend=%u,%u op=%u mask=%u",src,dst,op,mask);
        m_valid=false;m_failed=true;return;
    }
    SavedState saved(d);
    if(!saved.valid||saved.color!=m_eye||saved.depth!=m_depth){m_valid=false;return;}
    DWORD stencil=0,write=0,fail=0,zfail=0,pass=0;
    bool ok=SUCCEEDED(d->GetRenderState(D3DRS_STENCILENABLE,&stencil));
    if(stencil)ok=ok&&SUCCEEDED(d->GetRenderState(D3DRS_STENCILWRITEMASK,&write))
        &&SUCCEEDED(d->GetRenderState(D3DRS_STENCILFAIL,&fail))&&SUCCEEDED(d->GetRenderState(D3DRS_STENCILZFAIL,&zfail))
        &&SUCCEEDED(d->GetRenderState(D3DRS_STENCILPASS,&pass))&&(!write||(fail==D3DSTENCILOP_KEEP&&zfail==D3DSTENCILOP_KEEP&&pass==D3DSTENCILOP_KEEP));
    // Never replay stencil mutations or write depth a second time.
    if(ok)ok=SUCCEEDED(d->SetRenderTarget(m_mask,m_depth))&&SUCCEEDED(d->SetViewport(&saved.viewport))
        &&SUCCEEDED(d->SetRenderState(D3DRS_COLORWRITEENABLE,8))&&SUCCEEDED(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE))
        &&SUCCEEDED(d->SetRenderState(D3DRS_STENCILWRITEMASK,0))&&SUCCEEDED(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE))&&separateAlpha(d);
    if(ok&&kind==CoverageDraw::Solid) {
        DWORD alphaTest=0;
        ok=SUCCEEDED(d->GetRenderState(D3DRS_ALPHATESTENABLE,&alphaTest))&&!alphaTest;
        // Water's original draw has already consumed legacy destination alpha.
        // The extra alpha-only draw covers the surface without changing original water RGB.
        ok=ok&&SUCCEEDED(d->SetPixelShader(0))&&SUCCEEDED(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE))
            &&SUCCEEDED(d->SetRenderState(D3DRS_TEXTUREFACTOR,0xffffffff))
            &&SUCCEEDED(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1))
            &&SUCCEEDED(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TFACTOR))
            &&SUCCEEDED(d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1))
            &&SUCCEEDED(d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TFACTOR))
            &&SUCCEEDED(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE))
            &&SUCCEEDED(d->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_DISABLE));
    }
    if(ok)ok=indexed?SUCCEEDED(d->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(primitive),first,vertices,start,count))
        :SUCCEEDED(d->DrawPrimitive(static_cast<D3DPRIMITIVETYPE>(primitive),first,count));
    ok=saved.restore()&&ok;m_valid=ok;m_failed=!ok;if(ok)++m_replays;
}
void WorldCoverage::finish(IDirect3DDevice8 *d,bool presenting) {
    if(active!=this)return;
    active=nullptr;bool ok=m_valid&&!presenting&&d==m_device;
    if(ok) {
        SavedState saved(d);ok=saved.valid&&saved.color==m_eye&&saved.depth==m_depth;
        const D3DVIEWPORT8 full{0,0,m_width,m_height,0,1};
        if(ok)ok=SUCCEEDED(d->SetViewport(&full));
        if(ok)ok=fixedQuadState(d)&&SUCCEEDED(d->SetTexture(0,m_texture))&&drawQuad(d,m_width,m_height);
        // Union late transparent coverage with all depth-writing geometry, including blue objects.
        if(ok)ok=SUCCEEDED(d->SetTexture(0,nullptr))&&SUCCEEDED(d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TFACTOR))
            &&SUCCEEDED(d->SetRenderState(D3DRS_ZENABLE,D3DZB_TRUE))&&SUCCEEDED(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_GREATER))
            &&drawQuad(d,m_width,m_height);
        if(saved.valid)ok=saved.restore()&&ok;
    }
    if(m_eyeIndex<2)m_ready[m_eyeIndex]=ok;
    m_valid=false;m_failed=!ok;
    if(++m_frames%240==1)__android_log_print(ANDROID_LOG_INFO,tag,
        "Quest world coverage: eye=%u complete=%d transparent_draws=%u; original RGB/depth/stencil retained",m_eyeIndex,ok,m_replays);
}
void WorldCoverage::resetFrame(){if(active==this)active=nullptr;m_valid=false;m_ready[0]=m_ready[1]=false;}
void WorldCoverage::destroy(){resetFrame();if(m_mask)m_mask->Release();if(m_texture)m_texture->Release();m_mask=nullptr;m_texture=nullptr;m_eye=m_depth=nullptr;m_device=nullptr;m_width=m_height=0;m_failed=false;}
void CaptureWorldCoverageDraw(IDirect3DDevice8 *d,uint32_t p,uint32_t f,uint32_t v,uint32_t s,uint32_t n){if(active)active->capture(d,p,f,v,s,n);}
void CaptureWorldCoveragePrimitive(IDirect3DDevice8 *d,uint32_t p,uint32_t f,uint32_t n){if(active)active->capture(d,p,f,0,0,n,false);}
void FinishWorldCoverage(IDirect3DDevice8 *d,bool presenting){if(active)active->finish(d,presenting);}
}
#endif
