// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestDepthDiscard.h"
#include "VRDevice/QuestPerformance.h"
#include <cstdio>
#include <cstdlib>
namespace { bool enabled=false;unsigned queued=0;const void *expectedDevice=nullptr,*expectedDepth=nullptr; }
namespace QuestXR {
bool PerformanceRequested(PerfOption option) { return enabled && option==PerfOption::DepthDiscard; }
bool PerformanceMetricsRequested() { return false; }
}
bool QuestXR_QueueEyeDepthDiscard(IDirect3DDevice8 *device,IDirect3DSurface8 *depth,unsigned eye) {
    if (device!=expectedDevice || depth!=expectedDepth || eye>=2) std::abort();
    ++queued;return true;
}
void require(bool value) { if(!value) std::abort(); }
int main() {
    int deviceToken=1,depthToken=2,otherToken=3;
    auto *device=reinterpret_cast<IDirect3DDevice8 *>(&deviceToken);
    auto *depth=reinterpret_cast<IDirect3DSurface8 *>(&depthToken);
    auto *other=reinterpret_cast<IDirect3DDevice8 *>(&otherToken);
    expectedDevice=device;expectedDepth=depth;
    QuestXR::DiscardCompletedEyeDepth(device,false);require(queued==0);
    { QuestXR::EyeDepthDiscardScope scope(device,depth,0);
      QuestXR::DiscardCompletedEyeDepth(device,false);require(queued==0); }
    enabled=true;
    for(unsigned i=0;i<200;++i) {
        const unsigned before=queued;
        { QuestXR::EyeDepthDiscardScope scope(device,depth,i%2);
          QuestXR::DiscardCompletedEyeDepth(device,false);
          QuestXR::DiscardCompletedEyeDepth(device,false);require(queued==before+1); }
        QuestXR::DiscardCompletedEyeDepth(device,false);require(queued==before+1);
    }
    for(unsigned invalid=0;invalid<6;++invalid) {
        const unsigned before=queued;
        QuestXR::EyeDepthDiscardScope scope(invalid==0?nullptr:device,invalid==1?nullptr:depth,invalid==2?2:0);
        QuestXR::DiscardCompletedEyeDepth(invalid==3?other:invalid==4?nullptr:device,invalid==5);
        QuestXR::DiscardCompletedEyeDepth(device,false);require(queued==before);
    }
    const unsigned before=queued;
    { QuestXR::EyeDepthDiscardScope outer(device,depth,0);
      { QuestXR::EyeDepthDiscardScope inner(device,depth,1);QuestXR::DiscardCompletedEyeDepth(device,false); }
      QuestXR::DiscardCompletedEyeDepth(device,false);require(queued==before+2); }
    QuestXR::EyeDepthTicket stale{device,depth,0,true,false};
    require(!stale.Consume(other,false));require(!stale.Consume(device,false));
    std::puts("PASS actual completed-eye scope: off/outside, each eye, consumed once, wrong/null device/depth, invalid eye, present, nesting and expired scope");
}
