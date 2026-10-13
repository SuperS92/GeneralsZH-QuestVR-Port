// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestShroudPass.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include "VRDevice/QuestPerformance.h"
#include <android/log.h>
#include <utility>
namespace {
struct Mirror {
    QuestXR::ShroudWhiteMask mask;
    IDirect3DTexture8 *texture=nullptr;
    IDirect3DSurface8 *source=nullptr,*destination=nullptr;
    const void *pixels=nullptr;
    unsigned width=0,height=0,sourceWidth=0,sourceHeight=0,pitch=0;
    bool uploading=false;
};
thread_local Mirror mirror;
thread_local const QuestXR::TerrainPatchBounds *drawBounds=nullptr;
thread_local unsigned drawCount=0;
thread_local IDirect3DVertexBuffer8 *drawVertices=nullptr;
thread_local QuestXR::ShroudPassStats counters;
bool requested() { return QuestXR::PerformanceRequested(QuestXR::PerfOption::NeutralShroud); }
void attempted(unsigned skipped,unsigned runs) {
    if (!QuestXR::PerformanceMetricsRequested()) return;
    counters.attempted+=drawCount;counters.skipped+=skipped;counters.submittedRuns+=runs;
}
bool contract(IDirect3DDevice8 *device,D3DMATRIX &view,D3DMATRIX &texture) {
    uint32_t reason=1;
    struct FailureReason { uint32_t &reason;~FailureReason() { counters.contractMask|=reason; } } failureReason{reason};
    if (!device || !mirror.texture || !drawBounds || !drawCount || drawCount>10) return false;
    IDirect3DBaseTexture8 *bound=nullptr;
    HRESULT result=device->GetTexture(0,&bound);
    const bool matches=SUCCEEDED(result) && bound==mirror.texture;
    if(bound) bound->Release();if(!matches) return false;
    IDirect3DVertexBuffer8 *vertices=nullptr;UINT stride=0;D3DVERTEXBUFFER_DESC vertexDesc{};
    result=device->GetStreamSource(0,&vertices,&stride);
    const bool vertexMatches=SUCCEEDED(result) && vertices && vertices==drawVertices && stride==32
        && SUCCEEDED(vertices->GetDesc(&vertexDesc)) && vertexDesc.Size==drawCount*4096*32;
    if(vertices) vertices->Release();if(!vertexMatches) return false;
    reason=2;DWORD pixel=1,vertex=0;
    if(FAILED(device->GetPixelShader(&pixel)) || pixel || FAILED(device->GetVertexShader(&vertex))
        || vertex!=(D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX2)) return false;
    const std::pair<D3DRENDERSTATETYPE,DWORD> render[]={
        {D3DRS_ALPHABLENDENABLE,TRUE},{D3DRS_SRCBLEND,D3DBLEND_ZERO},{D3DRS_DESTBLEND,D3DBLEND_SRCCOLOR},
        {D3DRS_BLENDOP,D3DBLENDOP_ADD},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_ZENABLE,D3DZB_TRUE},
        {D3DRS_ZWRITEENABLE,FALSE},{D3DRS_ZFUNC,D3DCMP_EQUAL},{D3DRS_STENCILENABLE,FALSE},
        {D3DRS_FOGENABLE,FALSE},{D3DRS_VERTEXBLEND,D3DVBF_DISABLE},{D3DRS_INDEXEDVERTEXBLENDENABLE,FALSE}};
    reason=4;
    for(auto state:render) { DWORD value=0;if(FAILED(device->GetRenderState(state.first,&value)) || value!=state.second) {
        counters.lastState=state.first;counters.lastExpected=state.second;counters.lastActual=value;return false;
    } }
    const std::pair<D3DTEXTURESTAGESTATETYPE,DWORD> stage[]={
        {D3DTSS_COLOROP,D3DTOP_SELECTARG1},{D3DTSS_COLORARG1,D3DTA_TEXTURE},
        {D3DTSS_ALPHAOP,D3DTOP_SELECTARG1},{D3DTSS_ALPHAARG1,D3DTA_TEXTURE},
        {D3DTSS_TEXCOORDINDEX,D3DTSS_TCI_CAMERASPACEPOSITION},{D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_COUNT2}};
    reason=8;
    for(auto state:stage) { DWORD value=0;if(FAILED(device->GetTextureStageState(0,state.first,&value)) || value!=state.second) {
        counters.lastState=state.first;counters.lastExpected=state.second;counters.lastActual=value;return false;
    } }
    for(auto state:{D3DTSS_MINFILTER,D3DTSS_MAGFILTER}) { DWORD v=0;if(FAILED(device->GetTextureStageState(0,state,&v)) || (v!=D3DTEXF_POINT && v!=D3DTEXF_LINEAR)) return false; }
    DWORD color=0,second=0;
    if(FAILED(device->GetRenderState(D3DRS_COLORWRITEENABLE,&color)) || (color!=7 && color!=15)
        || FAILED(device->GetTextureStageState(1,D3DTSS_COLOROP,&second)) || second!=D3DTOP_DISABLE) return false;
    reason=16;IDirect3DIndexBuffer8 *indices=nullptr;UINT base=1;
    result=device->GetIndices(&indices,&base);if(indices) indices->Release();
    if(FAILED(result) || !indices || base) return false;
    reason=32;IDirect3DSurface8 *target=nullptr;D3DSURFACE_DESC desc{};
    result=device->GetRenderTarget(&target);if(SUCCEEDED(result) && target) result=target->GetDesc(&desc);
    if(target) target->Release();if(FAILED(result) || !target || desc.Format!=D3DFMT_A8R8G8B8) return false;
    reason=64;D3DMATRIX world{};
    if(FAILED(device->GetTransform(D3DTS_WORLD,&world)) || FAILED(device->GetTransform(D3DTS_VIEW,&view))
        || FAILED(device->GetTransform(D3DTS_TEXTURE0,&texture))) return false;
    for(unsigned row=0;row<4;++row) for(unsigned col=0;col<4;++col)
        if(world.m[row][col]!=(row==col?1.0f:0.0f)) return false;
    // Camera-space position ignores a projective view W; exclude that case.
    const bool valid=view._14==0 && view._24==0 && view._34==0 && view._44==1;
    if(valid)reason=0;return valid;
}
}
namespace QuestXR {
ShroudUploadScope::ShroudUploadScope(IDirect3DSurface8 *source,IDirect3DSurface8 *destination,
    IDirect3DTexture8 *texture,const void *pixels,unsigned pitch,unsigned sourceWidth,unsigned sourceHeight) {
    if (!requested() || mirror.uploading || !source || !destination || !texture || !pixels) return;
    D3DSURFACE_DESC src{},dst{},tex{};
    if(FAILED(source->GetDesc(&src)) || FAILED(destination->GetDesc(&dst)) || FAILED(texture->GetLevelDesc(0,&tex))
        || texture->GetLevelCount()!=1 || src.Format!=D3DFMT_R5G6B5 || dst.Format!=D3DFMT_R5G6B5
        || tex.Format!=D3DFMT_R5G6B5 || dst.Width!=tex.Width || dst.Height!=tex.Height
        || sourceWidth!=src.Width || sourceHeight!=src.Height || pitch<uint64_t(sourceWidth)*2) {
        mirror.mask.Invalidate();return;
    }
    if(mirror.texture!=texture || mirror.source!=source || mirror.width!=dst.Width || mirror.height!=dst.Height) {
        mirror={};
        if(!mirror.mask.Initialize(dst.Width,dst.Height)) return;
        mirror.texture=texture;mirror.source=source;mirror.width=dst.Width;mirror.height=dst.Height;
    }
    mirror.destination=destination;mirror.pixels=pixels;mirror.pitch=pitch;
    mirror.sourceWidth=sourceWidth;mirror.sourceHeight=sourceHeight;mirror.uploading=true;active=true;
}
ShroudUploadScope::~ShroudUploadScope() {
    if(active) { mirror.mask.Finish();mirror.uploading=false;mirror.destination=nullptr;mirror.pixels=nullptr; }
}
ShroudDrawScope::ShroudDrawScope(const TerrainPatchBounds *bounds,unsigned count,IDirect3DVertexBuffer8 *vertices)
    :previous(drawBounds),previousCount(drawCount),previousVertices(drawVertices) {
    drawBounds=requested() && PerformanceEye()>=0?bounds:nullptr;drawCount=drawBounds?count:0;
    drawVertices=drawBounds?vertices:nullptr;
}
ShroudDrawScope::~ShroudDrawScope() { drawBounds=previous;drawCount=previousCount;drawVertices=previousVertices; }
void ForgetShroudTexture(IDirect3DTexture8 *texture) { if(texture && mirror.texture==texture) mirror={}; }
ShroudPassStats TakeShroudPassStats() { auto out=counters;counters={};return out; }
}
bool QuestXR_CopyShroudRects(IDirect3DDevice8 *device,IDirect3DSurface8 *source,const RECT *rectangles,
    unsigned count,IDirect3DSurface8 *destination,const POINT *points,HRESULT &result) {
    if(!mirror.uploading || destination!=mirror.destination || !device) return false;
    result=device->CopyRects(source,rectangles,count,destination,points);
    bool tracked=SUCCEEDED(result) && source==mirror.source && rectangles && points && count;
    if(tracked) for(unsigned i=0;i<count;++i) tracked=mirror.mask.Observe(mirror.pixels,mirror.sourceWidth,
        mirror.sourceHeight,mirror.pitch,{rectangles[i].left,rectangles[i].top,rectangles[i].right,rectangles[i].bottom},
        points[i].x,points[i].y,true) && tracked;
    if(!tracked) { mirror.mask.Invalidate();if(QuestXR::PerformanceMetricsRequested()) ++counters.uploadFailures; }
    return true;
}
bool QuestXR_DrawNeutralShroud(IDirect3DDevice8 *device,unsigned primitive,unsigned minVertex,
    unsigned vertices,unsigned startIndex,unsigned polygons) {
    if(!drawBounds || !drawCount || !requested()) return false;
    D3DMATRIX view{},texture{};
    if(primitive!=D3DPT_TRIANGLELIST || minVertex || startIndex || vertices!=drawCount*4096
        || polygons!=drawCount*2048 || !contract(device,view,texture)) {
        if(QuestXR::PerformanceMetricsRequested()) counters.contractFallback+=drawCount;
        attempted(0,1);return false;
    }
    uint32_t skip=0;unsigned skipped=0;
    for(unsigned i=0;i<drawCount;++i) {
        if(!drawBounds[i].Complete() || !drawBounds[i].uploaded) { if(QuestXR::PerformanceMetricsRequested()) ++counters.unknownBounds;continue; }
        QuestXR::ShroudRect footprint;
        if(QuestXR::ShroudFootprint(drawBounds[i].bounds,&view._11,&texture._11,mirror.width,mirror.height,footprint)
            && mirror.mask.Neutral(footprint)) { skip|=1u<<i;++skipped; }
    }
    if(!skip) { attempted(0,1);return false; }
    const auto runs=QuestXR::ShroudRetainedRuns(drawCount,skip);
    attempted(skipped,runs.count);
    for(unsigned r=0;r<runs.count;++r) {
        const auto run=runs.ranges[r];
        const HRESULT result=device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,run.first*4096,run.count*4096,
            run.first*6144,run.count*2048);
        if(FAILED(result)) {
            __android_log_print(ANDROID_LOG_ERROR,"GeneralsXR","Neutral shroud indexed draw failed: 0x%x",unsigned(result));
            break; // Never redraw a partly completed multiplicative pass.
        }
    }
    return true;
}
#endif
