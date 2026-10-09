// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
namespace QuestCampaign
{
bool IsFlatSequence();
bool NeedsFullUI();
bool IsBlockingMovie();
bool PollMovieAbort();
bool BeginMovieDraw();
void EndMovieDraw();
class BlockingMovieScope
{
public:
    BlockingMovieScope();
    ~BlockingMovieScope();
    BlockingMovieScope(const BlockingMovieScope &) = delete;
    BlockingMovieScope &operator=(const BlockingMovieScope &) = delete;
private:
    bool owner;
};
}
#endif
