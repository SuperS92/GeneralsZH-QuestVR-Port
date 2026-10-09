// SPDX-License-Identifier: GPL-3.0-or-later
#include "QuestEyeCapture.h"
#include "VRDevice/QuestDrawCapture.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <android/log.h>
#include <chrono>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/statvfs.h>
#endif

namespace QuestXR {
namespace {
constexpr uint32_t kFrames = 8;
constexpr VkDeviceSize kMaxBytes = 64ull * 1024 * 1024;
bool makeDirectory(const std::string &path) {
#ifdef _WIN32
    return _mkdir(path.c_str()) == 0;
#else
    return mkdir(path.c_str(), 0700) == 0;
#endif
}
bool supported(VkFormat f) {
    return f == VK_FORMAT_R8G8B8A8_UNORM || f == VK_FORMAT_B8G8R8A8_UNORM;
}
bool writeBytes(const std::string &path, const void *data, size_t bytes) {
    FILE *file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    const bool ok = std::fwrite(data, 1, bytes, file) == bytes;
    return std::fclose(file) == 0 && ok;
}
void floats(FILE *file, const float *values, uint32_t count) {
    std::fputc('[', file);
    for (uint32_t i = 0; i < count; ++i) {
        if (i) std::fputc(',', file);
        std::fprintf(file, "%.9g", static_cast<double>(values[i]));
    }
    std::fputc(']', file);
}
}

void EyeCapture::status(const char *state, uint32_t written) {
    if (m_storage.empty()) return;
    const std::string path = m_storage + "/quest-eye-capture.status";
    FILE *file = std::fopen((path + ".tmp").c_str(), "wb");
    if (!file) return;
    const std::string name = m_directory.substr(m_directory.find_last_of('/') + 1);
    std::fprintf(file, "{\"state\":\"%s\",\"session\":\"%s\",\"writtenFrames\":%u,\"targetFrames\":%u}\n",
        state, name.c_str(), written, kFrames);
    if (std::fclose(file) == 0) {
#ifdef _WIN32
        std::remove(path.c_str()); // Windows host fixture; Android rename replaces atomically.
#endif
        std::rename((path + ".tmp").c_str(), path.c_str());
    }
    __android_log_print(ANDROID_LOG_INFO, "GeneralsXR", "Eye capture %s: %u/%u (%s)",
        state, written, kFrames, name.c_str());
}

bool EyeCapture::allocate(VkPhysicalDevice physical) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = m_bytes;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vkCreateBuffer(m_device, &info, nullptr, &m_buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(m_device, m_buffer, &requirements);
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((requirements.memoryTypeBits & (1u << i)) &&
            (properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            type = i;
            if (properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) break;
        }
    }
    if (type == UINT32_MAX) return false;
    m_coherent = (properties.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    return vkAllocateMemory(m_device, &allocation, nullptr, &m_memory) == VK_SUCCESS &&
        vkBindBufferMemory(m_device, m_buffer, m_memory, 0) == VK_SUCCESS;
}

bool EyeCapture::prepare(const char *storage, VkPhysicalDevice physical, VkDevice device,
    uint32_t width, uint32_t height, const std::array<VkFormat, 2> &formats,
    const std::array<VkImageUsageFlags, 2> &usage, const Frame &frame) {
    ++m_seenFrames;
    if (!storage || !*storage || m_pending) return false;
    if (m_active && m_done) {
        if (m_writer.joinable()) m_writer.join();
        freeBuffer();
        m_active = false;
    }
    // A request arriving after beginFrame waits for a frame observed from its start.
    if (frame.drawStates == "null") return false;
    if (!m_active) {
        if (frame.monotonicNs - m_lastPollNs < 500000000) return false;
        m_lastPollNs = frame.monotonicNs;
        const std::string request = std::string(storage) + "/quest-eye-capture.request";
        FILE *file = std::fopen(request.c_str(), "rb");
        if (!file) return false;
        std::fclose(file);
        if (std::remove(request.c_str()) != 0) return false;
        m_drawRequestSeen = false;
        m_storage = storage;
        m_directory = m_storage + "/quest-eye-captures";
        makeDirectory(m_directory); // May already exist; the session mkdir must succeed.
        m_directory += "/capture-" + std::to_string(frame.monotonicNs);
        m_device = device;
        m_width = width; m_height = height; m_formats = formats;
        m_planes = frame.filtered ? 4 : 2;
        m_planeBytes = static_cast<VkDeviceSize>(width) * height * 4;
        m_bytes = m_planeBytes * m_planes;
        if (!physical || !device || !width || !height || width > 4096 || height > 4096 ||
            m_bytes > kMaxBytes || !supported(formats[0]) || !supported(formats[1]) ||
            !(usage[0] & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) || !(usage[1] & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
            !makeDirectory(m_directory)) {
            status("FAILED_SETUP", 0); return false;
        }
#ifndef _WIN32
        struct statvfs disk{};
        if (statvfs(m_storage.c_str(), &disk) != 0 ||
            static_cast<uint64_t>(disk.f_bavail) * disk.f_frsize < m_bytes * kFrames + kMaxBytes) {
            status("FAILED_STORAGE", 0); return false;
        }
#endif
        if (!allocate(physical)) { freeBuffer(); status("FAILED_BUFFER", 0); return false; }
        m_metadata = std::fopen((m_directory + "/frames.jsonl").c_str(), "wb");
        const std::string config = DrawCaptureConfigJson();
        if (!writeBytes(m_directory + "/renderer-config.json", config.data(), config.size())) {
            if (m_metadata) std::fclose(m_metadata);
            m_metadata = nullptr; freeBuffer(); status("FAILED_FILES", 0); return false;
        }
        FILE *manifest = std::fopen((m_directory + "/manifest.json").c_str(), "wb");
        if (!m_metadata || !manifest) {
            if (manifest) std::fclose(manifest);
            if (m_metadata) std::fclose(m_metadata);
            m_metadata = nullptr; freeBuffer(); status("FAILED_FILES", 0); return false;
        }
        std::fprintf(manifest,
            "{\"schema\":2,\"width\":%u,\"height\":%u,\"targetFrames\":%u,\"aaRequested\":%u,\"aaApplied\":%s,"
            "\"sourceFormats\":[%u,%u],\"finalFormat\":%u,\"rowOrder\":\"Vulkan image texel rows, no flip\","
            "\"encoding\":\"tightly packed lossless 4-byte UNORM pixels\",\"diagnosticTimingOnly\":true,"
            "\"terrainFarMipDiagnostics\":%s,\"fenceContourDiagnostics\":%s,\"treeContourDiagnostics\":%s,\"treeWindDiagnostics\":%s,\"softCutoutDiagnostics\":%s,\"softCutoutsRequested\":%s,\"softCutoutTreeDraws\":%u,\"softCutoutNamedDraws\":%u,\"softCutoutFailures\":%u,"
            "\"roadMipsRequested\":%s,\"cutoutMipsRequested\":%s,\"cutoutMipUploads\":%u,\"cutoutMipFailures\":%u,"
            "\"textureFilterChoice\":%u,\"renderScaleChoice\":%u,"
            "\"terrainMipFilterMode\":%u,\"terrainMipFilterRegions\":%u,\"terrainMipFilterFailures\":%u,"
            "\"terrainDetailTestRequested\":%s,\"terrainDetailUploads\":%u,\"offFinalAliasesRaw\":%s}\n", width, height, kFrames, frame.requestedAa,
            frame.filtered ? "true" : "false", formats[0], formats[1],
            frame.filtered ? VK_FORMAT_R8G8B8A8_UNORM : formats[0],
            frame.terrainFarMipDiagnostics.c_str(), frame.fenceContourDiagnostics.c_str(), frame.treeContourDiagnostics.c_str(), frame.treeWindDiagnostics.c_str(), frame.softCutoutDiagnostics.c_str(), frame.softCutoutsRequested ? "true" : "false", frame.softCutoutTreeDraws,
            frame.softCutoutNamedDraws, frame.softCutoutFailures,
            frame.roadMipsRequested ? "true" : "false", frame.cutoutMipsRequested ? "true" : "false",
            frame.cutoutMipUploads, frame.cutoutMipFailures,
            frame.textureFilterChoice, frame.renderScaleChoice,
            frame.terrainMipFilterMode, frame.terrainMipFilterRegions, frame.terrainMipFilterFailures,
            frame.terrainDetailTestRequested ? "true" : "false", frame.terrainDetailUploads,
            frame.filtered ? "false" : "true");
        if (std::fclose(manifest) != 0) {
            std::fclose(m_metadata); m_metadata = nullptr; freeBuffer(); status("FAILED_FILES", 0); return false;
        }
        m_recorded = 0; m_stop = false; m_failed = false; m_done = false;
        m_active = true;
        status("CAPTURING", 0);
        try { m_writer = std::thread(&EyeCapture::writer, this); }
        catch (...) {
            std::fclose(m_metadata); m_metadata = nullptr; m_done = true;
            m_active = false; freeBuffer(); status("FAILED_THREAD", 0); return false;
        }
    }
    if (m_recorded >= kFrames || m_outstanding >= 2 || m_failed) return false;
    if (width != m_width || height != m_height || formats != m_formats ||
        m_planes != (frame.filtered ? 4u : 2u)) { abort("FAILED_CHANGED_TARGET"); return false; }
    m_frame = frame;
    m_frame.renderFrame = m_seenFrames;
    m_pending = true;
    m_copyStarted = false;
    return true;
}

void EyeCapture::copy(VkCommandBuffer command, uint32_t eye, bool final,
    VkImage image, VkImageLayout layout, bool restoreLayout) {
    if (!m_pending || eye >= 2 || (final && m_planes != 4)) return;
    if (!m_copyStarted) {
        VkBufferMemoryBarrier reuse{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        reuse.srcAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        reuse.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        reuse.srcQueueFamilyIndex = reuse.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        reuse.buffer = m_buffer; reuse.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &reuse, 0, nullptr);
        m_copyStarted = true;
    }
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = layout; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (restoreLayout) vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy region{};
    region.bufferOffset = m_planeBytes * (eye + (final ? 2u : 0u));
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {m_width, m_height, 1};
    vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_buffer, 1, &region);
    if (restoreLayout) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barrier.newLayout = layout;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
}

void EyeCapture::hostBarrier(VkCommandBuffer command) {
    if (!m_pending) return;
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = m_buffer; barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, nullptr, 1, &barrier, 0, nullptr);
}

void EyeCapture::complete() {
    if (!m_pending) return;
    void *mapped = nullptr;
    if (vkMapMemory(m_device, m_memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
        abort("FAILED_MAP"); return;
    }
    if (!m_coherent) {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = m_memory; range.size = VK_WHOLE_SIZE;
        if (vkInvalidateMappedMemoryRanges(m_device, 1, &range) != VK_SUCCESS) {
            vkUnmapMemory(m_device, m_memory); abort("FAILED_INVALIDATE"); return;
        }
    }
    bool mappedLive = true;
    try {
        Job job{m_frame, m_recorded, std::vector<uint8_t>(static_cast<size_t>(m_bytes))};
        std::memcpy(job.bytes.data(), mapped, static_cast<size_t>(m_bytes));
        vkUnmapMemory(m_device, m_memory);
        mappedLive = false;
        { std::lock_guard<std::mutex> lock(m_mutex);
          if (m_failed || m_done) { m_pending = false; return; }
          m_jobs.push_back(std::move(job)); ++m_outstanding; }
        ++m_recorded; m_pending = false; m_wake.notify_one();
    } catch (...) {
        if (mappedLive) vkUnmapMemory(m_device, m_memory);
        abort("FAILED_HOST_MEMORY");
    }
}

void EyeCapture::writer() {
    uint32_t written = 0;
    while (written < kFrames && !m_failed) {
        Job job;
        { std::unique_lock<std::mutex> lock(m_mutex);
          m_wake.wait(lock, [&] { return m_stop || !m_jobs.empty(); });
          if (m_jobs.empty()) break;
          job = std::move(m_jobs.front()); m_jobs.pop_front(); }
        bool ok = true;
        for (uint32_t plane = 0; plane < m_planes; ++plane) {
            char name[64];
            std::snprintf(name, sizeof(name), "frame-%02u-eye-%u-%s.bin", job.index,
                plane % 2, plane < 2 ? "raw" : "final");
            ok = writeBytes(m_directory + "/" + name, job.bytes.data() + m_planeBytes * plane,
                static_cast<size_t>(m_planeBytes)) && ok;
        }
        FILE *f = m_metadata;
        std::fprintf(f, "{\"index\":%u,\"renderFrame\":%llu,\"monotonicNs\":%lld,\"predictedDisplayTime\":%lld,\"eyes\":[",
            job.index, static_cast<unsigned long long>(job.frame.renderFrame),
            static_cast<long long>(job.frame.monotonicNs), static_cast<long long>(job.frame.predictedDisplayTime));
        for (uint32_t eye = 0; eye < 2; ++eye) {
            if (eye) std::fputc(',', f);
            const auto &view = job.frame.eyes[eye];
            const auto &camera = job.frame.cameras[eye];
            std::fprintf(f, "{\"positionMeters\":"); floats(f, view.position, 3);
            std::fprintf(f, ",\"orientationXYZW\":"); floats(f, view.orientation, 4);
            const float fov[] = {view.angleLeft, view.angleRight, view.angleUp, view.angleDown};
            std::fprintf(f, ",\"fovLeftRightUpDown\":"); floats(f, fov, 4);
            std::fprintf(f, ",\"cameraValid\":%s,\"worldUnitsPerMeter\":%.9g,\"cameraToWorld3x4\":",
                camera.valid ? "true" : "false", static_cast<double>(camera.worldUnitsPerMeter));
            floats(f, camera.cameraToWorld, 12); std::fputc('}', f);
        }
        std::fprintf(f, "],\"terrainFarMipDiagnostics\":%s,\"fenceContourDiagnostics\":%s,\"treeContourDiagnostics\":%s,\"treeWindDiagnostics\":%s,\"softCutoutDiagnostics\":%s,\"softCutoutsRequested\":%s,\"softCutoutTreeDraws\":%u,\"softCutoutNamedDraws\":%u,\"softCutoutFailures\":%u,\"roadMipsRequested\":%s,\"cutoutMipsRequested\":%s,\"cutoutMipUploads\":%u,\"cutoutMipFailures\":%u,\"textureFilterChoice\":%u,\"renderScaleChoice\":%u,\"terrainMipFilterMode\":%u,\"terrainMipFilterRegions\":%u,\"terrainMipFilterFailures\":%u,\"terrainDetailTestRequested\":%s,\"terrainDetailUploads\":%u,\"drawStates\":%s}\n",
            job.frame.terrainFarMipDiagnostics.c_str(), job.frame.fenceContourDiagnostics.c_str(), job.frame.treeContourDiagnostics.c_str(), job.frame.treeWindDiagnostics.c_str(), job.frame.softCutoutDiagnostics.c_str(), job.frame.softCutoutsRequested ? "true" : "false", job.frame.softCutoutTreeDraws,
            job.frame.softCutoutNamedDraws, job.frame.softCutoutFailures,
            job.frame.roadMipsRequested ? "true" : "false", job.frame.cutoutMipsRequested ? "true" : "false",
            job.frame.cutoutMipUploads, job.frame.cutoutMipFailures,
            job.frame.textureFilterChoice, job.frame.renderScaleChoice,
            job.frame.terrainMipFilterMode, job.frame.terrainMipFilterRegions, job.frame.terrainMipFilterFailures,
            job.frame.terrainDetailTestRequested ? "true" : "false", job.frame.terrainDetailUploads,
            job.frame.drawStates.c_str());
        ok = std::fflush(f) == 0 && !std::ferror(f) && ok;
        --m_outstanding;
        if (!ok) { m_failed = true; break; }
        ++written;
    }
    // A delayed file-close error must not produce a completed session.
    if (std::fclose(m_metadata) != 0) m_failed = true;
    m_metadata = nullptr;
    { std::lock_guard<std::mutex> lock(m_mutex); m_jobs.clear(); }
    m_outstanding = 0;
    status(!m_failed && written == kFrames ? "COMPLETE" : "INCOMPLETE", written);
    m_done = true;
}

void EyeCapture::abort(const char *reason) {
    m_pending = false; m_failed = true;
    { std::lock_guard<std::mutex> lock(m_mutex); m_stop = true; }
    m_wake.notify_one();
    __android_log_print(ANDROID_LOG_ERROR, "GeneralsXR", "Eye capture abort: %s", reason);
}
void EyeCapture::freeBuffer() {
    if (m_buffer) vkDestroyBuffer(m_device, m_buffer, nullptr);
    if (m_memory) vkFreeMemory(m_device, m_memory, nullptr);
    m_buffer = VK_NULL_HANDLE; m_memory = VK_NULL_HANDLE;
}
void EyeCapture::destroy() {
    if (m_writer.joinable()) {
        { std::lock_guard<std::mutex> lock(m_mutex); m_stop = true; }
        m_wake.notify_one(); m_writer.join();
    }
    freeBuffer(); m_device = VK_NULL_HANDLE;
    m_active = m_pending = false; m_done = true; m_failed = false;
    m_drawRequestSeen = false;
}
bool EyeCapture::wantsDrawObservation(const char *storage, int64_t nowNs) {
    if (m_active && !m_done) return m_recorded < kFrames && !m_failed;
    if (!storage || !*storage) return false;
    if (nowNs - m_lastDrawPollNs >= 500000000) {
        m_lastDrawPollNs = nowNs;
        FILE *file = std::fopen((std::string(storage) + "/quest-eye-capture.request").c_str(), "rb");
        m_drawRequestSeen = file != nullptr;
        if (file) std::fclose(file);
    }
    return m_drawRequestSeen;
}
}
#endif
