// SPDX-License-Identifier: GPL-3.0-or-later
// Host fixture only: the D3D8 subset used by the production helper on real D3D9.
// Never place this directory on native game include paths.
#pragma once
#include <d3d9.h>
using IDirect3DBaseTexture8=IDirect3DBaseTexture9;
using IDirect3DTexture8=IDirect3DTexture9;
using IDirect3DSurface8=IDirect3DSurface9;
using IDirect3DVertexBuffer8=IDirect3DVertexBuffer9;
using IDirect3DIndexBuffer8=IDirect3DIndexBuffer9;
constexpr D3DTEXTURESTAGESTATETYPE D3DTSS_MAGFILTER=static_cast<D3DTEXTURESTAGESTATETYPE>(16);
constexpr D3DTEXTURESTAGESTATETYPE D3DTSS_MINFILTER=static_cast<D3DTEXTURESTAGESTATETYPE>(17);
constexpr D3DTEXTURESTAGESTATETYPE D3DTSS_MIPFILTER=static_cast<D3DTEXTURESTAGESTATETYPE>(18);
class IDirect3DDevice8 {
    IDirect3DDevice9 *device;
    unsigned base=0;
    static D3DSAMPLERSTATETYPE sampler(D3DTEXTURESTAGESTATETYPE s) {
        return s==D3DTSS_MAGFILTER?D3DSAMP_MAGFILTER:s==D3DTSS_MINFILTER?D3DSAMP_MINFILTER:D3DSAMP_MIPFILTER;
    }
public:
    explicit IDirect3DDevice8(IDirect3DDevice9 *d):device(d) {}
    HRESULT GetTexture(DWORD s,IDirect3DBaseTexture8 **t) { return device->GetTexture(s,t); }
    HRESULT SetTexture(DWORD s,IDirect3DBaseTexture8 *t) { return device->SetTexture(s,t); }
    HRESULT GetStreamSource(UINT s,IDirect3DVertexBuffer8 **v,UINT *stride) {
        UINT offset=0;HRESULT result=device->GetStreamSource(s,v,&offset,stride);
        return SUCCEEDED(result) && offset?D3DERR_INVALIDCALL:result;
    }
    HRESULT SetStreamSource(UINT s,IDirect3DVertexBuffer8 *v,UINT stride) { return device->SetStreamSource(s,v,0,stride); }
    HRESULT GetPixelShader(DWORD *value) {
        IDirect3DPixelShader9 *s=nullptr;HRESULT r=device->GetPixelShader(&s);*value=s?1:0;if(s)s->Release();return r;
    }
    HRESULT GetVertexShader(DWORD *value) {
        IDirect3DVertexShader9 *s=nullptr;HRESULT r=device->GetVertexShader(&s);
        if(s) { *value=1;s->Release();return r; }return FAILED(r)?r:device->GetFVF(value);
    }
    HRESULT SetVertexShader(DWORD fvf) { HRESULT r=device->SetVertexShader(nullptr);return FAILED(r)?r:device->SetFVF(fvf); }
    HRESULT GetRenderState(D3DRENDERSTATETYPE s,DWORD *v) { return device->GetRenderState(s,v); }
    HRESULT SetRenderState(D3DRENDERSTATETYPE s,DWORD v) { return device->SetRenderState(s,v); }
    HRESULT GetTextureStageState(DWORD stage,D3DTEXTURESTAGESTATETYPE s,DWORD *v) {
        return s==D3DTSS_MAGFILTER || s==D3DTSS_MINFILTER || s==D3DTSS_MIPFILTER?
            device->GetSamplerState(stage,sampler(s),v):device->GetTextureStageState(stage,s,v);
    }
    HRESULT SetTextureStageState(DWORD stage,D3DTEXTURESTAGESTATETYPE s,DWORD v) {
        return s==D3DTSS_MAGFILTER || s==D3DTSS_MINFILTER || s==D3DTSS_MIPFILTER?
            device->SetSamplerState(stage,sampler(s),v):device->SetTextureStageState(stage,s,v);
    }
    HRESULT GetIndices(IDirect3DIndexBuffer8 **i,UINT *b) { *b=base;return device->GetIndices(i); }
    HRESULT SetIndices(IDirect3DIndexBuffer8 *i,UINT b) { base=b;return device->SetIndices(i); }
    HRESULT GetRenderTarget(IDirect3DSurface8 **t) { return device->GetRenderTarget(0,t); }
    HRESULT SetRenderTarget(IDirect3DSurface8 *t,IDirect3DSurface8 *z) {
        HRESULT r=device->SetRenderTarget(0,t);return FAILED(r)?r:device->SetDepthStencilSurface(z);
    }
    HRESULT GetTransform(D3DTRANSFORMSTATETYPE s,D3DMATRIX *m) { return device->GetTransform(s,m); }
    HRESULT SetTransform(D3DTRANSFORMSTATETYPE s,const D3DMATRIX *m) { return device->SetTransform(s,m); }
    HRESULT CopyRects(IDirect3DSurface8 *src,const RECT *rects,UINT count,IDirect3DSurface8 *dst,const POINT *points) {
        if(!rects || !points || !count)return D3DERR_INVALIDCALL;
        for(UINT i=0;i<count;++i) { HRESULT r=device->UpdateSurface(src,&rects[i],dst,&points[i]);if(FAILED(r))return r; }
        return D3D_OK;
    }
    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE t,UINT min,UINT vertices,UINT start,UINT polygons) {
        return device->DrawIndexedPrimitive(t,base,min,vertices,start,polygons);
    }
    HRESULT CreateRenderTarget(UINT w,UINT h,D3DFORMAT f,D3DMULTISAMPLE_TYPE ms,BOOL lock,IDirect3DSurface8 **s) {
        return device->CreateRenderTarget(w,h,f,ms,0,lock,s,nullptr);
    }
    HRESULT CreateDepthStencilSurface(UINT w,UINT h,D3DFORMAT f,D3DMULTISAMPLE_TYPE ms,IDirect3DSurface8 **s) {
        return device->CreateDepthStencilSurface(w,h,f,ms,0,FALSE,s,nullptr);
    }
    HRESULT CreateTexture(UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT f,D3DPOOL pool,IDirect3DTexture8 **t) {
        return device->CreateTexture(w,h,levels,usage,f,pool,t,nullptr);
    }
    HRESULT CreateImageSurface(UINT w,UINT h,D3DFORMAT f,IDirect3DSurface8 **s) { return device->CreateOffscreenPlainSurface(w,h,f,D3DPOOL_SYSTEMMEM,s,nullptr); }
    HRESULT CreateVertexBuffer(UINT size,DWORD usage,DWORD fvf,D3DPOOL pool,IDirect3DVertexBuffer8 **v) { return device->CreateVertexBuffer(size,usage,fvf,pool,v,nullptr); }
    HRESULT CreateIndexBuffer(UINT size,DWORD usage,D3DFORMAT f,D3DPOOL pool,IDirect3DIndexBuffer8 **v) { return device->CreateIndexBuffer(size,usage,f,pool,v,nullptr); }
    HRESULT SetViewport(const D3DVIEWPORT9 *v) { return device->SetViewport(v); }
    HRESULT Clear(DWORD n,const D3DRECT *r,DWORD flags,D3DCOLOR c,float z,DWORD s) { return device->Clear(n,r,flags,c,z,s); }
    HRESULT BeginScene() { return device->BeginScene(); }
    HRESULT EndScene() { return device->EndScene(); }
    ULONG Release() { return device->Release(); }
};
using D3DVIEWPORT8=D3DVIEWPORT9;
