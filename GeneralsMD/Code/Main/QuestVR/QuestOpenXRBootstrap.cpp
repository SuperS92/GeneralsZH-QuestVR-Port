/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2025 Electronic Arts Inc.
** Copyright 2026 GeneralsVRQuest3 contributors
**
** GPL-3.0-or-later. See LICENSE and LICENSE.md.
*/

#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)

#include <d3d8.h>
#include "QuestOpenXRBootstrap.h"
#include "QuestTaaDepthFormat.h"
#include "VRDevice/QuestHudTransfer.h"
#include "VRDevice/QuestColorCopy.h"
#include "VRDevice/QuestTransferBatch.h"
#include "VRDevice/QuestPerformance.h"
#include "VRDevice/QuestTaaBridge.h"
#include "VRDevice/QuestXRInterface.h"
#include "VRDevice/QuestVRMath.h"
#include "VRDevice/QuestDrawCapture.h"
#include "VRDevice/QuestTerrainDetailTest.h"
#include "VRDevice/QuestTerrainMipFilter.h"
#include "VRDevice/QuestGraphicsChoices.h"
#include "VRDevice/QuestCutoutMips.h"
#include "VRDevice/QuestSoftCutouts.h"
#include "VRDevice/QuestTreeWind.h"
#include "VRDevice/QuestTreeContours.h"
#include "VRDevice/QuestFenceContours.h"
#include "VRDevice/QuestTerrainFarMips.h"

#include <SDL3/SDL_system.h>
#include <android/log.h>
#include <jni.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
constexpr const char *kLogTag = "GeneralsXR";
// GeneralsX @bugfix Codex 23/09/2026 Keep the proven single-sample stereo
// path active. Quest tests of the experimental DXVK 2x resolve showed black
// or stale/disparate eye images; retain the implementation for offline repair.
constexpr bool kEnableExperimentalEyeMsaa = false;
// GeneralsX @performance Codex 16/10/2026 One opt-in bundle, restricted to Lite and its existing AA compatibility gate.
bool fastTaaTransfersRequested() {
    const char *value=std::getenv("GX_QUEST_TAA_FAST_TRANSFERS");
    return value&&std::strcmp(value,"1")==0&&QuestXR_TaaMode()==3&&QuestXR_TaaRequested();
}
constexpr float kMenuDistanceMeters = 1.5f;
constexpr float kMenuWidthMeters = 1.6f;
constexpr float kHudDistanceMeters = 1.25f;
constexpr float kHudWidthMeters = 1.35f;
constexpr float kHudCenterYMeters = -0.42f;
constexpr float kHudCropTop = 0.65f;
constexpr float kWristHudWidthMeters = 0.4125f;
constexpr int64_t kMenuLongPressNs = 450000000;

struct MenuGeometry
{
	float width;
	float cropTop;
	XrPosef pose;
	bool viewLocked;
};

struct ColorFormatChoice
{
	VkFormat format;
	bool requiresBlit;
};

constexpr bool containsFormat(const int64_t *formats, size_t count, VkFormat format)
{
	for (size_t index = 0; index < count; ++index) {
		if (formats[index] == static_cast<int64_t>(format)) {
			return true;
		}
	}
	return false;
}

// GeneralsX @bugfix Codex 15/09/2026 Meta Quest exposes RGBA8 OpenXR
// swapchains, while D3DFMT_A8R8G8B8 is a BGRA8 Vulkan image in DXVK. Prefer
// an exact copy-compatible format, then a same-colour-space RGBA8 target that
// can be populated with Vulkan's defined format-converting blit operation.
constexpr ColorFormatChoice chooseColorFormat(
	VkFormat sourceFormat, const int64_t *formats, size_t count)
{
	const bool sourceIsSrgb = sourceFormat == VK_FORMAT_B8G8R8A8_SRGB;
	const std::array<ColorFormatChoice, 4> candidates = {{
		{sourceFormat, false},
		{sourceIsSrgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM, true},
		{sourceIsSrgb ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_B8G8R8A8_SRGB, false},
		{sourceIsSrgb ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB, true},
	}};
	for (const ColorFormatChoice &candidate : candidates) {
		if (containsFormat(formats, count, candidate.format)) {
			return candidate;
		}
	}
	return {VK_FORMAT_UNDEFINED, false};
}

constexpr int64_t kQuestRgbaFormats[] = {
	VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM};
constexpr ColorFormatChoice kQuestRgbaChoice = chooseColorFormat(
	VK_FORMAT_B8G8R8A8_UNORM, kQuestRgbaFormats,
	sizeof(kQuestRgbaFormats) / sizeof(kQuestRgbaFormats[0]));
static_assert(kQuestRgbaChoice.format == VK_FORMAT_R8G8B8A8_UNORM,
	"Quest RGBA8 fallback must preserve the DXVK source colour space");
static_assert(kQuestRgbaChoice.requiresBlit,
	"BGRA8 to RGBA8 requires a channel-converting Vulkan blit");

XrQuaternionf multiplyQuaternion(const XrQuaternionf &a, const XrQuaternionf &b)
{
	return {
		a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
		a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
		a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
		a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

XrQuaternionf conjugateQuaternion(const XrQuaternionf &q)
{
	return {-q.x, -q.y, -q.z, q.w};
}

XrVector3f rotateVector(const XrQuaternionf &q, const XrVector3f &v)
{
	const XrQuaternionf vector = {v.x, v.y, v.z, 0.0f};
	const XrQuaternionf rotated = multiplyQuaternion(
		multiplyQuaternion(q, vector), conjugateQuaternion(q));
	return {rotated.x, rotated.y, rotated.z};
}

MenuGeometry getMenuGeometry(uint32_t presentation, float menuCropTop,
	uint32_t menuWidth, uint32_t menuHeight,
	const std::array<QuestXRControllerState, 2> &controllers,
	const XrPosef *frontendAnchor = nullptr)
{
	MenuGeometry geometry{};
	geometry.pose.orientation.w = 1.0f;
	geometry.viewLocked = true;
	if (presentation == QUEST_XR_MENU_HUD) {
		const QuestXRControllerState &left = controllers[0];
		geometry.width = kHudWidthMeters;
		// Use the same integer subimage edge for display and ray intersection.
		geometry.cropTop = menuHeight > 0
			? std::floor(menuCropTop * menuHeight) / menuHeight : menuCropTop;
		geometry.pose.position.y = kHudCenterYMeters;
		geometry.pose.position.z = -kHudDistanceMeters;
		if (left.connected != 0 && left.poseValid != 0) {
			// GeneralsX @feature Codex 15/09/2026 Match GeneralsVR's
			// readable left-wrist HUD placement while retaining the existing
			// bottom control-bar crop until transparent UI composition lands.
			const XrQuaternionf hand = {left.orientation[0], left.orientation[1],
				left.orientation[2], left.orientation[3]};
			const float halfTilt = -0.45f;
			const XrQuaternionf tilt = {std::sin(halfTilt), 0.0f, 0.0f,
				std::cos(halfTilt)};
			geometry.pose.orientation = multiplyQuaternion(hand, tilt);
			const XrVector3f offset = rotateVector(hand, {0.0f, 0.05f, -0.13f});
			geometry.pose.position = {left.position[0] + offset.x,
				left.position[1] + offset.y, left.position[2] + offset.z};
			geometry.width = kWristHudWidthMeters;
			geometry.viewLocked = false;
		}
		// GeneralsX @bugfix Codex 30/09/2026 A centered growing quad moved
		// hovered buttons away from the ray. Grow upward in the panel's own
		// tilted plane, keeping its bottom and every existing source pixel fixed.
		if (menuWidth > 0) {
			const float fullHeight = geometry.width * static_cast<float>(menuHeight)
				/ static_cast<float>(menuWidth);
			const float compactCrop = menuHeight > 0
				? std::floor(kHudCropTop * menuHeight) / menuHeight : kHudCropTop;
			const XrVector3f growth = rotateVector(geometry.pose.orientation,
				{0.0f, QuestXRHudGrowthCenterOffset(geometry.cropTop, compactCrop, fullHeight), 0.0f});
			geometry.pose.position.x += growth.x;
			geometry.pose.position.y += growth.y;
			geometry.pose.position.z += growth.z;
		}
		return geometry;
	}
	geometry.width = kMenuWidthMeters;
	geometry.cropTop = 0.0f;
	geometry.pose.position.z = -kMenuDistanceMeters;
	if (presentation == QUEST_XR_MENU_FRONTEND && frontendAnchor != nullptr) {
		geometry.pose = *frontendAnchor;
		geometry.viewLocked = false;
	}
	return geometry;
}

XrQuaternionf yawOnly(const XrQuaternionf &orientation)
{
	// Recenter horizontal heading while keeping the application space aligned
	// with gravity. Copying headset pitch/roll into LOCAL would tilt subsequent
	// room-scale translations and the whole battlefield.
	const float sinYaw = 2.0f * (orientation.w * orientation.y
		+ orientation.x * orientation.z);
	const float cosYaw = 1.0f - 2.0f * (orientation.y * orientation.y
		+ orientation.z * orientation.z);
	const float halfYaw = 0.5f * std::atan2(sinYaw, cosYaw);
	return {0.0f, std::sin(halfYaw), 0.0f, std::cos(halfYaw)};
}

constexpr GUID kDxvkD3D8BridgeId = {
	0xd3d9d3d8, 0x42a9, 0x4c1e, {0xaa, 0x97, 0xbe, 0xef, 0xca, 0xfe, 0x20, 0x00}};
constexpr GUID kD3D9VkInteropDeviceId = {
	0x2eaa4b89, 0x0107, 0x4bdb, {0x87, 0xf7, 0x0f, 0x54, 0x1c, 0x49, 0x3c, 0xe0}};

// First two methods of DXVK's public ID3D9VkInteropDevice contract. Keeping
// this ABI-only declaration local avoids importing DXVK's private C++ types
// into the game while preserving the exact COM vtable used by DXVK 2.6.
struct DxvkVulkanInteropDevice : IUnknown
{
	virtual void STDMETHODCALLTYPE GetVulkanHandles(
		VkInstance *instance, VkPhysicalDevice *physicalDevice, VkDevice *device) = 0;
	virtual void STDMETHODCALLTYPE GetSubmissionQueue(
		VkQueue *queue, uint32_t *queueIndex, uint32_t *queueFamilyIndex) = 0;
	virtual void STDMETHODCALLTYPE TransitionTextureLayout(
		void *texture, const void *subresources, VkImageLayout oldLayout, VkImageLayout newLayout) = 0;
	virtual void STDMETHODCALLTYPE FlushRenderingCommands() = 0;
	virtual void STDMETHODCALLTYPE LockSubmissionQueue() = 0;
	virtual void STDMETHODCALLTYPE ReleaseSubmissionQueue() = 0;
};

// Quest-owned IID; append to the complete inherited vtable without changing its ABI.
struct DxvkQuestDepthInterop : DxvkVulkanInteropDevice {
    virtual void STDMETHODCALLTYPE LockDevice() = 0;
    virtual void STDMETHODCALLTYPE UnlockDevice() = 0;
    virtual bool STDMETHODCALLTYPE WaitForResource(void *resource, DWORD flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateImage(const void *description, void **resource) = 0;
    virtual HRESULT STDMETHODCALLTYPE DiscardCompletedEyeDepth(void *texture) = 0;
};
constexpr GUID kQuestDepthInteropId = {
    0x7a643eb1, 0x922e, 0x4876, {0x9a, 0x90, 0x69, 0xa3, 0xc8, 0x84, 0x29, 0x76}};

class DxvkQueueGuard final
{
public:
	explicit DxvkQueueGuard(void *interop)
		: m_interop(static_cast<DxvkVulkanInteropDevice *>(interop))
	{
		if (m_interop != nullptr) {
			m_interop->LockSubmissionQueue();
		}
	}
	~DxvkQueueGuard()
	{
		if (m_interop != nullptr) {
			m_interop->ReleaseSubmissionQueue();
		}
	}

	DxvkQueueGuard(const DxvkQueueGuard &) = delete;
	DxvkQueueGuard &operator=(const DxvkQueueGuard &) = delete;

private:
	DxvkVulkanInteropDevice *m_interop;
};

int64_t monotonicNanoseconds()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct DxvkVulkanInteropTexture : IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetVulkanImageInfo(
		VkImage *image, VkImageLayout *layout, VkImageCreateInfo *info) = 0;
};

constexpr GUID kD3D9VkInteropTextureId = {
	0xd56344f5, 0x8d35, 0x46fd, {0x80, 0x6d, 0x94, 0xc3, 0x51, 0xb4, 0x72, 0xc1}};

bool HasExtension(const std::vector<XrExtensionProperties> &extensions, const char *name)
{
	return std::any_of(extensions.begin(), extensions.end(), [name](const XrExtensionProperties &extension) {
		return std::strcmp(extension.extensionName, name) == 0;
	});
}

bool QueryExtensionString(XrInstance instance, XrSystemId systemId, const char *functionName, std::string &result)
{
	PFN_xrGetVulkanInstanceExtensionsKHR function = nullptr;
	XrResult xrResult = xrGetInstanceProcAddr(instance, functionName,
		reinterpret_cast<PFN_xrVoidFunction *>(&function));
	if (XR_FAILED(xrResult) || function == nullptr) {
		return false;
	}

	uint32_t size = 0;
	xrResult = function(instance, systemId, 0, &size, nullptr);
	if (XR_FAILED(xrResult) || size == 0) {
		return false;
	}

	std::vector<char> buffer(size);
	xrResult = function(instance, systemId, size, &size, buffer.data());
	if (XR_FAILED(xrResult)) {
		return false;
	}
	result.assign(buffer.data());
	return true;
}
} // namespace

namespace QuestXR
{

Bootstrap::~Bootstrap()
{
	shutdown();
}

Bootstrap &GetBootstrap()
{
	static Bootstrap bootstrap;
	return bootstrap;
}

bool Bootstrap::initialize()
{
	if (isReady()) {
		return true;
	}
	shutdown();
	const char *fixedScreen = std::getenv("GX_QUEST_FIXED_SCREEN");
	m_fixedFrontend = fixedScreen && std::strcmp(fixedScreen, "1") == 0;
	const char *passthrough = std::getenv("GX_QUEST_FRONTEND_PASSTHROUGH");
	m_passthroughRequested = passthrough && std::strcmp(passthrough, "1") == 0;
	__android_log_print(ANDROID_LOG_INFO, kLogTag, "Quest local build: %s; front-end passthrough requested=%d",
		std::getenv("GX_QUEST_TEST_LABEL") ? std::getenv("GX_QUEST_TEST_LABEL") : "release", m_passthroughRequested);

	if (!initializeLoader() || !createInstance() || !selectSystemAndViews() || !queryVulkanRequirements()) {
		shutdown();
		return false;
	}

	__android_log_print(ANDROID_LOG_INFO, kLogTag,
		"OpenXR bootstrap ready: %u stereo views, recommended eye %ux%u, Vulkan %u.%u.%u..%u.%u.%u",
		m_viewCount, m_recommendedEyeWidth, m_recommendedEyeHeight,
		XR_VERSION_MAJOR(m_vulkanRequirements.minApiVersion),
		XR_VERSION_MINOR(m_vulkanRequirements.minApiVersion),
		XR_VERSION_PATCH(m_vulkanRequirements.minApiVersion),
		XR_VERSION_MAJOR(m_vulkanRequirements.maxApiVersion),
		XR_VERSION_MINOR(m_vulkanRequirements.maxApiVersion),
		XR_VERSION_PATCH(m_vulkanRequirements.maxApiVersion));
	__android_log_print(ANDROID_LOG_INFO, kLogTag, "Required Vulkan instance extensions: %s",
		m_vulkanRequirements.instanceExtensions.c_str());
	__android_log_print(ANDROID_LOG_INFO, kLogTag, "Required Vulkan device extensions: %s",
		m_vulkanRequirements.deviceExtensions.c_str());
	return true;
}

bool Bootstrap::attachGraphics(IDirect3DDevice8 *device, uint32_t menuWidth, uint32_t menuHeight)
{
	if (!isReady() || device == nullptr || menuWidth == 0 || menuHeight == 0) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag,
			"Cannot attach DXVK graphics before OpenXR bootstrap, D3D8 device creation, and a valid menu canvas");
		return false;
	}
	if (m_session != XR_NULL_HANDLE) {
		return true;
	}

	IUnknown *bridge = nullptr;
	HRESULT hr = device->QueryInterface(kDxvkD3D8BridgeId, reinterpret_cast<void **>(&bridge));
	if (FAILED(hr) || bridge == nullptr) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag,
			"DXVK D3D8 bridge unavailable (HRESULT 0x%08x)", static_cast<unsigned int>(hr));
		return false;
	}

	DxvkVulkanInteropDevice *interop = nullptr;
	hr = bridge->QueryInterface(kD3D9VkInteropDeviceId, reinterpret_cast<void **>(&interop));
	bridge->Release();
	if (FAILED(hr) || interop == nullptr) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag,
			"DXVK Vulkan interop unavailable (HRESULT 0x%08x)", static_cast<unsigned int>(hr));
		return false;
	}

	interop->GetVulkanHandles(&m_vkInstance, &m_vkPhysicalDevice, &m_vkDevice);
	interop->GetSubmissionQueue(&m_vkQueue, &m_vkQueueIndex, &m_vkQueueFamilyIndex);
	m_dxvkInterop = interop;
    if (PerformanceRequested(PerfOption::DepthDiscard))
        interop->QueryInterface(kQuestDepthInteropId, &m_depthDiscardInterop);
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
        "Quest eye depth discard bridge: requested=%d available=%d (full restart)",
        PerformanceRequested(PerfOption::DepthDiscard), m_depthDiscardInterop != nullptr);
	if (m_vkInstance == VK_NULL_HANDLE || m_vkPhysicalDevice == VK_NULL_HANDLE
		|| m_vkDevice == VK_NULL_HANDLE || m_vkQueue == VK_NULL_HANDLE) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag, "DXVK returned incomplete Vulkan handles");
		detachGraphics();
		return false;
	}

	PFN_xrGetVulkanGraphicsDeviceKHR getGraphicsDevice = nullptr;
	XrResult result = xrGetInstanceProcAddr(m_instance, "xrGetVulkanGraphicsDeviceKHR",
		reinterpret_cast<PFN_xrVoidFunction *>(&getGraphicsDevice));
	if (XR_FAILED(result) || getGraphicsDevice == nullptr) {
		logResult("xrGetInstanceProcAddr(xrGetVulkanGraphicsDeviceKHR)", result);
		detachGraphics();
		return false;
	}

	VkPhysicalDevice runtimePhysicalDevice = VK_NULL_HANDLE;
	result = getGraphicsDevice(m_instance, m_systemId, m_vkInstance, &runtimePhysicalDevice);
	if (XR_FAILED(result)) {
		logResult("xrGetVulkanGraphicsDeviceKHR", result);
		detachGraphics();
		return false;
	}
	if (runtimePhysicalDevice != m_vkPhysicalDevice) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag,
			"DXVK selected a Vulkan physical device different from the OpenXR runtime");
		detachGraphics();
		return false;
	}

	XrGraphicsBindingVulkanKHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
	graphicsBinding.instance = m_vkInstance;
	graphicsBinding.physicalDevice = m_vkPhysicalDevice;
	graphicsBinding.device = m_vkDevice;
	graphicsBinding.queueFamilyIndex = m_vkQueueFamilyIndex;
	graphicsBinding.queueIndex = m_vkQueueIndex;

	XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
	sessionInfo.next = &graphicsBinding;
	sessionInfo.systemId = m_systemId;
	result = xrCreateSession(m_instance, &sessionInfo, &m_session);
	if (XR_FAILED(result)) {
		logResult("xrCreateSession", result);
		detachGraphics();
		return false;
	}

	XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
	result = xrCreateReferenceSpace(m_session, &spaceInfo, &m_localSpace);
	if (XR_FAILED(result)) {
		logResult("xrCreateReferenceSpace(LOCAL)", result);
		detachGraphics();
		return false;
	}
	result = xrCreateReferenceSpace(m_session, &spaceInfo, &m_gameSpace);
	if (XR_FAILED(result)) {
		logResult("xrCreateReferenceSpace(game LOCAL)", result);
		detachGraphics();
		return false;
	}
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	result = xrCreateReferenceSpace(m_session, &spaceInfo, &m_viewSpace);
	if (XR_FAILED(result)) {
		logResult("xrCreateReferenceSpace(VIEW)", result);
		detachGraphics();
		return false;
	}

	uint32_t blendModeCount = 0;
	result = xrEnumerateEnvironmentBlendModes(m_instance, m_systemId,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &blendModeCount, nullptr);
	if (XR_FAILED(result) || blendModeCount == 0) {
		logResult("xrEnumerateEnvironmentBlendModes(count)", result);
		detachGraphics();
		return false;
	}
	std::vector<XrEnvironmentBlendMode> blendModes(blendModeCount);
	result = xrEnumerateEnvironmentBlendModes(m_instance, m_systemId,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, blendModeCount, &blendModeCount, blendModes.data());
	if (XR_FAILED(result)) {
		logResult("xrEnumerateEnvironmentBlendModes(list)", result);
		detachGraphics();
		return false;
	}
	m_blendMode = blendModes.front();
	if (std::find(blendModes.begin(), blendModes.end(), XR_ENVIRONMENT_BLEND_MODE_OPAQUE) != blendModes.end()) {
		m_blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	}

	if (!createActions() || !createEyeTargets(device, menuWidth, menuHeight) || !createSwapchain()
		|| !createMenuTarget(device, menuWidth, menuHeight)
		|| !createCommandResources()) {
		detachGraphics();
		return false;
	}

	__android_log_print(ANDROID_LOG_INFO, kLogTag,
		"OpenXR Vulkan session ready: format %lld, eyes %ux%u x %u layers, menu %ux%u, DXVK queue family %u queue %u",
		static_cast<long long>(m_colorFormat), m_recommendedEyeWidth, m_recommendedEyeHeight,
		m_viewCount, m_menuWidth, m_menuHeight, m_vkQueueFamilyIndex, m_vkQueueIndex);
	return true;
}

bool Bootstrap::createEyeTargets(IDirect3DDevice8 *device, uint32_t menuWidth, uint32_t menuHeight)
{
	VkPhysicalDeviceProperties properties{};
	vkGetPhysicalDeviceProperties(m_vkPhysicalDevice, &properties);
	if (m_recommendedEyeWidth > properties.limits.maxImageDimension2D
		|| m_recommendedEyeHeight > properties.limits.maxImageDimension2D) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag, "Eye resolution exceeds Vulkan image limit");
		return false;
	}
    // GeneralsX @performance Codex 14/10/2026 Probe only owned RGBA8 presentation targets.
    const bool requested = PerformanceRequested(PerfOption::DirectColorCopy);
    const char *aa = std::getenv("GX_QUEST_SPATIAL_AA");
    const bool aaOff = !aa || std::strcmp(aa,"0")==0;
    bool runtimeRgba = false, nativeRgba = false;
    if (requested && aaOff && !kEnableExperimentalEyeMsaa) {
        uint32_t count = 0;
        if (XR_SUCCEEDED(xrEnumerateSwapchainFormats(m_session,0,&count,nullptr)) && count) {
            std::vector<int64_t> formats(count);
            if (XR_SUCCEEDED(xrEnumerateSwapchainFormats(m_session,count,&count,formats.data())))
                runtimeRgba = containsFormat(formats.data(),count,VK_FORMAT_R8G8B8A8_UNORM);
        }
        VkImageFormatProperties format{};
        VkFormatProperties support{};
        vkGetPhysicalDeviceFormatProperties(m_vkPhysicalDevice,VK_FORMAT_R8G8B8A8_UNORM,&support);
        nativeRgba = vkGetPhysicalDeviceImageFormatProperties(m_vkPhysicalDevice,VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            0,&format)==VK_SUCCESS && (format.sampleCounts & VK_SAMPLE_COUNT_1_BIT)
                && (support.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)
                && format.maxExtent.width>=std::max(m_recommendedEyeWidth,menuWidth)
                && format.maxExtent.height>=std::max(m_recommendedEyeHeight,menuHeight);
    }
    m_eyeColorFormat = ChooseEyeColorFormat(requested,aaOff,runtimeRgba,nativeRgba,kEnableExperimentalEyeMsaa);
    HRESULT hr = D3D_OK;
    for (;;) {
        bool complete = true;
	for (uint32_t eye = 0; eye < m_viewCount; ++eye) {
		hr = device->CreateTexture(m_recommendedEyeWidth, m_recommendedEyeHeight, 1,
			D3DUSAGE_RENDERTARGET, static_cast<D3DFORMAT>(m_eyeColorFormat), D3DPOOL_DEFAULT, &m_eyeTextures[eye]);
		if (FAILED(hr) || m_eyeTextures[eye] == nullptr) {
			__android_log_print(ANDROID_LOG_ERROR, kLogTag,
				"CreateTexture for OpenXR eye %u failed: 0x%08x", eye, static_cast<unsigned int>(hr));
			complete = false; break;
		}
		hr = m_eyeTextures[eye]->GetSurfaceLevel(0, &m_eyeSurfaces[eye]);
		if (FAILED(hr) || m_eyeSurfaces[eye] == nullptr) {
			__android_log_print(ANDROID_LOG_ERROR, kLogTag,
				"GetSurfaceLevel for OpenXR eye %u failed: 0x%08x", eye, static_cast<unsigned int>(hr));
			complete = false; break;
		}

		VkImage image = VK_NULL_HANDLE;
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkFormat sourceFormat = VK_FORMAT_UNDEFINED;
		if (!queryEyeImage(eye, image, layout, &sourceFormat)) {
			__android_log_print(ANDROID_LOG_ERROR, kLogTag,
				"DXVK D3D8 texture bridge unavailable for OpenXR eye %u", eye);
			complete = false; break;
		}
		if (eye == 0) {
			m_sourceColorFormat = sourceFormat;
		} else if (sourceFormat != m_sourceColorFormat) {
			__android_log_print(ANDROID_LOG_ERROR, kLogTag,
				"DXVK eye targets use inconsistent Vulkan formats: %d and %d",
				static_cast<int>(m_sourceColorFormat), static_cast<int>(sourceFormat));
			complete = false; break;
		}
	}

        if (complete) break;
        for (uint32_t eye=0;eye<m_viewCount;++eye) {
            if (m_eyeSurfaces[eye]) { m_eyeSurfaces[eye]->Release();m_eyeSurfaces[eye]=nullptr; }
            if (m_eyeTextures[eye]) { m_eyeTextures[eye]->Release();m_eyeTextures[eye]=nullptr; }
        }
        m_sourceColorFormat=VK_FORMAT_UNDEFINED;
        if (m_eyeColorFormat==kLegacyEyeColorFormat) return false;
        m_eyeColorFormat=kLegacyEyeColorFormat;
        __android_log_print(ANDROID_LOG_INFO,kLogTag,"Quest direct color copy: RGBA allocation/interop unsupported; original BGRA fallback");
    }
    __android_log_print(ANDROID_LOG_INFO,kLogTag,
        "Quest direct color copy: requested=%d active=%d aa_off=%d runtime_rgba=%d native_rgba=%d d3d_format=%u source_format=%d (full restart)",
        requested,m_eyeColorFormat==kRgbaEyeColorFormat,aaOff,runtimeRgba,nativeRgba,m_eyeColorFormat,static_cast<int>(m_sourceColorFormat));

	// GeneralsX @feature Codex 23/09/2026 Render each eye into one reusable
	// 2x-MSAA surface, then let DXVK resolve it into the eye's single-sample
	// texture. The two eyes render sequentially, so a shared target avoids a
	// second large multisampled allocation. Unsupported color/depth pairs fall
	// back together to the established single-sample path.
	HRESULT colorMsaaResult = D3DERR_NOTAVAILABLE;
	HRESULT depthMsaaResult = D3DERR_NOTAVAILABLE;
	if (kEnableExperimentalEyeMsaa) {
		colorMsaaResult = device->CreateRenderTarget(m_recommendedEyeWidth,
			m_recommendedEyeHeight, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_2_SAMPLES,
			FALSE, &m_msaaEyeSurface);
		if (SUCCEEDED(colorMsaaResult) && m_msaaEyeSurface != nullptr) {
			depthMsaaResult = device->CreateDepthStencilSurface(m_recommendedEyeWidth,
				m_recommendedEyeHeight, D3DFMT_D24S8, D3DMULTISAMPLE_2_SAMPLES,
				&m_depthSurface);
		}
	}
	if (FAILED(colorMsaaResult) || m_msaaEyeSurface == nullptr
		|| FAILED(depthMsaaResult) || m_depthSurface == nullptr) {
		if (m_depthSurface != nullptr) {
			m_depthSurface->Release();
			m_depthSurface = nullptr;
		}
		if (m_msaaEyeSurface != nullptr) {
			m_msaaEyeSurface->Release();
			m_msaaEyeSurface = nullptr;
		}
		if (!fastTaaTransfersRequested() || !createTaaDepthSurface(device)) {
		    hr = device->CreateDepthStencilSurface(m_recommendedEyeWidth,
			    m_recommendedEyeHeight, D3DFMT_D24S8, D3DMULTISAMPLE_NONE,
			    &m_depthSurface);
		}
		if (FAILED(hr) || m_depthSurface == nullptr) {
			__android_log_print(ANDROID_LOG_ERROR, kLogTag,
				"CreateDepthStencilSurface for OpenXR eyes failed: 0x%08x",
				static_cast<unsigned int>(hr));
			return false;
		}
		if (kEnableExperimentalEyeMsaa) {
			__android_log_print(ANDROID_LOG_INFO, kLogTag,
				"OpenXR eye anti-aliasing: single-sample fallback (color 0x%08x, depth 0x%08x)",
				static_cast<unsigned int>(colorMsaaResult),
				static_cast<unsigned int>(depthMsaaResult));
		} else {
			__android_log_print(ANDROID_LOG_INFO, kLogTag,
				"OpenXR eye anti-aliasing: proven single-sample path; experimental 2x MSAA disabled");
		}
	} else {
		__android_log_print(ANDROID_LOG_INFO, kLogTag,
			"OpenXR eye anti-aliasing: 2x MSAA with DXVK resolve");
	}
	return true;
}

bool Bootstrap::createTaaDepthSurface(IDirect3DDevice8 *device)
{
    // D3D8 forwards a standard D24S8 depth texture to the pinned D3D9 backend.
    // Unlike attachment-only surfaces, its actual Vulkan image includes SAMPLED usage.
    IDirect3DTexture8 *candidate=nullptr;IDirect3DSurface8 *surface=nullptr;
    HRESULT result=device->CreateTexture(m_recommendedEyeWidth,m_recommendedEyeHeight,1,
        D3DUSAGE_DEPTHSTENCIL,D3DFMT_D24S8,D3DPOOL_DEFAULT,&candidate);
    if(SUCCEEDED(result)&&candidate)result=candidate->GetSurfaceLevel(0,&surface);
    bool valid=false;
    if(SUCCEEDED(result)&&surface) {
        D3DSURFACE_DESC desc{};DxvkVulkanInteropTexture *interop=nullptr;
        if(SUCCEEDED(surface->GetDesc(&desc))&&desc.Format==D3DFMT_D24S8
            &&desc.Width==m_recommendedEyeWidth&&desc.Height==m_recommendedEyeHeight&&desc.MultiSampleType==D3DMULTISAMPLE_NONE
            &&SUCCEEDED(surface->QueryInterface(kD3D9VkInteropTextureId,reinterpret_cast<void **>(&interop)))&&interop) {
            TaaImage image;
            valid=SUCCEEDED(interop->GetVulkanImageInfo(&image.image,&image.layout,&image.info))&&image.image
                &&IsSampledTaaEyeDepth(image.info,image.layout,m_recommendedEyeWidth,m_recommendedEyeHeight);
            interop->Release();
        }
    }
    if(valid) {m_taaDepthTexture=candidate;m_depthSurface=surface;}
    else {if(surface)surface->Release();if(candidate)candidate->Release();}
    __android_log_print(ANDROID_LOG_INFO,kLogTag,"Quest TAA sampled eye depth: requested=1 active=%d d24s8=1; original surface on failure",valid);
    return valid;
}

bool Bootstrap::discardCompletedEyeDepth(IDirect3DDevice8 *device, IDirect3DSurface8 *depth, unsigned eye)
{
    if (!device || !depth || depth != m_depthSurface || eye >= m_viewCount || eye >= 2
        || !m_frameActive || !m_shouldRender || !m_depthDiscardInterop || m_msaaEyeSurface)
        return false;
    IDirect3DSurface8 *boundColor = nullptr, *boundDepth = nullptr;
    const HRESULT colorResult = device->GetRenderTarget(&boundColor);
    const HRESULT depthResult = device->GetDepthStencilSurface(&boundDepth);
    const bool bound = SUCCEEDED(colorResult) && SUCCEEDED(depthResult)
        && boundColor == m_eyeSurfaces[eye] && boundDepth == depth;
    if (boundColor) boundColor->Release();
    if (boundDepth) boundDepth->Release();
    if (!bound) return false;
    D3DSURFACE_DESC desc{};
    if (FAILED(depth->GetDesc(&desc)) || desc.Format != D3DFMT_D24S8
        || desc.MultiSampleType != D3DMULTISAMPLE_NONE || desc.Width != m_recommendedEyeWidth
        || desc.Height != m_recommendedEyeHeight) return false;
    DxvkVulkanInteropTexture *texture = nullptr;
    if (FAILED(depth->QueryInterface(kD3D9VkInteropTextureId, reinterpret_cast<void **>(&texture)))
        || !texture) return false;
    const HRESULT result = static_cast<DxvkQuestDepthInterop *>(m_depthDiscardInterop)
        ->DiscardCompletedEyeDepth(texture);
    texture->Release();
    return SUCCEEDED(result);
}

// GeneralsX @feature Codex 16/10/2026 The filter consumes the original same-eye
// world color/depth before the pointer, without another scene or EndScene call.
bool Bootstrap::captureTaaCamera(IDirect3DDevice8 *device, unsigned eye, TaaCamera &camera) const
{
    camera = {};
    if (!QuestXR_TaaRequested() || m_taaUnavailable || !device || eye >= 2
        || !m_frameActive || !m_shouldRender || m_msaaEyeSurface) return false;
    IDirect3DSurface8 *color = nullptr, *depth = nullptr;
    const HRESULT colorHr = device->GetRenderTarget(&color);
    const HRESULT depthHr = device->GetDepthStencilSurface(&depth);
    const bool bound = SUCCEEDED(colorHr) && SUCCEEDED(depthHr)
        && color == m_eyeSurfaces[eye] && depth == m_depthSurface;
    if (color) color->Release();
    if (depth) depth->Release();
    D3DVIEWPORT8 viewport{};
    D3DMATRIX view{}, projection{};
    if (!bound || FAILED(device->GetViewport(&viewport))
        || viewport.X || viewport.Y || viewport.Width != m_recommendedEyeWidth
        || viewport.Height != m_recommendedEyeHeight || viewport.MinZ != 0 || viewport.MaxZ != 1
        || FAILED(device->GetTransform(D3DTS_VIEW, &view))
        || FAILED(device->GetTransform(D3DTS_PROJECTION, &projection))) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            __android_log_print(ANDROID_LOG_WARN, kLogTag,
                "Quest TAA camera unavailable: bound=%d viewport=%u,%u %ux%u depth=%.3f..%.3f; keeping original rendering",
                bound, viewport.X, viewport.Y, viewport.Width, viewport.Height, viewport.MinZ, viewport.MaxZ);
        }
        return false;
    }
    static_assert(sizeof(view) == sizeof(camera.view), "D3D camera array layout");
    std::memcpy(camera.view, &view, sizeof view);
    std::memcpy(camera.projection, &projection, sizeof projection);
    camera.width = viewport.Width; camera.height = viewport.Height; camera.valid = true;
    return true;
}

bool Bootstrap::applyTaa(IDirect3DDevice8 *device, unsigned eye, const TaaCamera &camera)
{
    if (!camera.valid || !QuestXR_TaaRequested() || m_taaUnavailable || !m_dxvkInterop) return false;
    TaaCamera bound;
    if (!captureTaaCamera(device, eye, bound)) { m_temporalAA.invalidate(); return false; }
    const auto query = [](IUnknown *source, TaaImage &image) {
        if (!source) return false;
        DxvkVulkanInteropTexture *texture = nullptr;
        if (FAILED(source->QueryInterface(kD3D9VkInteropTextureId, reinterpret_cast<void **>(&texture))) || !texture) return false;
        const auto result = texture->GetVulkanImageInfo(&image.image, &image.layout, &image.info);
        texture->Release(); return SUCCEEDED(result) && image.image;
    };
    std::array<TaaImage,2> eyes{}; TaaImage depth;
    if (!query(m_eyeTextures[0], eyes[0]) || !query(m_eyeTextures[1], eyes[1]) || !query(m_depthSurface, depth)) {
        m_taaUnavailable = true;
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "Quest TAA Lite unavailable: color/depth interop query; keeping original rendering");
        return false;
    }
    const auto start = monotonicNanoseconds();
    // The pinned D3D8 adapter drains pending primitive batches in SetViewport's
    // StateChange, even for the identical viewport. D3D9 interop alone cannot
    // drain an earlier D3D8 batch. Do not add an EndScene/BeginScene pair.
    const D3DVIEWPORT8 viewport{0, 0, camera.width, camera.height, 0, 1};
    if (FAILED(device->SetViewport(&viewport))) { m_temporalAA.invalidate(); return false; }
    auto *interop = static_cast<DxvkVulkanInteropDevice *>(m_dxvkInterop);
    interop->FlushRenderingCommands();
    // This lock first drains DXVK's submission worker. Native work and the next
    // original DXVK draw therefore share one ordered graphics queue.
    DxvkQueueGuard guard(m_dxvkInterop);
    if (!m_taaAttempted) {
        m_taaAttempted = true;
        const auto quality=static_cast<TaaQuality>(QuestXR_TaaMode());
        const bool fast=fastTaaTransfersRequested();
        bool initialized=m_temporalAA.initialize(m_vkPhysicalDevice,m_vkDevice,m_vkQueueFamilyIndex,eyes,depth,quality,fast,GpuBundleRequested());
        if(!initialized&&fast) {
            __android_log_print(ANDROID_LOG_WARN,kLogTag,"Quest TAA fast transfers unavailable; retrying the original Lite apply path");
            initialized=m_temporalAA.initialize(m_vkPhysicalDevice,m_vkDevice,m_vkQueueFamilyIndex,eyes,depth,quality,false,GpuBundleRequested());
        }
        m_taaUnavailable=!initialized;
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "Quest TAA Lite requested=1 active=%d full_image=%u jitter=0 motion_vectors=0; original path on failure", !m_taaUnavailable,quality==TaaQuality::Lite?0u:1u);
    }
    if (m_taaUnavailable) return false;
    if (!m_temporalAA.matches(eyes, depth)) {
        m_taaUnavailable = true; m_temporalAA.invalidate();
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "Quest TAA resources changed; keeping original rendering until graphics reattachment");
        return false;
    }
    const bool applied = m_temporalAA.process(m_vkQueue, eye, camera, m_taaFrame, uint64_t(m_predictedDisplayTime));
    if (!m_temporalAA.ready()) m_taaUnavailable = true;
    static unsigned reports = 0; static double cpuMs = 0;
    cpuMs += double(monotonicNanoseconds() - start) / 1000000.0;
    if (++reports == 240) {
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "Quest TAA bridge: cpu_eye_ms=%.3f (includes producer flush/queue synchronization)", cpuMs / reports);
        reports = 0; cpuMs = 0;
    }
    return applied;
}

bool Bootstrap::queryEyeImage(uint32_t eye, VkImage &image, VkImageLayout &layout,
	VkFormat *format, VkImageUsageFlags *usage) const
{
	if (eye >= m_viewCount || m_eyeTextures[eye] == nullptr) {
		return false;
	}
	return queryTextureImage(m_eyeTextures[eye], m_recommendedEyeWidth,
		m_recommendedEyeHeight, image, layout, format, usage);
}

bool Bootstrap::queryTextureImage(IDirect3DTexture8 *texture, uint32_t width, uint32_t height,
	VkImage &image, VkImageLayout &layout, VkFormat *format, VkImageUsageFlags *usage) const
{
	image = VK_NULL_HANDLE;
	layout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (texture == nullptr) {
		return false;
	}

	DxvkVulkanInteropTexture *interopTexture = nullptr;
	HRESULT hr = texture->QueryInterface(kD3D9VkInteropTextureId,
		reinterpret_cast<void **>(&interopTexture));
	if (FAILED(hr) || interopTexture == nullptr) {
		return false;
	}

	VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
	hr = interopTexture->GetVulkanImageInfo(&image, &layout, &imageInfo);
	interopTexture->Release();
	if (FAILED(hr) || image == VK_NULL_HANDLE
        || (m_eyeColorFormat==kRgbaEyeColorFormat
            ? imageInfo.format!=VK_FORMAT_R8G8B8A8_UNORM
            : (imageInfo.format!=VK_FORMAT_B8G8R8A8_UNORM && imageInfo.format!=VK_FORMAT_B8G8R8A8_SRGB))
		|| (imageInfo.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0
		|| imageInfo.extent.width != width
		|| imageInfo.extent.height != height) {
		return false;
	}
	if (format != nullptr) {
		*format = imageInfo.format;
	}
	if (usage != nullptr) {
		*usage = imageInfo.usage;
	}
	return true;
}

IDirect3DSurface8 *Bootstrap::getMenuDepthSurface(IDirect3DDevice8 *device)
{
    // Allocate only on the first campaign camera sequence; release with the UI target.
    if (!m_menuDepthSurface && device && m_menuSurface) {
        const HRESULT hr = device->CreateDepthStencilSurface(m_menuWidth, m_menuHeight,
            D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &m_menuDepthSurface);
        if (FAILED(hr)) __android_log_print(ANDROID_LOG_ERROR, kLogTag,
            "Campaign flat depth allocation failed: 0x%08x", static_cast<unsigned int>(hr));
    }
    return m_menuDepthSurface;
}

bool Bootstrap::createMenuTarget(IDirect3DDevice8 *device, uint32_t width, uint32_t height)
{
	// GeneralsX @bugfix Codex 15/09/2026 SAGE lays out its frontend in the
	// logical render resolution. The Android window is a Quest compositor
	// surface (4128x2208 on current hardware), so deriving the menu texture
	// from that backbuffer leaves a 1280x720 UI stranded in its top-left.
	m_menuWidth = width;
	m_menuHeight = height;
	__android_log_print(ANDROID_LOG_INFO, kLogTag,
		"OpenXR menu target uses logical SAGE canvas %ux%u", m_menuWidth, m_menuHeight);

	HRESULT hr = device->CreateTexture(m_menuWidth, m_menuHeight, 1, D3DUSAGE_RENDERTARGET,
		static_cast<D3DFORMAT>(m_eyeColorFormat), D3DPOOL_DEFAULT, &m_menuTexture);
	if (FAILED(hr) || m_menuTexture == nullptr) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag,
			"CreateTexture for OpenXR menu failed: 0x%08x", static_cast<unsigned int>(hr));
		return false;
	}
	hr = m_menuTexture->GetSurfaceLevel(0, &m_menuSurface);
	if (FAILED(hr) || m_menuSurface == nullptr) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag,
			"GetSurfaceLevel for OpenXR menu failed: 0x%08x", static_cast<unsigned int>(hr));
		return false;
	}

	VkImage image = VK_NULL_HANDLE;
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkFormat sourceFormat = VK_FORMAT_UNDEFINED;
	if (!queryTextureImage(m_menuTexture, m_menuWidth, m_menuHeight, image, layout,
		&sourceFormat)) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag,
			"DXVK D3D8 texture bridge unavailable for OpenXR menu");
		return false;
	}
	if (sourceFormat != m_sourceColorFormat) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag,
			"DXVK menu target format %d differs from eye target format %d",
			static_cast<int>(sourceFormat), static_cast<int>(m_sourceColorFormat));
		return false;
	}

	XrSwapchainCreateInfo createInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
	createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
	createInfo.format = m_colorFormat;
	createInfo.sampleCount = 1;
	createInfo.width = m_menuWidth;
	createInfo.height = m_menuHeight;
	createInfo.faceCount = 1;
	createInfo.arraySize = 1;
	createInfo.mipCount = 1;
	XrResult result = xrCreateSwapchain(m_session, &createInfo, &m_menuSwapchain);
	if (XR_FAILED(result)) {
		logResult("xrCreateSwapchain(menu)", result);
		return false;
	}

	uint32_t imageCount = 0;
	result = xrEnumerateSwapchainImages(m_menuSwapchain, 0, &imageCount, nullptr);
	if (XR_FAILED(result) || imageCount == 0) {
		logResult("xrEnumerateSwapchainImages(menu count)", result);
		return false;
	}
	std::vector<XrSwapchainImageVulkanKHR> xrImages(imageCount);
	for (XrSwapchainImageVulkanKHR &swapchainImage : xrImages) {
		swapchainImage.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
	}
	result = xrEnumerateSwapchainImages(m_menuSwapchain, imageCount, &imageCount,
		reinterpret_cast<XrSwapchainImageBaseHeader *>(xrImages.data()));
	if (XR_FAILED(result)) {
		logResult("xrEnumerateSwapchainImages(menu list)", result);
		return false;
	}
	m_menuSwapchainImages.reserve(imageCount);
	for (const XrSwapchainImageVulkanKHR &swapchainImage : xrImages) {
		m_menuSwapchainImages.push_back(swapchainImage.image);
	}
    // GeneralsX @performance Codex 13/10/2026 One optional half-height HUD
    // swapchain; the full menu swapchain remains the fallback for every frame.
    if (PerformanceRequested(PerfOption::HudTransfer) && m_menuHeight >= 128) {
        createInfo.height = m_menuHeight - m_menuHeight / 2;
        result = xrCreateSwapchain(m_session, &createInfo, &m_compactMenuSwapchain);
        uint32_t compactCount = 0;
        if (XR_SUCCEEDED(result)) result = xrEnumerateSwapchainImages(m_compactMenuSwapchain, 0, &compactCount, nullptr);
        std::vector<XrSwapchainImageVulkanKHR> compactImages(compactCount);
        for (auto &image : compactImages) image.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
        if (XR_SUCCEEDED(result) && compactCount) result = xrEnumerateSwapchainImages(m_compactMenuSwapchain,
            compactCount, &compactCount, reinterpret_cast<XrSwapchainImageBaseHeader *>(compactImages.data()));
        if (XR_SUCCEEDED(result) && compactCount && compactCount <= compactImages.size()) {
            for (uint32_t i = 0; i < compactCount; ++i) m_compactMenuImages.push_back(compactImages[i].image);
        } else {
            if (m_compactMenuSwapchain != XR_NULL_HANDLE) xrDestroySwapchain(m_compactMenuSwapchain);
            m_compactMenuSwapchain = XR_NULL_HANDLE;
            __android_log_print(ANDROID_LOG_WARN, kLogTag, "Compact HUD output unavailable (%d); retaining full UI transfer", result);
        }
    }
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
        "Quest HUD transfer: requested=%d active=%d canvas=%ux%u compact_height=%u images=%zu (full restart)",
        PerformanceRequested(PerfOption::HudTransfer), !m_compactMenuImages.empty(),
        m_menuWidth, m_menuHeight, m_menuHeight - m_menuHeight / 2, m_compactMenuImages.size());
    return true;
}

bool Bootstrap::createActions()
{
	XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
	std::strncpy(setInfo.actionSetName, "gameplay", XR_MAX_ACTION_SET_NAME_SIZE - 1);
	std::strncpy(setInfo.localizedActionSetName, "Gameplay", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
	XrResult result = xrCreateActionSet(m_instance, &setInfo, &m_actionSet);
	if (XR_FAILED(result)) {
		logResult("xrCreateActionSet(gameplay)", result);
		return false;
	}

	constexpr std::array<const char *, 2> handPathNames = {
		"/user/hand/left", "/user/hand/right"};
	for (uint32_t hand = 0; hand < handPathNames.size(); ++hand) {
		result = xrStringToPath(m_instance, handPathNames[hand], &m_handPaths[hand]);
		if (XR_FAILED(result)) {
			logResult("xrStringToPath(hand)", result);
			return false;
		}
	}

	struct ActionDefinition
	{
		XrAction *action;
		const char *name;
		const char *localizedName;
		XrActionType type;
	};
	const std::array<ActionDefinition, 9> definitions = {{
		{&m_aimPoseAction, "aim_pose", "Aim Pose", XR_ACTION_TYPE_POSE_INPUT},
		{&m_triggerAction, "trigger", "Trigger", XR_ACTION_TYPE_BOOLEAN_INPUT},
		// GeneralsX @bugfix Codex 16/09/2026 Read the analogue squeeze
		// explicitly so incidental controller grip cannot drag the battlefield.
		{&m_gripAction, "grip", "Grip", XR_ACTION_TYPE_FLOAT_INPUT},
		{&m_stickAction, "stick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT},
		{&m_primaryAction, "primary", "Primary Button", XR_ACTION_TYPE_BOOLEAN_INPUT},
		{&m_secondaryAction, "secondary", "Secondary Button", XR_ACTION_TYPE_BOOLEAN_INPUT},
		{&m_menuAction, "menu", "Menu Button", XR_ACTION_TYPE_BOOLEAN_INPUT},
		{&m_stickClickAction, "stick_click", "Thumbstick Click", XR_ACTION_TYPE_BOOLEAN_INPUT},
		{&m_hapticAction, "haptic", "Haptic Feedback", XR_ACTION_TYPE_VIBRATION_OUTPUT},
	}};
	for (const ActionDefinition &definition : definitions) {
		XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
		actionInfo.actionType = definition.type;
		std::strncpy(actionInfo.actionName, definition.name, XR_MAX_ACTION_NAME_SIZE - 1);
		std::strncpy(actionInfo.localizedActionName, definition.localizedName,
			XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
		actionInfo.countSubactionPaths = static_cast<uint32_t>(m_handPaths.size());
		actionInfo.subactionPaths = m_handPaths.data();
		result = xrCreateAction(m_actionSet, &actionInfo, definition.action);
		if (XR_FAILED(result)) {
			logResult("xrCreateAction", result);
			return false;
		}
	}

	struct BindingDefinition
	{
		XrAction action;
		const char *path;
	};
	const std::array<BindingDefinition, 17> bindingDefinitions = {{
		{m_aimPoseAction, "/user/hand/left/input/aim/pose"},
		{m_aimPoseAction, "/user/hand/right/input/aim/pose"},
		{m_triggerAction, "/user/hand/left/input/trigger/value"},
		{m_triggerAction, "/user/hand/right/input/trigger/value"},
		{m_gripAction, "/user/hand/left/input/squeeze/value"},
		{m_gripAction, "/user/hand/right/input/squeeze/value"},
		{m_stickAction, "/user/hand/left/input/thumbstick"},
		{m_stickAction, "/user/hand/right/input/thumbstick"},
		{m_primaryAction, "/user/hand/left/input/x/click"},
		{m_primaryAction, "/user/hand/right/input/a/click"},
		{m_secondaryAction, "/user/hand/left/input/y/click"},
		{m_secondaryAction, "/user/hand/right/input/b/click"},
		{m_stickClickAction, "/user/hand/left/input/thumbstick/click"},
		{m_stickClickAction, "/user/hand/right/input/thumbstick/click"},
		{m_menuAction, "/user/hand/left/input/menu/click"},
		{m_hapticAction, "/user/hand/left/output/haptic"},
		{m_hapticAction, "/user/hand/right/output/haptic"},
	}};
	std::vector<XrActionSuggestedBinding> bindings;
	bindings.reserve(bindingDefinitions.size());
	for (const BindingDefinition &definition : bindingDefinitions) {
		XrPath path = XR_NULL_PATH;
		result = xrStringToPath(m_instance, definition.path, &path);
		if (XR_FAILED(result)) {
			logResult("xrStringToPath(binding)", result);
			return false;
		}
		bindings.push_back({definition.action, path});
	}

	XrPath touchProfile = XR_NULL_PATH;
	result = xrStringToPath(m_instance, "/interaction_profiles/oculus/touch_controller", &touchProfile);
	if (XR_FAILED(result)) {
		logResult("xrStringToPath(Touch profile)", result);
		return false;
	}
	XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
	suggested.interactionProfile = touchProfile;
	suggested.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
	suggested.suggestedBindings = bindings.data();
	result = xrSuggestInteractionProfileBindings(m_instance, &suggested);
	if (XR_FAILED(result)) {
		logResult("xrSuggestInteractionProfileBindings(Touch)", result);
		return false;
	}

	for (uint32_t hand = 0; hand < m_aimSpaces.size(); ++hand) {
		XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
		spaceInfo.action = m_aimPoseAction;
		spaceInfo.subactionPath = m_handPaths[hand];
		spaceInfo.poseInActionSpace.orientation.w = 1.0f;
		result = xrCreateActionSpace(m_session, &spaceInfo, &m_aimSpaces[hand]);
		if (XR_FAILED(result)) {
			logResult("xrCreateActionSpace(aim)", result);
			return false;
		}
	}

	XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
	attachInfo.countActionSets = 1;
	attachInfo.actionSets = &m_actionSet;
	result = xrAttachSessionActionSets(m_session, &attachInfo);
	if (XR_FAILED(result)) {
		logResult("xrAttachSessionActionSets", result);
		return false;
	}
	__android_log_print(ANDROID_LOG_INFO, kLogTag,
		"OpenXR Touch actions ready: aim, trigger, grip, stick, face, menu, haptics");
	return true;
}

void Bootstrap::pulseHaptic(uint32_t hand, float amplitude)
{
	if (hand >= m_handPaths.size() || m_hapticAction == XR_NULL_HANDLE
		|| m_sessionState != XR_SESSION_STATE_FOCUSED) {
		return;
	}
	XrHapticActionInfo actionInfo{XR_TYPE_HAPTIC_ACTION_INFO};
	actionInfo.action = m_hapticAction;
	actionInfo.subactionPath = m_handPaths[hand];
	XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
	vibration.duration = XR_MIN_HAPTIC_DURATION;
	vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
	vibration.amplitude = amplitude;
	const XrResult result = xrApplyHapticFeedback(m_session, &actionInfo,
		reinterpret_cast<const XrHapticBaseHeader *>(&vibration));
	if (XR_FAILED(result) && result != XR_SESSION_NOT_FOCUSED) {
		logResult("xrApplyHapticFeedback", result);
	}
}

bool Bootstrap::createSwapchain()
{
	uint32_t formatCount = 0;
	XrResult result = xrEnumerateSwapchainFormats(m_session, 0, &formatCount, nullptr);
	if (XR_FAILED(result) || formatCount == 0) {
		logResult("xrEnumerateSwapchainFormats(count)", result);
		return false;
	}
	std::vector<int64_t> formats(formatCount);
	result = xrEnumerateSwapchainFormats(m_session, formatCount, &formatCount, formats.data());
	if (XR_FAILED(result)) {
		logResult("xrEnumerateSwapchainFormats(list)", result);
		return false;
	}

	std::string formatList;
	for (int64_t format : formats) {
		if (!formatList.empty()) {
			formatList += ",";
		}
		formatList += std::to_string(format);
	}
	__android_log_print(ANDROID_LOG_INFO, kLogTag,
		"OpenXR Vulkan swapchain formats: %s", formatList.c_str());

	const ColorFormatChoice choice = chooseColorFormat(
		m_sourceColorFormat, formats.data(), formats.size());
	if (choice.format == VK_FORMAT_UNDEFINED) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag,
			"OpenXR exposes neither BGRA8 nor RGBA8 for DXVK source format %d",
			static_cast<int>(m_sourceColorFormat));
		return false;
	}
	if (choice.requiresBlit) {
		VkFormatProperties sourceProperties{};
		VkFormatProperties destinationProperties{};
		vkGetPhysicalDeviceFormatProperties(m_vkPhysicalDevice,
			m_sourceColorFormat, &sourceProperties);
		vkGetPhysicalDeviceFormatProperties(m_vkPhysicalDevice,
			choice.format, &destinationProperties);
		if ((sourceProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) == 0
			|| (destinationProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) == 0) {
			__android_log_print(ANDROID_LOG_ERROR, kLogTag,
				"Vulkan device cannot blit DXVK format %d to OpenXR format %d",
				static_cast<int>(m_sourceColorFormat), static_cast<int>(choice.format));
			return false;
		}
	}
	m_colorFormat = static_cast<int64_t>(choice.format);
	m_colorRequiresBlit = choice.requiresBlit;
	__android_log_print(ANDROID_LOG_INFO, kLogTag,
		"OpenXR color transfer: DXVK format %d -> swapchain format %d via %s",
		static_cast<int>(m_sourceColorFormat), static_cast<int>(choice.format),
		m_colorRequiresBlit ? "vkCmdBlitImage" : "vkCmdCopyImage");

	XrSwapchainCreateInfo createInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
	createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
	createInfo.format = m_colorFormat;
	createInfo.sampleCount = 1;
	createInfo.width = m_recommendedEyeWidth;
	createInfo.height = m_recommendedEyeHeight;
	createInfo.faceCount = 1;
	createInfo.arraySize = m_viewCount;
	createInfo.mipCount = 1;
	result = xrCreateSwapchain(m_session, &createInfo, &m_colorSwapchain);
	if (XR_FAILED(result)) {
		logResult("xrCreateSwapchain(stereo color)", result);
		return false;
	}

	uint32_t imageCount = 0;
	result = xrEnumerateSwapchainImages(m_colorSwapchain, 0, &imageCount, nullptr);
	if (XR_FAILED(result) || imageCount == 0) {
		logResult("xrEnumerateSwapchainImages(count)", result);
		return false;
	}
	std::vector<XrSwapchainImageVulkanKHR> xrImages(imageCount);
	for (XrSwapchainImageVulkanKHR &image : xrImages) {
		image.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
	}
	result = xrEnumerateSwapchainImages(m_colorSwapchain, imageCount, &imageCount,
		reinterpret_cast<XrSwapchainImageBaseHeader *>(xrImages.data()));
	if (XR_FAILED(result)) {
		logResult("xrEnumerateSwapchainImages(list)", result);
		return false;
	}
	m_swapchainImages.reserve(imageCount);
	for (const XrSwapchainImageVulkanKHR &image : xrImages) {
		m_swapchainImages.push_back(image.image);
	}
	return true;
}

bool Bootstrap::createCommandResources()
{
	VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = m_vkQueueFamilyIndex;
	VkResult vkResult = vkCreateCommandPool(m_vkDevice, &poolInfo, nullptr, &m_commandPool);
	if (vkResult != VK_SUCCESS) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag, "vkCreateCommandPool failed: %d", vkResult);
		return false;
	}

	m_commandBuffers.resize(m_swapchainImages.size());
	VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
	allocateInfo.commandPool = m_commandPool;
	allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocateInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());
	vkResult = vkAllocateCommandBuffers(m_vkDevice, &allocateInfo, m_commandBuffers.data());
	if (vkResult != VK_SUCCESS) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag, "vkAllocateCommandBuffers failed: %d", vkResult);
		return false;
	}

	m_commandFences.resize(m_swapchainImages.size(), VK_NULL_HANDLE);
	VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	for (VkFence &fence : m_commandFences) {
		vkResult = vkCreateFence(m_vkDevice, &fenceInfo, nullptr, &fence);
		if (vkResult != VK_SUCCESS) {
			__android_log_print(ANDROID_LOG_ERROR, kLogTag, "vkCreateFence failed: %d", vkResult);
			return false;
		}
	}

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(m_vkPhysicalDevice, &queueFamilyCount, nullptr);
	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(m_vkPhysicalDevice, &queueFamilyCount,
		queueFamilies.data());
	VkPhysicalDeviceProperties properties{};
	vkGetPhysicalDeviceProperties(m_vkPhysicalDevice, &properties);
	if (m_vkQueueFamilyIndex < queueFamilies.size()
		&& queueFamilies[m_vkQueueFamilyIndex].timestampValidBits != 0
		&& properties.limits.timestampPeriod > 0.0f) {
		VkQueryPoolCreateInfo queryInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
		queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
		queryInfo.queryCount = static_cast<uint32_t>(m_commandBuffers.size() * 2);
		vkResult = vkCreateQueryPool(m_vkDevice, &queryInfo, nullptr, &m_timestampQueryPool);
		if (vkResult == VK_SUCCESS) {
			m_timestampPeriodNs = properties.limits.timestampPeriod;
			m_timestampValidBits = queueFamilies[m_vkQueueFamilyIndex].timestampValidBits;
			m_timestampQueryValid.assign(m_commandBuffers.size(), 0);
			__android_log_print(ANDROID_LOG_INFO, kLogTag,
				"Vulkan copy GPU timestamps enabled: %.3f ns/tick, %u valid bits",
				m_timestampPeriodNs,
				queueFamilies[m_vkQueueFamilyIndex].timestampValidBits);
		} else {
			__android_log_print(ANDROID_LOG_WARN, kLogTag,
				"Vulkan copy GPU timestamps unavailable: vkCreateQueryPool=%d", vkResult);
		}
	} else {
		__android_log_print(ANDROID_LOG_INFO, kLogTag,
			"Vulkan copy GPU timestamps unavailable on selected queue family");
	}
	return true;
}

void Bootstrap::collectCopyTimestamp(uint32_t imageIndex)
{
	if (m_timestampQueryPool == VK_NULL_HANDLE
		|| imageIndex >= m_timestampQueryValid.size()
		|| m_timestampQueryValid[imageIndex] == 0) {
		return;
	}
	uint64_t timestamps[2] = {};
	const VkResult result = vkGetQueryPoolResults(m_vkDevice, m_timestampQueryPool,
		imageIndex * 2, 2, sizeof(timestamps), timestamps, sizeof(uint64_t),
		VK_QUERY_RESULT_64_BIT);
	if (result == VK_SUCCESS) {
		const uint64_t mask = m_timestampValidBits >= 64
			? ~uint64_t{0} : (uint64_t{1} << m_timestampValidBits) - 1;
		const uint64_t elapsedTicks = (timestamps[1] - timestamps[0]) & mask;
		m_lastCopyGpuMs = static_cast<double>(elapsedTicks)
			* static_cast<double>(m_timestampPeriodNs) / 1000000.0;
	}
	m_timestampQueryValid[imageIndex] = 0;
}

bool Bootstrap::pollEvents()
{
	XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
	XrResult result = XR_SUCCESS;
	while ((result = xrPollEvent(m_instance, &event)) == XR_SUCCESS) {
		if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
			__android_log_print(ANDROID_LOG_ERROR, kLogTag, "OpenXR instance loss pending");
			return false;
		}
		if (event.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
			const auto *change = reinterpret_cast<const XrEventDataReferenceSpaceChangePending *>(&event);
			__android_log_print(ANDROID_LOG_INFO, kLogTag,
				"OpenXR reference-space change pending: type %d at %lld",
				static_cast<int>(change->referenceSpaceType),
				static_cast<long long>(change->changeTime));
		}
		if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
			const auto *state = reinterpret_cast<const XrEventDataSessionStateChanged *>(&event);
			if (state->session == m_session) {
				m_sessionState = state->state;
				__android_log_print(ANDROID_LOG_INFO, kLogTag, "OpenXR session state: %d",
					static_cast<int>(m_sessionState));
				if (m_sessionState == XR_SESSION_STATE_READY && !m_sessionRunning) {
					XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
					beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
					result = xrBeginSession(m_session, &beginInfo);
					if (XR_FAILED(result)) {
						logResult("xrBeginSession", result);
						return false;
					}
					m_sessionRunning = true;
				} else if (m_sessionState == XR_SESSION_STATE_STOPPING && m_sessionRunning) {
					updateFrontendPassthrough(false);
					result = xrEndSession(m_session);
					if (XR_FAILED(result)) {
						logResult("xrEndSession", result);
						return false;
					}
					m_sessionRunning = false;
				} else if (m_sessionState == XR_SESSION_STATE_EXITING
					|| m_sessionState == XR_SESSION_STATE_LOSS_PENDING) {
					return false;
				}
			}
		}
		event = {XR_TYPE_EVENT_DATA_BUFFER};
	}
	if (result != XR_EVENT_UNAVAILABLE) {
		logResult("xrPollEvent", result);
		return false;
	}
	return true;
}

bool Bootstrap::syncActions()
{
	if (m_actionSet == XR_NULL_HANDLE || !m_sessionRunning) {
		return true;
	}

	XrActiveActionSet activeSet{};
	activeSet.actionSet = m_actionSet;
	XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
	syncInfo.countActiveActionSets = 1;
	syncInfo.activeActionSets = &activeSet;
	XrResult result = xrSyncActions(m_session, &syncInfo);
	if (result == XR_SESSION_NOT_FOCUSED) {
		for (uint32_t hand = 0; hand < m_controllers.size(); ++hand) {
			m_controllers[hand] = {};
			m_controllers[hand].orientation[3] = 1.0f;
			m_controllers[hand].released = m_previousButtons[hand];
			m_previousButtons[hand] = 0;
		}
		m_menuButtonDownNs = 0;
		m_menuButtonLongFired = false;
		return true;
	}
	if (XR_FAILED(result)) {
		logResult("xrSyncActions", result);
		return false;
	}

	for (uint32_t hand = 0; hand < m_controllers.size(); ++hand) {
		QuestXRControllerState next{};
		next.orientation[3] = 1.0f;
		XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
		getInfo.subactionPath = m_handPaths[hand];

		auto readButton = [&](XrAction action, uint32_t bit) -> bool {
			getInfo.action = action;
			XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
			XrResult stateResult = xrGetActionStateBoolean(m_session, &getInfo, &state);
			if (XR_FAILED(stateResult)) {
				return false;
			}
			if (state.isActive) {
				next.connected = 1;
				if (state.currentState == XR_TRUE) {
					next.buttons |= bit;
				}
			}
			return true;
		};
		if (!readButton(m_triggerAction, QUEST_XR_BUTTON_TRIGGER)
			|| !readButton(m_primaryAction, QUEST_XR_BUTTON_PRIMARY)
			|| !readButton(m_secondaryAction, QUEST_XR_BUTTON_SECONDARY)
			|| !readButton(m_stickClickAction, QUEST_XR_BUTTON_STICK)
			|| !readButton(m_menuAction, QUEST_XR_BUTTON_MENU)) {
			__android_log_print(ANDROID_LOG_ERROR, kLogTag,
				"xrGetActionStateBoolean failed for hand %u", hand);
			return false;
		}

		getInfo.action = m_gripAction;
		XrActionStateFloat gripState{XR_TYPE_ACTION_STATE_FLOAT};
		result = xrGetActionStateFloat(m_session, &getInfo, &gripState);
		if (XR_FAILED(result)) {
			logResult("xrGetActionStateFloat(grip)", result);
			return false;
		}
		if (gripState.isActive) {
			next.connected = 1;
			const bool wasPressed = (m_previousButtons[hand] & QUEST_XR_BUTTON_GRIP) != 0;
			const float threshold = wasPressed ? 0.55f : 0.75f;
			if (gripState.currentState >= threshold) {
				next.buttons |= QUEST_XR_BUTTON_GRIP;
			}
		}

		getInfo.action = m_stickAction;
		XrActionStateVector2f stickState{XR_TYPE_ACTION_STATE_VECTOR2F};
		result = xrGetActionStateVector2f(m_session, &getInfo, &stickState);
		if (XR_FAILED(result)) {
			logResult("xrGetActionStateVector2f", result);
			return false;
		}
		if (stickState.isActive) {
			next.connected = 1;
			next.stick[0] = stickState.currentState.x;
			next.stick[1] = stickState.currentState.y;
		}

		getInfo.action = m_aimPoseAction;
		XrActionStatePose poseState{XR_TYPE_ACTION_STATE_POSE};
		result = xrGetActionStatePose(m_session, &getInfo, &poseState);
		if (XR_FAILED(result)) {
			logResult("xrGetActionStatePose", result);
			return false;
		}
		if (poseState.isActive) {
			next.connected = 1;
			XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
			result = xrLocateSpace(m_aimSpaces[hand], m_gameSpace,
				m_predictedDisplayTime, &location);
			if (XR_SUCCEEDED(result)
				&& (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0
				&& (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
				next.poseValid = 1;
				next.position[0] = location.pose.position.x;
				next.position[1] = location.pose.position.y;
				next.position[2] = location.pose.position.z;
				next.orientation[0] = location.pose.orientation.x;
				next.orientation[1] = location.pose.orientation.y;
				next.orientation[2] = location.pose.orientation.z;
				next.orientation[3] = location.pose.orientation.w;
			} else if (XR_FAILED(result)) {
				logResult("xrLocateSpace(aim)", result);
			}

			// Locate the same physical aim pose in VIEW space for the front-end
			// screen and fixed HUD fallback. The wrist HUD instead uses the game-space
			// pose above, exactly like its compositor quad.
			XrSpaceLocation viewLocation{XR_TYPE_SPACE_LOCATION};
			result = xrLocateSpace(m_aimSpaces[hand], m_viewSpace,
				m_predictedDisplayTime, &viewLocation);
			const bool viewPoseValid = XR_SUCCEEDED(result)
				&& (viewLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0
				&& (viewLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
			if (m_menuPresentation != QUEST_XR_MENU_HIDDEN
				&& m_menuWidth != 0 && m_menuHeight != 0) {
				const MenuGeometry geometry = getMenuGeometry(m_menuPresentation,
					m_menuCropTop, m_menuWidth, m_menuHeight, m_controllers,
					m_fixedFrontend && m_frontendAnchorValid ? &m_frontendAnchor : nullptr);
				// A hand never points at the panel it is holding. Only the right
				// controller can operate the left-wrist HUD.
				const bool ownsWristPanel = m_menuPresentation == QUEST_XR_MENU_HUD
					&& !geometry.viewLocked && hand == 0;
				const XrPosef *rayPose = geometry.viewLocked && viewPoseValid
					? &viewLocation.pose : nullptr;
				XrPosef gameRayPose{};
				if (!geometry.viewLocked && next.poseValid != 0) {
					gameRayPose.orientation = {next.orientation[0], next.orientation[1],
						next.orientation[2], next.orientation[3]};
					gameRayPose.position = {next.position[0], next.position[1], next.position[2]};
					rayPose = &gameRayPose;
				}
				if (!ownsWristPanel && rayPose != nullptr) {
					const QuestXRFloat3 aim = QuestXRRotateAim(rayPose->orientation.x,
						rayPose->orientation.y, rayPose->orientation.z, rayPose->orientation.w);
					const XrQuaternionf inverse = conjugateQuaternion(geometry.pose.orientation);
					const XrVector3f relative = {
						rayPose->position.x - geometry.pose.position.x,
						rayPose->position.y - geometry.pose.position.y,
						rayPose->position.z - geometry.pose.position.z};
					const XrVector3f localOrigin = rotateVector(inverse, relative);
					const XrVector3f localDirection = rotateVector(inverse,
						{aim.x, aim.y, aim.z});
					if (std::fabs(localDirection.z) > 0.0001f) {
						const float distance = -localOrigin.z / localDirection.z;
						const float cropFraction = 1.0f - geometry.cropTop;
						const float menuHeight = geometry.width * static_cast<float>(m_menuHeight)
							* cropFraction / static_cast<float>(m_menuWidth);
						const float hitX = localOrigin.x + localDirection.x * distance;
						const float hitY = localOrigin.y + localDirection.y * distance;
						const float u = hitX / geometry.width + 0.5f;
						const float v = 0.5f - hitY / menuHeight;
						if (distance > 0.0f && u >= 0.0f && u <= 1.0f
							&& v >= 0.0f && v <= 1.0f) {
							next.menuPointValid = 1;
							next.menuPoint[0] = u;
							next.menuPoint[1] = QuestXRMapCroppedV(v, geometry.cropTop);
						}
					}
				}
			}
			if (XR_FAILED(result)) {
				logResult("xrLocateSpace(menu aim)", result);
			}
		}

		next.pressed = next.buttons & ~m_previousButtons[hand];
		next.released = m_previousButtons[hand] & ~next.buttons;
		// GeneralsX @feature Codex 16/09/2026 Match GeneralsVR's menu-button
		// gesture without allowing a long press to recenter first. A short release
		// recenters; crossing 450 ms emits a one-frame event for follow-selection.
		if (hand == 0) {
			const int64_t nowNs = monotonicNanoseconds();
			if ((next.pressed & QUEST_XR_BUTTON_MENU) != 0) {
				m_menuButtonDownNs = nowNs;
				m_menuButtonLongFired = false;
			}
			if ((next.buttons & QUEST_XR_BUTTON_MENU) != 0
				&& m_menuButtonDownNs != 0 && !m_menuButtonLongFired
				&& nowNs - m_menuButtonDownNs >= kMenuLongPressNs) {
				next.pressed |= QUEST_XR_BUTTON_MENU_LONG;
				m_menuButtonLongFired = true;
				pulseHaptic(hand, 0.35f);
			}
			if ((next.released & QUEST_XR_BUTTON_MENU) != 0) {
				if (m_menuButtonDownNs != 0 && !m_menuButtonLongFired) {
					// Apply on the next frame boundary. The current eye poses were
					// already located in the old space and use that same space.
					m_recenterRequested = true;
				}
				m_menuButtonDownNs = 0;
				m_menuButtonLongFired = false;
			}
		}
		m_previousButtons[hand] = next.buttons;
		m_controllers[hand] = next;
		if ((next.pressed & (QUEST_XR_BUTTON_TRIGGER | QUEST_XR_BUTTON_PRIMARY
			| QUEST_XR_BUTTON_SECONDARY | QUEST_XR_BUTTON_STICK | QUEST_XR_BUTTON_MENU)) != 0) {
			pulseHaptic(hand, 0.25f);
		}
	}
	return true;
}

bool Bootstrap::recenterGameSpace()
{
	if (m_session == XR_NULL_HANDLE || m_localSpace == XR_NULL_HANDLE
		|| m_viewSpace == XR_NULL_HANDLE) {
		return false;
	}
	XrSpaceLocation headInLocal{XR_TYPE_SPACE_LOCATION};
	XrResult result = xrLocateSpace(m_viewSpace, m_localSpace,
		m_predictedDisplayTime, &headInLocal);
	if (XR_FAILED(result)
		|| (headInLocal.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == 0
		|| (headInLocal.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) == 0) {
		if (XR_FAILED(result)) {
			logResult("xrLocateSpace(recenter)", result);
		}
		return false;
	}

	XrReferenceSpaceCreateInfo createInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	createInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	createInfo.poseInReferenceSpace = headInLocal.pose;
	createInfo.poseInReferenceSpace.orientation = yawOnly(headInLocal.pose.orientation);
	XrSpace replacement = XR_NULL_HANDLE;
	result = xrCreateReferenceSpace(m_session, &createInfo, &replacement);
	if (XR_FAILED(result)) {
		logResult("xrCreateReferenceSpace(recenter)", result);
		return false;
	}
	if (m_gameSpace != XR_NULL_HANDLE) {
		xrDestroySpace(m_gameSpace);
	}
	m_gameSpace = replacement;
	m_frontendAnchorValid = false;
	__android_log_print(ANDROID_LOG_INFO, kLogTag, "OpenXR game space recentered");
    m_temporalAA.invalidate();
	return true;
}

// GeneralsX @performance Codex 16/10/2026 Clear the copied HUD guard and all visible pixels, with full-frame fallback for expanded UI.
uint32_t Bootstrap::getHudClearTop(bool hud,uint32_t width,uint32_t height) const {
    if(!GpuBundleRequested()||!hud||width!=m_menuWidth||height!=m_menuHeight)return 0;
    const auto geometry=getMenuGeometry(QUEST_XR_MENU_HUD,m_menuCropTop,m_menuWidth,m_menuHeight,m_controllers);
    const int32_t crop=int32_t(std::lround(float(height)*geometry.cropTop));
    const auto plan=PlanHudTransfer(height,crop,true,m_compactMenuSwapchain!=XR_NULL_HANDLE&&!m_compactMenuImages.empty());
    return PlanHudCopy(plan,true).sourceTop;
}
bool Bootstrap::renderCompositorImages(uint32_t imageIndex, bool copyEyeTargets,
	uint32_t menuImageIndex, bool copyMenu, const HudTransferPlan &hudPlan)
{
    const auto &menuImages = hudPlan.compact ? m_compactMenuImages : m_menuSwapchainImages;
	if (imageIndex >= m_swapchainImages.size()
		|| (copyMenu && menuImageIndex >= menuImages.size())) {
		return false;
	}
	VkFence fence = m_commandFences[imageIndex];
	const int64_t fenceWaitStart = monotonicNanoseconds();
	VkResult vkResult = vkWaitForFences(m_vkDevice, 1, &fence, VK_TRUE, UINT64_MAX);
	m_accumulatedFenceWaitMs += static_cast<double>(
		monotonicNanoseconds() - fenceWaitStart) / 1000000.0;
	if (vkResult != VK_SUCCESS) {
		return false;
	}
	collectCopyTimestamp(imageIndex);

	std::array<VkImage, 2> eyeImages = {};
	std::array<VkImageLayout, 2> eyeLayouts = {};
	std::array<VkImageUsageFlags, 2> eyeUsage = {};
	std::array<VkFormat, 2> eyeFormats = {};
	VkImage menuImage = VK_NULL_HANDLE;
	VkImageLayout menuLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (copyEyeTargets || copyMenu) {
		// Finish DXVK's command-stream submission before recording barriers that
		// consume render targets on the same Vulkan queue.
		static_cast<DxvkVulkanInteropDevice *>(m_dxvkInterop)->FlushRenderingCommands();
	}
	if (copyEyeTargets) {
		for (uint32_t eye = 0; eye < m_viewCount; ++eye) {
			if (!queryEyeImage(eye, eyeImages[eye], eyeLayouts[eye], &eyeFormats[eye], &eyeUsage[eye])) {
				return false;
			}
		}
	}
	if (copyMenu && !queryTextureImage(m_menuTexture, m_menuWidth, m_menuHeight,
		menuImage, menuLayout)) {
		return false;
	}
	// GeneralsX @feature Codex 30/09/2026 First optional AA experiment uses
	// immutable per-eye/per-fence-slot postprocess resources. No MSAA, history,
	// eye sharing, scene traversal or changes to the menu/HUD copy path.
	if (copyEyeTargets && !m_spatialAaAttempted) {
		m_spatialAaAttempted = true;
		const char *choice = std::getenv("GX_QUEST_SPATIAL_AA");
        // GeneralsX @feature Codex 01/10/2026 0=off, 1=gentle spatial, 2=SMAA 1x.
        const uint32_t mode = choice == nullptr ? 0 : std::strcmp(choice, "1") == 0
            ? 1 : std::strcmp(choice, "2") == 0 ? 2 : 0;
        if (mode != 0 && eyeFormats[0] == eyeFormats[1]
            && m_colorFormat == VK_FORMAT_R8G8B8A8_UNORM) {
            DxvkQueueGuard queueGuard(m_dxvkInterop); // SMAA's one-time lookup upload.
            m_spatialAA.initialize(m_vkPhysicalDevice, m_vkDevice, m_vkQueueFamilyIndex,
                eyeImages, eyeFormats[0], eyeUsage, m_recommendedEyeWidth,
                m_recommendedEyeHeight, static_cast<uint32_t>(m_commandBuffers.size()), mode, m_vkQueue);
		}
		__android_log_print(ANDROID_LOG_INFO, kLogTag,
            "Quest AA mode=%u active=%d (0=off 1=gentle 2=SMAA); historical MSAA remains disabled", mode, m_spatialAA.ready());
	}
	const bool filteredEyes = copyEyeTargets && m_spatialAA.ready() && m_spatialAA.matches(eyeImages);
	// GeneralsX @feature Codex 01/10/2026 Manually requested bounded raw-eye capture.
	EyeCapture::Frame captureFrame;
	captureFrame.monotonicNs = monotonicNanoseconds();
	captureFrame.predictedDisplayTime = m_predictedDisplayTime;
	captureFrame.filtered = filteredEyes;
	const char *aaChoice = std::getenv("GX_QUEST_SPATIAL_AA");
	captureFrame.requestedAa = aaChoice && std::strcmp(aaChoice, "2") == 0 ? 2
		: aaChoice && std::strcmp(aaChoice, "1") == 0 ? 1 : 0;
	captureFrame.cameras = m_diagnosticCameras;
	captureFrame.drawStates = DrawCaptureJson();
	captureFrame.terrainDetailTestRequested = TerrainDetailTestRequested();
	captureFrame.terrainDetailUploads = TerrainDetailTestUploads();
	captureFrame.terrainMipFilterMode = TerrainMipFilterMode();
	captureFrame.terrainMipFilterRegions = TerrainMipFilterRegions();
	captureFrame.terrainMipFilterFailures = TerrainMipFilterFailures();
    captureFrame.softCutoutsRequested = SoftCutoutsRequested();
    captureFrame.softCutoutTreeDraws = SoftCutoutTreeDraws();
    captureFrame.softCutoutNamedDraws = SoftCutoutNamedDraws();
    captureFrame.softCutoutFailures = SoftCutoutFailures();
    // GeneralsX @bugfix Codex 07/10/2026 Only requested draw diagnostics serialize rejection counters.
    if (captureFrame.drawStates != "null") captureFrame.softCutoutDiagnostics = SoftCutoutDiagnosticsJson();
    if (captureFrame.drawStates != "null") captureFrame.treeWindDiagnostics = TreeWindDiagnosticsJson();
    if (captureFrame.drawStates != "null") captureFrame.treeContourDiagnostics = TreeContourDiagnosticsJson();
    if (captureFrame.drawStates != "null") captureFrame.fenceContourDiagnostics = FenceContourDiagnosticsJson();
    if (captureFrame.drawStates != "null") captureFrame.terrainFarMipDiagnostics = TerrainFarMipDiagnosticsJson();
    captureFrame.roadMipsRequested = RoadMipFixRequested();
    captureFrame.cutoutMipsRequested = CutoutMipFixRequested();
    captureFrame.cutoutMipUploads = CutoutMipUploads();
    captureFrame.cutoutMipFailures = CutoutMipFailures();
	captureFrame.textureFilterChoice = TextureFilterChoice();
	captureFrame.renderScaleChoice = RenderScaleChoice();
	for (uint32_t eye = 0; eye < 2; ++eye) getEyeView(eye, captureFrame.eyes[eye]);
	const bool captureEyes = copyEyeTargets && m_viewCount == 2 && m_eyeCapture.prepare(
		SDL_GetAndroidInternalStoragePath(), m_vkPhysicalDevice, m_vkDevice,
		m_recommendedEyeWidth, m_recommendedEyeHeight, eyeFormats, eyeUsage, captureFrame);

	VkCommandBuffer commandBuffer = m_commandBuffers[imageIndex];
	if (vkResetCommandBuffer(commandBuffer, 0) != VK_SUCCESS) {
		if (captureEyes) m_eyeCapture.abort("FAILED_COMMAND_RESET");
		return false;
	}
	VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
		if (captureEyes) m_eyeCapture.abort("FAILED_COMMAND_BEGIN");
		return false;
	}
	if (m_timestampQueryPool != VK_NULL_HANDLE) {
		const uint32_t firstQuery = imageIndex * 2;
		vkCmdResetQueryPool(commandBuffer, m_timestampQueryPool, firstQuery, 2);
		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			m_timestampQueryPool, firstQuery);
	}

    // GeneralsX @performance Codex 15/10/2026 Keep the original filtered/capture/front-end path.
    bool batchedTransfer = false;
    if (PerformanceRequested(PerfOption::TransferBatch) && copyEyeTargets && m_viewCount==2
        && !filteredEyes && !captureEyes) {
        ColorTransferBatch batch;
        batch.eyes=eyeImages; batch.layouts=eyeLayouts;
        batch.eyeDestination=m_swapchainImages[imageIndex];
        batch.eyeWidth=m_recommendedEyeWidth; batch.eyeHeight=m_recommendedEyeHeight;
        batch.convertFormat=m_colorRequiresBlit; batch.copyMenu=copyMenu;
        if (copyMenu) {
            batch.menu=menuImage; batch.menuLayout=menuLayout;
            batch.menuDestination=menuImages[menuImageIndex];
            batch.menuWidth=m_menuWidth; batch.menuHeight=m_menuHeight; batch.hud=hudPlan;
        }
        batchedTransfer=RecordColorTransferBatch(commandBuffer,batch);
        if (batchedTransfer && copyMenu)
            RecordHudTransfer(m_menuWidth,m_menuHeight,hudPlan.destinationHeight,hudPlan.compact);
    }
    // Count actual use rather than successful initialization or preference alone.
    static unsigned batchFrames=0, legacyFrames=0, transferReports=0;
    if (PerformanceMetricsRequested()) {
        if (batchedTransfer) ++batchFrames; else ++legacyFrames;
        if (batchFrames+legacyFrames==120) {
            __android_log_print(ANDROID_LOG_INFO,kLogTag,
                "Quest transfer batch: enabled=%d batched=%u legacy=%u report=%u (120 frames)",
                PerformanceRequested(PerfOption::TransferBatch),batchFrames,legacyFrames,++transferReports);
            batchFrames=legacyFrames=0;
        }
    }
    if (!batchedTransfer) {
	VkImageSubresourceRange allLayers{};
	allLayers.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	allLayers.levelCount = 1;
	allLayers.layerCount = m_viewCount;
	VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
	toTransfer.srcAccessMask = 0;
	toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toTransfer.image = m_swapchainImages[imageIndex];
	toTransfer.subresourceRange = allLayers;
	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);

	if (copyEyeTargets) {
		for (uint32_t eye = 0; eye < m_viewCount; ++eye) {
			VkImageSubresourceRange sourceRange{};
			sourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			sourceRange.levelCount = 1;
			sourceRange.layerCount = 1;
			VkImageMemoryBarrier sourceToTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
			// GeneralsX @bugfix Codex 23/09/2026 The optional MSAA path
			// resolves into this single-sample texture with a Vulkan transfer
			// write inside DXVK. Include that producer in the external bridge
			// dependency; waiting only for color-attachment writes can copy the
			// texture's initial black contents even though CopyRects succeeded.
			sourceToTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
				| VK_ACCESS_TRANSFER_WRITE_BIT;
			sourceToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			sourceToTransfer.oldLayout = eyeLayouts[eye];
			sourceToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			sourceToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			sourceToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			sourceToTransfer.image = eyeImages[eye];
			sourceToTransfer.subresourceRange = sourceRange;
			VkImage transferSource = eyeImages[eye];
			if (filteredEyes) {
				if (captureEyes) m_eyeCapture.copy(commandBuffer, eye, false,
					eyeImages[eye], eyeLayouts[eye], true);
				transferSource = m_spatialAA.record(commandBuffer, imageIndex, eye,
					eyeImages[eye], eyeLayouts[eye]);
			} else {
				vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
					VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &sourceToTransfer);
			}

			if (captureEyes) m_eyeCapture.copy(commandBuffer, eye, filteredEyes,
				transferSource, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, false);

            CopyEyeColorImage(commandBuffer,transferSource,m_swapchainImages[imageIndex],
                m_recommendedEyeWidth,m_recommendedEyeHeight,eye,!filteredEyes && m_colorRequiresBlit);

			VkImageMemoryBarrier sourceRestore = sourceToTransfer;
			sourceRestore.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			// The next frame may either render directly into this image or receive
			// another DXVK resolve, so make both possible writers visible here.
			sourceRestore.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
				| VK_ACCESS_TRANSFER_WRITE_BIT;
			sourceRestore.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			sourceRestore.newLayout = eyeLayouts[eye];
			if (!filteredEyes) {
				vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &sourceRestore);
			}
		}
	} else {
		// GeneralsX @bugfix Codex 10/10/2026 A flat-screen background must match
		// between eyes. Remove the old blue/purple compositor diagnostic colors.
		constexpr VkClearColorValue background = {{0.0f, 0.0f, 0.0f, 1.0f}};
		for (uint32_t eye = 0; eye < m_viewCount; ++eye) {
			VkImageSubresourceRange layer = allLayers;
			layer.baseArrayLayer = eye;
			layer.layerCount = 1;
			vkCmdClearColorImage(commandBuffer, m_swapchainImages[imageIndex],
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &background, 1, &layer);
		}
	}

	VkImageMemoryBarrier toCompositor = toTransfer;
	toCompositor.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	toCompositor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	toCompositor.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toCompositor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &toCompositor);

	if (copyMenu) {
		VkImageSubresourceRange menuRange{};
		menuRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		menuRange.levelCount = 1;
		menuRange.layerCount = 1;

		VkImageMemoryBarrier menuDestination{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		menuDestination.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		menuDestination.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		menuDestination.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		menuDestination.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		menuDestination.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		menuDestination.image = menuImages[menuImageIndex];
		menuDestination.subresourceRange = menuRange;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &menuDestination);

		VkImageMemoryBarrier menuSource{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		menuSource.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		menuSource.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		menuSource.oldLayout = menuLayout;
		menuSource.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		menuSource.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		menuSource.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		menuSource.image = menuImage;
		menuSource.subresourceRange = menuRange;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &menuSource);

        CopyHudColorImage(commandBuffer, menuImage, menuImages[menuImageIndex],
            m_menuWidth, hudPlan, m_colorRequiresBlit,GpuBundleRequested());
        const auto hudRegion=PlanHudCopy(hudPlan,GpuBundleRequested());
        RecordHudTransfer(m_menuWidth, m_menuHeight, hudRegion.height, hudPlan.compact);
        if(GpuBundleRequested()&&PerformanceMetricsRequested()) {
            static unsigned samples=0;
            if(++samples%120==0)__android_log_print(ANDROID_LOG_INFO,kLogTag,
                "Quest GPU HUD: compact=%d source_top=%u destination_top=%u rows=%u original_rows=%u",
                hudPlan.compact,hudRegion.sourceTop,hudRegion.destinationTop,hudRegion.height,m_menuHeight);
        }

		VkImageMemoryBarrier menuSourceRestore = menuSource;
		menuSourceRestore.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		menuSourceRestore.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		menuSourceRestore.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		menuSourceRestore.newLayout = menuLayout;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &menuSourceRestore);

		VkImageMemoryBarrier menuToCompositor = menuDestination;
		menuToCompositor.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		menuToCompositor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		menuToCompositor.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		menuToCompositor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1,
			&menuToCompositor);
	}
    }
	if (m_timestampQueryPool != VK_NULL_HANDLE) {
		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			m_timestampQueryPool, imageIndex * 2 + 1);
	}
	if (captureEyes) m_eyeCapture.hostBarrier(commandBuffer);
	if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
		if (captureEyes) m_eyeCapture.abort("FAILED_COMMAND_END");
		return false;
	}

	VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	if (vkResetFences(m_vkDevice, 1, &fence) != VK_SUCCESS) {
		if (captureEyes) m_eyeCapture.abort("FAILED_FENCE_RESET");
		return false;
	}
	{
		DxvkQueueGuard queueGuard(m_dxvkInterop);
		vkResult = vkQueueSubmit(m_vkQueue, 1, &submitInfo, fence);
	}
	if (vkResult != VK_SUCCESS) {
		if (captureEyes) m_eyeCapture.abort("FAILED_SUBMIT");
		return false;
	}
	if (captureEyes) {
		// Read only after this owned submission completes; never queue-idle per frame.
		if (vkWaitForFences(m_vkDevice, 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS)
			m_eyeCapture.complete();
		else m_eyeCapture.abort("FAILED_CAPTURE_FENCE");
	}
	if (m_timestampQueryPool != VK_NULL_HANDLE) {
		m_timestampQueryValid[imageIndex] = 1;
	}
	// Release happens only after commands referencing the image are submitted to
	// the bound queue. Reuse waits on this image's fence, preserving multiple
	// frames in flight without a host wait after every submission.
	return true;
}

bool Bootstrap::beginFrame()
{
	if (m_session == XR_NULL_HANDLE || m_frameLoopFailed) {
		return !m_frameLoopFailed;
	}
	if (m_frameActive) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag,
			"OpenXR frame was not ended before the next begin");
		m_frameLoopFailed = true;
		return false;
	}
	if (!pollEvents()) {
		m_frameLoopFailed = true;
		return false;
	}
	if (!m_sessionRunning) {
		return true;
	}

	XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
	XrFrameState frameState{XR_TYPE_FRAME_STATE};
	const int64_t waitFrameStart = monotonicNanoseconds();
	XrResult result = xrWaitFrame(m_session, &waitInfo, &frameState);
	m_accumulatedWaitFrameMs += static_cast<double>(
		monotonicNanoseconds() - waitFrameStart) / 1000000.0;
	if (XR_FAILED(result)) {
		logResult("xrWaitFrame", result);
		m_frameLoopFailed = true;
		return false;
	}
	XrFrameBeginInfo frameBeginInfo{XR_TYPE_FRAME_BEGIN_INFO};
	{
		DxvkQueueGuard queueGuard(m_dxvkInterop);
		result = xrBeginFrame(m_session, &frameBeginInfo);
	}
	if (XR_FAILED(result)) {
		logResult("xrBeginFrame", result);
		m_frameLoopFailed = true;
		return false;
	}
	m_frameActive = true;
	++m_taaFrame;
	m_shouldRender = frameState.shouldRender == XR_TRUE;
	if (!m_shouldRender) {
		++m_suppressedRenderFrames;
	}
	m_frameCpuStartNs = monotonicNanoseconds();
	if (m_previousPredictedDisplayTime != 0
		&& frameState.predictedDisplayTime > m_previousPredictedDisplayTime) {
		m_lastPredictedIntervalMs = static_cast<double>(
			frameState.predictedDisplayTime - m_previousPredictedDisplayTime) / 1000000.0;
	}
	m_previousPredictedDisplayTime = frameState.predictedDisplayTime;
	m_predictedDisplayTime = frameState.predictedDisplayTime;
	m_viewsValid = false;
	m_eyeRenderingComplete = false;
	m_diagnosticCameras = {};
	// GeneralsX @feature Codex 02/10/2026 Begin observing before either eye is drawn.
	BeginDrawCapture(m_eyeCapture.wantsDrawObservation(
		SDL_GetAndroidInternalStoragePath(), monotonicNanoseconds()));
	m_menuRenderingComplete = false;
	if (m_recenterRequested) {
		if (recenterGameSpace()) {
			m_recenterRequested = false;
		}
	}

	for (XrView &view : m_views) {
		view = {XR_TYPE_VIEW};
	}
	XrViewState viewState{XR_TYPE_VIEW_STATE};
	uint32_t locatedViewCount = 0;
	XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
	locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locateInfo.displayTime = m_predictedDisplayTime;
	locateInfo.space = m_gameSpace;
	result = xrLocateViews(m_session, &locateInfo, &viewState,
		static_cast<uint32_t>(m_views.size()), &locatedViewCount, m_views.data());
	m_viewsValid = XR_SUCCEEDED(result) && locatedViewCount == m_viewCount
		&& (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0
		&& (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
	if (XR_FAILED(result)) {
		logResult("xrLocateViews", result);
	}
	// GeneralsX @feature Codex 10/10/2026 Capture once in the same game space
	// used by compositor and rays. Head movement never updates this anchor.
	if (m_fixedFrontend && !m_frontendAnchorValid && m_viewsValid) {
		XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
		if (XR_SUCCEEDED(xrLocateSpace(m_viewSpace, m_gameSpace, m_predictedDisplayTime, &head))
			&& (head.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0
			&& (head.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
			m_frontendAnchor.orientation = yawOnly(head.pose.orientation);
			const XrVector3f offset = rotateVector(m_frontendAnchor.orientation,
				{0.0f, 0.0f, -kMenuDistanceMeters});
			m_frontendAnchor.position = {head.pose.position.x + offset.x,
				head.pose.position.y + offset.y, head.pose.position.z + offset.z};
			m_frontendAnchorValid = true;
		}
	}
	if (!syncActions()) {
		// Controller loss must not stall head tracking or rendering. Runtime errors
		// clear the adapter snapshot; reconnecting can recover on a later sync.
		for (uint32_t hand = 0; hand < m_controllers.size(); ++hand) {
			m_controllers[hand] = {};
			m_controllers[hand].orientation[3] = 1.0f;
			m_previousButtons[hand] = 0;
		}
	}
	return true;
}

// GeneralsX @feature Codex 12/10/2026 Optional reconstruction underlay for flat front-end screens.
bool Bootstrap::updateFrontendPassthrough(bool wanted)
{
    if (!wanted) {
        if (m_passthroughRunning) {
            const XrResult layer = m_pausePassthroughLayer(m_passthroughLayer);
            const XrResult feature = m_pausePassthrough(m_passthrough);
            m_passthroughRunning = false;
            if (XR_FAILED(layer) || XR_FAILED(feature)) {
                logResult("pause front-end passthrough", XR_FAILED(layer) ? layer : feature);
                destroyFrontendPassthrough();
                m_passthroughFailed = true;
            }
        }
        return false;
    }
    if (!m_passthroughEnabled || m_passthroughFailed || !m_sessionRunning) return false;
    if (m_passthroughRunning) return true;
    if (m_passthrough == XR_NULL_HANDLE) {
        XrSystemPassthroughPropertiesFB support{XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES_FB};
        XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};
        properties.next = &support;
        if (XR_FAILED(xrGetSystemProperties(m_instance, m_systemId, &properties)) || !support.supportsPassthrough) {
            m_passthroughFailed = true;
            __android_log_print(ANDROID_LOG_WARN, kLogTag, "System passthrough unavailable; retain black background");
            return false;
        }
        const auto load = [this](const char *name, auto *function) {
            return XR_SUCCEEDED(xrGetInstanceProcAddr(m_instance, name,
                reinterpret_cast<PFN_xrVoidFunction *>(function))) && *function != nullptr;
        };
        if (!load("xrCreatePassthroughFB", &m_createPassthrough)
            || !load("xrDestroyPassthroughFB", &m_destroyPassthrough)
            || !load("xrPassthroughStartFB", &m_startPassthrough)
            || !load("xrPassthroughPauseFB", &m_pausePassthrough)
            || !load("xrCreatePassthroughLayerFB", &m_createPassthroughLayer)
            || !load("xrDestroyPassthroughLayerFB", &m_destroyPassthroughLayer)
            || !load("xrPassthroughLayerResumeFB", &m_resumePassthroughLayer)
            || !load("xrPassthroughLayerPauseFB", &m_pausePassthroughLayer)) {
            m_passthroughFailed = true;
            __android_log_print(ANDROID_LOG_WARN, kLogTag, "Passthrough functions unavailable; retain black background");
            return false;
        }
        XrPassthroughCreateInfoFB info{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
        XrResult result = m_createPassthrough(m_session, &info, &m_passthrough);
        if (XR_SUCCEEDED(result)) {
            XrPassthroughLayerCreateInfoFB layer{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
            layer.passthrough = m_passthrough;
            layer.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
            result = m_createPassthroughLayer(m_session, &layer, &m_passthroughLayer);
        }
        if (XR_FAILED(result)) {
            logResult("create front-end passthrough", result);
            destroyFrontendPassthrough(); m_passthroughFailed = true;
            return false;
        }
    }
    XrResult result = m_startPassthrough(m_passthrough);
    if (XR_SUCCEEDED(result)) result = m_resumePassthroughLayer(m_passthroughLayer);
    if (XR_FAILED(result)) {
        logResult("start front-end passthrough", result);
        destroyFrontendPassthrough(); m_passthroughFailed = true;
        return false;
    }
    m_passthroughRunning = true;
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Front-end passthrough active; gameplay remains opaque stereo");
    return true;
}

void Bootstrap::destroyFrontendPassthrough()
{
    if (m_passthroughLayer != XR_NULL_HANDLE && m_destroyPassthroughLayer) m_destroyPassthroughLayer(m_passthroughLayer);
    if (m_passthrough != XR_NULL_HANDLE && m_destroyPassthrough) m_destroyPassthrough(m_passthrough);
    m_passthroughLayer = XR_NULL_HANDLE; m_passthrough = XR_NULL_HANDLE;
    m_passthroughRunning = false;
}

bool Bootstrap::endFrame()
{
	if (!m_frameActive) {
		return !m_frameLoopFailed;
	}

	std::array<XrCompositionLayerProjectionView, 2> projectionViews = {{
		{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
		{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
	}};
	XrCompositionLayerProjection projectionLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
	const bool frontendPassthrough = updateFrontendPassthrough(m_shouldRender && m_viewsValid
		&& m_menuRenderingComplete && m_menuPresentation == QUEST_XR_MENU_FRONTEND && !m_eyeRenderingComplete);
	XrCompositionLayerPassthroughFB passthroughLayer{XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
	passthroughLayer.layerHandle = m_passthroughLayer;
	passthroughLayer.flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
	XrCompositionLayerQuad menuLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
	const XrCompositionLayerBaseHeader *layers[2] = {};
	uint32_t layerCount = 0;
	bool frameOperationOk = true;
	if (m_shouldRender && m_viewsValid) {
		uint32_t imageIndex = 0;
		XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
		XrResult result = XR_SUCCESS;
		{
			DxvkQueueGuard queueGuard(m_dxvkInterop);
			result = xrAcquireSwapchainImage(m_colorSwapchain, &acquireInfo, &imageIndex);
		}
		const bool acquired = XR_SUCCEEDED(result);
		bool waited = false;
		if (XR_SUCCEEDED(result)) {
			XrSwapchainImageWaitInfo imageWaitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
			imageWaitInfo.timeout = XR_INFINITE_DURATION;
			result = xrWaitSwapchainImage(m_colorSwapchain, &imageWaitInfo);
			waited = XR_SUCCEEDED(result);
		}

        const MenuGeometry menuGeometry = getMenuGeometry(m_menuPresentation,
            m_menuCropTop, m_menuWidth, m_menuHeight, m_controllers,
            m_fixedFrontend && m_frontendAnchorValid ? &m_frontendAnchor : nullptr);
        const int32_t originalCropTop = static_cast<int32_t>(std::lround(
            static_cast<float>(m_menuHeight) * menuGeometry.cropTop));
        const HudTransferPlan hudPlan = PlanHudTransfer(m_menuHeight, originalCropTop,
            m_menuPresentation == QUEST_XR_MENU_HUD && m_eyeRenderingComplete,
            m_compactMenuSwapchain != XR_NULL_HANDLE && !m_compactMenuImages.empty());
        const XrSwapchain activeMenuSwapchain = hudPlan.compact ? m_compactMenuSwapchain : m_menuSwapchain;
        uint32_t menuImageIndex = 0;
        bool menuAcquired = false;
		bool menuWaited = false;
		if (XR_SUCCEEDED(result) && m_menuRenderingComplete) {
			{
				DxvkQueueGuard queueGuard(m_dxvkInterop);
				result = xrAcquireSwapchainImage(activeMenuSwapchain, &acquireInfo, &menuImageIndex);
			}
			menuAcquired = XR_SUCCEEDED(result);
			if (XR_SUCCEEDED(result)) {
				XrSwapchainImageWaitInfo menuWaitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
				menuWaitInfo.timeout = XR_INFINITE_DURATION;
				result = xrWaitSwapchainImage(activeMenuSwapchain, &menuWaitInfo);
				menuWaited = XR_SUCCEEDED(result);
			}
		}

		const bool rendered = XR_SUCCEEDED(result) && waited
			&& renderCompositorImages(imageIndex, m_eyeRenderingComplete,
				menuImageIndex, menuWaited && m_menuRenderingComplete, hudPlan);
		bool released = true;
		if (acquired && waited) {
			XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
			XrResult releaseResult = XR_SUCCESS;
			{
				DxvkQueueGuard queueGuard(m_dxvkInterop);
				releaseResult = xrReleaseSwapchainImage(m_colorSwapchain, &releaseInfo);
			}
			released = XR_SUCCEEDED(releaseResult);
			if (XR_FAILED(releaseResult)) {
				logResult("xrReleaseSwapchainImage(stereo)", releaseResult);
			}
		}
		if (menuAcquired && menuWaited) {
			XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
			XrResult releaseResult = XR_SUCCESS;
			{
				DxvkQueueGuard queueGuard(m_dxvkInterop);
				releaseResult = xrReleaseSwapchainImage(activeMenuSwapchain, &releaseInfo);
			}
			released = released && XR_SUCCEEDED(releaseResult);
			if (XR_FAILED(releaseResult)) {
				logResult("xrReleaseSwapchainImage(menu)", releaseResult);
			}
		}

		if (rendered && released) {
			for (uint32_t eye = 0; eye < m_viewCount; ++eye) {
				projectionViews[eye].pose = m_views[eye].pose;
				projectionViews[eye].fov = m_views[eye].fov;
				projectionViews[eye].subImage.swapchain = m_colorSwapchain;
				projectionViews[eye].subImage.imageRect.extent = {
					static_cast<int32_t>(m_recommendedEyeWidth), static_cast<int32_t>(m_recommendedEyeHeight)};
				projectionViews[eye].subImage.imageArrayIndex = eye;
			}
			projectionLayer.space = m_gameSpace;
			projectionLayer.viewCount = m_viewCount;
			projectionLayer.views = projectionViews.data();
			// Keep both eye buffers opaque in gameplay. Only replace the black front-end projection.
			layers[layerCount++] = frontendPassthrough
				? reinterpret_cast<const XrCompositionLayerBaseHeader *>(&passthroughLayer)
				: reinterpret_cast<const XrCompositionLayerBaseHeader *>(&projectionLayer);

			if (menuWaited && m_menuRenderingComplete) {
                const MenuGeometry &geometry = menuGeometry;
                const int32_t cropHeight = static_cast<int32_t>(hudPlan.visibleHeight);
				menuLayer.space = geometry.viewLocked ? m_viewSpace : m_gameSpace;
				menuLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
				menuLayer.subImage.swapchain = activeMenuSwapchain;
				menuLayer.subImage.imageRect.offset = {0, static_cast<int32_t>(hudPlan.subImageTop)};
				menuLayer.subImage.imageRect.extent = {
					static_cast<int32_t>(m_menuWidth), cropHeight};
				menuLayer.pose = geometry.pose;
				menuLayer.size.width = geometry.width;
				menuLayer.size.height = geometry.width * static_cast<float>(cropHeight)
					/ static_cast<float>(m_menuWidth);
				layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&menuLayer);
			}
		}
		if (XR_FAILED(result)) {
			logResult("OpenXR swapchain acquire/wait", result);
			frameOperationOk = false;
		} else if (!rendered) {
			__android_log_print(ANDROID_LOG_ERROR, kLogTag,
				"Vulkan compositor submission failed (game eyes=%s, menu=%s)",
				m_eyeRenderingComplete ? "yes" : "no",
				m_menuRenderingComplete ? "yes" : "no");
			frameOperationOk = false;
		} else if (!released) {
			frameOperationOk = false;
		}
	}

	XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
	if (layerCount == 0) updateFrontendPassthrough(false);
	endInfo.displayTime = m_predictedDisplayTime;
	endInfo.environmentBlendMode = m_blendMode;
	endInfo.layerCount = layerCount;
	endInfo.layers = layerCount != 0 ? layers : nullptr;
	XrResult result = XR_SUCCESS;
	{
		DxvkQueueGuard queueGuard(m_dxvkInterop);
		result = xrEndFrame(m_session, &endInfo);
	}
	if (m_frameCpuStartNs != 0) {
		m_accumulatedFrameCpuMs += static_cast<double>(
			monotonicNanoseconds() - m_frameCpuStartNs) / 1000000.0;
	}
	recordFrameDiagnostics(layerCount);
	m_frameActive = false;
	m_shouldRender = false;
	m_viewsValid = false;
	m_eyeRenderingComplete = false;
	m_menuRenderingComplete = false;
	if (XR_FAILED(result)) {
		logResult("xrEndFrame", result);
		m_frameLoopFailed = true;
		return false;
	}
	if (!frameOperationOk) {
		m_frameLoopFailed = true;
		return false;
	}
	return true;
}

void Bootstrap::recordFrameDiagnostics(uint32_t layerCount)
{
	const int64_t now = monotonicNanoseconds();
	if (m_lastDiagnosticNs == 0) {
		m_lastDiagnosticNs = now;
	}
	++m_diagnosticFrames;
	if (m_diagnosticFrames < 120) {
		return;
	}
	const double elapsedSeconds = static_cast<double>(now - m_lastDiagnosticNs)
		/ 1000000000.0;
	const double fps = elapsedSeconds > 0.0
		? static_cast<double>(m_diagnosticFrames) / elapsedSeconds : 0.0;
	const double divisor = static_cast<double>(m_diagnosticFrames);
	if (m_lastCopyGpuMs >= 0.0) {
		__android_log_print(ANDROID_LOG_INFO, kLogTag,
			"XR perf: %.1f fps, app_cpu %.2f ms, xr_wait %.2f ms, "
			"fence_wait %.3f ms, copy_gpu %.3f ms, display_interval %.3f ms, "
			"render_suppressed %llu, layers %u, eye %ux%u",
			fps, m_accumulatedFrameCpuMs / divisor,
			m_accumulatedWaitFrameMs / divisor,
			m_accumulatedFenceWaitMs / divisor, m_lastCopyGpuMs,
			m_lastPredictedIntervalMs,
			static_cast<unsigned long long>(m_suppressedRenderFrames), layerCount,
			m_recommendedEyeWidth, m_recommendedEyeHeight);
	} else {
		__android_log_print(ANDROID_LOG_INFO, kLogTag,
			"XR perf: %.1f fps, app_cpu %.2f ms, xr_wait %.2f ms, "
			"fence_wait %.3f ms, copy_gpu unavailable, display_interval %.3f ms, "
			"render_suppressed %llu, layers %u, eye %ux%u",
			fps, m_accumulatedFrameCpuMs / divisor,
			m_accumulatedWaitFrameMs / divisor,
			m_accumulatedFenceWaitMs / divisor, m_lastPredictedIntervalMs,
			static_cast<unsigned long long>(m_suppressedRenderFrames), layerCount,
			m_recommendedEyeWidth, m_recommendedEyeHeight);
	}
	m_diagnosticFrames = 0;
	m_suppressedRenderFrames = 0;
	m_accumulatedFrameCpuMs = 0.0;
	m_accumulatedWaitFrameMs = 0.0;
	m_accumulatedFenceWaitMs = 0.0;
	m_lastDiagnosticNs = now;
}

bool Bootstrap::getEyeView(uint32_t eye, QuestXREyeView &view) const
{
	if (!m_frameActive || !m_viewsValid || eye >= m_viewCount) {
		return false;
	}
	const XrView &xrView = m_views[eye];
	view.position[0] = xrView.pose.position.x;
	view.position[1] = xrView.pose.position.y;
	view.position[2] = xrView.pose.position.z;
	view.orientation[0] = xrView.pose.orientation.x;
	view.orientation[1] = xrView.pose.orientation.y;
	view.orientation[2] = xrView.pose.orientation.z;
	view.orientation[3] = xrView.pose.orientation.w;
	view.angleLeft = xrView.fov.angleLeft;
	view.angleRight = xrView.fov.angleRight;
	view.angleUp = xrView.fov.angleUp;
	view.angleDown = xrView.fov.angleDown;
	return true;
}

IDirect3DSurface8 *Bootstrap::getEyeSurface(uint32_t eye) const
{
	if (eye >= m_viewCount) {
		return nullptr;
	}
	return m_msaaEyeSurface != nullptr ? m_msaaEyeSurface : m_eyeSurfaces[eye];
}

bool Bootstrap::resolveEyeSurface(IDirect3DDevice8 *device, uint32_t eye) const
{
	if (eye >= m_viewCount || m_eyeSurfaces[eye] == nullptr) {
		return false;
	}
	if (m_msaaEyeSurface == nullptr) {
		return true;
	}
	if (device == nullptr) {
		return false;
	}
	// GeneralsX @feature Codex 23/09/2026 DXVK maps this DEFAULT-to-DEFAULT
	// CopyRects operation to its multisample resolve path.
	const HRESULT hr = device->CopyRects(m_msaaEyeSurface, nullptr, 0,
		m_eyeSurfaces[eye], nullptr);
	if (FAILED(hr)) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag,
			"OpenXR eye %u MSAA resolve failed: 0x%08x", eye,
			static_cast<unsigned int>(hr));
		return false;
	}
	// GeneralsX @bugfix Codex 23/09/2026 Diagnose the black-world MSAA
	// regression at the D3D8 boundary once per process. Reading both surfaces
	// separates an empty render target from a failed resolve or Vulkan copy.
	static bool diagnosedFirstEye = false;
	if (eye == 0 && !diagnosedFirstEye) {
		diagnosedFirstEye = true;
		auto inspectSurface = [this, device](const char *name,
			IDirect3DSurface8 *source) {
			IDirect3DSurface8 *readback = nullptr;
			HRESULT readResult = device->CreateImageSurface(m_recommendedEyeWidth,
				m_recommendedEyeHeight, D3DFMT_A8R8G8B8, &readback);
			if (SUCCEEDED(readResult) && readback != nullptr) {
				readResult = device->CopyRects(source, nullptr, 0, readback, nullptr);
			}
			D3DLOCKED_RECT locked{};
			if (SUCCEEDED(readResult) && readback != nullptr) {
				readResult = readback->LockRect(&locked, nullptr, D3DLOCK_READONLY);
			}
			if (SUCCEEDED(readResult)) {
				uint32_t nonblack = 0;
				uint32_t sample = 0;
				for (uint32_t y = 0; y < 16; ++y) {
					const uint32_t row = ((y * 2 + 1) * m_recommendedEyeHeight) / 32;
					for (uint32_t x = 0; x < 16; ++x) {
						const uint32_t column = ((x * 2 + 1) * m_recommendedEyeWidth) / 32;
						uint32_t pixel = 0;
						const uint8_t *address = static_cast<const uint8_t *>(locked.pBits)
							+ row * locked.Pitch + column * sizeof(pixel);
						std::memcpy(&pixel, address, sizeof(pixel));
						nonblack += (pixel & 0x00ffffffu) != 0;
						if (x == 8 && y == 8) sample = pixel;
					}
				}
				__android_log_print(ANDROID_LOG_INFO, kLogTag,
					"OpenXR MSAA diagnostic %s: %u/256 nonblack, center 0x%08x",
					name, nonblack, sample);
				readback->UnlockRect();
			} else {
				__android_log_print(ANDROID_LOG_ERROR, kLogTag,
					"OpenXR MSAA diagnostic %s readback failed: 0x%08x",
					name, static_cast<unsigned int>(readResult));
			}
			if (readback != nullptr) readback->Release();
		};
		inspectSurface("source", m_msaaEyeSurface);
		inspectSurface("resolved", m_eyeSurfaces[eye]);
	}
	return true;
}

bool Bootstrap::getControllerState(uint32_t hand, QuestXRControllerState &state) const
{
	if (hand >= m_controllers.size()) {
		return false;
	}
	state = m_controllers[hand];
	return true;
}

void Bootstrap::setMenuPresentation(uint32_t presentation)
{
	if (presentation == QUEST_XR_MENU_FRONTEND) m_temporalAA.invalidate();
	if (presentation == QUEST_XR_MENU_FRONTEND && m_menuPresentation != presentation)
		m_frontendAnchorValid = false; // returning from play places the screen in front again
	m_menuPresentation = presentation == QUEST_XR_MENU_FRONTEND
		|| presentation == QUEST_XR_MENU_HUD
		? presentation : QUEST_XR_MENU_HIDDEN;
}

void Bootstrap::setMenuCropTop(float cropTop)
{
	// GeneralsX @feature Codex 16/09/2026 Let the existing window system grow
	// the wrist panel upward for object details and promotion/dialog windows.
	m_menuCropTop = std::max(0.0f, std::min(cropTop, kHudCropTop));
}

void Bootstrap::markMenuRenderingComplete(uint32_t presentation)
{
	setMenuPresentation(presentation);
	m_menuRenderingComplete = m_menuPresentation != QUEST_XR_MENU_HIDDEN;
}

void Bootstrap::detachGraphics()
{
	if (m_frameActive && m_session != XR_NULL_HANDLE) {
		XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
		endInfo.displayTime = m_predictedDisplayTime;
		endInfo.environmentBlendMode = m_blendMode;
		{
			DxvkQueueGuard queueGuard(m_dxvkInterop);
			xrEndFrame(m_session, &endInfo);
		}
		m_frameActive = false;
	}
	if (m_vkDevice != VK_NULL_HANDLE && m_vkQueue != VK_NULL_HANDLE && m_dxvkInterop != nullptr) {
		auto *interop = static_cast<DxvkVulkanInteropDevice *>(m_dxvkInterop);
		interop->LockSubmissionQueue();
		vkQueueWaitIdle(m_vkQueue);
		interop->ReleaseSubmissionQueue();
	}
	m_eyeCapture.destroy(); // Queue completed above; drain the file writer before teardown.
	m_spatialAA.destroy();
    m_temporalAA.destroy();
    m_taaAttempted = m_taaUnavailable = false;
    m_taaFrame = 0;
	m_spatialAaAttempted = false;
	for (VkFence fence : m_commandFences) {
		if (fence != VK_NULL_HANDLE) {
			vkDestroyFence(m_vkDevice, fence, nullptr);
		}
	}
	if (m_timestampQueryPool != VK_NULL_HANDLE) {
		vkDestroyQueryPool(m_vkDevice, m_timestampQueryPool, nullptr);
	}
	if (m_commandPool != VK_NULL_HANDLE) {
		vkDestroyCommandPool(m_vkDevice, m_commandPool, nullptr);
	}
	for (IDirect3DSurface8 *&surface : m_eyeSurfaces) {
		if (surface != nullptr) {
			surface->Release();
			surface = nullptr;
		}
	}
	if (m_msaaEyeSurface != nullptr) {
		m_msaaEyeSurface->Release();
		m_msaaEyeSurface = nullptr;
	}
	for (IDirect3DTexture8 *&texture : m_eyeTextures) {
		if (texture != nullptr) {
			texture->Release();
			texture = nullptr;
		}
	}
	if (m_depthSurface != nullptr) {
		m_depthSurface->Release();
		m_depthSurface = nullptr;
	}
	// The TAA views/descriptors are destroyed before releasing this texture owner.
	if (m_taaDepthTexture != nullptr) {
	    m_taaDepthTexture->Release();
	    m_taaDepthTexture=nullptr;
	}
	if (m_menuDepthSurface != nullptr) {
		m_menuDepthSurface->Release();
		m_menuDepthSurface = nullptr;
	}
	if (m_menuSurface != nullptr) {
		m_menuSurface->Release();
		m_menuSurface = nullptr;
	}
	if (m_menuTexture != nullptr) {
		m_menuTexture->Release();
		m_menuTexture = nullptr;
	}
    if (m_compactMenuSwapchain != XR_NULL_HANDLE) xrDestroySwapchain(m_compactMenuSwapchain);
    m_compactMenuSwapchain = XR_NULL_HANDLE;
    m_compactMenuImages.clear();
	if (m_menuSwapchain != XR_NULL_HANDLE) {
		xrDestroySwapchain(m_menuSwapchain);
	}
	if (m_colorSwapchain != XR_NULL_HANDLE) {
		xrDestroySwapchain(m_colorSwapchain);
	}
	for (XrSpace &space : m_aimSpaces) {
		if (space != XR_NULL_HANDLE) {
			xrDestroySpace(space);
			space = XR_NULL_HANDLE;
		}
	}
	if (m_gameSpace != XR_NULL_HANDLE) {
		xrDestroySpace(m_gameSpace);
	}
	if (m_viewSpace != XR_NULL_HANDLE) {
		xrDestroySpace(m_viewSpace);
	}
	if (m_localSpace != XR_NULL_HANDLE) {
		xrDestroySpace(m_localSpace);
	}
	destroyFrontendPassthrough();
	m_passthroughFailed = false;
	if (m_session != XR_NULL_HANDLE) {
		xrDestroySession(m_session);
	}
	if (m_actionSet != XR_NULL_HANDLE) {
		xrDestroyActionSet(m_actionSet);
	}
	m_localSpace = XR_NULL_HANDLE;
	m_gameSpace = XR_NULL_HANDLE;
	m_viewSpace = XR_NULL_HANDLE;
	m_session = XR_NULL_HANDLE;
	m_colorSwapchain = XR_NULL_HANDLE;
	m_menuSwapchain = XR_NULL_HANDLE;
	m_actionSet = XR_NULL_HANDLE;
	m_aimPoseAction = XR_NULL_HANDLE;
	m_triggerAction = XR_NULL_HANDLE;
	m_gripAction = XR_NULL_HANDLE;
	m_stickAction = XR_NULL_HANDLE;
	m_primaryAction = XR_NULL_HANDLE;
	m_secondaryAction = XR_NULL_HANDLE;
	m_menuAction = XR_NULL_HANDLE;
	m_stickClickAction = XR_NULL_HANDLE;
	m_hapticAction = XR_NULL_HANDLE;
	m_handPaths = {};
	m_controllers = {};
	m_previousButtons = {};
	m_sessionState = XR_SESSION_STATE_UNKNOWN;
	m_sessionRunning = false;
	m_frameLoopFailed = false;
	m_shouldRender = false;
	m_viewsValid = false;
	m_eyeRenderingComplete = false;
	m_menuRenderingComplete = false;
	m_menuPresentation = QUEST_XR_MENU_HIDDEN;
	m_recenterRequested = false;
	m_frontendAnchorValid = false;
	m_menuButtonDownNs = 0;
	m_menuButtonLongFired = false;
	m_predictedDisplayTime = 0;
	m_colorFormat = 0;
    m_eyeColorFormat = kLegacyEyeColorFormat;
	m_sourceColorFormat = VK_FORMAT_UNDEFINED;
	m_colorRequiresBlit = false;
	m_swapchainImages.clear();
	m_menuSwapchainImages.clear();
	m_menuWidth = 0;
	m_menuHeight = 0;
	m_commandPool = VK_NULL_HANDLE;
	m_commandBuffers.clear();
	m_commandFences.clear();
	m_timestampQueryPool = VK_NULL_HANDLE;
	m_timestampQueryValid.clear();
	m_timestampPeriodNs = 0.0f;
	m_timestampValidBits = 0;
    if (m_depthDiscardInterop) {
        static_cast<DxvkQuestDepthInterop *>(m_depthDiscardInterop)->Release();
        m_depthDiscardInterop = nullptr;
    }
	if (m_dxvkInterop != nullptr) {
		static_cast<DxvkVulkanInteropDevice *>(m_dxvkInterop)->Release();
		m_dxvkInterop = nullptr;
	}
	m_vkInstance = VK_NULL_HANDLE;
	m_vkPhysicalDevice = VK_NULL_HANDLE;
	m_vkDevice = VK_NULL_HANDLE;
	m_vkQueue = VK_NULL_HANDLE;
	m_vkQueueFamilyIndex = 0;
	m_vkQueueIndex = 0;
	m_diagnosticFrames = 0;
	m_suppressedRenderFrames = 0;
	m_frameCpuStartNs = 0;
	m_lastDiagnosticNs = 0;
	m_previousPredictedDisplayTime = 0;
	m_accumulatedFrameCpuMs = 0.0;
	m_accumulatedWaitFrameMs = 0.0;
	m_accumulatedFenceWaitMs = 0.0;
	m_lastPredictedIntervalMs = 0.0;
	m_lastCopyGpuMs = -1.0;
}

bool Bootstrap::initializeLoader()
{
	JNIEnv *environment = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
	if (environment == nullptr || activity == nullptr) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag,
			"OpenXR loader init requires SDL's current Android Activity/JNI environment");
		return false;
	}

	JavaVM *vm = nullptr;
	if (environment->GetJavaVM(&vm) != JNI_OK || vm == nullptr) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag, "JNI GetJavaVM failed");
		return false;
	}

	PFN_xrInitializeLoaderKHR initializeLoader = nullptr;
	XrResult result = xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
		reinterpret_cast<PFN_xrVoidFunction *>(&initializeLoader));
	if (XR_FAILED(result) || initializeLoader == nullptr) {
		logResult("xrGetInstanceProcAddr(xrInitializeLoaderKHR)", result);
		return false;
	}

	XrLoaderInitInfoAndroidKHR initInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
	initInfo.applicationVM = vm;
	initInfo.applicationContext = activity;
	result = initializeLoader(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR *>(&initInfo));
	if (XR_FAILED(result)) {
		logResult("xrInitializeLoaderKHR", result);
		return false;
	}
	return true;
}

bool Bootstrap::createInstance()
{
	uint32_t extensionCount = 0;
	XrResult result = xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
	if (XR_FAILED(result)) {
		logResult("xrEnumerateInstanceExtensionProperties(count)", result);
		return false;
	}

	std::vector<XrExtensionProperties> extensions(extensionCount);
	for (XrExtensionProperties &extension : extensions) {
		extension.type = XR_TYPE_EXTENSION_PROPERTIES;
	}
	result = xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data());
	if (XR_FAILED(result)) {
		logResult("xrEnumerateInstanceExtensionProperties(list)", result);
		return false;
	}

	if (!HasExtension(extensions, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME)) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag, "Runtime lacks %s",
			XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME);
		return false;
	}
	if (!HasExtension(extensions, XR_KHR_VULKAN_ENABLE_EXTENSION_NAME)) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag,
			"Runtime lacks %s required by the current DXVK interop path",
			XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
		return false;
	}
	m_enableVulkan2 = HasExtension(extensions, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);

	std::vector<const char *> enabledExtensions = {
		XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
		XR_KHR_VULKAN_ENABLE_EXTENSION_NAME,
	};
	if (m_enableVulkan2) {
		enabledExtensions.push_back(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);
	}

	m_passthroughEnabled = m_passthroughRequested && HasExtension(extensions, XR_FB_PASSTHROUGH_EXTENSION_NAME);
	if (m_passthroughEnabled) enabledExtensions.push_back(XR_FB_PASSTHROUGH_EXTENSION_NAME);
	if (m_passthroughRequested && !m_passthroughEnabled)
		__android_log_print(ANDROID_LOG_WARN, kLogTag, "Passthrough extension unavailable; retain black background");
	JNIEnv *environment = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
	JavaVM *vm = nullptr;
	if (environment == nullptr || activity == nullptr || environment->GetJavaVM(&vm) != JNI_OK) {
		return false;
	}

	XrInstanceCreateInfoAndroidKHR androidInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
	androidInfo.applicationVM = vm;
	androidInfo.applicationActivity = activity;

	XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
	createInfo.next = &androidInfo;
	std::strncpy(createInfo.applicationInfo.applicationName, "Generals VR Quest 3",
		XR_MAX_APPLICATION_NAME_SIZE - 1);
	createInfo.applicationInfo.applicationVersion = 1;
	std::strncpy(createInfo.applicationInfo.engineName, "GeneralsX",
		XR_MAX_ENGINE_NAME_SIZE - 1);
	createInfo.applicationInfo.engineVersion = 1;
	createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
	createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
	createInfo.enabledExtensionNames = enabledExtensions.data();

	result = xrCreateInstance(&createInfo, &m_instance);
	if (XR_FAILED(result) && m_passthroughEnabled) {
		logResult("optional passthrough instance; retry black background", result);
		enabledExtensions.pop_back();
		createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
		m_passthroughEnabled = false;
		m_instance = XR_NULL_HANDLE;
		result = xrCreateInstance(&createInfo, &m_instance);
	}
	if (XR_FAILED(result)) {
		logResult("xrCreateInstance", result);
		return false;
	}

	XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};
	result = xrGetInstanceProperties(m_instance, &properties);
	if (XR_SUCCEEDED(result)) {
		__android_log_print(ANDROID_LOG_INFO, kLogTag, "Runtime: %s %u.%u.%u; vulkan_enable2=%s",
			properties.runtimeName, XR_VERSION_MAJOR(properties.runtimeVersion),
			XR_VERSION_MINOR(properties.runtimeVersion), XR_VERSION_PATCH(properties.runtimeVersion),
			m_enableVulkan2 ? "yes" : "no");
	}
	return true;
}

bool Bootstrap::selectSystemAndViews()
{
	XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	XrResult result = xrGetSystem(m_instance, &systemInfo, &m_systemId);
	if (XR_FAILED(result)) {
		logResult("xrGetSystem(HMD)", result);
		return false;
	}

	XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};
	result = xrGetSystemProperties(m_instance, m_systemId, &properties);
	if (XR_FAILED(result)) {
		logResult("xrGetSystemProperties", result);
		return false;
	}
	__android_log_print(ANDROID_LOG_INFO, kLogTag, "System: %s, max swapchain %ux%u layers=%u",
		properties.systemName, properties.graphicsProperties.maxSwapchainImageWidth,
		properties.graphicsProperties.maxSwapchainImageHeight,
		properties.graphicsProperties.maxLayerCount);

	uint32_t count = 0;
	result = xrEnumerateViewConfigurationViews(m_instance, m_systemId,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr);
	if (XR_FAILED(result) || count != 2) {
		logResult("xrEnumerateViewConfigurationViews(count)", result);
		return false;
	}

	std::vector<XrViewConfigurationView> views(count);
	for (XrViewConfigurationView &view : views) {
		view.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
	}
	result = xrEnumerateViewConfigurationViews(m_instance, m_systemId,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, count, &count, views.data());
	if (XR_FAILED(result) || count != 2) {
		logResult("xrEnumerateViewConfigurationViews(list)", result);
		return false;
	}

	uint32_t maxWidth = 4096, maxHeight = 4096;
	m_viewCount = count;
	for (const XrViewConfigurationView &view : views) {
		m_recommendedEyeWidth = std::max(m_recommendedEyeWidth, view.recommendedImageRectWidth);
		m_recommendedEyeHeight = std::max(m_recommendedEyeHeight, view.recommendedImageRectHeight);
		maxWidth = std::min(maxWidth, view.maxImageRectWidth);
		maxHeight = std::min(maxHeight, view.maxImageRectHeight);
	}
	// GeneralsX @feature Codex 02/10/2026 Same bounded extent for color/depth/AA/XR projection.
	const uint32_t baseWidth = m_recommendedEyeWidth, baseHeight = m_recommendedEyeHeight;
	if (!ChooseEyeExtent(baseWidth, baseHeight, RenderScaleChoice(), maxWidth, maxHeight,
		m_recommendedEyeWidth, m_recommendedEyeHeight)) return false;
	__android_log_print(ANDROID_LOG_INFO, kLogTag,
		"Graphics choices: texture=%u scale=%u/1000 base=%ux%u actual=%ux%u; single-sample stereo",
		TextureFilterChoice(), RenderScaleChoice(), baseWidth, baseHeight,
		m_recommendedEyeWidth, m_recommendedEyeHeight);
	return true;
}

bool Bootstrap::queryVulkanRequirements()
{
	PFN_xrGetVulkanGraphicsRequirementsKHR getRequirements = nullptr;
	XrResult result = xrGetInstanceProcAddr(m_instance, "xrGetVulkanGraphicsRequirementsKHR",
		reinterpret_cast<PFN_xrVoidFunction *>(&getRequirements));
	if (XR_FAILED(result) || getRequirements == nullptr) {
		logResult("xrGetInstanceProcAddr(xrGetVulkanGraphicsRequirementsKHR)", result);
		return false;
	}

	XrGraphicsRequirementsVulkanKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
	result = getRequirements(m_instance, m_systemId, &requirements);
	if (XR_FAILED(result)) {
		logResult("xrGetVulkanGraphicsRequirementsKHR", result);
		return false;
	}
	m_vulkanRequirements.minApiVersion = requirements.minApiVersionSupported;
	m_vulkanRequirements.maxApiVersion = requirements.maxApiVersionSupported;

	if (!QueryExtensionString(m_instance, m_systemId, "xrGetVulkanInstanceExtensionsKHR",
		m_vulkanRequirements.instanceExtensions)) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag, "Unable to query required Vulkan instance extensions");
		return false;
	}
	if (!QueryExtensionString(m_instance, m_systemId, "xrGetVulkanDeviceExtensionsKHR",
		m_vulkanRequirements.deviceExtensions)) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag, "Unable to query required Vulkan device extensions");
		return false;
	}

	// DXVK creates the Vulkan instance and device after this bootstrap step. Its
	// Android OpenXR extension provider consumes these process-local values before
	// either object is created, so the graphics binding satisfies this runtime's
	// requirements instead of relying on a hard-coded Quest extension list.
	if (setenv("DXVK_OPENXR_INSTANCE_EXTENSIONS",
		m_vulkanRequirements.instanceExtensions.c_str(), 1) != 0
		|| setenv("DXVK_OPENXR_DEVICE_EXTENSIONS",
			m_vulkanRequirements.deviceExtensions.c_str(), 1) != 0) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag,
			"Unable to publish OpenXR Vulkan requirements to DXVK");
		return false;
	}
	return true;
}

void Bootstrap::shutdown()
{
	detachGraphics();
	if (m_instance != XR_NULL_HANDLE) {
		xrDestroyInstance(m_instance);
	}
	m_instance = XR_NULL_HANDLE;
	m_systemId = XR_NULL_SYSTEM_ID;
	m_viewCount = 0;
	m_recommendedEyeWidth = 0;
	m_recommendedEyeHeight = 0;
	m_enableVulkan2 = false;
	m_vulkanRequirements = {};
	unsetenv("DXVK_OPENXR_INSTANCE_EXTENSIONS");
	unsetenv("DXVK_OPENXR_DEVICE_EXTENSIONS");
}

void Bootstrap::logResult(const char *operation, XrResult result) const
{
	std::array<char, XR_MAX_RESULT_STRING_SIZE> text{};
	if (m_instance != XR_NULL_HANDLE) {
		xrResultToString(m_instance, result, text.data());
	}
	__android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s failed: %d %s", operation,
		static_cast<int>(result), text.data());
}

} // namespace QuestXR

extern "C" bool QuestXR_AttachD3D8Device(IDirect3DDevice8 *device,
	uint32_t menuWidth, uint32_t menuHeight)
{
	return QuestXR::GetBootstrap().attachGraphics(device, menuWidth, menuHeight);
}

extern "C" void QuestXR_DetachD3D8Device()
{
	QuestXR::GetBootstrap().detachGraphics();
}

extern "C" bool QuestXR_PollLifecycle()
{
    return QuestXR::GetBootstrap().pollLifecycle();
}

extern "C" bool QuestXR_BeginFrame()
{
	return QuestXR::GetBootstrap().beginFrame();
}

extern "C" bool QuestXR_IsFrameActive() { return QuestXR::GetBootstrap().isFrameActive(); }
extern "C" bool QuestXR_ShouldRenderFrame() { return QuestXR::GetBootstrap().shouldRenderFrame(); }
extern "C" IDirect3DSurface8 *QuestXR_GetMenuDepthSurface(IDirect3DDevice8 *device)
{ return QuestXR::GetBootstrap().getMenuDepthSurface(device); }

extern "C" bool QuestXR_EndFrame()
{
	return QuestXR::GetBootstrap().endFrame();
}

extern "C" bool QuestXR_GetEyeView(uint32_t eye, QuestXREyeView *view)
{
	return view != nullptr && QuestXR::GetBootstrap().getEyeView(eye, *view);
}

extern "C" void QuestXR_RecordDiagnosticCamera(uint32_t eye, const QuestXRDiagnosticCamera *camera)
{
	if (camera) QuestXR::GetBootstrap().recordDiagnosticCamera(eye, *camera);
}

extern "C" IDirect3DSurface8 *QuestXR_GetEyeSurface(uint32_t eye)
{
	return QuestXR::GetBootstrap().getEyeSurface(eye);
}

extern "C" bool QuestXR_ResolveEyeSurface(IDirect3DDevice8 *device, uint32_t eye)
{
	return QuestXR::GetBootstrap().resolveEyeSurface(device, eye);
}

extern "C" IDirect3DSurface8 *QuestXR_GetDepthSurface()
{
	return QuestXR::GetBootstrap().getDepthSurface();
}

extern "C" void QuestXR_MarkEyeRenderingComplete()
{
	QuestXR::GetBootstrap().markEyeRenderingComplete();
}

extern "C" IDirect3DSurface8 *QuestXR_GetMenuSurface()
{
	return QuestXR::GetBootstrap().getMenuSurface();
}

extern "C" std::uint32_t QuestXR_GetHudClearTop(bool hud,std::uint32_t width,std::uint32_t height) {
    return QuestXR::GetBootstrap().getHudClearTop(hud,width,height);
}
extern "C" void QuestXR_SetMenuPresentation(uint32_t presentation)
{
	QuestXR::GetBootstrap().setMenuPresentation(presentation);
}

extern "C" void QuestXR_SetMenuCropTop(float cropTop)
{
	QuestXR::GetBootstrap().setMenuCropTop(cropTop);
}

extern "C" void QuestXR_MarkMenuRenderingComplete(uint32_t presentation)
{
	QuestXR::GetBootstrap().markMenuRenderingComplete(presentation);
}

extern "C" bool QuestXR_GetControllerState(uint32_t hand, QuestXRControllerState *state)
{
	return state != nullptr && QuestXR::GetBootstrap().getControllerState(hand, *state);
}

#endif

#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
// GeneralsX @feature Codex 16/10/2026 Explicit quality choice; legacy enabled builds select the unchanged Full filter.
extern "C" unsigned QuestXR_TaaMode() {
    const char *mode=std::getenv("GX_QUEST_TAA_MODE");
    if(mode) {
        if(std::strcmp(mode,"1")==0)return 1;
        if(std::strcmp(mode,"2")==0)return 2;
        if(std::strcmp(mode,"3")==0)return 3;
        return 0;
    }
    return QuestXR::PerformanceRequested(QuestXR::PerfOption::TaaLite)?2:0;
}
extern "C" bool QuestXR_TaaRequested() {
    const char *spatial = std::getenv("GX_QUEST_SPATIAL_AA");
    const bool requested = QuestXR_TaaMode()!=0;
    const bool compatible = !spatial || std::strcmp(spatial, "0") == 0;
    static bool reported = false;
    if (requested && !compatible && !reported) {
        reported = true;
        __android_log_print(ANDROID_LOG_WARN, "GeneralsXR", "Quest TAA Lite disabled: set other anti-aliasing to Off for this trial");
    }
    return requested && compatible;
}
extern "C" bool QuestXR_CaptureTaaCamera(IDirect3DDevice8 *device, unsigned eye, QuestXR::TaaCamera *camera) {
    return camera && QuestXR::GetBootstrap().captureTaaCamera(device, eye, *camera);
}
extern "C" bool QuestXR_ApplyTaa(IDirect3DDevice8 *device, unsigned eye, const QuestXR::TaaCamera *camera) {
    return camera && QuestXR::GetBootstrap().applyTaa(device, eye, *camera);
}
bool QuestXR_QueueEyeDepthDiscard(IDirect3DDevice8 *device, IDirect3DSurface8 *depth, unsigned eye) {
    return QuestXR::GetBootstrap().discardCompletedEyeDepth(device, depth, eye);
}
#endif
