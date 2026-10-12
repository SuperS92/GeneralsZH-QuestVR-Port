// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the actual raster transfer on a Vulkan GPU and compare every byte
// against nearest-format blits, with two distinct eyes, HUD and in-flight slots.
#include "QuestColorTransfer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static void require(bool ok, const char *why) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",why); std::exit(1); } }
static void check(VkResult result) { require(result == VK_SUCCESS,"Vulkan operation"); }
struct Image { VkImage image = {}; VkDeviceMemory memory = {}; };
int main() {
    VkInstance instance{}; VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    check(vkCreateInstance(&create,nullptr,&instance));
    uint32_t count=0; check(vkEnumeratePhysicalDevices(instance,&count,nullptr)); require(count>0,"GPU present");
    std::vector<VkPhysicalDevice> devices(count); check(vkEnumeratePhysicalDevices(instance,&count,devices.data()));
    VkPhysicalDevice physical=devices[0]; VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical,&properties); std::printf("Host GPU: %s\n",properties.deviceName);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);
    std::vector<VkQueueFamilyProperties> families(count); vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
    uint32_t family=0; while(family<count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
    require(family<count,"Graphics queue");
    float priority=1; VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex=family; queueInfo.queueCount=1; queueInfo.pQueuePriorities=&priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; deviceInfo.queueCreateInfoCount=1; deviceInfo.pQueueCreateInfos=&queueInfo;
    VkDevice device{}; check(vkCreateDevice(physical,&deviceInfo,nullptr,&device)); VkQueue queue{}; vkGetDeviceQueue(device,family,0,&queue);
    VkPhysicalDeviceMemoryProperties memories{}; vkGetPhysicalDeviceMemoryProperties(physical,&memories);
    auto allocate=[&](VkMemoryRequirements requirements,VkMemoryPropertyFlags flags) {
        uint32_t type=0; while(type<memories.memoryTypeCount && (!(requirements.memoryTypeBits&(1u<<type)) || (memories.memoryTypes[type].propertyFlags&flags)!=flags)) ++type;
        require(type<memories.memoryTypeCount,"Memory type");
        VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; info.allocationSize=requirements.size; info.memoryTypeIndex=type;
        VkDeviceMemory memory{}; check(vkAllocateMemory(device,&info,nullptr,&memory)); return memory;
    };
    std::vector<Image> images;
    auto image=[&](uint32_t w,uint32_t h,uint32_t layers,VkFormat format,VkImageUsageFlags usage) {
        Image value{}; VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType=VK_IMAGE_TYPE_2D; info.format=format; info.extent={w,h,1}; info.mipLevels=1; info.arrayLayers=layers;
        info.samples=VK_SAMPLE_COUNT_1_BIT; info.tiling=VK_IMAGE_TILING_OPTIMAL; info.usage=usage;
        check(vkCreateImage(device,&info,nullptr,&value.image)); VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device,value.image,&requirements); value.memory=allocate(requirements,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkBindImageMemory(device,value.image,value.memory,0)); images.push_back(value); return value.image;
    };
    const uint32_t widths[]={17,17,23}, heights[]={13,13,19};
    const uint32_t bytes[]={17*13*4,17*13*4,23*19*4};
    const uint32_t offsets[]={0,bytes[0],bytes[0]+bytes[1]}, total=bytes[0]+bytes[1]+bytes[2];
    std::array<QuestXR::ColorTransfer::Source,3> sources{};
    std::array<VkImage,2> stereo{},hud{};
    for(unsigned slot=0;slot<2;++slot) {
        stereo[slot]=image(17,13,2,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        hud[slot]=image(23,19,1,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    }
    for(unsigned channel=0;channel<3;++channel) {
        auto &s=sources[channel]; s.width=widths[channel]; s.height=heights[channel]; s.destinationLayer=channel<2?channel:0;
        s.format=VK_FORMAT_B8G8R8A8_UNORM; s.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        s.image=image(s.width,s.height,1,s.format,s.usage);
        for(unsigned slot=0;slot<2;++slot) s.destinations.push_back(channel<2?stereo[slot]:hud[slot]);
    }
    QuestXR::ColorTransfer transfer;
    require(!transfer.initialize(physical,device,VK_FORMAT_R8G8B8A8_SRGB,sources),"sRGB falls back before allocating");
    auto invalid=sources; invalid[1].usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    require(!transfer.initialize(physical,device,VK_FORMAT_R8G8B8A8_UNORM,invalid),"Unsampleable source falls back");
    invalid=sources; invalid[2].destinations.clear();
    require(!transfer.initialize(physical,device,VK_FORMAT_R8G8B8A8_UNORM,invalid),"Missing destinations fall back");
    require(transfer.initialize(physical,device,VK_FORMAT_R8G8B8A8_UNORM,sources),"Actual transfer initialization");
    require(!transfer.matches(0,sources[1].image,0),"Eye identity gate");
    require(!transfer.matches(3,sources[0].image,0),"Channel bounds gate");
    require(!transfer.matches(0,sources[0].image,2),"Slot bounds gate");
    VkBuffer buffer{}; VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size=total*6; bufferInfo.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check(vkCreateBuffer(device,&bufferInfo,nullptr,&buffer)); VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device,buffer,&requirements); VkDeviceMemory bufferMemory=allocate(requirements,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkBindBufferMemory(device,buffer,bufferMemory,0)); void *mapped{}; check(vkMapMemory(device,bufferMemory,0,VK_WHOLE_SIZE,0,&mapped));
    auto *data=static_cast<unsigned char*>(mapped);
    VkCommandPool pool{}; VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; poolInfo.queueFamilyIndex=family; poolInfo.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    check(vkCreateCommandPool(device,&poolInfo,nullptr,&pool)); VkCommandBuffer commands[2]{};
    VkCommandBufferAllocateInfo commandsInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; commandsInfo.commandPool=pool; commandsInfo.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; commandsInfo.commandBufferCount=2;
    check(vkAllocateCommandBuffers(device,&commandsInfo,commands)); VkFence fences[2]{};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; for(auto &fence:fences) check(vkCreateFence(device,&fenceInfo,nullptr,&fence));
    auto barrier=[&](VkCommandBuffer command,VkImage target,uint32_t layer,VkImageLayout oldLayout,VkImageLayout newLayout,VkAccessFlags before,VkAccessFlags after) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.image=target; b.oldLayout=oldLayout; b.newLayout=newLayout; b.srcAccessMask=before; b.dstAccessMask=after;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,layer,1};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
    };
    for(unsigned batch=0;batch<8;++batch) {
        for(unsigned slot=0;slot<2;++slot) {
            for(unsigned channel=0;channel<3;++channel) for(unsigned y=0;y<heights[channel];++y) for(unsigned x=0;x<widths[channel];++x) {
                const unsigned p=slot*total+offsets[channel]+(y*widths[channel]+x)*4;
                data[p]=(x*71+y*17+channel*53+batch*23+slot*19)&255;
                data[p+1]=(x*13+y*97+channel*31+batch*61+slot*7)&255;
                data[p+2]=(x*43+y*11+channel*113+batch*17+slot*29)&255;
                data[p+3]=(x*37+y*53+channel*3+batch*47)&255;
            }
            check(vkResetCommandBuffer(commands[slot],0)); VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; check(vkBeginCommandBuffer(commands[slot],&begin));
            for(unsigned channel=0;channel<3;++channel) {
                const auto &s=sources[channel]; VkImage target=s.destinations[slot];
                barrier(commands[slot],s.image,0,batch==0&&slot==0?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
                VkBufferImageCopy region{}; region.bufferOffset=slot*total+offsets[channel]; region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; region.imageExtent={s.width,s.height,1};
                vkCmdCopyBufferToImage(commands[slot],buffer,s.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
                barrier(commands[slot],s.image,0,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
                transfer.record(commands[slot],channel,slot,VK_IMAGE_LAYOUT_GENERAL);
                barrier(commands[slot],target,s.destinationLayer,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
                region.bufferOffset=total*2+slot*total+offsets[channel]; region.imageSubresource.baseArrayLayer=s.destinationLayer;
                vkCmdCopyImageToBuffer(commands[slot],target,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&region);
                // Reference blit overwrites the same layer only after its raster readback.
                barrier(commands[slot],s.image,0,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT);
                barrier(commands[slot],target,s.destinationLayer,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
                VkImageBlit blit{}; blit.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,s.destinationLayer,1};
                blit.srcOffsets[1]=blit.dstOffsets[1]={int32_t(s.width),int32_t(s.height),1};
                vkCmdBlitImage(commands[slot],s.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,target,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_NEAREST);
                barrier(commands[slot],target,s.destinationLayer,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
                region.bufferOffset=total*4+slot*total+offsets[channel]; vkCmdCopyImageToBuffer(commands[slot],target,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&region);
                barrier(commands[slot],s.image,0,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
            }
            VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER}; host.buffer=buffer; host.size=VK_WHOLE_SIZE; host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            vkCmdPipelineBarrier(commands[slot],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
            check(vkEndCommandBuffer(commands[slot])); VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount=1; submit.pCommandBuffers=&commands[slot];
            check(vkQueueSubmit(queue,1,&submit,fences[slot]));
        }
        check(vkWaitForFences(device,2,fences,VK_TRUE,UINT64_MAX));
        require(std::memcmp(data+2*total,data+4*total,total*2)==0,"Raster pixels equal reference GPU blit");
        for(unsigned p=0;p<total*2;p+=4) {
            require(data[2*total+p]==data[p+2] && data[2*total+p+1]==data[p+1] && data[2*total+p+2]==data[p] && data[2*total+p+3]==data[p+3],"Exact BGRA to RGBA including alpha and orientation");
        }
        check(vkResetFences(device,2,fences));
    }
    transfer.destroy(); transfer.destroy();
    require(!transfer.matches(0,sources[0].image,0),"Destroyed helper falls back");
    for(auto fence:fences) vkDestroyFence(device,fence,nullptr); vkDestroyCommandPool(device,pool,nullptr);
    vkUnmapMemory(device,bufferMemory); vkDestroyBuffer(device,buffer,nullptr); vkFreeMemory(device,bufferMemory,nullptr);
    for(auto value:images) { vkDestroyImage(device,value.image,nullptr); vkFreeMemory(device,value.memory,nullptr); }
    vkDestroyDevice(device,nullptr); vkDestroyInstance(instance,nullptr);
    std::printf("PASS exact raster/reference/CPU pixels: 48 channel transfers, two slots in flight; fallback, reuse and teardown\n");
}
