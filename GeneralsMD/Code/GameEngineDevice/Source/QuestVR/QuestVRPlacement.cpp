// SPDX-License-Identifier: GPL-3.0-or-later
// Spatial input reuses upstream's placement validation and command generation.
#include "GameClient/InGameUI.h"
#include "GameClient/PlaceEventTranslator.h"
#include "Common/MessageStream.h"
#include "QuestVRPlacement.h"

void QuestVRCommitPlacement(const Coord3D &position, Real angle)
{
    if (TheInGameUI == nullptr || TheInGameUI->getPendingPlaceType() == nullptr)
        return;
    TheInGameUI->setPlacementWorldOverride(&position, angle);
    const ICoord2D anchor = {0, 0};
    TheInGameUI->setPlacementStart(&anchor);
    GameMessage *click = newInstance(GameMessage)(GameMessage::MSG_MOUSE_LEFT_CLICK);
    click->appendPixelArgument(anchor);
    PlaceEventTranslator upstreamPlacement;
    upstreamPlacement.translateGameMessage(click);
    deleteInstance(click);
    TheInGameUI->setPlacementStart(nullptr);
}
