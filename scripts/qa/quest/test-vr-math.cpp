// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestVRMath.h"
#include <cmath>
#include <initializer_list>
#include <cstdio>
#include <cstdlib>

static void require(bool value, const char *message)
{
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main()
{
    for (int i = -100; i <= 100; ++i) {
        const float angle = i * 0.0314159265f;
        const auto aim = QuestXRRotateAim(0, std::sin(angle / 2), 0, std::cos(angle / 2));
        require(std::fabs(aim.x + std::sin(angle)) < 1e-5f, "Yaw aim direction");
        require(std::fabs(aim.z + std::cos(angle)) < 1e-5f, "Yaw aim depth");
        require(std::fabs(aim.x * aim.x + aim.y * aim.y + aim.z * aim.z - 1) < 1e-5f,
                "Normalized tracking quaternion must produce a unit ray");
        const float stick = i / 100.0f;
        const float dead = QuestXRApplyDeadzone(stick);
        require(std::fabs(dead) <= 1.0f, "Deadzone range");
        require(std::fabs(dead + QuestXRApplyDeadzone(-stick)) < 1e-6f, "Deadzone symmetry");
        if (std::fabs(stick) <= .20f) require(dead == 0, "Neutral stick cannot move camera");
    }
    auto axis = QuestXRLockCameraAxis(QUEST_VR_CAMERA_STICK_NONE, 0.8f, 0.3f, false);
    require(axis == QUEST_VR_CAMERA_STICK_TURN, "Dominant turn gesture");
    require(QuestXRLockCameraAxis(axis, 0.3f, 0.9f, false) == axis, "Gesture axis remains locked");
    require(QuestXRLockCameraAxis(axis, 0, 0, false) == QUEST_VR_CAMERA_STICK_NONE, "Neutral releases axis");
    require(QuestXRLockCameraAxis(axis, 1, 0, true) == QUEST_VR_CAMERA_STICK_NONE, "Radial consumes axis");
    require(QuestXRLockCameraAxis(QUEST_VR_CAMERA_STICK_NONE, 0.1f, 0.9f, false) == QUEST_VR_CAMERA_STICK_ZOOM,
            "Dominant height gesture");
    for (float crop : {0.0f, 0.50f, 0.65f, 0.95f}) {
        for (int i = 0; i <= 100; ++i) {
            const float v = i / 100.0f;
            const float source = QuestXRMapCroppedV(v, crop);
            require(std::fabs((source - crop) / (1 - crop) - v) < 2e-6f,
                    "Cropped HUD pointer must invert the displayed subimage");
        }
    }
    require(QuestXRClampWorldScale(1) == 20, "Scale lower bound");
    // Regression: hover a fixed physical point while the description opens.
    // It must still address the same source pixel with a tilted wrist plane.
    for (float tilt : {0.0f, -0.9f, 0.6f}) {
        const float fullHeight = 0.4125f * 1080.0f / 1920.0f;
        const float compactHeight = fullHeight * (1.0f - 0.65f);
        for (float sourceV : {0.66f, 0.75f, 0.95f, 1.0f}) {
            const float fixedY = compactHeight * 0.5f - (sourceV - 0.65f) * fullHeight;
            const float fixedWorldY = fixedY * std::cos(tilt);
            const float fixedWorldZ = fixedY * std::sin(tilt);
            for (float crop : {0.65f, 0.50f, 0.30f, 0.0f}) {
                const float center = QuestXRHudGrowthCenterOffset(crop, 0.65f, fullHeight);
                const float relativeY = (fixedWorldY - center * std::cos(tilt)) * std::cos(tilt)
                    + (fixedWorldZ - center * std::sin(tilt)) * std::sin(tilt);
                const float height = fullHeight * (1.0f - crop);
                const float hit = QuestXRMapCroppedV(0.5f - relativeY / height, crop);
                require(std::fabs(hit - sourceV) < 2e-6f, "HUD growth must not move a hovered target");
            }
        }
    }
    require(QuestXRClampWorldScale(2000) == 1200, "Scale upper bound");
    require(QuestXRBattlefieldVerticalOffset(100, 10, 1, 20) == -77,
            "Battlefield anchor must account for tracked eye height");
    std::puts("Quest ray, deadzone, crop and anchor behavior passed");
}
