/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2025 Electronic Arts Inc.
** Copyright 2026 GeneralsVRQuest3 contributors
**
** GPL-3.0-or-later. See LICENSE and LICENSE.md.
*/

#pragma once

#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)

// Consumes the controller snapshot produced for the current predicted XR frame
// and translates world interactions into the existing Generals client/message
// abstractions. Called before GameEngine::update consumes the input stream.
extern "C" void QuestVRInput_Update();

// The battle HUD is user-toggleable, while an open radial temporarily summons
// it. Full-screen shell/options menus remain controlled by the engine.
extern "C" bool QuestVRInput_ShouldShowHud();
extern "C" bool QuestVRInput_IsRadialOpen();
extern "C" bool QuestVRInput_ConsumesUiPoint(float normalizedX, float normalizedY);

struct QuestVRWorldTransform
{
	float worldUnitsPerMeter;
	float yawRadians;
	float offset[3];
	float comfortPitchRadians;
	float comfortPivotMeters[3];
};

// Shared by the eye renderer and ray adapter so grabbed tabletop transforms
// cannot make visual pointers diverge from their collision rays.
void QuestVRInput_GetWorldTransform(QuestVRWorldTransform *transform);

class CameraClass;
// Draws controller rays and the active selection rectangle into the current
// eye render pass using the exact camera already bound by the stereo renderer.
void QuestVRInput_Render(CameraClass *camera);
void QuestVRInput_DrawUiOverlay();
void QuestVRInput_Shutdown();

#endif
