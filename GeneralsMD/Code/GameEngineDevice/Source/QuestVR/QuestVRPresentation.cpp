// SPDX-License-Identifier: GPL-3.0-or-later
// Presentation adapted exclusively from offline v0.44 (176a7ce7).
#include "WWMath/quat.h"
#include "WinMain.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DView.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Override.h"
#include "GameClient/GameFont.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/TerrainLogic.h"
#include "GameClient/InGameUI.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GameWindow.h"
#include "GameClient/ControlBar.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Shell.h"
#include "GameClient/Mouse.h"
#include "GameClient/DisplayStringManager.h"
#include "WW3D2/ww3d.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/camera.h"
#include "VRDevice/QuestXRInterface.h"
#include "VRDevice/QuestEyeCaptureCamera.h"
#include "VRDevice/QuestDrawCapture.h"
#include "VRDevice/QuestSoftCutouts.h"
#include "VRDevice/QuestPerformance.h"
#include "VRDevice/QuestTreeWind.h"
#include "VRDevice/QuestVRInput.h"
#include "VRDevice/QuestVRKeyboard.h"
#include "VRDevice/QuestCampaign.h"
#include <android/log.h>
#include <chrono>
#include <cmath>
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include "QuestAppVersion.h"
extern Bool g_gxIntroBlackoutAllowed;
namespace
{
Real getQuestXRUiCropTop()
{
	constexpr Real kControlBarTop = 0.65f;
	constexpr Real kSelectedObjectTop = 0.50f;
	Real top = kControlBarTop;
	if (TheWindowManager == nullptr || TheDisplay == nullptr
		|| TheDisplay->getWidth() <= 0 || TheDisplay->getHeight() <= 0) {
		return top;
	}

	const Real screenHeight = static_cast<Real>(TheDisplay->getHeight());
	auto includeWindow = [&top, screenHeight](GameWindow *window) {
		if (window == nullptr || window->winIsHidden()) {
			return;
		}
		Int x = 0;
		Int y = 0;
		Int width = 0;
		Int height = 0;
		window->winGetScreenPosition(&x, &y);
		window->winGetSize(&width, &height);
		if (y + height <= 0 || static_cast<Real>(y) >= screenHeight) {
			return;
		}
		constexpr Int kTextMargin = 24;
		top = MIN(top, MAX(0.0f, static_cast<Real>(y - kTextMargin) / screenHeight));
	};

	// GeneralsX @bugfix Codex 16/09/2026 The selected-object name and status
	// extend above the compact command strip. Grow only while a selection needs
	// that information instead of keeping an opaque half-screen panel forever.
	if (TheInGameUI != nullptr && TheInGameUI->getSelectCount() > 0) {
		top = kSelectedObjectTop;
	}

	// GeneralsX @bugfix Codex 16/09/2026 Do not scan every visible root
	// window: ControlBar owns a permanent full-screen root at y=0, which made
	// the opaque Quest HUD a large black rectangle forever. Only UI that really
	// extends beyond the compact control bar may grow the panel.
	GameWindow *promotion = TheNameKeyGenerator != nullptr
		? TheWindowManager->winGetWindowFromId(nullptr,
			TheNameKeyGenerator->nameToKey("GeneralsExpPoints.wnd:GenExpParent"))
		: nullptr;
	includeWindow(promotion);
	// GeneralsX @bugfix Codex 30/09/2026 Building descriptions resize and
	// move above the selected-object strip. Include their real visible root,
	// rather than cutting off a line at the fixed 50% HUD boundary.
	WindowLayout *description = TheControlBar != nullptr
		? TheControlBar->getQuestBuildTooltipLayout() : nullptr;
	if (description != nullptr && !description->isHidden()) {
		includeWindow(description->getFirstWindow());
	}
	return top;
}
} // namespace

// W3DDisplay::drawQuestXRMenu ===============================================
/** Repaint the existing Generals UI into a real D3D8 render target. The
	* OpenXR owner presents either the complete front-end/pause menu or a crop
	* containing the in-game control bar. No widget logic is duplicated. */
//=============================================================================
void W3DDisplay::drawQuestXRMenu()
{
	const bool worldReady = TheGameLogic != nullptr && TheGameLogic->isInGame()
		&& TheTerrainRenderObject != nullptr && TheTerrainRenderObject->getMap() != nullptr;
	if (TheInGameUI == nullptr) {
		return;
	}
	// GeneralsX @bugfix Codex 15/09/2026 GAME_SHELL has a live terrain map,
	// but it is still the front end. Treating every rendered map as a match
	// cropped the main menu to the bottom HUD strip until B opened a dialog.
	const bool shellActive = TheShell != nullptr && TheShell->isShellActive();
	const bool fullScreenUI = !worldReady || shellActive || TheInGameUI->isQuitMenuVisible()
		|| QuestVRKeyboard::IsOpen() || QuestCampaign::NeedsFullUI();
	const uint32_t presentation = fullScreenUI ? QUEST_XR_MENU_FRONTEND
		: (QuestVRInput_ShouldShowHud() ? QUEST_XR_MENU_HUD : QUEST_XR_MENU_HIDDEN);
	// GeneralsX @feature Codex 16/09/2026 Grow the wrist panel upward to the
	// highest visible UI window. Normal play remains a compact control bar, but
	// object details and the Generals promotion screen are no longer cropped.
	QuestXR_SetMenuCropTop(presentation == QUEST_XR_MENU_HUD
		? (QuestVRInput_IsRadialOpen() ? 0.0f : getQuestXRUiCropTop()) : 0.0f);
	IDirect3DSurface8 *menuSurface = QuestXR_GetMenuSurface();
	if (menuSurface == nullptr) {
		return;
	}

	const bool flatWorld = QuestCampaign::IsFlatSequence() && worldReady
        && !QuestCampaign::IsBlockingMovie() && !isMoviePlaying()
        && TheGlobalData && !TheGlobalData->m_loadScreenRender;
    IDirect3DSurface8 *flatDepth = flatWorld
        ? QuestXR_GetMenuDepthSurface(DX8Wrapper::_Get_D3D_Device8()) : nullptr;
    if (flatWorld && !flatDepth) return;
    if (flatWorld) DX8Wrapper::Set_Render_Target(menuSurface, flatDepth);
    else DX8Wrapper::Set_Render_Target(menuSurface, true);
	bool rendered = false;
	if (WW3D::Begin_Render(true, flatWorld, Vector3(0.0f, 0.0f, 0.0f)) == WW3D_ERROR_OK) {
        // Original composed camera, perspective and view filters; no tracked eye substitution.
        if (flatWorld) drawViews();
		TheInGameUI->DRAW();
		QuestVRInput_DrawUiOverlay();
        if (flatWorld) renderLetterBox(timeGetTime());
        if (m_cinematicText != AsciiString::TheEmptyString && m_cinematicTextFrames != 0
            && TheDisplayStringManager) {
            DisplayString *text = TheDisplayStringManager->newDisplayString();
            if (text) {
                text->setWordWrap(getWidth() - 20);
                text->setWordWrapCentered(TRUE);
                UnicodeString translated;
                translated.translate(m_cinematicText);
                text->setText(translated);
                text->setFont(m_cinematicFont);
                const Int x = text->getWidth() > getWidth() ? 20 : (getWidth() - text->getWidth()) / 2;
                text->draw(x, static_cast<Int>(getHeight() * .9f),
                    GameMakeColor(255,255,255,255), GameMakeColor(0,0,0,0));
                TheDisplayStringManager->freeDisplayString(text);
                --m_cinematicTextFrames;
            }
        }
		// The flat W3D pass is skipped in XR. Present its movie texture and
		// between-movie blackout on the same front-end quad as the menus.
		if (m_videoStream != nullptr && m_videoBuffer != nullptr) {
			drawScaledVideoBuffer(m_videoBuffer, m_videoStream);
		} else if (TheGlobalData != nullptr
			&& (TheGlobalData->m_playIntro || TheGlobalData->m_afterIntro)
			&& g_gxIntroBlackoutAllowed) {
			drawFillRect(0, 0, getWidth(), getHeight(), GameMakeColor(0, 0, 0, 255));
		}
		if ((worldReady || shellActive)
			&& TheDisplayStringManager != nullptr && TheFontLibrary != nullptr) {
			// GeneralsX @feature Codex 16/09/2026 One unobtrusive corner label
			// shares the existing UI pass: APK version in the front end and FPS
			// during play, without an extra XR layer or scene traversal.
			if (m_benchmarkDisplayString == nullptr) {
				m_benchmarkDisplayString = TheDisplayStringManager->newDisplayString();
				if (m_benchmarkDisplayString != nullptr) {
					m_benchmarkDisplayString->setFont(TheFontLibrary->getFont("Arial", 20, TRUE));
				}
			}
			if (m_benchmarkDisplayString != nullptr) {
				// GeneralsX @tweak Codex 30/09/2026 The wrist label was unreadable
				// on hardware. Both FPS and the front-end version now use fourfold size.
				m_benchmarkDisplayString->setFont(TheFontLibrary->getFont("Arial", 80, TRUE));
				UnicodeString cornerText;
				if (shellActive) {
					cornerText.format(L"v%S", QUEST_XR_APP_VERSION);
				} else {
					// GeneralsX @tweak Codex 30/09/2026 Keep the small wrist FPS
					// label steady between half-second samples. Read only the
					// existing render average; never change simulation timing.
					static auto nextFpsSample = std::chrono::steady_clock::time_point{};
					static Real displayedFps = 0.0f;
					const auto now = std::chrono::steady_clock::now();
					if (now >= nextFpsSample) {
						displayedFps = getAverageFPS();
						nextFpsSample = now + std::chrono::milliseconds(500);
					}
					cornerText.format(L"%.0f FPS", displayedFps);
				}
				m_benchmarkDisplayString->setText(cornerText);
				Int textWidth = 0;
				Int textHeight = 0;
				m_benchmarkDisplayString->getSize(&textWidth, &textHeight);
				m_benchmarkDisplayString->draw(getWidth() - textWidth - 8,
					getHeight() - textHeight - 6,
					shellActive ? GameMakeColor(170, 180, 185, 210)
						: GameMakeColor(210, 220, 220, 230),
					GameMakeColor(0, 0, 0, 220));
			}
		}
		if (TheMouse != nullptr && !QuestVRKeyboard::IsOpen()) {
			TheMouse->DRAW();
		}
		// GeneralsX @feature Codex 09/10/2026 Keep text keys and the reticle in the same UI texture.
		QuestVRKeyboard::Draw();

		// GeneralsX @feature Codex 15/09/2026 SDL3 renders its cursor through
		// the Android window system, which is not part of this OpenXR texture.
		// Draw a small, high-contrast reticle at the same normalized controller
		// point that the input adapter maps into the legacy mouse queue.
		QuestXRControllerState pointer{};
		if (QuestXR_GetControllerState(1, &pointer) && pointer.menuPointValid != 0) {
			constexpr Int kPointerRadius = 10;
			const Int maxX = static_cast<Int>(getWidth()) - kPointerRadius - 1;
			const Int maxY = static_cast<Int>(getHeight()) - kPointerRadius - 1;
			Int pointerX = static_cast<Int>(pointer.menuPoint[0] * getWidth());
			Int pointerY = static_cast<Int>(pointer.menuPoint[1] * getHeight());
			pointerX = MAX(kPointerRadius, MIN(pointerX, maxX));
			pointerY = MAX(kPointerRadius, MIN(pointerY, maxY));
			const UnsignedInt outline = GameMakeColor(0, 0, 0, 255);
			const UnsignedInt color = GameMakeColor(64, 224, 255, 255);
			drawLine(pointerX - kPointerRadius, pointerY,
				pointerX + kPointerRadius, pointerY, 4.0f, outline);
			drawLine(pointerX, pointerY - kPointerRadius,
				pointerX, pointerY + kPointerRadius, 4.0f, outline);
			drawLine(pointerX - kPointerRadius, pointerY,
				pointerX + kPointerRadius, pointerY, 2.0f, color);
			drawLine(pointerX, pointerY - kPointerRadius,
				pointerX, pointerY + kPointerRadius, 2.0f, color);
		}
		WW3D::End_Render(false);
		rendered = true;
	}
	DX8Wrapper::Set_Render_Target(static_cast<IDirect3DSurface8 *>(nullptr));
	if (rendered) {
		QuestXR_MarkMenuRenderingComplete(presentation);
	}
}

// W3DDisplay::drawQuestXRStereo =============================================
/** Native Quest dual-pass renderer. The tactical camera supplies position and
	* yaw, while OpenXR supplies each eye's tracked pose and asymmetric frustum.
	* Pitch and roll from the tactical camera are intentionally removed so the
	* tabletop horizon remains level as the user moves their head. */
//=============================================================================
void W3DDisplay::drawQuestXRStereo(W3DView *view)
{
	// Capture both terrain-independent menus and the in-game control bar before
	// applying the battlefield readiness gates below.
	const auto menuStart = std::chrono::steady_clock::now();
	drawQuestXRMenu();
	const double menuCpuMs = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - menuStart).count();
	if (QuestCampaign::IsFlatSequence()) return;
	if (view == nullptr || m_3DScene == nullptr || TheGameLogic == nullptr
		|| !TheGameLogic->isInGame() || TheTerrainRenderObject == nullptr
		|| TheTerrainRenderObject->getMap() == nullptr) {
		return;
	}

	CameraClass *tacticalCamera = view->get3DCamera();
	IDirect3DSurface8 *depthSurface = QuestXR_GetDepthSurface();
	if (tacticalCamera == nullptr || depthSurface == nullptr) {
		return;
	}

	// GeneralsX @bugfix Codex 16/09/2026 The legacy terrain renderer keeps a
	// small moving window around the tactical camera. XR eye frusta can see far
	// outside that window while independently culled buildings remain visible,
	// leaving an abrupt terrain edge in the headset. GeneralsVR uses the engine's
	// cinematic oversize hook to pin that window to the complete map; the hook
	// clamps to the actual map dimensions and subsequent updateTerrain() calls
	// preserve the override.
	TheTerrainRenderObject->oversizeTerrain(256);

	QuestXREyeView eyeViews[2]{};
	IDirect3DSurface8 *eyeSurfaces[2]{};
	for (uint32_t eye = 0; eye < 2; ++eye) {
		if (!QuestXR_GetEyeView(eye, &eyeViews[eye])) {
			return;
		}
		eyeSurfaces[eye] = QuestXR_GetEyeSurface(eye);
		if (eyeSurfaces[eye] == nullptr) {
			return;
		}
	}

	if (m_questXRCamera == nullptr) {
		m_questXRCamera = NEW_REF(CameraClass, ());
	}
	if (m_questXRCamera == nullptr) {
		return;
	}

	const Matrix3D &tacticalTransform = tacticalCamera->Get_Transform();
	const Vector3 worldUp(0.0f, 0.0f, 1.0f);
	Vector3 forward = -tacticalTransform.Get_Z_Vector();
	forward.Z = 0.0f;
	if (forward.Length2() < 0.0001f) {
		forward = Vector3(1.0f, 0.0f, 0.0f);
	}
	forward.Normalize();
	Vector3 right;
	Vector3::Cross_Product(forward, worldUp, &right);
	right.Normalize();
	const Matrix3D anchor(right, worldUp, -forward, tacticalCamera->Get_Position());

	QuestVRWorldTransform worldTransform{};
	QuestVRInput_GetWorldTransform(&worldTransform);
	const Real worldUnitsPerMeter = worldTransform.worldUnitsPerMeter;
	Quaternion tabletopOrientation(0.0f, std::sin(worldTransform.yawRadians * 0.5f),
		0.0f, std::cos(worldTransform.yawRadians * 0.5f));
	Matrix3D tabletopTransform;
	Build_Matrix3D(tabletopOrientation, tabletopTransform);
	tabletopTransform.Set_Translation(Vector3(worldTransform.offset[0],
		worldTransform.offset[1], worldTransform.offset[2]));
    // GeneralsX @performance Codex 05/10/2026 A cache lives only across this explicit stereo pair.
    QuestXR::PerformanceFrameScope performanceFrame;
    QuestXR::TreeWindFrameScope windFrame;
    static auto previousStereoStart = std::chrono::steady_clock::time_point{};
    const auto stereoStart = std::chrono::steady_clock::now();
    if (previousStereoStart != std::chrono::steady_clock::time_point{})
        QuestXR::RecordPerformanceFrameInterval(std::chrono::duration<double, std::milli>(
            stereoStart - previousStereoStart).count());
    previousStereoStart = stereoStart;
	bool renderedBothEyes = true;
	double worldCpuMs[2] = {};
	double pointerCpuMs[2] = {};
	double eyeCpuMs[2] = {};
	bool advancedSceneThisFrame = false;
	for (uint32_t eye = 0; eye < 2; ++eye) {
		const auto eyeStart = std::chrono::steady_clock::now();
		bool renderedEye = false;
		const QuestXREyeView &viewState = eyeViews[eye];
		Quaternion orientation(viewState.orientation[0], viewState.orientation[1],
			viewState.orientation[2], viewState.orientation[3]);
		Matrix3D trackedPose;
		Build_Matrix3D(orientation, trackedPose);
		trackedPose.Set_Translation(Vector3(
			viewState.position[0] * worldUnitsPerMeter,
			viewState.position[1] * worldUnitsPerMeter,
			viewState.position[2] * worldUnitsPerMeter));

		Matrix3D transformedPose;
		Matrix3D::Multiply(tabletopTransform, trackedPose, &transformedPose);
		Matrix3D eyeTransform;
		Matrix3D::Multiply(anchor, transformedPose, &eyeTransform);
		m_questXRCamera->Set_Transform(eyeTransform);
		m_questXRCamera->Set_View_Plane(
			Vector2(tanf(viewState.angleLeft), tanf(viewState.angleDown)),
			Vector2(tanf(viewState.angleRight), tanf(viewState.angleUp)));
		m_questXRCamera->Set_Clip_Planes(0.05f * worldUnitsPerMeter, 120000.0f);
		m_questXRCamera->Set_Viewport(Vector2(0.0f, 0.0f), Vector2(1.0f, 1.0f));
		// GeneralsX @feature Codex 01/10/2026 Copy the actual camera, never override it.
		QuestXRDiagnosticCamera diagnosticCamera;
		diagnosticCamera.worldUnitsPerMeter = worldUnitsPerMeter;
		diagnosticCamera.valid = true;
		for (uint32_t row = 0; row < 3; ++row)
			for (uint32_t column = 0; column < 4; ++column)
				diagnosticCamera.cameraToWorld[row * 4 + column] = eyeTransform[row][column];
		QuestXR_RecordDiagnosticCamera(eye, &diagnosticCamera);

		DX8Wrapper::Set_Render_Target(eyeSurfaces[eye], depthSurface);
		// GeneralsX @bugfix Codex 23/09/2026 Confirm that the legacy wrapper
		// actually bound the multisampled color/depth pair before drawing. Its
		// release-build DX8CALL macro otherwise discards the HRESULT.
		static bool loggedEyeTargetBinding = false;
		if (eye == 0 && !loggedEyeTargetBinding) {
			loggedEyeTargetBinding = true;
			IDirect3DSurface8 *boundColor = nullptr;
			IDirect3DSurface8 *boundDepth = nullptr;
			IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
			const HRESULT colorResult = device->GetRenderTarget(&boundColor);
			const HRESULT depthResult = device->GetDepthStencilSurface(&boundDepth);
			D3DSURFACE_DESC colorDesc{};
			D3DSURFACE_DESC depthDesc{};
			if (boundColor != nullptr) boundColor->GetDesc(&colorDesc);
			if (boundDepth != nullptr) boundDepth->GetDesc(&depthDesc);
			__android_log_print(ANDROID_LOG_INFO, "GeneralsXR",
				"OpenXR eye target binding: color hr=0x%08x match=%d samples=%u, depth hr=0x%08x match=%d samples=%u",
				static_cast<unsigned int>(colorResult), boundColor == eyeSurfaces[eye],
				static_cast<unsigned int>(colorDesc.MultiSampleType),
				static_cast<unsigned int>(depthResult), boundDepth == depthSurface,
				static_cast<unsigned int>(depthDesc.MultiSampleType));
			if (boundColor != nullptr) boundColor->Release();
			if (boundDepth != nullptr) boundDepth->Release();
		}
		if (WW3D::Begin_Render(true, true, Vector3(0.03f, 0.06f, 0.12f)) == WW3D_ERROR_OK) {
			m_questXRCamera->Apply();
			const auto worldStart = std::chrono::steady_clock::now();
			// GeneralsX @performance Codex 18/09/2026 On_Frame_Update advances
			// particle emitters and buffers. Advance them for the first successful
			// eye only; rendering, visibility, shadows, and particles remain stereo.
			m_3DScene->setFrameUpdateEnabled(!advancedSceneThisFrame);
			QuestXR::SetDrawCaptureEye(static_cast<int>(eye));
            {
                // GeneralsX @feature Codex 02/10/2026 Cutout edges only inside an eye's world draw.
                QuestXR::SoftCutoutWorldScope softWorldScope(static_cast<int>(eye));
                QuestXR::PerformanceEyeScope performanceEye(static_cast<int>(eye));
                QuestXR::TreeWindEyeScope windEye(static_cast<int>(eye));
                WW3D::Render(m_3DScene, m_questXRCamera);
            }
			QuestXR::SetDrawCaptureEye(-1);
			advancedSceneThisFrame = true;
			const auto worldDone = std::chrono::steady_clock::now();
			QuestVRInput_Render(m_questXRCamera);
			const auto pointerDone = std::chrono::steady_clock::now();
			WW3D::End_Render(false);
			renderedEye = true;
			worldCpuMs[eye] = std::chrono::duration<double, std::milli>(
				worldDone - worldStart).count();
			pointerCpuMs[eye] = std::chrono::duration<double, std::milli>(
				pointerDone - worldDone).count();
		} else {
			renderedBothEyes = false;
		}
		DX8Wrapper::Set_Render_Target(static_cast<IDirect3DSurface8 *>(nullptr));
		// GeneralsX @feature Codex 23/09/2026 Resolve the reusable 2x-MSAA
		// target only after it is unbound; the single-sample fallback is a no-op.
		if (renderedEye && !QuestXR_ResolveEyeSurface(
			DX8Wrapper::_Get_D3D_Device8(), eye)) {
			renderedBothEyes = false;
		}
		eyeCpuMs[eye] = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - eyeStart).count();
	}
	// Preserve the normal single-view behavior outside this stereo call and on
	// the next frame even if neither eye could begin rendering this time.
	m_3DScene->setFrameUpdateEnabled(TRUE);

	if (renderedBothEyes) {
		// GeneralsX @performance Codex 18/09/2026 Split the existing whole-frame
		// CPU metric into per-eye world and pointer submission costs. This is
		// diagnostic only and does not alter scene ordering or render state.
		static UnsignedInt diagnosticFrames = 0;
		static double accumulatedWorldMs[2] = {};
		static double accumulatedPointerMs[2] = {};
		static double accumulatedEyeMs[2] = {};
		static double accumulatedMenuMs = 0.0;
		accumulatedMenuMs += menuCpuMs;
		for (uint32_t eye = 0; eye < 2; ++eye) {
			accumulatedWorldMs[eye] += worldCpuMs[eye];
			accumulatedPointerMs[eye] += pointerCpuMs[eye];
			accumulatedEyeMs[eye] += eyeCpuMs[eye];
		}
		if (++diagnosticFrames >= 120) {
			const double divisor = static_cast<double>(diagnosticFrames);
			__android_log_print(ANDROID_LOG_INFO, "GeneralsXR",
				"XR eye CPU: menu %.2f ms, left %.2f ms (world %.2f, pointer %.3f), "
				"right %.2f ms (world %.2f, pointer %.3f)",
				accumulatedMenuMs / divisor,
				accumulatedEyeMs[0] / divisor, accumulatedWorldMs[0] / divisor,
				accumulatedPointerMs[0] / divisor,
				accumulatedEyeMs[1] / divisor, accumulatedWorldMs[1] / divisor,
				accumulatedPointerMs[1] / divisor);
			diagnosticFrames = 0;
			accumulatedMenuMs = 0.0;
			for (uint32_t eye = 0; eye < 2; ++eye) {
				accumulatedWorldMs[eye] = 0.0;
				accumulatedPointerMs[eye] = 0.0;
				accumulatedEyeMs[eye] = 0.0;
			}
		}
		QuestXR_MarkEyeRenderingComplete();
	}
}
#endif
