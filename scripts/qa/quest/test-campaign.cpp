// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestCampaignModel.h"
#include <cassert>
#include <cstdio>
#include <vector>

struct Runtime
{
    bool inFrame = false, running = true, failed = false;
    std::vector<char> calls;
    bool active() const { return inFrame; }
    bool begin() {
        assert(!inFrame); calls.push_back('B');
        if (failed) return false;
        inFrame = running; return true;
    }
    void end() { assert(inFrame); calls.push_back('E'); inFrame = false; }
};
int main()
{
    assert(!QuestCampaign::FlatShell(false, true));
    assert(!QuestCampaign::FlatShell(true, false)); // matches/replays are never shell
    assert(QuestCampaign::FlatShell(true, true));
    // Every cue combination is excluded outside campaign, including a disabled
    // multiplayer victory UI, scripted shell camera and replay cinematic.
    for (unsigned cues=0; cues<16; ++cues) {
        assert(!QuestCampaign::FlatSequence(false,cues&1,cues&2,cues&4,cues&8));
        assert(QuestCampaign::FlatSequence(true,cues&1,cues&2,cues&4,cues&8)==(cues!=0));
    }
    Runtime runtime; runtime.begin();
    {
        QuestCampaign::MovieFrameLease<Runtime> movie(runtime);
        for (int frame=0;frame<120;++frame) {
            assert(movie.poll()); // movie-ready polling must not double-begin
            assert(movie.poll()); assert(movie.poll());
            movie.present();
        }
        assert(movie.poll()); // early Escape with a pending frame
    }
    assert(runtime.active()); runtime.end();
    assert(runtime.calls.size()==246);
    for (unsigned i=0;i<runtime.calls.size();++i)
        assert(runtime.calls[i]==(i%2 ? 'E':'B'));
    Runtime stopped;
    stopped.running=false;
    {
        QuestCampaign::MovieFrameLease<Runtime> movie(stopped);
        assert(movie.poll()); assert(!stopped.active());
        movie.present(); // no xrEndFrame without successful xrBeginFrame
        stopped.running=true;
        assert(movie.poll()); assert(stopped.active()); movie.present();
    }
    assert(!stopped.active());
    Runtime failure; failure.begin();
    {
        QuestCampaign::MovieFrameLease<Runtime> movie(failure);
        failure.failed=true; assert(!movie.poll()); movie.present();
    }
    assert(!failure.active());
    assert((failure.calls==std::vector<char>{'B','E','B','B'}));
    puts("PASS campaign mode exclusion, movie cadence, early abort, background/resume and failed begin ownership");
}
