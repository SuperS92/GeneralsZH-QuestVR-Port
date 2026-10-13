// SPDX-License-Identifier: GPL-3.0-or-later
// Actual production helper, real D3D9 GPU through a D3D8 contract adapter, CPU oracles.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "VRDevice/QuestShroudPass.h"
#include "VRDevice/QuestTerrainBatching.h"
#include "VRDevice/QuestPerformance.h"
#include <cstdio>
#include <cstdlib>
#include <random>
using namespace QuestXR;
static void require(bool ok,const char *why) { if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);} }
static void check(HRESULT r) { if(FAILED(r)){std::fprintf(stderr,"D3D8 failure 0x%08x\n",unsigned(r));std::exit(1);} }
static D3DMATRIX identity() { D3DMATRIX m{};m._11=m._22=m._33=m._44=1;return m; }
static void pureChecks() {
    TerrainPatchBounds uploadedBounds;
    const std::array<TerrainPoint,4> quad{{{0,0,0},{1,0,0},{1,1,0},{0,1,0}}};
    for(unsigned i=0;i<1024;++i)uploadedBounds.Observe(i,quad);
    require(uploadedBounds.Complete() && !uploadedBounds.uploaded,"complete CPU positions do not prove upload success");
    uploadedBounds.CompleteUpload(true);require(uploadedBounds.uploaded,"successful unlock acknowledges geometry");
    uploadedBounds.Observe(0,quad);require(!uploadedBounds.uploaded,"partial rewrite needs a fresh successful unlock");
    uploadedBounds.CompleteUpload(false);require(!uploadedBounds.Complete() && !uploadedBounds.uploaded,"failed geometry upload stays ineligible");
    ShroudWhiteMask mask;require(mask.Initialize(64,64),"mask allocation");
    std::vector<uint16_t> pixels(64*64,0xffff);
    require(!mask.Neutral({3,3,9,9}),"unknown upload stays nonneutral");
    require(mask.Observe(pixels.data(),64,64,128,{0,0,64,64},0,0,true),"successful upload");mask.Finish();
    require(mask.Neutral({3,3,9,9}),"fully white rectangle");
    pixels[5*64+5]=0xfffe;
    mask.Observe(pixels.data(),64,64,128,{5,5,6,6},5,5,true);mask.Finish();
    require(!mask.Neutral({3,3,9,9}),"one nonwhite RGB565 texel retained");
    require(mask.Neutral({10,10,20,20}),"unchanged neutral region");
    mask.Observe(pixels.data(),64,64,128,{0,0,64,64},0,0,false);mask.Finish();
    require(!mask.Neutral({10,10,20,20}),"failed copy invalidates earlier knowledge");
    mask.Initialize(64,64);mask.Observe(pixels.data(),64,64,128,{10,10,20,20},10,10,true);mask.Finish();
    require(mask.Neutral({10,10,20,20}) && !mask.Neutral({9,9,21,21}),"partial copy cannot qualify unknown neighbors");
    std::mt19937 random(131026);
    std::vector<uint8_t> expected(64*64,0);
    for(unsigned copy=0;copy<200;++copy) {
        for(auto &p:pixels)p=(random()%4)?0xffff:0;
        unsigned sx=random()%48,sy=random()%48,w=1+random()%16,h=1+random()%16,x=random()%(65-w),y=random()%(65-h);
        require(mask.Observe(pixels.data(),64,64,128,{int(sx),int(sy),int(sx+w),int(sy+h)},x,y,true),"random successful subcopy");
        for(unsigned j=0;j<h;++j)for(unsigned i=0;i<w;++i)expected[(y+j)*64+x+i]=pixels[(sy+j)*64+sx+i]==0xffff;
        // The initial partial upload is deliberately retained by both oracles.
        if(copy==0) for(unsigned j=10;j<20;++j)for(unsigned i=10;i<20;++i)
            if(!(i>=x && i<x+w && j>=y && j<y+h))expected[j*64+i]=1;
        mask.Finish();
        for(unsigned q=0;q<50;++q) {
            unsigned a=random()%63,b=random()%63,c=a+1+random()%(64-a),d=b+1+random()%(64-b);
            bool white=true;for(unsigned j=b;j<d;++j)for(unsigned i=a;i<c;++i)white=white && expected[j*64+i];
            require(mask.Neutral({int(a),int(b),int(c),int(d)})==white,"independent destination pixel oracle");
        }
    }
    for(unsigned count=1;count<=10;++count) {
        std::vector<uint16_t> indices(count*6144);require(FillTerrainGroupIndices(indices.data(),indices.size(),count),"actual group indices");
        for(unsigned bits=0;bits<(1u<<count);++bits) {
            auto runs=ShroudRetainedRuns(count,bits);std::vector<uint16_t> actual,oracle;
            for(unsigned r=0;r<runs.count;++r)actual.insert(actual.end(),indices.begin()+runs.ranges[r].first*6144,
                indices.begin()+(runs.ranges[r].first+runs.ranges[r].count)*6144);
            for(unsigned p=0;p<count;++p)if(!(bits&(1u<<p)))oracle.insert(oracle.end(),indices.begin()+p*6144,indices.begin()+(p+1)*6144);
            require(actual==oracle,"exact retained original triangle order and indices");
        }
    }
    unsigned accepted=0;
    for(unsigned trial=0;trial<20000;++trial) {
        auto view=identity(),tex=identity();
        float origin=float(int(random()%200000)-100000);view._41=-origin;view._42=origin*0.3f;
        tex._11=tex._22=1.f/512;tex._41=origin/512;tex._42=-origin*0.3f/512;
        TerrainBounds box;box.Include({double(32+random()%400),double(32+random()%400),0});box.Include({box.low.x+8,box.low.y+8,50});
        ShroudRect footprint;if(!ShroudFootprint(box,&view._11,&tex._11,512,512,footprint))continue;++accepted;
        for(unsigned point=0;point<10;++point) {
            float x=float(box.low.x+(box.high.x-box.low.x)*(random()%1001)/1000.0),
                y=float(box.low.y+(box.high.y-box.low.y)*(random()%1001)/1000.0);
            float cameraX=x+view._41,cameraY=y+view._42;
            float px=(cameraX*tex._11+tex._41)*512-0.5f,py=(cameraY*tex._22+tex._42)*512-0.5f;
            require(std::floor(px)>=footprint.left && std::floor(px)+1<footprint.right
                && std::floor(py)>=footprint.top && std::floor(py)+1<footprint.bottom,"actual float pipeline's bilinear neighbors within guarded footprint");
        }
    }
    require(accepted>1000,"exercise guarded footprints");
    std::puts("PASS CPU: destination upload oracle, unknown/failure/partial data, 2046 exact index masks and 20000 camera cancellation footprints");
}
struct Vertex { float x,y,z;DWORD color;float u,v,u2,v2; };
int main(int argc,char **argv) {
    pureChecks();
    if(argc==2 && std::strcmp(argv[1],"--cpu-only")==0)return 0;
    HWND window=CreateWindowExW(0,L"STATIC",L"Quest shroud host fixture",WS_POPUP,0,0,512,128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(window!=nullptr,"hidden fixture window");
    IDirect3D9 *api=Direct3DCreate9(D3D_SDK_VERSION);require(api!=nullptr,"D3D9 host runtime");
    std::printf("Host D3D9 adapters: %u\n",api->GetAdapterCount());std::fflush(stdout);
    D3DDISPLAYMODE display{};check(api->GetAdapterDisplayMode(0,&display));
    D3DPRESENT_PARAMETERS present{};present.Windowed=TRUE;present.SwapEffect=D3DSWAPEFFECT_DISCARD;
    present.hDeviceWindow=window;present.BackBufferFormat=display.Format;present.BackBufferWidth=512;present.BackBufferHeight=128;
    present.BackBufferCount=1;
    const auto requestedPresent=present;
    IDirect3DDevice9 *nativeDevice=nullptr;
    HRESULT created=api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&present,&nativeDevice);
    if(FAILED(created)) { present=requestedPresent;created=api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&present,&nativeDevice); }
    check(created);std::puts("Host D3D9 device created");std::fflush(stdout);
    IDirect3DDevice8 bridge(nativeDevice);IDirect3DDevice8 *device=&bridge;
    IDirect3DSurface8 *target=nullptr,*depth=nullptr,*source=nullptr,*destination=nullptr;
    check(device->CreateRenderTarget(512,128,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,TRUE,&target));
    check(device->CreateDepthStencilSurface(512,128,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,&depth));check(device->SetRenderTarget(target,depth));
    D3DVIEWPORT8 viewport{0,0,512,128,0,1};check(device->SetViewport(&viewport));
    IDirect3DTexture8 *texture=nullptr;check(device->CreateTexture(512,512,1,0,D3DFMT_R5G6B5,D3DPOOL_DEFAULT,&texture));
    check(texture->GetSurfaceLevel(0,&destination));check(device->CreateImageSurface(512,512,D3DFMT_R5G6B5,&source));
    IDirect3DVertexBuffer8 *vb=nullptr;IDirect3DIndexBuffer8 *ib=nullptr;
    constexpr unsigned count=4,fvf=D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX2;
    check(device->CreateVertexBuffer(count*4096*sizeof(Vertex),0,fvf,D3DPOOL_MANAGED,&vb));
    check(device->CreateIndexBuffer(count*6144*2,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib));
    void *raw=nullptr;check(ib->Lock(0,0,&raw,0));require(FillTerrainGroupIndices(reinterpret_cast<uint16_t*>(raw),count*6144,count),"native indices");check(ib->Unlock());
    check(vb->Lock(0,0,&raw,0));auto *vertices=reinterpret_cast<Vertex*>(raw);
    std::array<TerrainPatchBounds,count> bounds{};
    for(unsigned p=0;p<count;++p)for(unsigned cell=0;cell<1024;++cell) {
        float x=16+p*80+cell%32,y=16+cell/32;
        std::array<TerrainPoint,4> q{{{x,y,.5},{x+1,y,.5},{x+1,y+1,.5},{x,y+1,.5}}};bounds[p].Observe(cell,q);
        for(unsigned v=0;v<4;++v)vertices[p*4096+cell*4+v]={float(q[v].x),float(q[v].y),float(q[v].z),0x7936ace2u+p*0x0b070311u,0,0,0,0};
    }
    check(vb->Unlock());for(auto &p:bounds)p.CompleteUpload(true);
    check(device->SetVertexShader(fvf));check(device->SetStreamSource(0,vb,sizeof(Vertex)));check(device->SetIndices(ib,0));
    auto projection=identity();projection._11=2.f/512;projection._22=-2.f/128;projection._41=-1;projection._42=1;
    check(device->SetTransform(D3DTS_PROJECTION,&projection));auto world=identity();check(device->SetTransform(D3DTS_WORLD,&world));
    _putenv_s("GX_QUEST_NEUTRAL_SHROUD","1");_putenv_s("GX_QUEST_PERF_METRICS","1");
    PerformanceFrameScope frame;PerformanceEyeScope eye(0);
    unsigned gpuCases=0,skipped=0;
    for(unsigned scenario=0;scenario<6;++scenario)for(unsigned eyeIndex=0;eyeIndex<2;++eyeIndex)for(bool linear:{false,true}) {
        std::vector<uint16_t> pixels(512*512,0x7bef);
        for(unsigned p:{0u,2u})for(unsigned y=10;y<54;++y)for(unsigned x=10+p*80;x<54+p*80;++x)pixels[y*512+x]=0xffff;
        if(scenario==1)pixels[25*512+180]=0xfffe; // subtle fade within a normally neutral patch
        if(scenario==2)for(unsigned y=0;y<512;++y)for(unsigned x=0;x<512;++x)pixels[y*512+x]=0xffff;
        D3DLOCKED_RECT locked{};check(source->LockRect(&locked,nullptr,0));
        for(unsigned y=0;y<512;++y)std::memcpy(static_cast<uint8_t*>(locked.pBits)+y*locked.Pitch,pixels.data()+y*512,1024);
        check(source->UnlockRect());
        { ShroudUploadScope upload(source,destination,texture,pixels.data(),1024,512,512);
          RECT rect{0,0,512,512};POINT point{0,0};HRESULT result;
          require(QuestXR_CopyShroudRects(device,source,&rect,1,destination,&point,result),"production upload observer handles copy");check(result);
          if(scenario==3) { RECT bad{-1,0,1,1};require(QuestXR_CopyShroudRects(device,source,&bad,1,destination,&point,result),"failed upload handled");require(FAILED(result),"invalid copy fails"); }
        }
        if(scenario==4)ForgetShroudTexture(texture);
        auto view=identity(),transform=identity();view._41=eyeIndex?4.f:-4.f;transform._11=transform._22=1.f/512;transform._41=-view._41/512;
        check(device->SetTransform(D3DTS_VIEW,&view));check(device->SetTransform(D3DTS_TEXTURE0,&transform));
        auto draw=[&](bool optimized) {
            check(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0x53718ac3,1,0));check(device->BeginScene());
            check(device->SetTexture(0,nullptr));check(device->SetRenderState(D3DRS_LIGHTING,FALSE));check(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
            check(device->SetRenderState(D3DRS_FOGENABLE,FALSE));check(device->SetRenderState(D3DRS_STENCILENABLE,FALSE));
            check(device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));check(device->SetRenderState(D3DRS_ZENABLE,TRUE));
            check(device->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));check(device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
            check(device->SetRenderState(D3DRS_COLORWRITEENABLE,scenario%2?7:15));check(device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
            check(device->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));
            check(device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));check(device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));
            check(device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));check(device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE));
            check(device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));check(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,count*4096,0,count*2048));
            check(device->SetTexture(0,texture));check(device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE));check(device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE));
            check(device->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,D3DTSS_TCI_CAMERASPACEPOSITION));
            check(device->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_COUNT2));
            check(device->SetTextureStageState(0,D3DTSS_MINFILTER,linear?D3DTEXF_LINEAR:D3DTEXF_POINT));
            check(device->SetTextureStageState(0,D3DTSS_MAGFILTER,linear?D3DTEXF_LINEAR:D3DTEXF_POINT));
            check(device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));check(device->SetRenderState(D3DRS_ZFUNC,D3DCMP_EQUAL));
            check(device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));check(device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ZERO));
            check(device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_SRCCOLOR));check(device->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
            if(scenario==5)check(device->SetRenderState(D3DRS_STENCILENABLE,TRUE));
            std::array<DWORD,5> before{},after{};const D3DRENDERSTATETYPE states[]={D3DRS_COLORWRITEENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_ALPHABLENDENABLE,D3DRS_STENCILENABLE};
            for(unsigned i=0;i<5;++i)check(device->GetRenderState(states[i],&before[i]));
            bool handled=false;
            if(optimized) { ShroudDrawScope scope(bounds.data(),count,vb);handled=QuestXR_DrawNeutralShroud(device,D3DPT_TRIANGLELIST,0,count*4096,0,count*2048); }
            if(!handled)check(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,count*4096,0,count*2048));
            for(unsigned i=0;i<5;++i)check(device->GetRenderState(states[i],&after[i]));require(before==after,"native state unchanged");
            check(device->EndScene());check(target->LockRect(&locked,nullptr,D3DLOCK_READONLY));
            std::vector<uint8_t> output(512*128*4);for(unsigned y=0;y<128;++y)std::memcpy(output.data()+y*512*4,static_cast<uint8_t*>(locked.pBits)+y*locked.Pitch,512*4);
            check(target->UnlockRect());return output;
        };
        const auto original=draw(false),optimized=draw(true);require(original==optimized,"actual native pixels including alpha exact across original and neutral subruns");
        auto stats=TakeShroudPassStats();skipped+=unsigned(stats.skipped);
        if(scenario==3 || scenario==4 || scenario==5)require(stats.skipped==0,"failure/lifecycle/state fallbacks keep full draw");
        ++gpuCases;
    }
    require(skipped>0,"actual production draw path skips neutral patches");
    _putenv_s("GX_QUEST_NEUTRAL_SHROUD","0");
    {ShroudDrawScope scope(bounds.data(),count,vb);require(!QuestXR_DrawNeutralShroud(device,D3DPT_TRIANGLELIST,0,count*4096,0,count*2048),"Off keeps original draw");}
    ForgetShroudTexture(texture);source->Release();destination->Release();texture->Release();vb->Release();ib->Release();target->Release();depth->Release();device->Release();api->Release();DestroyWindow(window);
    std::printf("PASS host D3D9 GPU via D3D8 contract adapter: %u exact RGBA cases, point/linear, two eyes, RGB/RGBA masks, fade, failed copy, texture reset, state fallback; %u neutral patches skipped\n",gpuCases,skipped);
}
