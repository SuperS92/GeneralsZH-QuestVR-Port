// SPDX-License-Identifier: GPL-3.0-or-later
// Isolated GPU test of the actual Quest postprocess, not a game/reference build.
#include "QuestSpatialAA.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static void require(bool condition, const char *message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void check(VkResult result) { require(result == VK_SUCCESS, "Vulkan operation"); }
struct Image { VkImage image = {}; VkDeviceMemory memory = {}; };

int main(int argc, char **argv) {
    require(argc==2,"Output fixture path supplied");
    std::FILE *output=std::fopen(argv[1],"wb");
    require(output!=nullptr,"Open output fixture");
    VkInstance instance = {};
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    check(vkCreateInstance(&instanceInfo, nullptr, &instance));
    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    require(count > 0, "GPU present");
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
    VkPhysicalDevice physical = devices[0];
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> queues(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, queues.data());
    uint32_t family = 0;
    while (family < count && !(queues[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) ++family;
    require(family < count, "Compute queue");
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical, &properties);
    std::printf("Host GPU: %s (isolated shader evidence only)\n", properties.deviceName);
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = family; queueInfo.queueCount = 1; queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1; deviceInfo.pQueueCreateInfos = &queueInfo;
    VkDevice device = {};
    check(vkCreateDevice(physical, &deviceInfo, nullptr, &device));
    VkQueue queue = {};
    vkGetDeviceQueue(device, family, 0, &queue);
    VkPhysicalDeviceMemoryProperties memories{};
    vkGetPhysicalDeviceMemoryProperties(physical, &memories);
    auto allocate = [&](VkMemoryRequirements requirements, VkMemoryPropertyFlags flags) {
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        uint32_t type = 0;
        while (type < memories.memoryTypeCount && (!(requirements.memoryTypeBits & (1u << type))
            || (memories.memoryTypes[type].propertyFlags & flags) != flags)) ++type;
        require(type < memories.memoryTypeCount, "Memory type");
        allocation.memoryTypeIndex = type;
        VkDeviceMemory memory = {};
        check(vkAllocateMemory(device, &allocation, nullptr, &memory));
        return memory;
    };
    constexpr uint32_t width = 17, height = 13, imageBytes = width * height * 4;
    VkBuffer buffer = {};
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = imageBytes * 6;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer));
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkDeviceMemory bufferMemory = allocate(requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkBindBufferMemory(device, buffer, bufferMemory, 0));
    void *mapped = nullptr;
    check(vkMapMemory(device, bufferMemory, 0, VK_WHOLE_SIZE, 0, &mapped));
    auto *pixels = static_cast<unsigned char *>(mapped);
    VkCommandPool pool = {};
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = family;
    check(vkCreateCommandPool(device, &poolInfo, nullptr, &pool));
    std::array<VkCommandBuffer, 3> commands = {};
    VkCommandBufferAllocateInfo commandAllocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandAllocation.commandPool = pool;
    commandAllocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandAllocation.commandBufferCount = commands.size();
    check(vkAllocateCommandBuffers(device, &commandAllocation, commands.data()));
    auto begin = [&](uint32_t index) {
        VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        check(vkBeginCommandBuffer(commands[index], &info));
    };
    auto submit = [&](uint32_t index) {
        check(vkEndCommandBuffer(commands[index]));
        VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        info.commandBufferCount = 1; info.pCommandBuffers = &commands[index];
        check(vkQueueSubmit(queue, 1, &info, VK_NULL_HANDLE));
    };
    uint64_t outputChecksum=1469598103934665603ull;
    for (VkFormat format : {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM}) {
        for (uint32_t mode : {1u, 2u}) {
        // Flat fields, opposite diagonal silhouettes, repeated texture features,
        // and an axis-aligned single-pixel stripe that must stay sharp.
        for (uint32_t fixture = 0; fixture < 5; ++fixture) {
            check(vkResetCommandPool(device, pool, 0));
            std::memset(pixels, 0, imageBytes * 6);
            for (uint32_t eye = 0; eye < 2; ++eye) for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
                unsigned char *pixel = pixels + eye * imageBytes + (y * width + x) * 4;
                const unsigned char lit = fixture == 0 ? 255 : fixture == 1
                    ? (x > y + 2 ? 255 : 0) : fixture == 2 ? ((x + 2 * y) % 5 < 2 ? 208 : 80)
                    : fixture == 3 ? (x == width / 2 ? 255 : 0) : (x + y > width - 3 ? 255 : 0);
                pixel[format == VK_FORMAT_B8G8R8A8_UNORM ? (eye == 0 ? 2 : 0) : (eye == 0 ? 0 : 2)] = lit;
                pixel[3] = 255;
            }
            std::array<Image, 2> input = {};
            std::array<VkImage, 2> sources = {};
            std::array<VkImageUsageFlags, 2> usage = {};
            VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            begin(0);
            for (uint32_t eye = 0; eye < 2; ++eye) {
                VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                imageInfo.imageType = VK_IMAGE_TYPE_2D; imageInfo.format = format;
                imageInfo.extent = {width, height, 1}; imageInfo.mipLevels = imageInfo.arrayLayers = 1;
                imageInfo.samples = VK_SAMPLE_COUNT_1_BIT; imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                check(vkCreateImage(device, &imageInfo, nullptr, &input[eye].image));
                vkGetImageMemoryRequirements(device, input[eye].image, &requirements);
                input[eye].memory = allocate(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                check(vkBindImageMemory(device, input[eye].image, input[eye].memory, 0));
                sources[eye] = input[eye].image; usage[eye] = imageInfo.usage;
                VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = sources[eye]; barrier.subresourceRange = range;
                vkCmdPipelineBarrier(commands[0], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier);
                VkBufferImageCopy copy{};
                copy.bufferOffset = eye * imageBytes;
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {width, height, 1};
                vkCmdCopyBufferToImage(commands[0], buffer, sources[eye], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                vkCmdPipelineBarrier(commands[0], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier);
            }
            submit(0);
            check(vkQueueWaitIdle(queue));
            QuestXR::SpatialAA aa;
            if (mode == 2 && fixture == 0) {
                QuestXR::SpatialAA rejected;
                require(!rejected.initialize(physical, device, family, sources, format, usage,
                    width, height, 2, mode, VK_NULL_HANDLE) && !rejected.ready(), "Missing SMAA upload queue falls back");
                auto unreadable = usage;
                unreadable[1] = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                require(!rejected.initialize(physical, device, family, sources, format, unreadable,
                    width, height, 2, mode, queue) && !rejected.ready(), "Unsampleable second eye falls back");
                rejected.destroy();
            }
            require(aa.initialize(physical, device, family, sources, format, usage, width, height, 2, mode, queue), "Actual AA initialization");
            // Two command slots in flight, then reuse their completed targets.
            for (uint32_t round = 0; round < 3; ++round) {
            if (round != 0) {
                check(vkResetCommandPool(device, pool, 0));
                std::memset(pixels + 2 * imageBytes, 0, 4 * imageBytes);
            }
            if (round == 2) {
                // Replace the patterned source with a flat field: retained edge
                // or weight pixels from the previous round must not survive.
                begin(0);
                for (uint32_t eye = 0; eye < 2; ++eye) {
                    for (uint32_t i = 0; i < width * height; ++i) {
                        unsigned char *pixel = pixels + eye * imageBytes + i * 4;
                        pixel[format == VK_FORMAT_B8G8R8A8_UNORM ? (eye == 0 ? 2 : 0) : (eye == 0 ? 0 : 2)]
                            = eye == 0 ? 51 : 203;
                    }
                    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                    barrier.image = sources[eye]; barrier.subresourceRange = range;
                    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    vkCmdPipelineBarrier(commands[0], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);
                    VkBufferImageCopy copy{};
                    copy.bufferOffset = eye * imageBytes;
                    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {width, height, 1};
                    vkCmdCopyBufferToImage(commands[0], buffer, sources[eye], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    vkCmdPipelineBarrier(commands[0], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);
                }
                submit(0);
                check(vkQueueWaitIdle(queue));
            }
            for (uint32_t slot = 0; slot < 2; ++slot) {
                begin(slot + 1);
                for (uint32_t eye = 0; eye < 2; ++eye) {
                    VkImage output = aa.record(commands[slot + 1], slot, eye, sources[eye], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    VkBufferImageCopy copy{};
                    copy.bufferOffset = (2 + slot * 2 + eye) * imageBytes;
                    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {width, height, 1};
                    vkCmdCopyImageToBuffer(commands[slot + 1], output, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
                }
                VkMemoryBarrier readback{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                readback.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; readback.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                vkCmdPipelineBarrier(commands[slot + 1], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                    0, 1, &readback, 0, nullptr, 0, nullptr);
                submit(slot + 1);
            }
            check(vkQueueWaitIdle(queue));
            require(std::fwrite(pixels+2*imageBytes,1,4*imageBytes,output)==4*imageBytes,"Store actual GPU output bytes");
            for (uint32_t i=2*imageBytes; i<6*imageBytes; ++i) {
                outputChecksum ^= pixels[i]; outputChecksum *= 1099511628211ull;
            }
            uint32_t blended = 0, maxChange = 0, totalChange = 0;
            for (uint32_t slot = 0; slot < 2; ++slot) for (uint32_t eye = 0; eye < 2; ++eye) for (uint32_t i = 0; i < width * height; ++i) {
                const unsigned char *pixel = pixels + (2 + slot * 2 + eye) * imageBytes + i * 4;
                require(pixel[1] == 0 && pixel[eye == 0 ? 2 : 0] == 0 && pixel[3] == 255, "Eye isolation and BGRA/RGBA channels");
                unsigned char value = pixel[eye == 0 ? 0 : 2];
                const unsigned char original = pixels[eye * imageBytes + i * 4
                    + (format == VK_FORMAT_B8G8R8A8_UNORM ? (eye == 0 ? 2 : 0) : (eye == 0 ? 0 : 2))];
                const uint32_t change = std::abs(int(value) - int(original));
                maxChange = change > maxChange ? change : maxChange;
                totalChange += change;
                if (fixture == 0 || round == 2) require(value == original, "Flat color exact; no stale edge/weight pixels");
                else {
                    if (change > 0) ++blended;
                    // Output retains the original side of a binary feature's contrast.
                    // A half-strength blend may move at most halfway toward the
                    // opposite color (one UNORM rounding step at the midpoint).
                    if (mode == 1) require(change <= (fixture == 2 ? 64u : 128u), "Original feature contrast retained");
                    if (fixture == 2) require(value >= 80 && value <= 208, "Texture has no sharpening halos");
                    if (fixture == 3) require(change == 0, "Axis-aligned single-pixel detail stays exact");
                }
            }
            if (fixture != 0 && fixture != 3 && round != 2) require(blended > 0, "Edges still receive AA");
            require(std::memcmp(pixels + 2 * imageBytes, pixels + 4 * imageBytes, imageBytes * 2) == 0, "Independent fence slots agree");
            std::printf("PASS format=%d mode=%u fixture=%u round=%u changed=%u maxChange=%u meanChange=%.3f\n",
                format, mode, fixture, round, blended, maxChange, double(totalChange) / (4 * width * height));
            }
            aa.destroy();
            for (const Image &image : input) { vkDestroyImage(device, image.image, nullptr); vkFreeMemory(device, image.memory, nullptr); }
        }
        }
    }
    vkUnmapMemory(device, bufferMemory);
    vkDestroyBuffer(device, buffer, nullptr); vkFreeMemory(device, bufferMemory, nullptr);
    vkDestroyCommandPool(device, pool, nullptr); vkDestroyDevice(device, nullptr); vkDestroyInstance(instance, nullptr);
    require(std::fclose(output)==0,"Close actual GPU output fixture");
    std::printf("OUTPUT_CHECKSUM=%016llx\n", (unsigned long long)outputChecksum);
    std::puts("Actual spatial AA GPU smoke passed; Quest runtime NOT_VERIFIED");
}
