/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2025 Electronic Arts Inc.
** Copyright 2026 GeneralsVRQuest3 contributors
**
** GPL-3.0-or-later. See LICENSE and LICENSE.md.
*/

#pragma once

#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)

#include <openxr/openxr.h>
#include <vulkan/vulkan.h>
#include "VRDevice/QuestXRInterface.h"
#include "QuestTemporalAA.h"
#include "VRDevice/QuestHudTransfer.h"
#include "QuestEyeCapture.h"
#include "QuestWorldComposite.h"
#include "VRDevice/QuestWorldCoverage.h"
#include "VRDevice/QuestVisibilityMask.h"

#include <cstdint>
#include <array>
#include <string>
#include <vector>

struct IDirect3DDevice8;
struct IDirect3DTexture8;
struct IDirect3DSurface8;

namespace QuestXR
{

struct VulkanRequirements
{
	XrVersion minApiVersion = 0;
	XrVersion maxApiVersion = 0;
	std::string instanceExtensions;
	std::string deviceExtensions;
};

// Owns the Android loader/instance, DXVK graphics binding, and the initial
// compositor smoke path. OpenXR is initialized before DXVK so the runtime's
// Vulkan requirements participate in instance/device creation.
class Bootstrap final
{
public:
	Bootstrap() = default;
	~Bootstrap();

	Bootstrap(const Bootstrap &) = delete;
	Bootstrap &operator=(const Bootstrap &) = delete;

	bool initialize();
	bool attachGraphics(::IDirect3DDevice8 *device, uint32_t menuWidth, uint32_t menuHeight);
	bool beginFrame();
	bool pollLifecycle() { return pollEvents(); }
	bool endFrame();
	bool isFrameActive() const { return m_frameActive; }
	bool shouldRenderFrame() const { return m_frameActive && m_shouldRender; }
    bool gameplayPassthroughEnabled() const {return m_gameplayPassthroughRequested&&m_worldLayerSupported&&m_passthroughEnabled&&!m_passthroughFailed&&m_worldCoverage.available();}
    bool beginWorldCoverage(::IDirect3DDevice8 *device,unsigned eye) {
        return gameplayPassthroughEnabled()&&m_frameActive&&m_shouldRender&&eye<2
            &&m_worldCoverage.begin(device,m_eyeSurfaces[eye],m_depthSurface,eye);
    }
	::IDirect3DSurface8 *getMenuDepthSurface(::IDirect3DDevice8 *device);
	void detachGraphics();
	void shutdown();
	bool getEyeView(uint32_t eye, ::QuestXREyeView &view) const;
	uint64_t comfortSpaceGeneration() const { return m_comfortSpaceGeneration; }
	void recordDiagnosticCamera(uint32_t eye, const QuestXRDiagnosticCamera &camera) {
		if (eye < m_diagnosticCameras.size()) m_diagnosticCameras[eye] = camera;
	}
	::IDirect3DSurface8 *getEyeSurface(uint32_t eye) const;
	bool resolveEyeSurface(::IDirect3DDevice8 *device, uint32_t eye) const;
    bool discardCompletedEyeDepth(::IDirect3DDevice8 *device, ::IDirect3DSurface8 *depth, unsigned eye);
    bool captureTaaCamera(::IDirect3DDevice8 *device, unsigned eye, TaaCamera &camera) const;
    bool applyTaa(::IDirect3DDevice8 *device, unsigned eye, const TaaCamera &camera);
    bool createTaaDepthSurface(::IDirect3DDevice8 *device);
    bool applyVisibilityMask(::IDirect3DDevice8 *device,unsigned eye);
    bool wantsVisibilityMask() const { return m_visibilityEnabled&&!m_visibilityFailed; }
	::IDirect3DSurface8 *getDepthSurface() const { return m_depthSurface; }
	void markEyeRenderingComplete() { m_eyeRenderingComplete = true; }
	::IDirect3DSurface8 *getMenuSurface() const { return m_menuSurface; }
	uint32_t getHudClearTop(bool hud,uint32_t width,uint32_t height) const;
	void setMenuPresentation(uint32_t presentation);
	void setMenuCropTop(float cropTop);
	void markMenuRenderingComplete(uint32_t presentation);
	bool getControllerState(uint32_t hand, ::QuestXRControllerState &state) const;

	bool isReady() const { return m_instance != XR_NULL_HANDLE && m_systemId != XR_NULL_SYSTEM_ID; }
	XrInstance instance() const { return m_instance; }
	XrSystemId systemId() const { return m_systemId; }
	XrSession session() const { return m_session; }
	XrSpace localSpace() const { return m_localSpace; }
	uint32_t viewCount() const { return m_viewCount; }
	uint32_t recommendedEyeWidth() const { return m_recommendedEyeWidth; }
	uint32_t recommendedEyeHeight() const { return m_recommendedEyeHeight; }
	const VulkanRequirements &vulkanRequirements() const { return m_vulkanRequirements; }

private:
	bool initializeLoader();
	bool createInstance();
	bool updateFrontendPassthrough(bool wanted);
	void destroyFrontendPassthrough();
	bool selectSystemAndViews();
	bool queryVulkanRequirements();
	bool createSwapchain();
	bool createEyeTargets(::IDirect3DDevice8 *device, uint32_t menuWidth, uint32_t menuHeight);
	bool createMenuTarget(::IDirect3DDevice8 *device, uint32_t width, uint32_t height);
	bool createActions();
	bool syncActions();
	bool recenterGameSpace();
	void pulseHaptic(uint32_t hand, float amplitude);
	bool createCommandResources();
	void collectCopyTimestamp(uint32_t imageIndex);
	void recordFrameDiagnostics(uint32_t layerCount);
	bool pollEvents();
	bool renderCompositorImages(uint32_t imageIndex, bool copyEyeTargets,
		uint32_t menuImageIndex, bool copyMenu, const HudTransferPlan &hudPlan);
	bool queryEyeImage(uint32_t eye, VkImage &image, VkImageLayout &layout,
		VkFormat *format = nullptr, VkImageUsageFlags *usage = nullptr) const;
	bool queryTextureImage(::IDirect3DTexture8 *texture, uint32_t width, uint32_t height,
		VkImage &image, VkImageLayout &layout, VkFormat *format = nullptr,
		VkImageUsageFlags *usage = nullptr) const;
	void logResult(const char *operation, XrResult result) const;

	XrInstance m_instance = XR_NULL_HANDLE;
	XrSystemId m_systemId = XR_NULL_SYSTEM_ID;
	XrSession m_session = XR_NULL_HANDLE;
	// GeneralsX @feature Codex 16/10/2026 Re-anchor comfort only when tracking coordinates actually change.
	uint64_t m_comfortSpaceGeneration = 1;
	std::vector<XrTime> m_comfortSpaceChanges;
	XrSpace m_localSpace = XR_NULL_HANDLE;
	XrSpace m_gameSpace = XR_NULL_HANDLE;
	XrSpace m_viewSpace = XR_NULL_HANDLE;
	XrSwapchain m_colorSwapchain = XR_NULL_HANDLE;
	XrSwapchain m_menuSwapchain = XR_NULL_HANDLE;
    XrSwapchain m_compactMenuSwapchain = XR_NULL_HANDLE;
    std::vector<VkImage> m_compactMenuImages;
	XrActionSet m_actionSet = XR_NULL_HANDLE;
	XrAction m_aimPoseAction = XR_NULL_HANDLE;
	XrAction m_triggerAction = XR_NULL_HANDLE;
	XrAction m_gripAction = XR_NULL_HANDLE;
	XrAction m_stickAction = XR_NULL_HANDLE;
	XrAction m_primaryAction = XR_NULL_HANDLE;
	XrAction m_secondaryAction = XR_NULL_HANDLE;
	XrAction m_menuAction = XR_NULL_HANDLE;
	XrAction m_stickClickAction = XR_NULL_HANDLE;
	XrAction m_hapticAction = XR_NULL_HANDLE;
	std::array<XrPath, 2> m_handPaths = {};
	std::array<XrSpace, 2> m_aimSpaces = {};
	std::array<::QuestXRControllerState, 2> m_controllers = {};
	std::array<uint32_t, 2> m_previousButtons = {};
	XrSessionState m_sessionState = XR_SESSION_STATE_UNKNOWN;
	XrEnvironmentBlendMode m_blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	bool m_sessionRunning = false;
	bool m_frameLoopFailed = false;
	bool m_frameActive = false;
	bool m_shouldRender = false;
	bool m_viewsValid = false;
	bool m_eyeRenderingComplete = false;
	bool m_menuRenderingComplete = false;
	bool m_recenterRequested = false;
	// GeneralsX @feature Codex 10/10/2026 Optional room-fixed front-end screen.
	// GeneralsX @feature Codex 12/10/2026 Optional front-end underlay, never world transparency.
	bool m_passthroughRequested = false, m_passthroughEnabled = false;
    bool m_frontendPassthroughRequested = false,m_gameplayPassthroughRequested = false;
    bool m_worldLayerSupported=false;
    WorldCoverage m_worldCoverage;
    WorldComposite m_worldComposite;
    bool m_worldCompositeAttempted=false,m_worldCompositeApplied=false;
	bool m_passthroughFailed = false, m_passthroughRunning = false;
	XrPassthroughFB m_passthrough = XR_NULL_HANDLE;
	XrPassthroughLayerFB m_passthroughLayer = XR_NULL_HANDLE;
	PFN_xrCreatePassthroughFB m_createPassthrough = nullptr;
	PFN_xrDestroyPassthroughFB m_destroyPassthrough = nullptr;
	PFN_xrPassthroughStartFB m_startPassthrough = nullptr;
	PFN_xrPassthroughPauseFB m_pausePassthrough = nullptr;
	PFN_xrCreatePassthroughLayerFB m_createPassthroughLayer = nullptr;
	PFN_xrDestroyPassthroughLayerFB m_destroyPassthroughLayer = nullptr;
	PFN_xrPassthroughLayerResumeFB m_resumePassthroughLayer = nullptr;
	PFN_xrPassthroughLayerPauseFB m_pausePassthroughLayer = nullptr;
    bool m_visibilityEnabled=false,m_visibilityFailed=false;
    PFN_xrGetVisibilityMaskKHR m_getVisibilityMask=nullptr;
    uint64_t m_visibilityGeneration=1;
    std::array<MaskCache,2> m_visibilityMasks{};
    std::array<uint64_t,2> m_visibilityApplied{};
    bool m_fixedFrontend = false;
	bool m_frontendAnchorValid = false;
	XrPosef m_frontendAnchor{};
	int64_t m_menuButtonDownNs = 0;
	bool m_menuButtonLongFired = false;
	uint32_t m_menuPresentation = QUEST_XR_MENU_HIDDEN;
	float m_menuCropTop = 0.65f;
	XrTime m_predictedDisplayTime = 0;
	std::array<XrView, 2> m_views = {{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
	int64_t m_colorFormat = 0;
	VkFormat m_sourceColorFormat = VK_FORMAT_UNDEFINED;
    uint32_t m_eyeColorFormat = 21;
	bool m_colorRequiresBlit = false;
	std::vector<VkImage> m_swapchainImages;
	std::array<::IDirect3DTexture8 *, 2> m_eyeTextures = {};
	std::array<::IDirect3DSurface8 *, 2> m_eyeSurfaces = {};
	::IDirect3DSurface8 *m_msaaEyeSurface = nullptr;
	::IDirect3DSurface8 *m_depthSurface = nullptr;
    ::IDirect3DTexture8 *m_taaDepthTexture = nullptr;
	std::vector<VkImage> m_menuSwapchainImages;
	::IDirect3DTexture8 *m_menuTexture = nullptr;
	::IDirect3DSurface8 *m_menuSurface = nullptr;
	::IDirect3DSurface8 *m_menuDepthSurface = nullptr;
	uint32_t m_menuWidth = 0;
	uint32_t m_menuHeight = 0;
	VkInstance m_vkInstance = VK_NULL_HANDLE;
	VkPhysicalDevice m_vkPhysicalDevice = VK_NULL_HANDLE;
	VkDevice m_vkDevice = VK_NULL_HANDLE;
	VkQueue m_vkQueue = VK_NULL_HANDLE;
	uint32_t m_vkQueueFamilyIndex = 0;
	uint32_t m_vkQueueIndex = 0;
	VkCommandPool m_commandPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> m_commandBuffers;
	std::vector<VkFence> m_commandFences;
	VkQueryPool m_timestampQueryPool = VK_NULL_HANDLE;
	std::vector<uint8_t> m_timestampQueryValid;
	float m_timestampPeriodNs = 0.0f;
	uint32_t m_timestampValidBits = 0;
	void *m_dxvkInterop = nullptr;
    void *m_depthDiscardInterop = nullptr;
    TemporalAA m_temporalAA;
    bool m_taaAttempted = false, m_taaUnavailable = false;
    uint64_t m_taaFrame = 0;
	EyeCapture m_eyeCapture;
	std::array<QuestXRDiagnosticCamera, 2> m_diagnosticCameras{};
	uint32_t m_viewCount = 0;
	uint32_t m_recommendedEyeWidth = 0;
	uint32_t m_recommendedEyeHeight = 0;
	bool m_enableVulkan2 = false;
	uint64_t m_diagnosticFrames = 0;
	uint64_t m_suppressedRenderFrames = 0;
	int64_t m_frameCpuStartNs = 0;
	int64_t m_lastDiagnosticNs = 0;
	XrTime m_previousPredictedDisplayTime = 0;
	double m_accumulatedFrameCpuMs = 0.0;
	double m_accumulatedWaitFrameMs = 0.0;
	double m_accumulatedFenceWaitMs = 0.0;
	double m_lastPredictedIntervalMs = 0.0;
	double m_lastCopyGpuMs = -1.0;
	VulkanRequirements m_vulkanRequirements;
};

Bootstrap &GetBootstrap();

} // namespace QuestXR

#endif
