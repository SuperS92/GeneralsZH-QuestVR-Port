/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2025 Electronic Arts Inc.
** Copyright 2026 GeneralsVRQuest3 contributors
**
** GPL-3.0-or-later. See LICENSE and LICENSE.md.
*/

#pragma once

#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)

#include <cstdint>

struct IDirect3DDevice8;
struct IDirect3DSurface8;

// One scale shared by stereo rendering and controller-to-world composition.
// A mismatch would make the ray visibly diverge from the rendered controller.
constexpr float QUEST_XR_WORLD_UNITS_PER_METER = 500.0f;

// C-compatible boundary between the engine renderer and the Android/OpenXR
// owner in libmain. Values use native OpenXR conventions: metres, quaternion
// (x,y,z,w), and asymmetric FOV angles in radians.
struct QuestXREyeView
{
	float position[3];
	float orientation[4];
	float angleLeft;
	float angleRight;
	float angleUp;
	float angleDown;
};

enum QuestXRControllerButton : std::uint32_t
{
	QUEST_XR_BUTTON_TRIGGER = 1u << 0,
	QUEST_XR_BUTTON_GRIP = 1u << 1,
	QUEST_XR_BUTTON_PRIMARY = 1u << 2,
	QUEST_XR_BUTTON_SECONDARY = 1u << 3,
	QUEST_XR_BUTTON_STICK = 1u << 4,
	QUEST_XR_BUTTON_MENU = 1u << 5,
	// One-frame gesture event synthesized by the OpenXR owner. It is carried
	// only in `pressed`, never in `buttons`, so it cannot become a stuck input.
	QUEST_XR_BUTTON_MENU_LONG = 1u << 6,
};

// Selects how the shared Generals UI render target is exposed by the OpenXR
// compositor. The HUD mode presents only the bottom control-bar region while
// preserving coordinates in the original full-size render target.
enum QuestXRMenuPresentation : std::uint32_t
{
	QUEST_XR_MENU_HIDDEN = 0,
	QUEST_XR_MENU_FRONTEND = 1,
	QUEST_XR_MENU_HUD = 2,
};

struct QuestXRControllerState
{
	std::uint32_t connected;
	std::uint32_t poseValid;
	std::uint32_t menuPointValid;
	std::uint32_t buttons;
	std::uint32_t pressed;
	std::uint32_t released;
	float stick[2];
	float menuPoint[2];
	float position[3];
	float orientation[4];
};

// GeneralsX @bugfix Codex 15/09/2026 The XR menu target follows the logical
// SAGE canvas, not the much larger Android compositor backbuffer.
extern "C" bool QuestXR_AttachD3D8Device(IDirect3DDevice8 *device,
	std::uint32_t menuWidth, std::uint32_t menuHeight);
extern "C" void QuestXR_DetachD3D8Device();
extern "C" bool QuestXR_PollLifecycle();
extern "C" bool QuestXR_BeginFrame();
extern "C" bool QuestXR_EndFrame();
extern "C" bool QuestXR_IsFrameActive();
extern "C" bool QuestXR_ShouldRenderFrame();
extern "C" IDirect3DSurface8 *QuestXR_GetMenuDepthSurface(IDirect3DDevice8 *device);
extern "C" bool QuestXR_GetEyeView(std::uint32_t eye, QuestXREyeView *view);
extern "C" IDirect3DSurface8 *QuestXR_GetEyeSurface(std::uint32_t eye);
extern "C" bool QuestXR_ResolveEyeSurface(IDirect3DDevice8 *device, std::uint32_t eye);
extern "C" IDirect3DSurface8 *QuestXR_GetDepthSurface();
extern "C" void QuestXR_MarkEyeRenderingComplete();
extern "C" IDirect3DSurface8 *QuestXR_GetMenuSurface();
extern "C" void QuestXR_SetMenuPresentation(std::uint32_t presentation);
extern "C" void QuestXR_SetMenuCropTop(float cropTop);
extern "C" void QuestXR_MarkMenuRenderingComplete(std::uint32_t presentation);
extern "C" bool QuestXR_GetControllerState(std::uint32_t hand, QuestXRControllerState *state);

#endif
