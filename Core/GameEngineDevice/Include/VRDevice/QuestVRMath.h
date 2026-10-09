/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 GeneralsVRQuest3 contributors
**
** GPL-3.0-or-later. See LICENSE and LICENSE.md.
*/

#pragma once

struct QuestXRFloat3
{
	float x;
	float y;
	float z;
};

// Rotate OpenXR's local -Z aim vector by quaternion (x, y, z, w).
constexpr QuestXRFloat3 QuestXRRotateAim(float x, float y, float z, float w)
{
	return {
		-2.0f * (x * z + w * y),
		 2.0f * (w * x - y * z),
		-1.0f + 2.0f * (x * x + y * y)
	};
}

constexpr float QuestXRApplyDeadzone(float value, float deadzone = 0.20f)
{
	return value > deadzone ? (value - deadzone) / (1.0f - deadzone)
		: value < -deadzone ? (value + deadzone) / (1.0f - deadzone)
		: 0.0f;
}

// Convert a normalized coordinate on an OpenXR sub-image back to the source
// render target. This is the critical half of cropped HUD hit testing.
constexpr float QuestXRMapCroppedV(float croppedV, float cropTop)
{
	return cropTop + croppedV * (1.0f - cropTop);
}

// GeneralsX @bugfix Codex 30/09/2026 Anchor the compact HUD bottom while
// descriptions extend its top. The physical source-pixel scale stays constant.
constexpr float QuestXRHudGrowthCenterOffset(float cropTop, float baseCropTop,
	float fullImageHeightMeters)
{
	return (baseCropTop - cropTop) * fullImageHeightMeters * 0.5f;
}

// Five controller-friendly control-group slots: neutral plus the four
// cardinal stick directions. The indices map directly to Generals teams 0-4.
constexpr int QuestXRControlGroupSlot(float x, float y, float threshold = 0.55f)
{
	return x * x + y * y < threshold * threshold ? 0
		: y >= x && y >= -x ? 1
		: x >= y && x >= -y ? 2
		: y <= x && y <= -x ? 3
		: 4;
}

constexpr float QuestXRClampWorldScale(float scale,
	float minimum = 20.0f, float maximum = 1200.0f)
{
	return scale < minimum ? minimum : scale > maximum ? maximum : scale;
}

// GeneralsX @feature Codex 11/09/2026 Ground-safe Quest Battlefield preset math.
// The tactical camera remains the renderer's anchor in Battlefield mode. This
// offset moves the tracked eye from that anchor to a requested physical eye
// height above the terrain without assuming whether LOCAL starts at head or
// floor level. It is pure so the placement can be host-tested without OpenXR.
constexpr float QuestXRBattlefieldVerticalOffset(float cameraHeight,
	float terrainHeight, float trackedEyeHeight, float worldUnitsPerMeter,
	float requestedEyeHeightMeters = 1.65f)
{
	return terrainHeight - cameraHeight
		+ (requestedEyeHeightMeters - trackedEyeHeight) * worldUnitsPerMeter;
}

// These compile in every Android build and cover the exact shared math used by
// menu intersection, world rays, and stick mapping.
static_assert(QuestXRRotateAim(0.0f, 0.0f, 0.0f, 1.0f).z == -1.0f);
static_assert(QuestXRRotateAim(0.0f, 1.0f, 0.0f, 0.0f).z == 1.0f);
static_assert(QuestXRRotateAim(1.0f, 0.0f, 0.0f, 0.0f).z == 1.0f);
static_assert(QuestXRApplyDeadzone(0.10f) == 0.0f);
static_assert(QuestXRApplyDeadzone(1.0f) == 1.0f);
static_assert(QuestXRApplyDeadzone(-1.0f) == -1.0f);
static_assert(QuestXRMapCroppedV(0.0f, 0.65f) == 0.65f);
static_assert(QuestXRMapCroppedV(0.5f, 0.50f) == 0.75f);
static_assert(QuestXRMapCroppedV(1.0f, 0.50f) == 1.0f);
static_assert(QuestXRControlGroupSlot(0.0f, 0.0f) == 0);
static_assert(QuestXRControlGroupSlot(0.0f, 1.0f) == 1);
static_assert(QuestXRControlGroupSlot(1.0f, 0.0f) == 2);
static_assert(QuestXRControlGroupSlot(0.0f, -1.0f) == 3);
static_assert(QuestXRControlGroupSlot(-1.0f, 0.0f) == 4);
static_assert(QuestXRClampWorldScale(10.0f) == 20.0f);
static_assert(QuestXRClampWorldScale(500.0f) == 500.0f);
static_assert(QuestXRClampWorldScale(2000.0f) == 1200.0f);
static_assert(QuestXRBattlefieldVerticalOffset(100.0f, 0.0f, 0.0f, 20.0f)
	== -67.0f);
static_assert(QuestXRBattlefieldVerticalOffset(100.0f, 10.0f, 1.0f, 20.0f)
	== -77.0f);

// Offline v0.44 right-stick arbitration, independent of left-stick panning.
enum QuestVRCameraStickAxis {
    QUEST_VR_CAMERA_STICK_NONE = 0,
    QUEST_VR_CAMERA_STICK_TURN,
    QUEST_VR_CAMERA_STICK_ZOOM,
};
constexpr QuestVRCameraStickAxis QuestXRLockCameraAxis(QuestVRCameraStickAxis previous,
    float turn, float height, bool consumed)
{
    if (consumed || (turn == 0.0f && height == 0.0f)) return QUEST_VR_CAMERA_STICK_NONE;
    if (previous != QUEST_VR_CAMERA_STICK_NONE) return previous;
    const float absTurn = turn < 0 ? -turn : turn;
    const float absHeight = height < 0 ? -height : height;
    return absTurn >= absHeight * 0.75f ? QUEST_VR_CAMERA_STICK_TURN : QUEST_VR_CAMERA_STICK_ZOOM;
}
