// SPDX-License-Identifier: GPL-3.0-or-later
// Narrow data/cache checks; this is not a game replay or runtime CRC certificate.
#include "VRDevice/QuestPerformance.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <utility>
static void require(bool value, const char *message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int main() {
    using namespace QuestXR;
    require(!PerformanceRequested(PerfOption::AudioGrowth), "Default off");
    _putenv_s("GX_QUEST_PERF_AUDIO", "true");
    require(!PerformanceRequested(PerfOption::AudioGrowth), "Only exact 1 enables option");
    _putenv_s("GX_QUEST_PERF_AUDIO", "1");
    require(PerformanceRequested(PerfOption::AudioGrowth), "Exact 1 enables option");
    require(!PerformanceRequested(PerfOption::TrackStreaming), "Track streaming defaults off");
    _putenv_s("GX_QUEST_TRACK_STREAMING", "true");
    require(!PerformanceRequested(PerfOption::TrackStreaming), "Track streaming requires exact 1");
    _putenv_s("GX_QUEST_TRACK_STREAMING", "1");
    require(PerformanceRequested(PerfOption::TrackStreaming), "Track streaming can be enabled independently");
    _putenv_s("GX_QUEST_TRACK_STREAMING", "");
    require(!PerformanceRequested(PerfOption::HudTransfer), "HUD transfer defaults off");
    _putenv_s("GX_QUEST_HUD_TRANSFER", "true");
    require(!PerformanceRequested(PerfOption::HudTransfer), "HUD transfer requires exact 1");
    _putenv_s("GX_QUEST_HUD_TRANSFER", "1");
    require(PerformanceRequested(PerfOption::HudTransfer), "HUD transfer can be enabled independently");
    _putenv_s("GX_QUEST_HUD_TRANSFER", "");
    require(!PerformanceRequested(PerfOption::TerrainCulling), "Terrain culling defaults off");
    _putenv_s("GX_QUEST_TERRAIN_CULLING", "true");
    require(!PerformanceRequested(PerfOption::TerrainCulling), "Terrain culling requires exact 1");
    _putenv_s("GX_QUEST_TERRAIN_CULLING", "1");
    require(!PerformanceRequested(PerfOption::TerrainCulling), "Retired terrain culling ignores stale exact-1 settings");
    _putenv_s("GX_QUEST_TERRAIN_CULLING", "");
    require(!PerformanceRequested(PerfOption::NeutralShroud), "Neutral shroud defaults off");
    _putenv_s("GX_QUEST_NEUTRAL_SHROUD", "true");
    require(!PerformanceRequested(PerfOption::NeutralShroud), "Neutral shroud requires exact 1");
    _putenv_s("GX_QUEST_NEUTRAL_SHROUD", "1");
    require(PerformanceRequested(PerfOption::NeutralShroud), "Neutral shroud enables independently");
    _putenv_s("GX_QUEST_NEUTRAL_SHROUD", "");
    require(!PerformanceRequested(PerfOption::DepthDiscard), "Depth discard defaults off");
    _putenv_s("GX_QUEST_DEPTH_DISCARD", "true");
    require(!PerformanceRequested(PerfOption::DepthDiscard), "Depth discard requires exact 1");
    _putenv_s("GX_QUEST_DEPTH_DISCARD", "1");
    require(PerformanceRequested(PerfOption::DepthDiscard), "Depth discard enables independently");
    _putenv_s("GX_QUEST_DEPTH_DISCARD", "");
    require(!PerformanceRequested(PerfOption::DirectColorCopy), "Direct copy defaults off");
    _putenv_s("GX_QUEST_DIRECT_COLOR_COPY", "true");
    require(!PerformanceRequested(PerfOption::DirectColorCopy), "Direct copy needs exact 1");
    _putenv_s("GX_QUEST_DIRECT_COLOR_COPY", "1");
    require(PerformanceRequested(PerfOption::DirectColorCopy), "Direct copy enables independently");
    _putenv_s("GX_QUEST_DIRECT_COLOR_COPY", "");
    for (const auto &test : {std::make_pair(PerfOption::TransferBatch,"GX_QUEST_TRANSFER_BATCH"),
        std::make_pair(PerfOption::ShadowCoverage,"GX_QUEST_SHADOW_COVERAGE")}) {
        require(!PerformanceRequested(test.first), "New trials default off");
        _putenv_s(test.second,"true");
        require(!PerformanceRequested(test.first), "New trials require exact 1");
        _putenv_s(test.second,"1");
        require(PerformanceRequested(test.first), "New trials enable independently");
        _putenv_s(test.second,"");
    }
    for (bool planar : {false, true}) for (size_t sampleBytes : {size_t(1),size_t(2),size_t(4)})
        for (size_t channels : {size_t(1),size_t(2),size_t(6)}) {
        std::vector<uint8_t> original, geometric;
        size_t oldGrowths=0, newGrowths=0;
        for (size_t block=0; block<256; ++block) {
            const size_t samples = block % 7 == 0 ? 0 : 256 + block % 31;
            const size_t bytes = samples * channels * sampleBytes;
            std::vector<uint8_t> frame(bytes);
            for (size_t i=0; i<bytes; ++i) frame[i] = uint8_t(i*31+block*7);
            const size_t oldCap=original.capacity(), newCap=geometric.capacity();
            PrepareAudioAppend(original,bytes,false); PrepareAudioAppend(geometric,bytes,true);
            if (planar) {
                for (size_t sample=0; sample<samples; ++sample) for (size_t channel=0; channel<channels; ++channel) {
                    const auto *p=frame.data()+(channel*samples+sample)*sampleBytes;
                    original.insert(original.end(),p,p+sampleBytes); geometric.insert(geometric.end(),p,p+sampleBytes);
                }
            } else if (bytes) {
                original.insert(original.end(),frame.begin(),frame.end()); geometric.insert(geometric.end(),frame.begin(),frame.end());
            }
            oldGrowths += original.capacity()!=oldCap; newGrowths += geometric.capacity()!=newCap;
        }
        require(original==geometric,"Planar/interleaved PCM bytes unchanged");
        require(newGrowths<oldGrowths/4,"Geometric growth reduces allocation churn");
    }
    int owner=0, resource=0, other=0, map=0;
    std::vector<uint8_t> vertices={1,2,3,4,5,6};
    require(!TrackUploadMatches(&owner,&resource,vertices.data(),vertices.size()),"Outside eye inert");
    {
        PerformanceFrameScope frame;
        { PerformanceEyeScope eye(0);
          RememberTrackUpload(&owner,&resource,vertices.data(),vertices.size());
          require(!TrackUploadMatches(&owner,&resource,vertices.data(),vertices.size()),"First eye always uploads");
          require(!ContinueShadowBuffer(&owner,&resource,&map),"First eye resets shadows"); }
        { PerformanceEyeScope eye(1);
          require(TrackUploadMatches(&owner,&resource,vertices.data(),vertices.size()),"Exact second-eye payload can reuse");
          vertices[2]^=1;
          require(!TrackUploadMatches(&owner,&resource,vertices.data(),vertices.size()),"Changed fade/position/order must upload");
          vertices[2]^=1;
          require(!TrackUploadMatches(&other,&resource,vertices.data(),vertices.size()),"Different owner excluded");
          require(!TrackUploadMatches(&owner,&other,vertices.data(),vertices.size()),"Different resource excluded");
          require(!TrackUploadMatches(&owner,&resource,vertices.data(),vertices.size()-1),"Changed visible count excluded");
          require(ContinueShadowBuffer(&owner,&resource,&map),"Second eye same buffers can append");
          require(!ContinueShadowBuffer(&owner,&resource,&map),"Repeated second-eye shadow pass resets");
          InvalidateTrackUpload();
          require(!TrackUploadMatches(&owner,&resource,vertices.data(),vertices.size()),"Reset invalidates upload");
          int diffuse=0;
          require(!TerrainLightLookup(&owner,&map,1,2,vertices,diffuse),"Uncached lighting misses");
          RememberTerrainLight(&owner,&map,1,2,vertices,1234);
          require(TerrainLightLookup(&owner,&map,1,2,vertices,diffuse)&&diffuse==1234,"Exact terrain input returns original color");
          vertices.push_back(7);
          require(!TerrainLightLookup(&owner,&map,1,2,vertices,diffuse),"Any changed light/height/depth byte misses");
          require(!TerrainLightLookup(&owner,&other,1,2,vertices,diffuse),"Map reload excludes stale light");
        }
        require(PerformanceEye()==-1,"Eye scope restores outside state");
    }
    {
        PerformanceFrameScope frame; PerformanceEyeScope eye(1);
        require(!TrackUploadMatches(&owner,&resource,vertices.data(),vertices.size()),"No cache crosses frames");
        int diffuse=0;
        require(!TerrainLightLookup(&owner,&map,1,2,vertices,diffuse),"Terrain cache cleared each pair");
        require(!ContinueShadowBuffer(&owner,&resource,&map),"Missing first eye cannot append");
    }
    std::puts("PASS PCM 9 formats x planar/interleaved; scoped exact-byte upload/light caches; shadow/reset gates");
}
