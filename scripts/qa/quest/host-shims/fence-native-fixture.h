// SPDX-License-Identifier: GPL-3.0-or-later
// This interface fixture exercises production resource-marker ordering and failures;
// it is not a real D3D8 device or native GPU upload.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
using DWORD=uint32_t;
using HRESULT=int32_t;
struct GUID { uint32_t a;uint16_t b,c;uint8_t d[8]; };
#define SUCCEEDED(hr) ((hr)>=0)
#define FAILED(hr) ((hr)<0)
enum { D3DFMT_A8R8G8B8=21,D3DPOOL_MANAGED=1 };
struct D3DSURFACE_DESC { uint32_t Format=21,Pool=1,Width=64,Height=64; };
struct IDirect3DBaseTexture8 {
    std::vector<uint8_t> marker;
    unsigned getters=0,setters=0,frees=0;
    unsigned failSet=0;
    bool failGet=false;
    HRESULT GetPrivateData(const GUID&,void *p,DWORD *bytes) {
        ++getters;
        if(failGet||marker.empty()||*bytes<marker.size())return -1;
        *bytes=DWORD(marker.size());std::memcpy(p,marker.data(),marker.size());return 0;
    }
    HRESULT SetPrivateData(const GUID&,const void *p,DWORD bytes,DWORD) {
        if(++setters==failSet)return -1;
        const auto *b=static_cast<const uint8_t*>(p);marker.assign(b,b+bytes);return 0;
    }
    HRESULT FreePrivateData(const GUID&) { ++frees;marker.clear();return 0; }
};
struct IDirect3DTexture8:IDirect3DBaseTexture8 {
    D3DSURFACE_DESC desc;
    uint32_t levels=5;
    bool failDesc=false;
    HRESULT GetLevelDesc(uint32_t,D3DSURFACE_DESC *p){ if(failDesc)return -1;*p=desc;return 0; }
    uint32_t GetLevelCount(){return levels;}
};
