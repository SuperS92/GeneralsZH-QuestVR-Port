// SPDX-License-Identifier: GPL-3.0-or-later
// Real Vulkan readback/writer fixture, isolated from game logic and OpenXR.
#include "QuestEyeCapture.h"
#include "QuestSpatialAA.h"
#include "VRDevice/QuestDrawCapture.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>
static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
#include "test-terrain-detail.h"
#include "test-terrain-mips.h"
#include "test-graphics-choices.h"
#include "test-cutout-mips.h"
#include "test-soft-cutouts.h"
#include "test-tree-wind.h"
#include "test-tree-contours.h"
#include "test-fence-contours.h"
#include "test-terrain-far-mips.h"
static void check(VkResult r) { require(r == VK_SUCCESS, "Vulkan operation"); }
static void checkDrawObservation(const std::string &storage) {
    using namespace QuestXR;
    DiagnosticDraw draw{};
    draw.renderValidMask = 511; draw.render[0] = 1; draw.render[1] = 96;
    auto &texture = draw.textures[0];
    texture.bindingValid = true; texture.identity = 123; texture.type = 3;
    texture.levels = 8; texture.levelValidMask = 255; texture.samplerValidMask = 511;
    texture.sampler = {2, 2, 2, 1056964608, 0, 16, 1, 1, 4};
    for (uint32_t i = 0; i < 8; ++i) texture.widths[i] = texture.heights[i] = 128 >> i;
    std::snprintf(texture.name.data(), texture.name.size(), "fence\"\\\n.dds");
    BeginDrawCapture(false); SetDrawCaptureEye(0); RecordDrawData(draw);
    require(DrawCaptureJson() == "null", "disabled diagnostic has no draw data");
    BeginDrawCapture(true); RecordDrawData(draw); // Outside an eye must be ignored.
    SetDrawCaptureEye(0); RecordDrawData(draw); RecordDrawData(draw);
    SetDrawCaptureEye(1); draw.textures[0].identity = 456; RecordDrawData(draw);
    SetDrawCaptureEye(-1); RecordDrawData(draw); // Pointer/UI scope excluded.
    std::ofstream(storage + "/draw-fixture.json") << DrawCaptureJson();
    BeginDrawCapture(true); SetDrawCaptureEye(0);
    for (uint32_t i = 0; i < 4100; ++i) {
        draw.textures[0].identity = i;
        if (DrawCaptureEnabled()) RecordDrawData(draw);
    }
    std::ofstream(storage + "/draw-bounds.json") << DrawCaptureJson();
    BeginDrawCapture(true); SetDrawCaptureEye(1); draw.renderValidMask = 0;
    draw.textures[0].samplerValidMask = 0; RecordDrawData(draw);
    std::ofstream(storage + "/draw-failed-getters.json") << DrawCaptureJson();
    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(storage);
    std::ofstream("dxvk.conf") << "d3d9.samplerLodBias = 0.5\nd3d9.samplerAnisotropy = 16\naccount = SECRET\n";
    std::ofstream("config-fixture.json") << DrawCaptureConfigJson();
    std::filesystem::current_path(previous);
    BeginDrawCapture(false);
}
int main(int argc, char **argv) {
    require(argc == 2, "fixture directory");
    const std::string storage = argv[1];
    std::filesystem::create_directories(storage);
    checkTerrainDetail();
    checkTerrainMips();
    checkGraphicsChoices();
    checkCutoutMips();
    checkSoftCutouts();
    checkTreeWind();
    checkTreeContours();
    checkFenceContours();
    checkTerrainFarMips();
    checkDrawObservation(storage);
    VkInstance instance{};
    VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    check(vkCreateInstance(&ii, nullptr, &instance));
    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    require(count > 0, "GPU present");
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
    VkPhysicalDevice physical = devices[0];
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    uint32_t family = 0;
    while (family < count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
    require(family < count, "graphics queue");
    float priority = 1;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = family; qi.queueCount = 1; qi.pQueuePriorities = &priority;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi;
    VkDevice device{}; check(vkCreateDevice(physical, &di, nullptr, &device));
    VkQueue queue{}; vkGetDeviceQueue(device, family, 0, &queue);
    VkPhysicalDeviceMemoryProperties memoryTypes{};
    vkGetPhysicalDeviceMemoryProperties(physical, &memoryTypes);
    std::array<VkImage, 4> images{};
    std::array<VkDeviceMemory, 4> memories{};
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    for (uint32_t i = 0; i < 4; ++i) {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = i < 2 ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
        info.extent = {17, 13, 1}; info.mipLevels = info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT; info.usage = usage;
        check(vkCreateImage(device, &info, nullptr, &images[i]));
        VkMemoryRequirements req{}; vkGetImageMemoryRequirements(device, images[i], &req);
        uint32_t type = 0; while (!(req.memoryTypeBits & (1u << type))) ++type;
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size; ai.memoryTypeIndex = type;
        check(vkAllocateMemory(device, &ai, nullptr, &memories[i]));
        check(vkBindImageMemory(device, images[i], memories[i], 0));
    }
    VkCommandPool pool{};
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pi.queueFamilyIndex = family;
    check(vkCreateCommandPool(device, &pi, nullptr, &pool));
    VkCommandBuffer cmd{};
    VkCommandBufferAllocateInfo ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ci.commandPool = pool; ci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ci.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(device, &ci, &cmd));
    VkFence fence{};
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    check(vkCreateFence(device, &fi, nullptr, &fence));
    auto begin = [&] {
        check(vkResetCommandBuffer(cmd, 0));
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        check(vkBeginCommandBuffer(cmd, &bi));
    };
    auto submit = [&] {
        check(vkEndCommandBuffer(cmd)); check(vkResetFences(device, 1, &fence));
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
        check(vkQueueSubmit(queue, 1, &si, fence));
        check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
    };
    begin();
    for (uint32_t i = 0; i < 4; ++i) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = images[i]; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
        VkClearColorValue color{};
        color.float32[3] = 1;
        color.float32[i == 0 ? 0 : i == 1 ? 2 : 1] = 1;
        if (i == 3) color.float32[0] = 1;
        vkCmdClearColorImage(cmd, images[i], VK_IMAGE_LAYOUT_GENERAL, &color, 1, &b.subresourceRange);
    }
    submit();
    const std::array<VkFormat, 2> formats{VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM};
    const std::array<VkImageUsageFlags, 2> usages{usage, usage};
    QuestXR::EyeCapture capture;
    QuestXR::EyeCapture::Frame frame;
    frame.monotonicNs = 1000000000;
    require(!capture.prepare(storage.c_str(), physical, device, 17, 13, formats, usages, frame), "dormant without marker");
    require(!capture.wantsDrawObservation(storage.c_str(), frame.monotonicNs), "draw observation dormant without marker");
    for (uint32_t mode = 0; mode < 3; ++mode) {
        QuestXR::SpatialAA aa;
        if (mode == 2) require(aa.initialize(physical, device, family,
            {images[0], images[1]}, formats[0], usages, 17, 13, 1, 2, queue), "actual SMAA capture chain");
        std::ofstream(storage + "/quest-eye-capture.request") << "capture\n";
        frame.monotonicNs = (mode + 2ll) * 100000000000ll;
        frame.drawStates = "null";
        require(!capture.prepare(storage.c_str(), physical, device, 17, 13, formats, usages, frame), "late request waits for world observation");
        require(std::filesystem::exists(storage + "/quest-eye-capture.request"), "late request remains pending");
        frame.filtered = mode != 0; frame.requestedAa = mode ? 2 : 0;
        frame.terrainDetailTestRequested = mode == 2; frame.terrainDetailUploads = mode == 2 ? 3 : 0;
        frame.terrainMipFilterMode = mode == 1 ? 1 : 0; frame.terrainMipFilterRegions = mode == 1 ? 12 : 0;
        frame.softCutoutsRequested = mode == 2;
        frame.softCutoutTreeDraws = mode == 2 ? 16 : 0;
        frame.softCutoutNamedDraws = mode == 2 ? 48 : 0;
        frame.softCutoutFailures = 0;
        frame.softCutoutDiagnostics = mode == 2 ? QuestXR::SoftCutoutDiagnosticsJson() : "null";
        frame.treeWindDiagnostics = mode == 2 ? QuestXR::TreeWindDiagnosticsJson() : "null";
        frame.treeContourDiagnostics = mode == 2 ? QuestXR::TreeContourDiagnosticsJson() : "null";
        frame.fenceContourDiagnostics = mode == 2 ? QuestXR::FenceContourDiagnosticsJson() : "null";
        _putenv_s("GX_QUEST_TERRAIN_FAR_MIPS", mode == 2 ? "1" : "0");
        _putenv_s("GX_QUEST_TEXTURE_FILTER", mode == 2 ? "2" : "0");
        _putenv_s("GX_QUEST_TERRAIN_FAR_FILTER", mode == 2 ? "1" : "0");
        frame.terrainFarMipDiagnostics = mode == 2 ? QuestXR::TerrainFarMipDiagnosticsJson() : "null";
        _putenv_s("GX_QUEST_TERRAIN_FAR_FILTER", "0");
        _putenv_s("GX_QUEST_TERRAIN_FAR_MIPS", "0");
        _putenv_s("GX_QUEST_TEXTURE_FILTER", "0");
        frame.roadMipsRequested = mode == 1; frame.cutoutMipsRequested = mode == 2;
        frame.cutoutMipUploads = mode == 2 ? 9 : 0;
        frame.textureFilterChoice = mode; frame.renderScaleChoice = mode == 0 ? 1000 : mode == 1 ? 1250 : 1500;
        frame.predictedDisplayTime = 123456789123ll;
        frame.eyes[0].position[0] = -0.032f; frame.eyes[1].position[0] = 0.032f;
        frame.cameras[0].valid = frame.cameras[1].valid = true;
        frame.cameras[0].worldUnitsPerMeter = frame.cameras[1].worldUnitsPerMeter = 500;
        uint32_t recorded = 0;
        for (uint32_t attempt = 0; attempt < 1000 && recorded < 8; ++attempt) {
            frame.monotonicNs += 500000000;
            require(capture.wantsDrawObservation(storage.c_str(), frame.monotonicNs), "first and active capture frames observe world draws");
            QuestXR::BeginDrawCapture(true);
            for (uint32_t eye = 0; eye < 2; ++eye) {
                QuestXR::SetDrawCaptureEye(eye);
                QuestXR::DiagnosticDraw draw{}; draw.textures[0].identity = 10 + eye;
                draw.render[1] = recorded; QuestXR::RecordDrawData(draw);
            }
            QuestXR::SetDrawCaptureEye(-1);
            frame.drawStates = QuestXR::DrawCaptureJson();
            if (!capture.prepare(storage.c_str(), physical, device, 17, 13, formats, usages, frame)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue;
            }
            begin();
            for (uint32_t eye = 0; eye < 2; ++eye) {
                capture.copy(cmd, eye, false, images[eye], VK_IMAGE_LAYOUT_GENERAL, true);
                if (mode == 1) capture.copy(cmd, eye, true, images[eye + 2], VK_IMAGE_LAYOUT_GENERAL, true);
                if (mode == 2) {
                    const VkImage final = aa.record(cmd, 0, eye, images[eye], VK_IMAGE_LAYOUT_GENERAL);
                    capture.copy(cmd, eye, true, final, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, false);
                }
            }
            capture.hostBarrier(cmd); submit(); capture.complete(); ++recorded;
        }
        require(recorded == 8, "bounded session records eight frames");
        capture.destroy();
        require(!capture.wantsDrawObservation(storage.c_str(), frame.monotonicNs + 500000000), "completed capture is dormant again");
        aa.destroy();
    }
    // An interrupted session must never claim completion.
    std::ofstream(storage + "/quest-eye-capture.request") << "capture\n";
    frame.monotonicNs += 100000000000ll;
    require(capture.prepare(storage.c_str(), physical, device, 17, 13, formats, usages, frame), "restart session");
    capture.abort("TEST_ABORT"); capture.destroy();
    // Force a real file-open failure after a successful GPU readback.
    std::ofstream(storage + "/quest-eye-capture.request") << "capture\n";
    frame.monotonicNs += 100000000000ll;
    require(capture.prepare(storage.c_str(), physical, device, 17, 13, formats, usages, frame), "writer failure session");
    std::filesystem::create_directory(storage + "/quest-eye-captures/capture-" +
        std::to_string(frame.monotonicNs) + "/frame-00-eye-0-raw.bin");
    begin();
    for (uint32_t eye = 0; eye < 2; ++eye) {
        capture.copy(cmd, eye, false, images[eye], VK_IMAGE_LAYOUT_GENERAL, true);
        capture.copy(cmd, eye, true, images[eye + 2], VK_IMAGE_LAYOUT_GENERAL, true);
    }
    capture.hostBarrier(cmd); submit(); capture.complete(); capture.destroy();
    std::ofstream(storage + "/quest-eye-capture.request") << "capture\n";
    frame.monotonicNs += 100000000000ll;
    require(!capture.prepare(storage.c_str(), physical, device, 17, 13, formats, {0, 0}, frame), "unsupported source rejected");
    capture.destroy();
    check(vkDeviceWaitIdle(device));
    vkDestroyFence(device, fence, nullptr); vkDestroyCommandPool(device, pool, nullptr);
    for (uint32_t i = 0; i < 4; ++i) { vkDestroyImage(device, images[i], nullptr); vkFreeMemory(device, memories[i], nullptr); }
    vkDestroyDevice(device, nullptr); vkDestroyInstance(instance, nullptr);
    std::puts("PASS: readback, bounded sessions/restart, separate eyes/phases, abort, writer failure and unsupported input");
}
