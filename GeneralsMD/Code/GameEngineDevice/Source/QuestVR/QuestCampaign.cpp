// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestCampaign.h"
#include "VRDevice/QuestCampaignModel.h"
#include "VRDevice/QuestXRInterface.h"
#include "GameLogic/GameLogic.h"
#include "GameClient/View.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Display.h"
#include <memory>
#include <cstdlib>
#include <cstring>

namespace QuestCampaign
{
namespace
{
struct XRDriver
{
    bool active() const { return QuestXR_IsFrameActive(); }
    bool begin() { return QuestXR_BeginFrame(); }
    void end() { QuestXR_EndFrame(); }
} driver;
std::unique_ptr<MovieFrameLease<XRDriver>> lease;
bool buttonsArmed = false;
}
bool IsBlockingMovie() { return lease != nullptr; }
bool IsFlatSequence()
{
    if (IsBlockingMovie()) return true; // scope exists only in campaign movie paths
    static const bool flatShell = [] {
        const char *value = std::getenv("GX_QUEST_FLAT_INTRO");
        return value && std::strcmp(value, "1") == 0;
    }();
    if (FlatShell(flatShell, TheGameLogic && TheGameLogic->getGameMode() == GAME_SHELL))
        return true;
    return FlatSequence(TheGameLogic && TheGameLogic->getGameMode() == GAME_SINGLE_PLAYER,
        TheTacticalView && TheTacticalView->isDoingScriptedCamera(),
        TheDisplay && (TheDisplay->isLetterBoxed() || TheDisplay->isLetterBoxFading()),
        TheInGameUI && !TheInGameUI->getInputEnabled(),
        TheDisplay && TheDisplay->isMoviePlaying());
}
bool NeedsFullUI()
{
    // Victory/defeat windows are centered above the compact wrist HUD in every mode.
    return IsFlatSequence() || (TheGameLogic && TheGameLogic->isInGame()
        && TheInGameUI && !TheInGameUI->getInputEnabled());
}
BlockingMovieScope::BlockingMovieScope() : owner(!lease)
{
    if (owner) {
        QuestXR_SetMenuPresentation(QUEST_XR_MENU_FRONTEND);
        lease.reset(new MovieFrameLease<XRDriver>(driver));
        buttonsArmed = false; // require release after the menu's selection click
    }
}
BlockingMovieScope::~BlockingMovieScope()
{
    if (owner) lease.reset();
}
bool PollMovieAbort()
{
    if (!lease) return false;
    if (!lease->poll()) return true; // same upstream cleanup as Escape on XR failure
    QuestXRControllerState right{};
    if (!QuestXR_IsFrameActive() || !QuestXR_GetControllerState(1, &right)
        || !right.connected) return false;
    const auto skip = QUEST_XR_BUTTON_TRIGGER | QUEST_XR_BUTTON_SECONDARY;
    if ((right.buttons & skip) == 0) buttonsArmed = true;
    return buttonsArmed && (right.pressed & skip) != 0;
}
bool BeginMovieDraw()
{
    if (!lease) return true;
    return lease->poll() && QuestXR_ShouldRenderFrame();
}
void EndMovieDraw() { if (lease) lease->present(); }
}
