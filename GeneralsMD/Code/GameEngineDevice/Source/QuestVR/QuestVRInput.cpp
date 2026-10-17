/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2025 Electronic Arts Inc.
** Copyright 2026 GeneralsVRQuest3 contributors
**
** GPL-3.0-or-later. See LICENSE and LICENSE.md.
*/

#include "VRDevice/QuestVRInput.h"
#include "VRDevice/QuestVRKeyboard.h"
#include "VRDevice/QuestCampaign.h"

#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)

#include "VRDevice/QuestXRInterface.h"
#include "VRDevice/QuestVRMath.h"
#include "VRDevice/QuestComfort.h"
#include <cstdlib>

#include "Common/Debug.h"
#include "Common/FramePacer.h"
#include "Common/GameCommon.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/Override.h"
#include "Common/Team.h"
#include "Common/ThingTemplate.h"
#include "Common/UserPreferences.h"
#include "GameClient/CommandXlat.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableInfo.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameClient.h"
#include "GameClient/InGameUI.h"
#include "GameClient/PlaceEventTranslator.h"
#include "QuestVRPlacement.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/TerrainLogic.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DView.h"
#include "WW3D2/coltest.h"
#include "WW3D2/line3d.h"
#include "WW3D2/rendobj.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/scene.h"
#include "WW3D2/ww3d.h"
#include "WWMath/lineseg.h"

#include <android/log.h>
#include <chrono>
#include <cmath>

namespace
{
// GeneralsX @bugfix Codex 18/09/2026 Selection feedback must remain legible
// after the opaque battlefield has populated the eye depth buffer. Keep the
// controller ray depth-tested, but render box and health feedback as a true
// alpha overlay without feeding WW3D's shared static-sort list.
class QuestOverlayLine3DClass final : public Line3DClass
{
	public:
		QuestOverlayLine3DClass(const Vector3 &start, const Vector3 &end,
			float width, float red, float green, float blue, float opacity) :
			Line3DClass(start, end, width, red, green, blue, opacity)
		{
			Shader.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);
			Shader.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);
			Set_Sort_Level(0); // SORT_LEVEL_NONE; keep this TU independent of W3D file-format headers.
		}
};

constexpr Real kAimMaxDistance = 20000.0f;
constexpr Real kAimStep = 60.0f;
constexpr std::uint32_t kRightHand = 1;
// GeneralsX @tweak Codex 30/09/2026 Keep the tested small-deflection baseline.
// The outer stick range gets a smooth boost up to twice the previous maximum.
constexpr Real kScrollAmount = 500.0f;
constexpr Real kMinimumTwoHandSpan = 0.08f;
constexpr Real kTwoHandScaleSensitivity = 2.0f;
constexpr Int kMaximumSelectionMarkers = 40;
// GeneralsX @feature Codex 16/09/2026 Match GeneralsVR's vertical-flight
// controls. World scale remains a separate deliberate two-grip gesture.
constexpr Real kStickHeightSpeed = 1.6f;
constexpr Real kMinimumCameraHeight = 30.0f;
constexpr Real kMaximumCameraHeight = 6000.0f;
// GeneralsX @feature Codex 11/09/2026 Named, persistent Tabletop/Battlefield presets.
constexpr Real kBattlefieldWorldUnitsPerMeter = 20.0f;
constexpr Real kBattlefieldEyeHeightMeters = 1.65f;
constexpr Int kRadialCommandSectors = 7;
constexpr Real kRadialPick = 0.55f;
constexpr Real kRadialRelease = 0.25f;
constexpr Real kPi = 3.14159265358979323846f;
constexpr auto kRadialTimeout = std::chrono::milliseconds(6000);
constexpr auto kBHoldTime = std::chrono::milliseconds(350);
const wchar_t *kRadialCommandNames[kRadialCommandSectors] = {
	L"Stop", L"Attack move", L"Guard", L"Scatter", L"Cheer", L"Idle worker", L"Menu"};

// GeneralsX @bugfix Codex 15/09/2026 The SAGE camera-angle convention is
// opposite to the OpenXR thumbstick X convention. A physical push to the right
// must turn the view to the right, which decreases the engine camera angle.
constexpr Real cameraTurnDelta(Real stickX, Real step)
{
	return -stickX * step;
}
static_assert(cameraTurnDelta(1.0f, 1.0f) < 0.0f,
	"right-stick input must decrease the SAGE camera angle");

enum QuestVRViewMode
{
	QUEST_VR_VIEW_TABLETOP = 0,
	QUEST_VR_VIEW_BATTLEFIELD = 1,
};

enum QuestVRRadialKind
{
	QUEST_VR_RADIAL_NONE = 0,
	QUEST_VR_RADIAL_COMMANDS,
	QUEST_VR_RADIAL_GROUPS,
};


struct BoxSelectionState
{
	Bool armed = FALSE;
	Bool boxing = FALSE;
	Bool additive = FALSE;
	Coord3D start{};
	Coord3D end{};
	std::chrono::steady_clock::time_point pressedAt{};
};

BoxSelectionState g_boxSelection;
SimpleSceneClass *g_pointerScene = nullptr;
Line3DClass *g_rayLines[2] = {nullptr, nullptr};
Line3DClass *g_boxLines[4] = {nullptr, nullptr, nullptr, nullptr};
Line3DClass *g_healthFill[kMaximumSelectionMarkers] = {};
Int g_lastDiagnosticMarkerCount = -1;
Vector3 g_boxRight(1.0f, 0.0f, 0.0f);
Vector3 g_boxForward(0.0f, 1.0f, 0.0f);
const ThingTemplate *g_activePlacement = nullptr;
Bool g_placementArmed = FALSE;
Real g_placementAngle = 0.0f;
std::uint32_t g_uiCapturedButtons = 0;
ObjectID g_lastSingleSelection = INVALID_ID;
std::chrono::steady_clock::time_point g_lastSingleSelectionTime{};
std::chrono::steady_clock::time_point g_lastRightStickClickTime{};
QuestVRWorldTransform g_worldTransform = {
	QUEST_XR_WORLD_UNITS_PER_METER, 0.0f, {0.0f, 0.0f, 0.0f}};
QuestVRWorldTransform g_tabletopTransform = g_worldTransform;
QuestVRViewMode g_viewMode = QUEST_VR_VIEW_TABLETOP;
Bool g_viewPreferencesLoaded = FALSE;
Real g_comfortDegrees = 0.0f;
Bool g_comfortPivotValid = FALSE;
std::uint64_t g_comfortSpaceGeneration = 0;
float g_comfortPivot[3] = {};
QuestComfort::TiltGesture g_comfortTiltGesture;
Bool g_tabletopTransformDirty = FALSE;
Bool g_wasInGame = FALSE;
Bool g_followActive = FALSE;
Bool g_hudVisible = TRUE;
QuestVRRadialKind g_radialKind = QUEST_VR_RADIAL_NONE;
QuestVRCameraStickAxis g_cameraStickAxis = QUEST_VR_CAMERA_STICK_NONE;
Int g_radialSector = -1;
Bool g_radialStickWasOut = FALSE;
std::chrono::steady_clock::time_point g_radialOpenTime{};
std::chrono::steady_clock::time_point g_bButtonDownTime{};
Bool g_bButtonLongFired = FALSE;
Int g_armedOrder = 0;
Int g_lastGroupRecalled = -1;
std::chrono::steady_clock::time_point g_lastGroupRecallTime{};
DisplayString *g_radialStrings[16] = {};

struct TabletopGrabState
{
	Bool active = FALSE;
	std::uint32_t handMask = 0;
	QuestVRWorldTransform startTransform{};
	Vector3 startSpan{};
};

TabletopGrabState g_tabletopGrab;

// GeneralsX @feature Codex 11/09/2026 Persist only cross-map-safe VR comfort state.
void saveViewPreferences()
{
	if (TheGlobalData == nullptr) {
		return;
	}
	UserPreferences preferences;
	preferences.load("QuestVR.ini");
	preferences.setReal("TabletopTiltDegrees", QuestComfort::ClampDegrees(g_comfortDegrees));
	preferences.setReal("TabletopScale",
		QuestXRClampWorldScale(g_tabletopTransform.worldUnitsPerMeter));
	if (!preferences.write()) {
		__android_log_print(ANDROID_LOG_WARN, "GeneralsXR",
			"Quest VR preferences: could not write QuestVR.ini");
	} else {
		// GeneralsX @tweak Codex 16/09/2026 Record the final pinch scale so a
		// hardware capture can distinguish input sensitivity from camera height.
		__android_log_print(ANDROID_LOG_INFO, "GeneralsXR",
			"Quest VR preferences: saved Tabletop scale %.1f",
			g_tabletopTransform.worldUnitsPerMeter);
	}
	g_tabletopTransformDirty = FALSE;
}

void loadViewPreferences()
{
	if (g_viewPreferencesLoaded || TheGlobalData == nullptr) {
		return;
	}
	g_viewPreferencesLoaded = TRUE;
	UserPreferences preferences;
	preferences.load("QuestVR.ini");
	g_comfortDegrees = QuestComfort::ClampDegrees(preferences.getReal("TabletopTiltDegrees", 0.0f));
	g_tabletopTransform.worldUnitsPerMeter = QuestXRClampWorldScale(
		preferences.getReal("TabletopScale", QUEST_XR_WORLD_UNITS_PER_METER));
	g_tabletopTransform.yawRadians = 0.0f;
	g_tabletopTransform.offset[0] = 0.0f;
	g_tabletopTransform.offset[1] = 0.0f;
	g_tabletopTransform.offset[2] = 0.0f;
	// GeneralsX @bugfix Codex 16/09/2026 Always enter gameplay in Tabletop
	// mode. A right-stick click was easy to trigger accidentally, and persisting
	// Battlefield trapped later sessions at ground level with no normal zoom-out.
	// Tabletop scale remains a safe cross-map preference; view mode is session-only.
	g_viewMode = QUEST_VR_VIEW_TABLETOP;
	g_worldTransform = g_tabletopTransform;
	__android_log_print(ANDROID_LOG_INFO, "GeneralsXR",
		"Quest VR preferences: Tabletop, scale %.1f (view mode is session-only)",
		g_tabletopTransform.worldUnitsPerMeter);
}

Bool updateBattlefieldTransform()
{
	if (g_viewMode != QUEST_VR_VIEW_BATTLEFIELD || TheTacticalView == nullptr
		|| TheTerrainLogic == nullptr) {
		return FALSE;
	}
	W3DView *view = static_cast<W3DView *>(TheTacticalView);
	CameraClass *camera = view->get3DCamera();
	if (camera == nullptr) {
		return FALSE;
	}

	const Vector3 cameraPosition = camera->Get_Position();
	const Coord3D target = view->getPosition();
	const Vector3 worldUp(0.0f, 0.0f, 1.0f);
	Vector3 forward = -camera->Get_Transform().Get_Z_Vector();
	forward.Z = 0.0f;
	if (forward.Length2() < 0.0001f) {
		forward = Vector3(1.0f, 0.0f, 0.0f);
	}
	forward.Normalize();
	Vector3 right;
	Vector3::Cross_Product(forward, worldUp, &right);
	right.Normalize();
	const Vector3 backward = -forward;
	const Vector3 toTarget(target.x - cameraPosition.X,
		target.y - cameraPosition.Y, 0.0f);

	Real trackedEyeHeight = 0.0f;
	Int trackedEyes = 0;
	for (std::uint32_t eye = 0; eye < 2; ++eye) {
		QuestXREyeView eyeView{};
		if (QuestXR_GetEyeView(eye, &eyeView)) {
			trackedEyeHeight += eyeView.position[1];
			++trackedEyes;
		}
	}
	if (trackedEyes != 0) {
		trackedEyeHeight /= trackedEyes;
	}

	g_worldTransform.worldUnitsPerMeter = kBattlefieldWorldUnitsPerMeter;
	g_worldTransform.yawRadians = 0.0f;
	g_worldTransform.offset[0] = Vector3::Dot_Product(toTarget, right);
	g_worldTransform.offset[1] = QuestXRBattlefieldVerticalOffset(
		cameraPosition.Z, TheTerrainLogic->getGroundHeight(target.x, target.y),
		trackedEyeHeight, kBattlefieldWorldUnitsPerMeter,
		kBattlefieldEyeHeightMeters);
	g_worldTransform.offset[2] = Vector3::Dot_Product(toTarget, backward);
	return TRUE;
}

Bool toggleViewMode(const QuestXRControllerState &leftController,
	const QuestXRControllerState &rightController)
{
	if ((rightController.pressed & QUEST_XR_BUTTON_STICK) == 0
		|| (leftController.buttons & QUEST_XR_BUTTON_GRIP) == 0
		|| (rightController.buttons & QUEST_XR_BUTTON_GRIP) == 0
		|| (TheInGameUI != nullptr && TheInGameUI->getPendingPlaceType() != nullptr)) {
		return FALSE;
	}
	// GeneralsX @bugfix Codex 16/09/2026 Require an explicit two-grip chord
	// before right-stick click changes view mode. Stick click alone is too easy
	// to press while turning or zooming and previously persisted the surprise.
	g_tabletopGrab = {};
	if (g_viewMode == QUEST_VR_VIEW_TABLETOP) {
		g_tabletopTransform = g_worldTransform;
		g_viewMode = QUEST_VR_VIEW_BATTLEFIELD;
		updateBattlefieldTransform();
		__android_log_print(ANDROID_LOG_INFO, "GeneralsXR",
			"Quest VR view mode: Battlefield");
	} else {
		g_viewMode = QUEST_VR_VIEW_TABLETOP;
		g_worldTransform = g_tabletopTransform;
		__android_log_print(ANDROID_LOG_INFO, "GeneralsXR",
			"Quest VR view mode: Tabletop");
	}
	saveViewPreferences();
	return TRUE;
}

// GeneralsX @feature Codex 16/09/2026 Match GeneralsVR's long Menu gesture.
// Camera locking is client-side view state and never enters deterministic game
// simulation or the network command stream.
Bool handleFollowSelection(const QuestXRControllerState &leftController)
{
	if ((leftController.pressed & QUEST_XR_BUTTON_MENU_LONG) == 0
		|| TheTacticalView == nullptr || TheInGameUI == nullptr) {
		return FALSE;
	}
	g_tabletopGrab = {};
	if (g_followActive) {
		TheTacticalView->userSetCameraLock(INVALID_ID);
		TheTacticalView->userSetCameraLockDrawable(nullptr);
		g_followActive = FALSE;
		__android_log_print(ANDROID_LOG_INFO, "GeneralsXR",
			"Quest VR follow selection: off");
		return TRUE;
	}

	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	if (selected == nullptr || selected->empty()) {
		return TRUE;
	}
	Drawable *first = selected->front();
	if (first == nullptr || first->getObject() == nullptr) {
		return TRUE;
	}
	TheTacticalView->userSetCameraLock(first->getObject()->getID());
	g_followActive = TRUE;
	__android_log_print(ANDROID_LOG_INFO, "GeneralsXR",
		"Quest VR follow selection: object %u", first->getObject()->getID());
	return TRUE;
}

Vector3 controllerPosition(const QuestXRControllerState &controller)
{
	return Vector3(controller.position[0], controller.position[1], controller.position[2]);
}

Real horizontalLength(const Vector3 &value)
{
	return std::sqrt(value.X * value.X + value.Z * value.Z);
}

void beginTabletopGrab(std::uint32_t handMask,
	const QuestXRControllerState &leftController,
	const QuestXRControllerState &rightController)
{
	g_tabletopGrab.active = TRUE;
	g_tabletopGrab.handMask = handMask;
	g_tabletopGrab.startTransform = g_worldTransform;
	const Vector3 left = controllerPosition(leftController);
	const Vector3 right = controllerPosition(rightController);
	g_tabletopGrab.startSpan = right - left;
}

Bool updateTabletopGrab(const QuestXRControllerState &leftController,
	const QuestXRControllerState &rightController)
{
	if (g_viewMode != QUEST_VR_VIEW_TABLETOP) {
		g_tabletopGrab = {};
		return FALSE;
	}
	std::uint32_t handMask = 0;
	if (leftController.connected != 0 && leftController.poseValid != 0
		&& (leftController.buttons & QUEST_XR_BUTTON_GRIP) != 0) {
		handMask |= 1;
	}
	if (rightController.connected != 0 && rightController.poseValid != 0
		&& (rightController.buttons & QUEST_XR_BUTTON_GRIP) != 0) {
		handMask |= 2;
	}
	// GeneralsX @bugfix Codex 16/09/2026 Match GeneralsVR's later input
	// design: a single squeeze must never translate the whole battlefield.
	// It caused tracking noise or an incidental grip to produce a runaway
	// slide towards a map edge. Both grips deliberately pinch only the scale;
	// sticks continue to own movement and yaw.
	if (handMask != 3) {
		if (g_tabletopGrab.active && g_viewMode == QUEST_VR_VIEW_TABLETOP
			&& g_tabletopTransformDirty) {
			g_tabletopTransform = g_worldTransform;
			saveViewPreferences();
		}
		g_tabletopGrab = {};
		return FALSE;
	}
	if (!g_tabletopGrab.active || g_tabletopGrab.handMask != handMask) {
		beginTabletopGrab(handMask, leftController, rightController);
		return TRUE;
	}

	const Vector3 left = controllerPosition(leftController);
	const Vector3 right = controllerPosition(rightController);
	Real newScale = g_tabletopGrab.startTransform.worldUnitsPerMeter;
	const Vector3 currentSpan = right - left;
	const Real startLength = horizontalLength(g_tabletopGrab.startSpan);
	const Real currentLength = horizontalLength(currentSpan);
	if (startLength >= kMinimumTwoHandSpan && currentLength >= kMinimumTwoHandSpan) {
		// GeneralsX @tweak Codex 16/09/2026 Double the logarithmic pinch
		// sensitivity. The ratio remains reciprocal in both directions, but a
		// comfortable forearm movement now produces a useful scale change.
		const Real scaleRatio = startLength / currentLength;
		newScale = QuestXRClampWorldScale(newScale
			* std::pow(scaleRatio, kTwoHandScaleSensitivity));
	}
	g_worldTransform.worldUnitsPerMeter = newScale;
	if (g_viewMode == QUEST_VR_VIEW_TABLETOP) {
		g_tabletopTransform = g_worldTransform;
		g_tabletopTransformDirty = TRUE;
	}
	return TRUE;
}

// GeneralsX @feature Codex 16/10/2026 Deliberate two-grip stick gestures tilt only presentation.
Bool updateComfortTilt(const QuestXRControllerState &left, const QuestXRControllerState &right, Bool allowed)
{
	const Bool enabled = allowed && g_viewMode == QUEST_VR_VIEW_TABLETOP
		&& left.connected && right.connected && left.poseValid && right.poseValid
		&& (left.buttons & QUEST_XR_BUTTON_GRIP) && (right.buttons & QUEST_XR_BUTTON_GRIP);
	const int step = g_comfortTiltGesture.Step(right.stick[0], right.stick[1], enabled);
	const Real next = step == 2 ? 0.0f : QuestComfort::ClampDegrees(g_comfortDegrees + step * 15.0f);
	if (step && next != g_comfortDegrees) {
		QuestVRWorldTransform previous{};
		QuestVRInput_GetWorldTransform(&previous);
		QuestXREyeView eyes[2]{};
		if (QuestXR_GetEyeView(0, &eyes[0]) && QuestXR_GetEyeView(1, &eyes[1])) {
			float center[3]{};
			bool valid = true;
			for (unsigned axis = 0; axis < 3; ++axis) {
				center[axis] = (eyes[0].position[axis] + eyes[1].position[axis]) * 0.5f;
				valid = valid && std::isfinite(center[axis]);
			}
			if (valid) {
				// Preserve the current world-space head center even after room movement.
				const QuestXRFloat3 head{center[0], center[1], center[2]};
				const auto before = QuestComfort::Point(head, previous.worldUnitsPerMeter, previous.yawRadians,
					previous.comfortPitchRadians, previous.comfortPivotMeters, previous.offset);
				const auto after = QuestComfort::Point(head, previous.worldUnitsPerMeter, previous.yawRadians,
					0.0f, center, previous.offset);
				g_worldTransform.offset[0] += before.x - after.x;
				g_worldTransform.offset[1] += before.y - after.y;
				g_worldTransform.offset[2] += before.z - after.z;
				for (unsigned axis = 0; axis < 3; ++axis) g_comfortPivot[axis] = center[axis];
				g_comfortSpaceGeneration = QuestXR_GetComfortSpaceGeneration();
				g_comfortPivotValid = TRUE;
				g_comfortDegrees = next;
				g_tabletopTransform = g_worldTransform;
				saveViewPreferences();
				__android_log_print(ANDROID_LOG_INFO, "GeneralsXR", "Quest comfort: tilt changed to %.0f degrees", next);
			}
		}
	}
	return g_comfortTiltGesture.active;
}

void ensurePointerScene()
{
	if (g_pointerScene != nullptr) {
		return;
	}
	g_pointerScene = NEW_REF(SimpleSceneClass, ());
	for (Int hand = 0; hand < 2; ++hand) {
		const Real red = hand == static_cast<Int>(kRightHand) ? 0.35f : 1.0f;
		const Real green = hand == static_cast<Int>(kRightHand) ? 0.85f : 0.72f;
		const Real blue = hand == static_cast<Int>(kRightHand) ? 1.0f : 0.25f;
		g_rayLines[hand] = NEW_REF(Line3DClass, (Vector3(), Vector3(0.0f, 0.0f, 1.0f),
			1.0f, red, green, blue, 0.90f));
		g_pointerScene->Add_Render_Object(g_rayLines[hand]);
		g_rayLines[hand]->Set_Hidden(true);
	}
	for (Int edge = 0; edge < 4; ++edge) {
		g_boxLines[edge] = NEW_REF(QuestOverlayLine3DClass, (Vector3(), Vector3(0.0f, 0.0f, 1.0f),
			1.0f, 0.30f, 1.0f, 0.45f, 0.95f));
		g_pointerScene->Add_Render_Object(g_boxLines[edge]);
		g_boxLines[edge]->Set_Hidden(true);
	}
	// GeneralsX @feature Codex 15/09/2026 Port GeneralsVR's selected-unit
	// health markers so a controller selection is visible in stereo.
	for (Int marker = 0; marker < kMaximumSelectionMarkers; ++marker) {
		// GeneralsX @bugfix Codex 18/09/2026 Render the useful health indication
		// explicitly after the pointer scene. The experimental red selection-only
		// row had no gameplay state of its own and was angle-dependent on Quest.
		g_healthFill[marker] = NEW_REF(QuestOverlayLine3DClass, (Vector3(), Vector3(0.0f, 0.0f, 1.0f),
			1.0f, 0.20f, 1.00f, 0.30f, 0.95f));
		g_healthFill[marker]->Set_Hidden(true);
	}
}

void hidePointerVisuals()
{
	for (Line3DClass *line : g_rayLines) {
		if (line != nullptr) {
			line->Set_Hidden(true);
		}
	}
	for (Line3DClass *line : g_boxLines) {
		if (line != nullptr) {
			line->Set_Hidden(true);
		}
	}
	for (Int marker = 0; marker < kMaximumSelectionMarkers; ++marker) {
		if (g_healthFill[marker] != nullptr) {
			g_healthFill[marker]->Set_Hidden(true);
		}
	}
}

Real applyDeadzone(Real value)
{
	return QuestXRApplyDeadzone(value);
}

Bool computeHandRay(const QuestXRControllerState &controller,
	Vector3 &outOrigin, Vector3 &outDirection)
{
	if (controller.connected == 0 || controller.poseValid == 0 || TheTacticalView == nullptr) {
		return FALSE;
	}

	W3DView *view = static_cast<W3DView *>(TheTacticalView);
	CameraClass *camera = view->get3DCamera();
	if (camera == nullptr) {
		return FALSE;
	}

	const Matrix3D &cameraTransform = camera->Get_Transform();
	const Vector3 worldUp(0.0f, 0.0f, 1.0f);
	Vector3 forward = -cameraTransform.Get_Z_Vector();
	forward.Z = 0.0f;
	if (forward.Length2() < 0.0001f) {
		forward = Vector3(1.0f, 0.0f, 0.0f);
	}
	forward.Normalize();
	Vector3 right;
	Vector3::Cross_Product(forward, worldUp, &right);
	right.Normalize();
	const Vector3 backward = -forward;

	QuestVRWorldTransform comfort{};
	QuestVRInput_GetWorldTransform(&comfort);
	const auto point = QuestComfort::Point({controller.position[0], controller.position[1], controller.position[2]},
		comfort.worldUnitsPerMeter, comfort.yawRadians, comfort.comfortPitchRadians,
		comfort.comfortPivotMeters, comfort.offset);
	const Vector3 localPosition(point.x, point.y, point.z);
	outOrigin = camera->Get_Position()
		+ right * localPosition.X
		+ worldUp * localPosition.Y
		+ backward * localPosition.Z;

	const Real x = controller.orientation[0];
	const Real y = controller.orientation[1];
	const Real z = controller.orientation[2];
	const Real w = controller.orientation[3];
	// OpenXR aim poses point along local -Z.
	const QuestXRFloat3 aim = QuestXRRotateAim(x, y, z, w);
	const auto direction = QuestComfort::Rotate(aim, comfort.yawRadians, comfort.comfortPitchRadians);
	const Vector3 localDirection(direction.x, direction.y, direction.z);
	outDirection = right * localDirection.X
		+ worldUp * localDirection.Y
		+ backward * localDirection.Z;
	if (outDirection.Length2() < 0.0001f) {
		return FALSE;
	}
	outDirection.Normalize();
	return TRUE;
}

Bool getHeadHorizontalBasis(Vector3 &outRight, Vector3 &outForward)
{
	if (TheTacticalView == nullptr) {
		return FALSE;
	}
	W3DView *view = static_cast<W3DView *>(TheTacticalView);
	CameraClass *camera = view->get3DCamera();
	if (camera == nullptr) {
		return FALSE;
	}
	const Vector3 up(0.0f, 0.0f, 1.0f);
	Vector3 cameraForward = -camera->Get_Transform().Get_Z_Vector();
	cameraForward.Z = 0.0f;
	if (cameraForward.Length2() < 0.0001f) {
		cameraForward = Vector3(1.0f, 0.0f, 0.0f);
	}
	cameraForward.Normalize();
	Vector3 cameraRight;
	Vector3::Cross_Product(cameraForward, up, &cameraRight);
	cameraRight.Normalize();

	QuestXREyeView head{};
	if (!QuestXR_GetEyeView(0, &head)) {
		outForward = cameraForward;
		outRight = cameraRight;
		return TRUE;
	}
	const QuestXRFloat3 aim = QuestXRRotateAim(head.orientation[0], head.orientation[1],
		head.orientation[2], head.orientation[3]);
	QuestVRWorldTransform comfort{};
	QuestVRInput_GetWorldTransform(&comfort);
	const auto direction = QuestComfort::Rotate(aim, comfort.yawRadians, comfort.comfortPitchRadians);
	const Vector3 local(direction.x, direction.y, direction.z);
	Vector3 gaze = cameraRight * local.X + up * local.Y - cameraForward * local.Z;
	gaze.Z = 0.0f;
	if (gaze.Length2() < 0.0001f) {
		gaze = cameraForward;
	}
	gaze.Normalize();
	outForward = gaze;
	Vector3::Cross_Product(gaze, up, &outRight);
	outRight.Normalize();
	return TRUE;
}

Bool traceTerrain(const Vector3 &origin, const Vector3 &direction, Coord3D &outHit)
{
	if (TheTerrainLogic == nullptr || direction.Z > -0.001f) {
		return FALSE;
	}

	Vector3 previous = origin;
	Real previousGap = origin.Z - TheTerrainLogic->getGroundHeight(origin.X, origin.Y);
	for (Real distance = kAimStep; distance <= kAimMaxDistance; distance += kAimStep) {
		const Vector3 point = origin + direction * distance;
		const Real gap = point.Z - TheTerrainLogic->getGroundHeight(point.X, point.Y);
		if (gap <= 0.0f && previousGap > 0.0f) {
			Vector3 above = previous;
			Vector3 below = point;
			for (Int iteration = 0; iteration < 12; ++iteration) {
				const Vector3 midpoint = (above + below) * 0.5f;
				if (midpoint.Z - TheTerrainLogic->getGroundHeight(midpoint.X, midpoint.Y) > 0.0f) {
					above = midpoint;
				} else {
					below = midpoint;
				}
			}
			outHit.x = below.X;
			outHit.y = below.Y;
			outHit.z = TheTerrainLogic->getGroundHeight(below.X, below.Y);
			return TRUE;
		}
		previous = point;
		previousGap = gap;
	}
	return FALSE;
}

Drawable *traceDrawable(const Vector3 &origin, const Vector3 &direction, Coord3D *outHit)
{
	if (W3DDisplay::m_3DScene == nullptr) {
		return nullptr;
	}

	LineSegClass segment;
	segment.Set(origin, origin + direction * kAimMaxDistance);
	CastResultStruct result;
	RayCollisionTestClass rayTest(segment, &result, COLL_TYPE_ALL, false, false);
	if (!W3DDisplay::m_3DScene->castRay(rayTest, true, static_cast<Int>(PICK_TYPE_ALL_DRAWABLES))
		|| rayTest.CollidedRenderObj == nullptr) {
		return nullptr;
	}

	DrawableInfo *info = static_cast<DrawableInfo *>(rayTest.CollidedRenderObj->Get_User_Data());
	Drawable *drawable = info != nullptr ? info->m_drawable : nullptr;
	if (drawable != nullptr && drawable->getFullyObscuredByShroud()) {
		return nullptr;
	}
	if (drawable != nullptr && outHit != nullptr) {
		const Vector3 point = rayTest.Ray.Get_P1();
		outHit->x = point.X;
		outHit->y = point.Y;
		outHit->z = point.Z;
	}
	return drawable;
}

Drawable *pickDrawable(const Vector3 &origin, const Vector3 &direction)
{
	return traceDrawable(origin, direction, nullptr);
}

void getReticleColor(const Vector3 &origin, const Vector3 &direction,
	Real &red, Real &green, Real &blue)
{
	red = 0.35f;
	green = 0.85f;
	blue = 1.00f;
	if (TheGameClient == nullptr || TheInGameUI == nullptr) {
		return;
	}
	const CommandButton *pending = TheInGameUI->getGUICommand();
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	if (pending == nullptr && (selected == nullptr || selected->empty())) {
		return;
	}
	Drawable *drawable = pickDrawable(origin, direction);
	Coord3D ground;
	const Bool onGround = traceTerrain(origin, direction, ground);
	const Coord3D *position = onGround ? &ground
		: drawable != nullptr ? drawable->getPosition() : nullptr;
	if (position == nullptr) {
		return;
	}
	// GeneralsX @feature Codex 15/09/2026 Use the real context-command
	// evaluator for GeneralsVR-compatible pointer feedback; no RTS action is
	// duplicated or guessed by the Quest adapter.
	const GameMessage::Type command = TheGameClient->evaluateContextCommand(
		drawable, position, CommandTranslator::EVALUATE_ONLY);
	switch (command) {
		case GameMessage::MSG_DO_ATTACK_OBJECT:
		case GameMessage::MSG_DO_FORCE_ATTACK_OBJECT:
		case GameMessage::MSG_DO_FORCE_ATTACK_GROUND:
		case GameMessage::MSG_DO_WEAPON_AT_OBJECT:
		case GameMessage::MSG_DO_WEAPON_AT_LOCATION:
			red = 1.00f; green = 0.20f; blue = 0.15f;
			break;
		case GameMessage::MSG_DO_GUARD_POSITION:
		case GameMessage::MSG_DO_GUARD_OBJECT:
			red = 0.15f; green = 0.95f; blue = 0.80f;
			break;
		case GameMessage::MSG_DO_SPECIAL_POWER_AT_OBJECT:
		case GameMessage::MSG_DO_SPECIAL_POWER_AT_LOCATION:
		case GameMessage::MSG_ENTER:
		case GameMessage::MSG_GET_REPAIRED:
		case GameMessage::MSG_GET_HEALED:
		case GameMessage::MSG_DOCK:
		case GameMessage::MSG_DO_REPAIR:
		case GameMessage::MSG_RESUME_CONSTRUCTION:
			red = 1.00f; green = 0.75f; blue = 0.15f;
			break;
		case GameMessage::MSG_DO_MOVETO:
		case GameMessage::MSG_DO_ATTACKMOVETO:
			red = 0.30f; green = 1.00f; blue = 0.40f;
			break;
		default:
			break;
	}
}

void updateBoxVisual()
{
	ensurePointerScene();
	if (!g_boxSelection.boxing || TheTerrainLogic == nullptr) {
		for (Line3DClass *line : g_boxLines) {
			line->Set_Hidden(true);
		}
		return;
	}
	constexpr Real lift = 4.0f;
	// GeneralsX @bugfix Codex 15/09/2026 Keep the swept rectangle aligned
	// to the player's heading captured at trigger-down, matching GeneralsVR.
	const Vector3 start(g_boxSelection.start.x, g_boxSelection.start.y, 0.0f);
	const Vector3 span(g_boxSelection.end.x - g_boxSelection.start.x,
		g_boxSelection.end.y - g_boxSelection.start.y, 0.0f);
	const Vector3 across = g_boxRight * Vector3::Dot_Product(span, g_boxRight);
	const Vector3 away = g_boxForward * Vector3::Dot_Product(span, g_boxForward);
	Vector3 corners[4] = {start, start + across, start + across + away, start + away};
	for (Vector3 &corner : corners) {
		corner.Z = TheTerrainLogic->getGroundHeight(corner.X, corner.Y) + lift;
	}
	const Real width = 0.004f * g_worldTransform.worldUnitsPerMeter;
	for (Int edge = 0; edge < 4; ++edge) {
		g_boxLines[edge]->Reset(corners[edge], corners[(edge + 1) % 4], width);
		g_boxLines[edge]->Set_Hidden(false);
	}
}

void updateSelectionMarkers()
{
	ensurePointerScene();
	Int used = 0;
	const DrawableList *selected = TheInGameUI != nullptr
		? TheInGameUI->getAllSelectedDrawables() : nullptr;
	if (selected != nullptr) {
		Vector3 barRight(1.0f, 0.0f, 0.0f);
		Vector3 unusedForward;
		getHeadHorizontalBasis(barRight, unusedForward);
		const Real scale = g_worldTransform.worldUnitsPerMeter;
		const Real lift = 0.05f * scale;
		const Real barWidth = 0.10f * scale;
		const Real thickness = 0.008f * scale;
		for (DrawableList::const_iterator it = selected->begin();
			it != selected->end() && used < kMaximumSelectionMarkers; ++it) {
			Drawable *drawable = *it;
			if (drawable == nullptr || drawable->getFullyObscuredByShroud()) {
				continue;
			}
			const Coord3D *position = drawable->getPosition();
			if (position == nullptr) {
				continue;
			}
			Object *object = drawable->getObject();
			Real top = position->z + lift;
			if (object != nullptr) {
				top += object->getGeometryInfo().getMaxHeightAbovePosition();
			}
			Real health = 1.0f;
			if (object != nullptr && object->getBodyModule() != nullptr) {
				const Real maximum = object->getBodyModule()->getMaxHealth();
				if (maximum > 0.0f) {
					health = object->getBodyModule()->getHealth() / maximum;
				}
			}
			health = MAX(0.0f, MIN(health, 1.0f));
			const Vector3 healthCenter(position->x, position->y, top);
			const Vector3 healthLeft = healthCenter - barRight * (barWidth * 0.5f);
			if (health > 0.01f) {
				g_healthFill[used]->Reset(healthLeft,
					healthLeft + barRight * (barWidth * health), thickness);
				g_healthFill[used]->Set_Hidden(false);
			} else {
				g_healthFill[used]->Set_Hidden(true);
			}
			++used;
		}
	}
	for (Int marker = used; marker < kMaximumSelectionMarkers; ++marker) {
		g_healthFill[marker]->Set_Hidden(true);
	}
	// GeneralsX @feature Codex 18/09/2026 Record state transitions for the
	// restart-only selection-feedback fault without adding per-frame log noise.
	if (used != g_lastDiagnosticMarkerCount) {
		__android_log_print(ANDROID_LOG_INFO, "GeneralsXR",
			"XR visuals: selected markers changed %d -> %d, scale %.1f",
			g_lastDiagnosticMarkerCount, used, g_worldTransform.worldUnitsPerMeter);
		g_lastDiagnosticMarkerCount = used;
	}
}

void updatePointerVisuals()
{
	ensurePointerScene();
	for (std::uint32_t hand = 0; hand < 2; ++hand) {
		QuestXRControllerState controller{};
		Vector3 origin;
		Vector3 direction;
		// GeneralsVR uses the right hand as the only pointer. The left hand
		// carries the HUD and a second laser only adds visual ambiguity.
		if (hand != kRightHand || !QuestXR_GetControllerState(hand, &controller)
			|| controller.menuPointValid != 0
			|| !computeHandRay(controller, origin, direction)) {
			g_rayLines[hand]->Set_Hidden(true);
			continue;
		}

		Coord3D hit;
		Vector3 endpoint;
		if (traceDrawable(origin, direction, &hit) != nullptr
			|| traceTerrain(origin, direction, hit)) {
			endpoint = Vector3(hit.x, hit.y, hit.z);
		} else {
			endpoint = origin + direction * (6.0f * g_worldTransform.worldUnitsPerMeter);
		}
		const Real width = 0.004f * g_worldTransform.worldUnitsPerMeter;
		g_rayLines[hand]->Reset(origin, endpoint, width);
		Real red = 0.35f;
		Real green = 0.85f;
		Real blue = 1.00f;
		getReticleColor(origin, direction, red, green, blue);
		g_rayLines[hand]->Re_Color(red, green, blue);
		g_rayLines[hand]->Set_Hidden(false);
	}
	updateBoxVisual();
	updateSelectionMarkers();
}

// GeneralsX @feature Codex 17/10/2026 Left grip extends selection; both grips remain reserved for world gestures.
Bool additiveSelectionHeld(const QuestXRControllerState &left, const QuestXRControllerState &right, Bool haveLeft)
{
	return haveLeft && left.connected != 0 && (left.buttons & QUEST_XR_BUTTON_GRIP) != 0
		&& (right.buttons & QUEST_XR_BUTTON_GRIP) == 0;
}

Bool canExtendUnitSelection()
{
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	if (selected == nullptr) return TRUE;
	for (DrawableList::const_iterator it = selected->begin(); it != selected->end(); ++it) {
		Object *object = *it != nullptr ? (*it)->getObject() : nullptr;
		if (object == nullptr || !object->isLocallyControlled() || object->isKindOf(KINDOF_STRUCTURE)) return FALSE;
	}
	return TRUE;
}

Int selectionLimit()
{
	const Int configured = TheInGameUI->getMaxSelectCount();
	return configured > 0 ? MIN(256, configured) : 256;
}

void selectUnderRay(const Vector3 &origin, const Vector3 &direction, Bool additive = FALSE)
{
	if (TheInGameUI == nullptr || TheMessageStream == nullptr) {
		return;
	}

	Drawable *drawable = pickDrawable(origin, direction);
	if (additive) {
		Object *object = drawable != nullptr ? drawable->getObject() : nullptr;
		g_lastSingleSelection = INVALID_ID;
		if (object == nullptr || !drawable->isSelectable() || drawable->isSelected()
			|| !object->isLocallyControlled() || object->isKindOf(KINDOF_STRUCTURE)
			|| object->getContainedBy() != nullptr || !canExtendUnitSelection()
			|| TheInGameUI->getSelectCount() >= selectionLimit()) return;
		TheInGameUI->selectDrawable(drawable);
		GameMessage *message = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP);
		message->appendBooleanArgument(FALSE);
		message->appendObjectIDArgument(object->getID());
		return;
	}
	const auto now = std::chrono::steady_clock::now();
	const Bool doubleSelect = drawable != nullptr && drawable->getObject() != nullptr
		&& drawable->getObject()->getID() == g_lastSingleSelection
		&& TheGlobalData != nullptr
		&& now - g_lastSingleSelectionTime
			<= std::chrono::milliseconds(TheGlobalData->m_doubleClickTimeMS);
	TheInGameUI->deselectAllDrawables();
	if (drawable == nullptr || !drawable->isSelectable() || drawable->getObject() == nullptr) {
		TheMessageStream->appendMessage(GameMessage::MSG_DESTROY_SELECTED_GROUP);
		g_lastSingleSelection = INVALID_ID;
		return;
	}

	g_lastSingleSelection = drawable->getObject()->getID();
	g_lastSingleSelectionTime = now;
	TheInGameUI->selectDrawable(drawable);
	GameMessage *message = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP);
	message->appendBooleanArgument(TRUE);
	message->appendObjectIDArgument(drawable->getObject()->getID());
	if (doubleSelect && drawable->isMassSelectable()
		&& drawable->getObject()->isLocallyControlled()) {
		TheInGameUI->selectMatchingAcrossScreen();
	}
}

void getRadialGeometry(Int &centerX, Int &centerY, Int &radius)
{
	const Int width = TheDisplay != nullptr ? static_cast<Int>(TheDisplay->getWidth()) : 800;
	const Int height = TheDisplay != nullptr ? static_cast<Int>(TheDisplay->getHeight()) : 600;
	centerX = width / 2;
	centerY = static_cast<Int>(height * 0.33f);
	radius = static_cast<Int>(height * 0.30f);
	radius = MIN(radius, width / 2 - 8);
}

Int radialSectorAt(float normalizedX, float normalizedY, Bool includeOuterMargin)
{
	if (g_radialKind == QUEST_VR_RADIAL_NONE || TheDisplay == nullptr) {
		return -1;
	}
	Int centerX = 0;
	Int centerY = 0;
	Int radius = 0;
	getRadialGeometry(centerX, centerY, radius);
	const Real pointX = normalizedX * TheDisplay->getWidth();
	const Real pointY = normalizedY * TheDisplay->getHeight();
	const Real deltaX = pointX - centerX;
	const Real deltaY = pointY - centerY;
	const Real distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
	const Real outer = radius * (includeOuterMargin ? 1.08f : 1.0f);
	if (distance > outer || distance < radius * 0.18f) {
		return -1;
	}
	const Int sectorCount = g_radialKind == QUEST_VR_RADIAL_GROUPS
		? 10 : kRadialCommandSectors;
	Real angle = std::atan2(deltaX, -deltaY);
	if (angle < 0.0f) {
		angle += 2.0f * kPi;
	}
	return static_cast<Int>(angle / (2.0f * kPi) * sectorCount + 0.5f) % sectorCount;
}

Bool radialContainsPoint(float normalizedX, float normalizedY)
{
	if (g_radialKind == QUEST_VR_RADIAL_NONE || TheDisplay == nullptr) {
		return FALSE;
	}
	Int centerX = 0;
	Int centerY = 0;
	Int radius = 0;
	getRadialGeometry(centerX, centerY, radius);
	const Real deltaX = normalizedX * TheDisplay->getWidth() - centerX;
	const Real deltaY = normalizedY * TheDisplay->getHeight() - centerY;
	return deltaX * deltaX + deltaY * deltaY <= radius * radius * 1.08f * 1.08f;
}

void closeRadial()
{
	g_radialKind = QUEST_VR_RADIAL_NONE;
	g_radialSector = -1;
	g_radialStickWasOut = FALSE;
	g_radialOpenTime = {};
}

void openRadial(QuestVRRadialKind kind)
{
	g_radialKind = kind;
	g_radialSector = -1;
	g_radialStickWasOut = FALSE;
	g_radialOpenTime = std::chrono::steady_clock::now();
	g_tabletopGrab = {};
	g_boxSelection = {};
	__android_log_print(ANDROID_LOG_INFO, "GeneralsXR", "Quest VR radial: %s",
		kind == QUEST_VR_RADIAL_GROUPS ? "groups" : "commands");
}

void jumpToSelection();

void applyControlGroup(Int group, Bool assign)
{
	if (TheMessageStream == nullptr || group < 0 || group > 9) {
		return;
	}
	const GameMessage::Type type = static_cast<GameMessage::Type>(
		(assign ? GameMessage::MSG_META_CREATE_TEAM0 : GameMessage::MSG_META_SELECT_TEAM0) + group);
	TheMessageStream->appendMessage(type);
	if (!assign) {
		const auto now = std::chrono::steady_clock::now();
		if (g_lastGroupRecalled == group
			&& g_lastGroupRecallTime.time_since_epoch().count() != 0
			&& now - g_lastGroupRecallTime < std::chrono::milliseconds(1200)) {
			jumpToSelection();
		}
		g_lastGroupRecalled = group;
		g_lastGroupRecallTime = now;
	}
}

void fireRadialSector(QuestVRRadialKind kind, Int sector,
	const QuestXRControllerState &rightController)
{
	if (kind == QUEST_VR_RADIAL_GROUPS) {
		applyControlGroup(sector,
			(rightController.buttons & QUEST_XR_BUTTON_TRIGGER) != 0);
		return;
	}
	if (TheMessageStream == nullptr) {
		return;
	}
	switch (sector) {
		case 0:
			TheMessageStream->appendMessage(GameMessage::MSG_DO_STOP);
			break;
		case 1:
			g_armedOrder = 1;
			break;
		case 2:
		{
			const CommandButton *guard = TheControlBar != nullptr
				? TheControlBar->findCommandButton("Command_Guard") : nullptr;
			if (guard != nullptr && TheInGameUI != nullptr) {
				TheInGameUI->setGUICommand(guard);
			} else {
				g_armedOrder = 2;
			}
			break;
		}
		case 3:
			TheMessageStream->appendMessage(GameMessage::MSG_DO_SCATTER);
			break;
		case 4:
			TheMessageStream->appendMessage(GameMessage::MSG_DO_CHEER);
			break;
		case 5:
			TheMessageStream->appendMessage(GameMessage::MSG_META_SELECT_NEXT_IDLE_WORKER);
			break;
		case 6:
			TheMessageStream->appendMessage(GameMessage::MSG_META_OPTIONS);
			break;
		default:
			break;
	}
}

Bool updatePanelAndRadials(const QuestXRControllerState &leftController,
	const QuestXRControllerState &rightController, Bool haveLeft, Bool haveRight)
{
	const auto now = std::chrono::steady_clock::now();
	if (haveRight && (rightController.pressed & QUEST_XR_BUTTON_SECONDARY) != 0) {
		g_bButtonDownTime = now;
		g_bButtonLongFired = FALSE;
	}
	if (haveRight && g_radialKind == QUEST_VR_RADIAL_NONE
		&& (rightController.buttons & QUEST_XR_BUTTON_SECONDARY) != 0
		&& g_bButtonDownTime.time_since_epoch().count() != 0
		&& !g_bButtonLongFired && now - g_bButtonDownTime >= kBHoldTime) {
		g_bButtonLongFired = TRUE;
		openRadial(QUEST_VR_RADIAL_GROUPS);
	}
	if (haveRight && (rightController.released & QUEST_XR_BUTTON_SECONDARY) != 0) {
		if (!g_bButtonLongFired && g_bButtonDownTime.time_since_epoch().count() != 0
			&& now - g_bButtonDownTime < kBHoldTime) {
			g_hudVisible = !g_hudVisible;
			__android_log_print(ANDROID_LOG_INFO, "GeneralsXR",
				"Quest VR wrist HUD: %s", g_hudVisible ? "shown" : "hidden");
		}
		g_bButtonDownTime = {};
	}

	if (g_radialKind == QUEST_VR_RADIAL_NONE && haveLeft
		&& (leftController.pressed & QUEST_XR_BUTTON_STICK) != 0) {
		openRadial(QUEST_VR_RADIAL_COMMANDS);
		return FALSE;
	}
	if (g_radialKind == QUEST_VR_RADIAL_NONE) {
		return FALSE;
	}

	const Bool groups = g_radialKind == QUEST_VR_RADIAL_GROUPS;
	const Bool cancelTap = groups
		? haveRight && (rightController.pressed & QUEST_XR_BUTTON_SECONDARY) != 0
		: haveLeft && (leftController.pressed & QUEST_XR_BUTTON_STICK) != 0;
	const Bool cancelTrigger = haveLeft
		&& (leftController.pressed & QUEST_XR_BUTTON_TRIGGER) != 0;
	if (cancelTap || cancelTrigger || now - g_radialOpenTime > kRadialTimeout) {
		if (groups && cancelTap) {
			g_bButtonLongFired = TRUE;
		}
		closeRadial();
		return cancelTrigger;
	}

	const Real stickX = groups ? rightController.stick[0] : leftController.stick[0];
	const Real stickY = groups ? rightController.stick[1] : leftController.stick[1];
	const Real deflection = std::sqrt(stickX * stickX + stickY * stickY);
	if (deflection >= kRadialPick) {
		const Int sectorCount = groups ? 10 : kRadialCommandSectors;
		Real angle = std::atan2(stickX, stickY);
		if (angle < 0.0f) {
			angle += 2.0f * kPi;
		}
		g_radialSector = static_cast<Int>(
			angle / (2.0f * kPi) * sectorCount + 0.5f) % sectorCount;
		g_radialStickWasOut = TRUE;
		g_radialOpenTime = now;
	} else if (deflection <= kRadialRelease && g_radialStickWasOut
		&& g_radialSector >= 0) {
		const QuestVRRadialKind kind = g_radialKind;
		const Int sector = g_radialSector;
		closeRadial();
		fireRadialSector(kind, sector, rightController);
	}
	return FALSE;
}

// GeneralsX @feature Codex 16/09/2026 Bring the tactical focus to the centre
// of the current selection, matching GeneralsVR's right-stick click action.
void jumpToSelection()
{
	if (TheInGameUI == nullptr || TheTacticalView == nullptr) {
		return;
	}
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	if (selected == nullptr || selected->empty()) {
		return;
	}
	Coord3D centre{};
	Int count = 0;
	for (DrawableList::const_iterator it = selected->begin(); it != selected->end(); ++it) {
		Drawable *drawable = *it;
		const Coord3D *position = drawable != nullptr ? drawable->getPosition() : nullptr;
		if (position == nullptr) {
			continue;
		}
		centre.x += position->x;
		centre.y += position->y;
		centre.z += position->z;
		++count;
	}
	if (count == 0) {
		return;
	}
	centre.x /= static_cast<Real>(count);
	centre.y /= static_cast<Real>(count);
	centre.z /= static_cast<Real>(count);
	TheTacticalView->lookAt(&centre);
}

void handleRightStickClick(const QuestXRControllerState &rightController)
{
	if ((rightController.pressed & QUEST_XR_BUTTON_STICK) == 0) {
		return;
	}
	const auto now = std::chrono::steady_clock::now();
	if (g_lastRightStickClickTime.time_since_epoch().count() != 0
		&& now - g_lastRightStickClickTime < std::chrono::milliseconds(400)) {
		if (TheMessageStream != nullptr) {
			TheMessageStream->appendMessage(GameMessage::MSG_META_VIEW_LAST_RADAR_EVENT);
		}
		g_lastRightStickClickTime = {};
	} else {
		jumpToSelection();
		g_lastRightStickClickTime = now;
	}
}

// GeneralsX @feature Codex 16/09/2026 The left trigger is GeneralsVR's
// universal cancel: leave a targeted command or clear the current selection.
void handleCancel(const QuestXRControllerState &leftController)
{
	if ((leftController.pressed & QUEST_XR_BUTTON_TRIGGER) == 0
		|| TheInGameUI == nullptr || TheMessageStream == nullptr) {
		return;
	}
	if (TheInGameUI->getPendingPlaceType() != nullptr) {
		TheInGameUI->placeBuildAvailable(nullptr, nullptr);
		TheInGameUI->setPlacementWorldOverride(nullptr, 0.0f);
		g_activePlacement = nullptr;
		g_placementArmed = FALSE;
		return;
	}
	if (TheInGameUI->getGUICommand() != nullptr) {
		TheInGameUI->setGUICommand(nullptr);
		return;
	}
	if (g_armedOrder != 0) {
		g_armedOrder = 0;
		return;
	}
	TheInGameUI->deselectAllDrawables();
	TheMessageStream->appendMessage(GameMessage::MSG_DESTROY_SELECTED_GROUP);
}

struct BoxSelectionContext
{
	GameMessage *message;
	Int count;
	Int limit;
	Bool additive;
	Vector3 origin;
	Vector3 right;
	Vector3 forward;
	Real acrossLo;
	Real acrossHi;
	Real awayLo;
	Real awayHi;
};

void addDrawableToBoxSelection(Drawable *drawable, void *userData)
{
	BoxSelectionContext *context = static_cast<BoxSelectionContext *>(userData);
	if (drawable == nullptr || context == nullptr || context->count >= context->limit
		|| !drawable->isSelectable() || drawable->isSelected()) {
		return;
	}
	Object *object = drawable->getObject();
	if (object == nullptr || !object->isLocallyControlled()
		|| object->isKindOf(KINDOF_STRUCTURE) || object->getContainedBy() != nullptr) {
		return;
	}
	const Coord3D *position = drawable->getPosition();
	if (position == nullptr) {
		return;
	}
	const Vector3 offset(position->x - context->origin.X,
		position->y - context->origin.Y, 0.0f);
	const Real across = Vector3::Dot_Product(offset, context->right);
	const Real away = Vector3::Dot_Product(offset, context->forward);
	if (across < context->acrossLo || across > context->acrossHi
		|| away < context->awayLo || away > context->awayHi) {
		return;
	}
	TheInGameUI->selectDrawable(drawable);
	if (context->message == nullptr) {
		context->message = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP);
		context->message->appendBooleanArgument(!context->additive);
	}
	context->message->appendObjectIDArgument(object->getID());
	++context->count;
}

void selectBox(const Coord3D &start, const Coord3D &end, Bool additive)
{
	if (TheGameClient == nullptr || TheInGameUI == nullptr || TheMessageStream == nullptr) {
		return;
	}
	if (additive && !canExtendUnitSelection()) return;
	const Vector3 origin(start.x, start.y, 0.0f);
	const Vector3 span(end.x - start.x, end.y - start.y, 0.0f);
	const Real acrossExtent = Vector3::Dot_Product(span, g_boxRight);
	const Real awayExtent = Vector3::Dot_Product(span, g_boxForward);
	const Vector3 corners[4] = {origin, origin + g_boxRight * acrossExtent,
		origin + g_boxRight * acrossExtent + g_boxForward * awayExtent,
		origin + g_boxForward * awayExtent};
	Region3D region;
	region.lo.x = MIN(MIN(corners[0].X, corners[1].X), MIN(corners[2].X, corners[3].X));
	region.hi.x = MAX(MAX(corners[0].X, corners[1].X), MAX(corners[2].X, corners[3].X));
	region.lo.y = MIN(MIN(corners[0].Y, corners[1].Y), MIN(corners[2].Y, corners[3].Y));
	region.hi.y = MAX(MAX(corners[0].Y, corners[1].Y), MAX(corners[2].Y, corners[3].Y));
	region.lo.z = (start.z < end.z ? start.z : end.z) - 500.0f;
	region.hi.z = (start.z > end.z ? start.z : end.z) + 2000.0f;

	if (!additive) TheInGameUI->deselectAllDrawables();
	BoxSelectionContext context{};
	context.additive = additive;
	context.count = TheInGameUI->getSelectCount();
	context.limit = selectionLimit();
	context.origin = origin;
	context.right = g_boxRight;
	context.forward = g_boxForward;
	context.acrossLo = MIN(0.0f, acrossExtent);
	context.acrossHi = MAX(0.0f, acrossExtent);
	context.awayLo = MIN(0.0f, awayExtent);
	context.awayHi = MAX(0.0f, awayExtent);
	TheGameClient->iterateDrawablesInRegion(&region, addDrawableToBoxSelection, &context);
	// An empty replacement needs the existing destroy message, not a zero-object create.
	if (!additive && context.count == 0) {
		TheMessageStream->appendMessage(GameMessage::MSG_DESTROY_SELECTED_GROUP);
	}
}

Bool updateBoxSelection(const QuestXRControllerState &rightController,
	const Vector3 &origin, const Vector3 &direction, Bool additive)
{
	Coord3D ground;
	const Bool onGround = traceTerrain(origin, direction, ground);
	if ((rightController.pressed & QUEST_XR_BUTTON_TRIGGER) != 0 && onGround) {
		g_boxSelection.armed = TRUE;
		g_boxSelection.boxing = FALSE;
		g_boxSelection.additive = additive;
		g_boxSelection.start = ground;
		g_boxSelection.end = ground;
		g_boxSelection.pressedAt = std::chrono::steady_clock::now();
		getHeadHorizontalBasis(g_boxRight, g_boxForward);
		return FALSE;
	}

	if ((rightController.buttons & QUEST_XR_BUTTON_TRIGGER) != 0
		&& g_boxSelection.armed && onGround) {
		g_boxSelection.end = ground;
		const auto held = std::chrono::steady_clock::now() - g_boxSelection.pressedAt;
		const Real deltaX = g_boxSelection.end.x - g_boxSelection.start.x;
		const Real deltaY = g_boxSelection.end.y - g_boxSelection.start.y;
		const Real minimumSweep = 0.05f * g_worldTransform.worldUnitsPerMeter;
		if (held >= std::chrono::milliseconds(250)
			&& deltaX * deltaX + deltaY * deltaY > minimumSweep * minimumSweep) {
			if (!g_boxSelection.boxing) {
				__android_log_print(ANDROID_LOG_INFO, "GeneralsXR",
					"XR visuals: box became active, sweep %.1f, threshold %.1f",
					std::sqrt(deltaX * deltaX + deltaY * deltaY), minimumSweep);
			}
			g_boxSelection.boxing = TRUE;
		}
		return FALSE;
	}

	if ((rightController.released & QUEST_XR_BUTTON_TRIGGER) != 0
		&& g_boxSelection.armed) {
		const Bool wasBoxing = g_boxSelection.boxing;
		__android_log_print(ANDROID_LOG_INFO, "GeneralsXR",
			"XR visuals: trigger released, box=%d", wasBoxing ? 1 : 0);
		if (wasBoxing) {
			selectBox(g_boxSelection.start, g_boxSelection.end, g_boxSelection.additive);
		}
		g_boxSelection = {};
		return wasBoxing;
	}
	if ((rightController.buttons & QUEST_XR_BUTTON_TRIGGER) == 0
		&& rightController.connected == 0) {
		g_boxSelection = {};
	}
	return FALSE;
}

void commandUnderRay(const Vector3 &origin, const Vector3 &direction, Bool forceAttack)
{
	if (TheGameClient == nullptr || TheMessageStream == nullptr) {
		return;
	}

	Drawable *drawable = pickDrawable(origin, direction);
	Coord3D ground;
	const Bool onGround = traceTerrain(origin, direction, ground);
	if (forceAttack) {
		if (drawable != nullptr && drawable->getObject() != nullptr) {
			GameMessage *message = TheMessageStream->appendMessage(
				GameMessage::MSG_DO_FORCE_ATTACK_OBJECT);
			message->appendObjectIDArgument(drawable->getObject()->getID());
		} else if (onGround) {
			GameMessage *message = TheMessageStream->appendMessage(
				GameMessage::MSG_DO_FORCE_ATTACK_GROUND);
			message->appendLocationArgument(ground);
		}
		return;
	}

	if (drawable == nullptr && !onGround) {
		return;
	}
	const Coord3D *position = onGround ? &ground : drawable->getPosition();
	TheGameClient->evaluateContextCommand(drawable, position, CommandTranslator::DO_COMMAND);
}

void fireArmedOrder(const Vector3 &origin, const Vector3 &direction)
{
	if (TheMessageStream == nullptr || g_armedOrder == 0) {
		return;
	}
	if (g_armedOrder == 1) {
		Drawable *target = pickDrawable(origin, direction);
		if (target != nullptr && target->getObject() != nullptr
			&& !target->getObject()->isLocallyControlled()) {
			GameMessage *message = TheMessageStream->appendMessage(
				GameMessage::MSG_DO_ATTACK_OBJECT);
			message->appendObjectIDArgument(target->getObject()->getID());
		} else {
			Coord3D ground;
			if (traceTerrain(origin, direction, ground)) {
				GameMessage *message = TheMessageStream->appendMessage(
					GameMessage::MSG_DO_ATTACKMOVETO);
				message->appendLocationArgument(ground);
			}
		}
	} else {
		Coord3D ground;
		if (traceTerrain(origin, direction, ground)) {
			GameMessage *message = TheMessageStream->appendMessage(
				GameMessage::MSG_DO_GUARD_POSITION);
			message->appendLocationArgument(ground);
			message->appendIntegerArgument(GUARDMODE_NORMAL);
		}
	}
	g_armedOrder = 0;
}

void targetPendingCommand(const Vector3 &origin, const Vector3 &direction)
{
	if (TheInGameUI == nullptr || TheMessageStream == nullptr) {
		return;
	}
	const CommandButton *command = TheInGameUI->getGUICommand();
	if (command == nullptr) {
		return;
	}
	const Int commandType = static_cast<Int>(command->getCommandType());
	if (commandType == GUI_COMMAND_GUARD
		|| commandType == GUI_COMMAND_GUARD_WITHOUT_PURSUIT
		|| commandType == GUI_COMMAND_GUARD_FLYING_UNITS_ONLY) {
		Int guardMode = GUARDMODE_NORMAL;
		if (commandType == GUI_COMMAND_GUARD_WITHOUT_PURSUIT) {
			guardMode = GUARDMODE_GUARD_WITHOUT_PURSUIT;
		} else if (commandType == GUI_COMMAND_GUARD_FLYING_UNITS_ONLY) {
			guardMode = GUARDMODE_GUARD_FLYING_UNITS_ONLY;
		}
		Drawable *target = BitIsSet(command->getOptions(), COMMAND_OPTION_NEED_OBJECT_TARGET)
			? pickDrawable(origin, direction) : nullptr;
		if (target != nullptr && target->getObject() != nullptr) {
			GameMessage *message = TheMessageStream->appendMessage(
				GameMessage::MSG_DO_GUARD_OBJECT);
			message->appendObjectIDArgument(target->getObject()->getID());
			message->appendIntegerArgument(guardMode);
		} else {
			Coord3D ground;
			if (traceTerrain(origin, direction, ground)) {
				GameMessage *message = TheMessageStream->appendMessage(
					GameMessage::MSG_DO_GUARD_POSITION);
				message->appendLocationArgument(ground);
				message->appendIntegerArgument(guardMode);
			}
		}
	} else if (commandType == GUI_COMMAND_ATTACK_MOVE) {
		Coord3D ground;
		if (traceTerrain(origin, direction, ground)) {
			GameMessage *message = TheMessageStream->appendMessage(
				GameMessage::MSG_DO_ATTACKMOVETO);
			message->appendLocationArgument(ground);
		}
	} else {
		commandUnderRay(origin, direction, FALSE);
	}
	TheInGameUI->setGUICommand(nullptr);
}

void smartTriggerUnderRay(const Vector3 &origin, const Vector3 &direction,
	Bool forceAttack, Bool additive)
{
	if (TheInGameUI == nullptr || TheGameClient == nullptr) {
		return;
	}
	if (additive) {
		selectUnderRay(origin, direction, TRUE);
		return;
	}
	Drawable *target = pickDrawable(origin, direction);
	const Bool ownSelectable = target != nullptr && target->getObject() != nullptr
		&& target->getObject()->isLocallyControlled() && target->isSelectable();
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	const Bool haveSelection = selected != nullptr && !selected->empty();
	Bool actionable = FALSE;
	if (haveSelection && target != nullptr) {
		Coord3D ground;
		const Coord3D *position = traceTerrain(origin, direction, ground)
			? &ground : target->getPosition();
		if (position != nullptr) {
			const GameMessage::Type context = TheGameClient->evaluateContextCommand(
				target, position, CommandTranslator::EVALUATE_ONLY);
			actionable = context != GameMessage::MSG_INVALID
				&& context != GameMessage::MSG_DO_MOVETO;
		}
	}
	if (actionable) {
		commandUnderRay(origin, direction, forceAttack);
	} else if (ownSelectable) {
		selectUnderRay(origin, direction);
	} else if (haveSelection) {
		commandUnderRay(origin, direction, forceAttack);
	} else {
		selectUnderRay(origin, direction);
	}
}

Bool placePendingBuilding(const QuestXRControllerState &rightController,
	const Vector3 &origin, const Vector3 &direction)
{
	const ThingTemplate *building = TheInGameUI != nullptr
		? TheInGameUI->getPendingPlaceType() : nullptr;
	if (building == nullptr) {
		if (TheInGameUI != nullptr) {
			TheInGameUI->setPlacementWorldOverride(nullptr, 0.0f);
		}
		g_activePlacement = nullptr;
		g_placementArmed = FALSE;
		return FALSE;
	}
	g_boxSelection = {};
	Coord3D position;
	const Bool havePosition = traceTerrain(origin, direction, position);
	// The release that activated a build button must never also place it. On the
	// first frame of a new pending building, adopt the mode only. A later neutral
	// frame arms placement; only a subsequent press/release can construct.
	if (building != g_activePlacement) {
		g_activePlacement = building;
		g_placementAngle = building->getPlacementViewAngle();
		g_placementArmed = FALSE;
		TheInGameUI->setPlacementWorldOverride(havePosition ? &position : nullptr,
			g_placementAngle);
		return TRUE;
	}
	if ((rightController.buttons & QUEST_XR_BUTTON_GRIP) != 0) {
		const Real frameRatio = TheFramePacer != nullptr
			? TheFramePacer->getBaseOverUpdateFpsRatio() : 1.0f;
		g_placementAngle += applyDeadzone(rightController.stick[0]) * 0.045f * frameRatio;
	}
	TheInGameUI->setPlacementWorldOverride(havePosition ? &position : nullptr,
		g_placementAngle);
	if (!g_placementArmed) {
		if ((rightController.buttons & QUEST_XR_BUTTON_TRIGGER) == 0) {
			g_placementArmed = TRUE;
		}
		return TRUE;
	}
	// Placement owns the trigger while active; never let the same release also
	// select or command something underneath the footprint.
	if ((rightController.released & QUEST_XR_BUTTON_TRIGGER) == 0
		|| TheMessageStream == nullptr) {
		return TRUE;
	}
	if (!havePosition) {
		return TRUE;
	}

	QuestVRCommitPlacement(position, g_placementAngle);
	if (TheInGameUI->getPendingPlaceType() == nullptr) {
		g_activePlacement = nullptr;
		g_placementArmed = FALSE;
	}
	return TRUE;
}

void updateCamera(const QuestXRControllerState &leftController,
	const QuestXRControllerState &rightController)
{
	if (TheTacticalView == nullptr) {
		return;
	}
	const Real frameRatio = TheFramePacer != nullptr
		? TheFramePacer->getBaseOverUpdateFpsRatio() : 1.0f;

	const Bool commandRadialOpen = g_radialKind == QUEST_VR_RADIAL_COMMANDS;
	const Real panX = commandRadialOpen ? 0.0f : applyDeadzone(leftController.stick[0]);
	const Real panY = commandRadialOpen ? 0.0f : applyDeadzone(leftController.stick[1]);
	if (panX != 0.0f || panY != 0.0f) {
		// GeneralsX @bugfix Codex 30/09/2026 Online startup replaces the
		// upstream scroll factors with its desktop camera settings. VR owns
		// this local camera response so skirmish and online use the same speed.
		// GeneralsX @tweak Codex 30/09/2026 Multiply the accepted pan response
		// by another 1.5, retaining its dead zone and fine-input acceleration curve.
		constexpr Real kVrPanFactor = 0.75f;
		const Real horizontalSpeed = kVrPanFactor * (1.0f + panX * panX);
		const Real verticalSpeed = kVrPanFactor * (1.0f + panY * panY);
		const Coord2D amount = {
			panX * horizontalSpeed * frameRatio * kScrollAmount,
			-panY * verticalSpeed * frameRatio * kScrollAmount
		};
		TheTacticalView->userScrollBy(&amount);
	}

	const Bool groupRadialOpen = g_radialKind == QUEST_VR_RADIAL_GROUPS;
	const Real turn = groupRadialOpen ? 0.0f : applyDeadzone(rightController.stick[0]);
	const Real zoom = groupRadialOpen ? 0.0f : applyDeadzone(rightController.stick[1]);
	const Bool placementRotation = TheInGameUI != nullptr
		&& TheInGameUI->getPendingPlaceType() != nullptr
		&& (rightController.buttons & QUEST_XR_BUTTON_GRIP) != 0;
	// GeneralsX @bugfix Codex 21/09/2026 Lock the right stick to the dominant
	// axis for each gesture. Independent X/Y processing made normal thumb wobble
	// accumulate altitude while turning. A slight turn bias protects rotation;
	// returning the stick to its dead zone releases the lock. This arbitration is
	// deliberately independent of the left stick, so pan plus turn/zoom remains
	// simultaneous. W3DView honors explicit height changes during fast panning.
    g_cameraStickAxis = QuestXRLockCameraAxis(g_cameraStickAxis, turn, zoom,
        groupRadialOpen || placementRotation);
	if (!placementRotation && g_cameraStickAxis == QUEST_VR_CAMERA_STICK_TURN && turn != 0.0f) {
		const Real turnStep = TheGlobalData != nullptr
			? TheGlobalData->m_keyboardCameraRotateSpeed * frameRatio
			: 0.025f * frameRatio;
		TheTacticalView->userSetAngle(
			TheTacticalView->getAngle() + cameraTurnDelta(turn, turnStep));
	}
	if (!placementRotation && g_cameraStickAxis == QUEST_VR_CAMERA_STICK_ZOOM && zoom != 0.0f) {
		// GeneralsX @feature Codex 16/09/2026 The PCVR implementation uses
		// right-stick Y for altitude, not world-scale resizing. Height-proportional
		// motion is precise near units and fast enough at overview altitude.
		const Real deltaTime = TheFramePacer != nullptr
			? TheFramePacer->getUpdateTime() : (1.0f / 90.0f);
		TheTacticalView->setZoomLimited(FALSE);
		const Real currentHeight = TheTacticalView->getHeightAboveGround();
		// GeneralsX @tweak Codex 16/09/2026 Preserve the hardware-proven legacy
		// direction: push forward to descend towards the map, pull back to rise.
		const Real newHeight = MAX(kMinimumCameraHeight, MIN(kMaximumCameraHeight,
			currentHeight * (1.0f - zoom * kStickHeightSpeed * deltaTime)));
		TheTacticalView->setHeightAboveGround(newHeight);
		const Coord3D position = TheTacticalView->getPosition();
		TheTacticalView->lookAt(&position);
	}

	// Keep a low-frequency hardware diagnostic so a capture can distinguish an
	// OpenXR action-state problem from camera math. This is aggregation only and
	// never changes input or deterministic simulation state.
	static UnsignedInt cameraInputSamples = 0;
	static UnsignedInt leftActiveSamples = 0;
	static UnsignedInt rightActiveSamples = 0;
	static UnsignedInt simultaneousSamples = 0;
	static UnsignedInt rightTurnSamples = 0;
	static UnsignedInt rightZoomSamples = 0;
	const Bool leftActive = panX != 0.0f || panY != 0.0f;
	const Bool rightActive = turn != 0.0f || zoom != 0.0f;
	leftActiveSamples += leftActive ? 1U : 0U;
	rightActiveSamples += rightActive ? 1U : 0U;
	simultaneousSamples += leftActive && rightActive ? 1U : 0U;
	rightTurnSamples += g_cameraStickAxis == QUEST_VR_CAMERA_STICK_TURN ? 1U : 0U;
	rightZoomSamples += g_cameraStickAxis == QUEST_VR_CAMERA_STICK_ZOOM ? 1U : 0U;
	if (++cameraInputSamples >= 240U) {
		const double divisor = static_cast<double>(cameraInputSamples);
		__android_log_print(ANDROID_LOG_INFO, "GeneralsXR",
			"XR camera input: left %.1f%%, right %.1f%%, simultaneous %.1f%%, turn-locked %.1f%%, zoom-locked %.1f%%; legacy pan %.3f/%.3f keyboard %.3f, frame_ratio %.3f, VR baseline %.1f",
			100.0 * leftActiveSamples / divisor,
			100.0 * rightActiveSamples / divisor,
			100.0 * simultaneousSamples / divisor,
			100.0 * rightTurnSamples / divisor,
			100.0 * rightZoomSamples / divisor,
			TheGlobalData != nullptr ? TheGlobalData->m_horizontalScrollSpeedFactor : 0.0f,
			TheGlobalData != nullptr ? TheGlobalData->m_verticalScrollSpeedFactor : 0.0f,
			TheGlobalData != nullptr ? TheGlobalData->m_keyboardScrollFactor : 0.0f,
			frameRatio, kScrollAmount * 0.75f);
		cameraInputSamples = 0;
		leftActiveSamples = 0;
		rightActiveSamples = 0;
		simultaneousSamples = 0;
		rightTurnSamples = 0;
		rightZoomSamples = 0;
	}
}
} // namespace

extern "C" void QuestVRInput_Update()
{
	if (TheGameLogic == nullptr || !TheGameLogic->isInGame()) {
		g_boxSelection = {};
		// GeneralsX @bugfix Codex 16/09/2026 A deliberately selected
		// Battlefield view is match-local. Returning to the shell restores the
		// saved Tabletop transform so the next match cannot inherit ground view.
		if (g_wasInGame) {
			g_viewMode = QUEST_VR_VIEW_TABLETOP;
			g_worldTransform = g_tabletopTransform;
			g_wasInGame = FALSE;
			g_comfortPivotValid = FALSE;
			g_followActive = FALSE;
			g_hudVisible = TRUE;
			g_cameraStickAxis = QUEST_VR_CAMERA_STICK_NONE;
			closeRadial();
			g_bButtonDownTime = {};
			g_bButtonLongFired = FALSE;
			g_armedOrder = 0;
			g_lastGroupRecalled = -1;
			g_lastGroupRecallTime = {};
		}
		g_tabletopGrab = {};
		hidePointerVisuals();
		return;
	}
	// GeneralsX @bugfix Codex 09/10/2026 Text entry consumes controls without pausing simulation.
	if (QuestVRKeyboard::InputBlocked() || QuestCampaign::IsFlatSequence()) {
		g_boxSelection = {};
		g_comfortTiltGesture = {};
		g_tabletopGrab = {};
		hidePointerVisuals();
		return;
	}
	loadViewPreferences();
	g_wasInGame = TRUE;
	updateBattlefieldTransform();

	QuestXRControllerState leftController{};
	QuestXRControllerState rightController{};
	const Bool haveLeft = QuestXR_GetControllerState(0, &leftController);
	const Bool haveRight = QuestXR_GetControllerState(kRightHand, &rightController);
	if (!haveLeft && !haveRight) {
		g_tabletopGrab = {};
		return;
	}

	if (TheGameLogic->isGamePaused()
		|| TheInGameUI == nullptr || !TheInGameUI->getInputEnabled()
		|| TheInGameUI->isQuitMenuVisible()) {
		g_boxSelection = {};
		g_tabletopGrab = {};
		hidePointerVisuals();
		return;
	}
	if (haveLeft && handleFollowSelection(leftController)) {
		return;
	}
	if (haveLeft && haveRight && toggleViewMode(leftController, rightController)) {
		return;
	}
	const Bool radialConsumedCancel = updatePanelAndRadials(
		leftController, rightController, haveLeft, haveRight);
	if (haveLeft && !radialConsumedCancel) {
		handleCancel(leftController);
	}
	if (haveRight && g_radialKind != QUEST_VR_RADIAL_GROUPS) {
		handleRightStickClick(rightController);
	}

	const Bool recenterHeld = haveLeft
		&& (leftController.buttons & QUEST_XR_BUTTON_MENU) != 0;
	const Bool placementActive = TheInGameUI->getPendingPlaceType() != nullptr;
	if (placementActive || recenterHeld) {
		g_tabletopGrab = {};
	}
	const Bool tilting = updateComfortTilt(leftController, rightController, !placementActive && !recenterHeld);
	if (tilting) g_tabletopGrab = {};
	const Bool tabletopGrabbed = tilting || (!placementActive && !recenterHeld
		&& updateTabletopGrab(leftController, rightController));
	if (!tabletopGrabbed) {
		updateCamera(leftController, rightController);
	}
	updatePointerVisuals();
	if (!haveRight || rightController.connected == 0 || rightController.poseValid == 0) {
		g_boxSelection = {};
		g_uiCapturedButtons = 0;
		return;
	}
	// The compositor UI sits outside the W3D depth buffer. Consume the right
	// controller here while it intersects that quad. A button that went down on
	// the UI remains captured until release even if the ray leaves the panel, so
	// the release cannot also select or command the world behind it.
	constexpr std::uint32_t uiButtons = QUEST_XR_BUTTON_TRIGGER | QUEST_XR_BUTTON_PRIMARY;
	if (rightController.menuPointValid != 0) {
		if (g_radialKind != QUEST_VR_RADIAL_NONE
			&& radialContainsPoint(rightController.menuPoint[0], rightController.menuPoint[1])) {
			const Int sector = radialSectorAt(
				rightController.menuPoint[0], rightController.menuPoint[1], TRUE);
			if (sector >= 0) {
				g_radialSector = sector;
				g_radialOpenTime = std::chrono::steady_clock::now();
				if ((rightController.pressed & QUEST_XR_BUTTON_TRIGGER) != 0) {
					const QuestVRRadialKind kind = g_radialKind;
					closeRadial();
					fireRadialSector(kind, sector, rightController);
				}
			}
			g_boxSelection = {};
			g_uiCapturedButtons |= rightController.buttons & uiButtons;
			return;
		}
		g_uiCapturedButtons |= rightController.buttons & uiButtons;
		g_uiCapturedButtons &= ~rightController.released;
		g_boxSelection = {};
		return;
	}
	const std::uint32_t capturedThisFrame = g_uiCapturedButtons
		& (rightController.buttons | rightController.released);
	g_uiCapturedButtons &= ~rightController.released;
	if (capturedThisFrame != 0) {
		g_boxSelection = {};
		return;
	}
	if (tabletopGrabbed) {
		g_boxSelection = {};
		return;
	}
	Vector3 origin;
	Vector3 direction;
	if (!computeHandRay(rightController, origin, direction)) {
		return;
	}
	if (placePendingBuilding(rightController, origin, direction)) {
		return;
	}

	const Bool pendingTarget = TheInGameUI->getGUICommand() != nullptr;
	// GeneralsX @feature Codex 17/10/2026 Capture the modifier at trigger-down; explicit orders keep ownership.
	const Bool additive = additiveSelectionHeld(leftController, rightController, haveLeft);
	const Bool additiveClick = g_boxSelection.armed ? g_boxSelection.additive : additive;
	Bool boxConsumedRelease = FALSE;
	if (pendingTarget || g_armedOrder != 0) {
		g_boxSelection = {};
	} else {
		boxConsumedRelease = updateBoxSelection(rightController, origin, direction, additive);
	}
	if ((rightController.released & QUEST_XR_BUTTON_TRIGGER) != 0 && !boxConsumedRelease) {
		const Bool forceAttack = haveLeft
			&& (leftController.buttons & QUEST_XR_BUTTON_SECONDARY) != 0;
		if (g_armedOrder != 0) {
			fireArmedOrder(origin, direction);
		} else if (pendingTarget) {
			targetPendingCommand(origin, direction);
		} else {
			smartTriggerUnderRay(origin, direction, forceAttack, additiveClick);
		}
	}
	if ((rightController.pressed & QUEST_XR_BUTTON_PRIMARY) != 0) {
		const Bool forceAttack = haveLeft
			&& (leftController.buttons & QUEST_XR_BUTTON_SECONDARY) != 0;
		commandUnderRay(origin, direction, forceAttack);
	}
}

void QuestVRInput_Render(CameraClass *camera)
{
	if (camera != nullptr && g_pointerScene != nullptr) {
		WW3D::Render(g_pointerScene, camera);
		// GeneralsX @bugfix Codex 18/09/2026 Render each selected-object health
		// line outside SimpleScene so scene culling and insertion order cannot
		// erase this controller-selection feedback across process restarts.
		RenderInfoClass overlayRenderInfo(*camera);
		for (Int marker = 0; marker < kMaximumSelectionMarkers; ++marker) {
			if (g_healthFill[marker] != nullptr && !g_healthFill[marker]->Is_Hidden()) {
				g_healthFill[marker]->Render(overlayRenderInfo);
			}
		}

		// GeneralsX @feature Codex 18/09/2026 Sample the actual render-object
		// visibility chosen by SimpleScene. This distinguishes input-state loss
		// from per-eye frustum/render loss on a restarted Quest process.
		static UnsignedInt diagnosticRenders = 0;
		if (++diagnosticRenders >= 120) {
			diagnosticRenders = 0;
			Int visibleBoxes = 0;
			Int visibleFills = 0;
			for (Line3DClass *line : g_boxLines) {
				if (line != nullptr && !line->Is_Hidden() && line->Is_Visible()) {
					++visibleBoxes;
				}
			}
			for (Int marker = 0; marker < kMaximumSelectionMarkers; ++marker) {
				if (g_healthFill[marker] != nullptr
					&& !g_healthFill[marker]->Is_Hidden()) {
					++visibleFills;
				}
			}
			const DrawableList *selected = TheInGameUI != nullptr
				? TheInGameUI->getAllSelectedDrawables() : nullptr;
			__android_log_print(ANDROID_LOG_INFO, "GeneralsXR",
				"XR visuals: selected=%d boxing=%d visible_box=%d visible_health=%d "
				"ray_hidden=%d ray_visible=%d scale=%.1f",
				selected != nullptr ? static_cast<Int>(selected->size()) : -1,
				g_boxSelection.boxing ? 1 : 0, visibleBoxes, visibleFills,
				g_rayLines[kRightHand] == nullptr || g_rayLines[kRightHand]->Is_Hidden() ? 1 : 0,
				g_rayLines[kRightHand] != nullptr && g_rayLines[kRightHand]->Is_Visible() ? 1 : 0,
				g_worldTransform.worldUnitsPerMeter);
		}
	}
}

extern "C" bool QuestVRInput_ShouldShowHud()
{
	return g_hudVisible || g_radialKind != QUEST_VR_RADIAL_NONE;
}

extern "C" bool QuestVRInput_IsRadialOpen()
{
	return g_radialKind != QUEST_VR_RADIAL_NONE;
}

extern "C" bool QuestVRInput_ConsumesUiPoint(float normalizedX, float normalizedY)
{
	return QuestVRKeyboard::InputBlocked() || radialContainsPoint(normalizedX, normalizedY);
}

void QuestVRInput_DrawUiOverlay()
{
	if (g_radialKind == QUEST_VR_RADIAL_NONE || TheDisplay == nullptr) {
		return;
	}
	Int centerX = 0;
	Int centerY = 0;
	Int radius = 0;
	getRadialGeometry(centerX, centerY, radius);
	const Bool groups = g_radialKind == QUEST_VR_RADIAL_GROUPS;
	const Int sectorCount = groups ? 10 : kRadialCommandSectors;
	const UnsignedInt background = GameMakeColor(10, 16, 28, 215);
	const UnsignedInt ring = GameMakeColor(45, 95, 118, 255);
	const UnsignedInt accent = GameMakeColor(89, 217, 255, 255);
	const UnsignedInt text = GameMakeColor(235, 240, 245, 255);
	const UnsignedInt dim = GameMakeColor(150, 160, 175, 255);
	const UnsignedInt shadow = GameMakeColor(0, 0, 0, 200);

	for (Int row = -radius; row <= radius; ++row) {
		const Real ratio = static_cast<Real>(row) / radius;
		const Real half = std::sqrt(MAX(0.0f, 1.0f - ratio * ratio)) * radius;
		TheDisplay->drawFillRect(centerX - static_cast<Int>(half), centerY + row,
			static_cast<Int>(half) * 2, 1, background);
	}
	constexpr Int segments = 96;
	for (Int index = 0; index < segments; ++index) {
		const Real angle0 = static_cast<Real>(index) / segments * 2.0f * kPi;
		const Real angle1 = static_cast<Real>(index + 1) / segments * 2.0f * kPi;
		TheDisplay->drawLine(centerX + static_cast<Int>(std::sin(angle0) * radius),
			centerY - static_cast<Int>(std::cos(angle0) * radius),
			centerX + static_cast<Int>(std::sin(angle1) * radius),
			centerY - static_cast<Int>(std::cos(angle1) * radius), 4.0f, ring);
	}
	for (Int sector = 0; sector < sectorCount; ++sector) {
		const Real angle = (static_cast<Real>(sector) - 0.5f) / sectorCount * 2.0f * kPi;
		TheDisplay->drawLine(
			centerX + static_cast<Int>(std::sin(angle) * radius * 0.35f),
			centerY - static_cast<Int>(std::cos(angle) * radius * 0.35f),
			centerX + static_cast<Int>(std::sin(angle) * radius),
			centerY - static_cast<Int>(std::cos(angle) * radius), 1.5f, ring);
	}
	if (g_radialSector >= 0) {
		const Real middle = static_cast<Real>(g_radialSector) / sectorCount * 2.0f * kPi;
		const Real halfSpan = kPi / sectorCount;
		constexpr Int highlightSegments = 24;
		for (Int index = 0; index < highlightSegments; ++index) {
			const Real angle0 = middle - halfSpan
				+ halfSpan * 2.0f * index / highlightSegments;
			const Real angle1 = middle - halfSpan
				+ halfSpan * 2.0f * (index + 1) / highlightSegments;
			TheDisplay->drawLine(centerX + static_cast<Int>(std::sin(angle0) * radius),
				centerY - static_cast<Int>(std::cos(angle0) * radius),
				centerX + static_cast<Int>(std::sin(angle1) * radius),
				centerY - static_cast<Int>(std::cos(angle1) * radius), 5.0f, accent);
		}
		TheDisplay->drawLine(centerX, centerY,
			centerX + static_cast<Int>(std::sin(middle) * radius * 0.9f),
			centerY - static_cast<Int>(std::cos(middle) * radius * 0.9f), 2.0f, accent);
	}

	if (TheDisplayStringManager == nullptr || TheFontLibrary == nullptr) {
		return;
	}
	for (Int sector = 0; sector < sectorCount; ++sector) {
		DisplayString *&label = g_radialStrings[sector];
		if (label == nullptr) {
			label = TheDisplayStringManager->newDisplayString();
			if (label != nullptr) {
				label->setFont(TheFontLibrary->getFont("Arial", 36, TRUE));
			}
		}
		if (label == nullptr) {
			continue;
		}
		UnicodeString value;
		if (groups) {
			value.format(L"%d", (sector + 1) % 10);
		} else {
			value.set(kRadialCommandNames[sector]);
		}
		label->setText(value);
		const Real angle = static_cast<Real>(sector) / sectorCount * 2.0f * kPi;
		Int width = 0;
		Int height = 0;
		label->getSize(&width, &height);
		label->draw(centerX + static_cast<Int>(std::sin(angle) * radius * 0.68f) - width / 2,
			centerY - static_cast<Int>(std::cos(angle) * radius * 0.68f) - height / 2,
			sector == g_radialSector ? accent : text, shadow);
	}
	DisplayString *&hint = g_radialStrings[15];
	if (hint == nullptr) {
		hint = TheDisplayStringManager->newDisplayString();
		if (hint != nullptr) {
			hint->setFont(TheFontLibrary->getFont("Arial", 20, FALSE));
		}
	}
	if (hint != nullptr) {
		UnicodeString value;
		value.set(groups ? L"flick = recall    trigger + flick = assign"
			: L"flick and release");
		hint->setText(value);
		Int width = 0;
		Int height = 0;
		hint->getSize(&width, &height);
		hint->draw(centerX - width / 2, centerY - height / 2, dim, shadow);
	}
}

void QuestVRInput_Shutdown()
{
	QuestVRKeyboard::Shutdown();
	if (TheDisplayStringManager != nullptr) {
		for (DisplayString *&string : g_radialStrings) {
			if (string != nullptr) {
				TheDisplayStringManager->freeDisplayString(string);
				string = nullptr;
			}
		}
	}
	for (Line3DClass *&line : g_rayLines) {
		if (line != nullptr) {
			if (g_pointerScene != nullptr) {
				g_pointerScene->Remove_Render_Object(line);
			}
			line->Release_Ref();
			line = nullptr;
		}
	}
	for (Line3DClass *&line : g_boxLines) {
		if (line != nullptr) {
			if (g_pointerScene != nullptr) {
				g_pointerScene->Remove_Render_Object(line);
			}
			line->Release_Ref();
			line = nullptr;
		}
	}
	for (Int marker = 0; marker < kMaximumSelectionMarkers; ++marker) {
		if (g_healthFill[marker] != nullptr) {
			g_healthFill[marker]->Release_Ref();
		}
		g_healthFill[marker] = nullptr;
	}
	if (g_pointerScene != nullptr) {
		g_pointerScene->Release_Ref();
		g_pointerScene = nullptr;
	}
	g_boxSelection = {};
	g_lastDiagnosticMarkerCount = -1;
	g_boxRight = Vector3(1.0f, 0.0f, 0.0f);
	g_boxForward = Vector3(0.0f, 1.0f, 0.0f);
	g_activePlacement = nullptr;
	g_placementArmed = FALSE;
	g_placementAngle = 0.0f;
	g_uiCapturedButtons = 0;
	g_lastSingleSelection = INVALID_ID;
	g_lastSingleSelectionTime = {};
	g_lastRightStickClickTime = {};
	g_worldTransform = {QUEST_XR_WORLD_UNITS_PER_METER, 0.0f, {0.0f, 0.0f, 0.0f}};
	g_tabletopTransform = g_worldTransform;
	g_viewMode = QUEST_VR_VIEW_TABLETOP;
	g_viewPreferencesLoaded = FALSE;
	g_tabletopTransformDirty = FALSE;
	g_wasInGame = FALSE;
	g_followActive = FALSE;
	g_hudVisible = TRUE;
	closeRadial();
	g_bButtonDownTime = {};
	g_bButtonLongFired = FALSE;
	g_armedOrder = 0;
	g_tabletopGrab = {};
}

void QuestVRInput_GetWorldTransform(QuestVRWorldTransform *transform)
{
	if (transform != nullptr) {
		*transform = g_worldTransform;
		transform->comfortPitchRadians = 0.0f;
		if (g_viewMode != QUEST_VR_VIEW_TABLETOP || g_comfortDegrees == 0.0f) {
			g_comfortPivotValid = FALSE;
			return;
		}
		const auto generation = QuestXR_GetComfortSpaceGeneration();
		if (!g_comfortPivotValid || generation != g_comfortSpaceGeneration) {
			QuestXREyeView left{}, right{};
			if (!QuestXR_GetEyeView(0, &left) || !QuestXR_GetEyeView(1, &right)) return;
			for (unsigned axis = 0; axis < 3; ++axis) {
				g_comfortPivot[axis] = (left.position[axis] + right.position[axis]) * 0.5f;
				if (!std::isfinite(g_comfortPivot[axis])) return;
			}
			g_comfortSpaceGeneration = generation;
			g_comfortPivotValid = TRUE;
			__android_log_print(ANDROID_LOG_INFO, "GeneralsXR", "Quest comfort: tilt=%.0f degrees; shared stereo/ray pivot; origin=%llu",
				g_comfortDegrees, static_cast<unsigned long long>(generation));
		}
		transform->comfortPitchRadians = -g_comfortDegrees * 0.017453292519943295f;
		for (unsigned axis = 0; axis < 3; ++axis) transform->comfortPivotMeters[axis] = g_comfortPivot[axis];
	}
}

#endif
