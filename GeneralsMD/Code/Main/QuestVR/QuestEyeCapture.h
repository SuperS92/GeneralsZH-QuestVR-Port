// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <vulkan/vulkan.h>
#include "VRDevice/QuestXRInterface.h"
#include "VRDevice/QuestEyeCaptureCamera.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace QuestXR {
// GeneralsX @feature Codex 01/10/2026 Bounded, manually requested raw-eye capture.
// Caller completes its submission fence before complete(), and its queue before
// destroy(). No game state, camera, scene traversal or AA settings are changed.
class EyeCapture final {
public:
    struct Frame {
        int64_t monotonicNs = 0, predictedDisplayTime = 0;
        uint64_t renderFrame = 0;
        uint32_t requestedAa = 0;
        bool filtered = false;
        bool terrainDetailTestRequested = false;
        uint32_t terrainDetailUploads = 0;
        uint32_t terrainMipFilterMode = 0, terrainMipFilterRegions = 0, terrainMipFilterFailures = 0;
        bool softCutoutsRequested = false;
        uint32_t softCutoutTreeDraws = 0, softCutoutNamedDraws = 0, softCutoutFailures = 0;
        bool roadMipsRequested = false, cutoutMipsRequested = false;
        uint32_t cutoutMipUploads = 0, cutoutMipFailures = 0;
        uint32_t textureFilterChoice = 0, renderScaleChoice = 1000;
        std::array<QuestXREyeView, 2> eyes{};
        std::array<QuestXRDiagnosticCamera, 2> cameras{};
        std::string drawStates = "null";
        // GeneralsX @bugfix Codex 07/10/2026 Skip counters accompany requested captures only.
        std::string softCutoutDiagnostics = "null";
        std::string treeWindDiagnostics = "null";
        std::string treeContourDiagnostics = "null";
        std::string fenceContourDiagnostics = "null";
        std::string terrainFarMipDiagnostics = "null";
    };
    ~EyeCapture() { destroy(); }
    bool wantsDrawObservation(const char *storage, int64_t nowNs);
    bool prepare(const char *storage, VkPhysicalDevice physical, VkDevice device,
        uint32_t width, uint32_t height, const std::array<VkFormat, 2> &formats,
        const std::array<VkImageUsageFlags, 2> &usage, const Frame &frame);
    void copy(VkCommandBuffer command, uint32_t eye, bool final,
        VkImage image, VkImageLayout layout, bool restoreLayout);
    void hostBarrier(VkCommandBuffer command);
    void complete(); // only after the submission fence signals
    void abort(const char *reason);
    void destroy();
private:
    struct Job { Frame frame; uint32_t index; std::vector<uint8_t> bytes; };
    bool allocate(VkPhysicalDevice physical);
    void freeBuffer();
    void writer();
    void status(const char *state, uint32_t written);
    VkDevice m_device = VK_NULL_HANDLE;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    bool m_coherent = false, m_active = false, m_pending = false, m_copyStarted = false;
    uint32_t m_width = 0, m_height = 0, m_planes = 0, m_recorded = 0;
    VkDeviceSize m_planeBytes = 0, m_bytes = 0;
    int64_t m_lastPollNs = 0;
    int64_t m_lastDrawPollNs = 0;
    bool m_drawRequestSeen = false;
    uint64_t m_seenFrames = 0;
    std::array<VkFormat, 2> m_formats{};
    Frame m_frame;
    std::string m_storage, m_directory;
    FILE *m_metadata = nullptr;
    std::thread m_writer;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<Job> m_jobs;
    std::atomic<uint32_t> m_outstanding{0};
    std::atomic<bool> m_done{true}, m_failed{false};
    bool m_stop = false;
};
}
#endif
