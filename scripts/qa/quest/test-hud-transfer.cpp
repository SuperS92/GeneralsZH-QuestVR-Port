// SPDX-License-Identifier: GPL-3.0-or-later
// Actual GPU transfer versus independent CPU pixels and full-canvas readback.
#include "VRDevice/QuestHudTransfer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static void require(bool ok,const char *why) { if(!ok) { std::fprintf(stderr,"FAIL: %s\n",why); std::exit(1); } }
static void check(VkResult result) { require(result==VK_SUCCESS,"Vulkan operation"); }
struct Image { VkImage image={}; VkDeviceMemory memory={}; };
int main() {
    using namespace QuestXR;
    // Independent rectangle oracle, including invalid crops and unavailable compact resources.
    for(uint32_t height=0;height<1026;++height) for(int crop=-2;crop<int(height)+3;++crop)
        for(bool hud:{false,true}) for(bool available:{false,true}) {
            const auto plan=PlanHudTransfer(height,crop,hud,available);
            if(!height) { require(!plan.destinationHeight,"Empty source rejected"); continue; }
            const unsigned clipped=crop<0?0:crop>=int(height)?height-1:unsigned(crop);
            const bool expected=hud&&available&&height>=128&&clipped>=height/2+64;
            require(plan.compact==expected,"Selection oracle");
            require(plan.visibleHeight==height-clipped,"Visible height preserved");
            require(plan.sourceTop+plan.subImageTop==clipped,"Original source cursor coordinate preserved");
            require(plan.sourceTop+plan.destinationHeight==height,"Entire output defined, source bottom aligned");
            require(plan.subImageTop+plan.visibleHeight==plan.destinationHeight,"Visible bottom remains fixed");
            if(expected) require(plan.subImageTop>=64,"Compositor guard retained");
            for(bool tight:{false,true}) {
                const auto copy=PlanHudCopy(plan,tight);
                require(copy.sourceTop+copy.height==height,"Copy and clear cover the source bottom");
                require(copy.destinationTop+copy.height==plan.destinationHeight,"Destination bottom remains fixed");
                require(copy.sourceTop-copy.destinationTop==plan.sourceTop,"Coordinate mapping unchanged");
                require(copy.destinationTop<=plan.subImageTop,"All visible rows copied");
                if(plan.compact)require(plan.subImageTop-copy.destinationTop>=64,"64 row filtering guard fully defined");
                if(!tight||!plan.compact)require(!copy.destinationTop&&copy.sourceTop==plan.sourceTop,"Reference or full fallback unchanged");
            }
        }
    require(!PlanHudTransfer(UINT32_MAX,0,true,true).destinationHeight,"Oversized source rejected");
    VkInstance instance{}; VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    check(vkCreateInstance(&create,nullptr,&instance)); uint32_t count=0;
    check(vkEnumeratePhysicalDevices(instance,&count,nullptr)); require(count>0,"GPU present");
    std::vector<VkPhysicalDevice> devices(count); check(vkEnumeratePhysicalDevices(instance,&count,devices.data()));
    VkPhysicalDevice physical=devices[0]; VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical,&properties); std::printf("Host GPU: %s\n",properties.deviceName);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);
    std::vector<VkQueueFamilyProperties> families(count); vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
    uint32_t family=0; while(family<count && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)) ++family;
    require(family<count,"Graphics queue"); float priority=1;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex=family; queueInfo.queueCount=1; queueInfo.pQueuePriorities=&priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; deviceInfo.queueCreateInfoCount=1; deviceInfo.pQueueCreateInfos=&queueInfo;
    VkDevice device{}; check(vkCreateDevice(physical,&deviceInfo,nullptr,&device)); VkQueue queue{}; vkGetDeviceQueue(device,family,0,&queue);
    VkPhysicalDeviceMemoryProperties memories{}; vkGetPhysicalDeviceMemoryProperties(physical,&memories);
    auto allocate=[&](VkMemoryRequirements requirements,VkMemoryPropertyFlags flags) {
        uint32_t type=0; while(type<memories.memoryTypeCount && (!(requirements.memoryTypeBits&(1u<<type)) || (memories.memoryTypes[type].propertyFlags&flags)!=flags)) ++type;
        require(type<memories.memoryTypeCount,"Memory type"); VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        info.allocationSize=requirements.size; info.memoryTypeIndex=type; VkDeviceMemory memory{};
        check(vkAllocateMemory(device,&info,nullptr,&memory)); return memory;
    };
    std::vector<Image> images;
    auto image=[&](uint32_t w,uint32_t h,VkFormat format) {
        Image value{}; VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType=VK_IMAGE_TYPE_2D; info.format=format; info.extent={w,h,1}; info.mipLevels=info.arrayLayers=1;
        info.samples=VK_SAMPLE_COUNT_1_BIT; info.tiling=VK_IMAGE_TILING_OPTIMAL;
        info.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        check(vkCreateImage(device,&info,nullptr,&value.image)); VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device,value.image,&requirements); value.memory=allocate(requirements,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkBindImageMemory(device,value.image,value.memory,0)); images.push_back(value); return value.image;
    };
    constexpr unsigned width=17,height=513,rowBytes=width*4,bytes=rowBytes*height;
    VkBuffer buffer{}; VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size=bytes*6; bufferInfo.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check(vkCreateBuffer(device,&bufferInfo,nullptr,&buffer)); VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device,buffer,&requirements);
    VkDeviceMemory bufferMemory=allocate(requirements,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkBindBufferMemory(device,buffer,bufferMemory,0)); void *mapped{};
    check(vkMapMemory(device,bufferMemory,0,VK_WHOLE_SIZE,0,&mapped)); auto *data=static_cast<unsigned char*>(mapped);
    VkCommandPool pool{}; VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex=family; poolInfo.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    check(vkCreateCommandPool(device,&poolInfo,nullptr,&pool)); VkCommandBuffer commands[2]{};
    VkCommandBufferAllocateInfo commandsInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandsInfo.commandPool=pool; commandsInfo.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; commandsInfo.commandBufferCount=2;
    check(vkAllocateCommandBuffers(device,&commandsInfo,commands)); VkFence fences[2]{};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; for(auto &fence:fences) check(vkCreateFence(device,&fenceInfo,nullptr,&fence));
    auto barrier=[&](VkCommandBuffer command,VkImage target,VkImageLayout oldLayout,VkImageLayout newLayout,VkAccessFlags before,VkAccessFlags after) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.image=target;
        b.oldLayout=oldLayout; b.newLayout=newLayout; b.srcAccessMask=before; b.dstAccessMask=after;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
    };
    const int crops[]={333,320,319,0,512,333,333,257};
    for(bool tight:{false,true}) for(bool convert:{false,true}) {
        VkImage source=image(width,height,convert?VK_FORMAT_B8G8R8A8_UNORM:VK_FORMAT_R8G8B8A8_UNORM);
        VkImage full[2]{},compact[2]{},reference[2]{};
        for(unsigned slot=0;slot<2;++slot) {
            full[slot]=image(width,height,VK_FORMAT_R8G8B8A8_UNORM);
            compact[slot]=image(width,height-height/2,VK_FORMAT_R8G8B8A8_UNORM);
            reference[slot]=image(width,height,VK_FORMAT_R8G8B8A8_UNORM);
        }
        for(unsigned batch=0;batch<8;++batch) {
            const auto plan=PlanHudTransfer(height,crops[batch],batch!=5,batch!=6);
            const auto copy=PlanHudCopy(plan,tight);
            for(unsigned slot=0;slot<2;++slot) {
                for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
                    const unsigned p=slot*bytes+(y*width+x)*4;
                    data[p]=(x*71+y*17+batch*23+slot*19)&255;
                    data[p+1]=(x*13+y*97+batch*61+slot*7)&255;
                    data[p+2]=(x*43+y*11+batch*17+slot*29)&255;
                    data[p+3]=(x*37+y*53+batch*47)&255;
                }
                check(vkResetCommandBuffer(commands[slot],0)); VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                check(vkBeginCommandBuffer(commands[slot],&begin));
                barrier(commands[slot],source,batch==0&&slot==0?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
                VkBufferImageCopy region{}; region.bufferOffset=slot*bytes;
                region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; region.imageExtent={width,height,1};
                vkCmdCopyBufferToImage(commands[slot],buffer,source,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
                barrier(commands[slot],source,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
                VkImage target=plan.compact?compact[slot]:full[slot];
                barrier(commands[slot],target,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
                CopyHudColorImage(commands[slot],source,target,width,plan,convert,tight);
                barrier(commands[slot],target,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
                region.bufferOffset=2*bytes+slot*bytes; region.imageExtent.height=plan.destinationHeight;
                vkCmdCopyImageToBuffer(commands[slot],target,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&region);
                barrier(commands[slot],reference[slot],VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
                // Full-canvas reference always writes all source rows.
                CopyHudColorImage(commands[slot],source,reference[slot],width,PlanHudTransfer(height,0,false,false),convert);
                barrier(commands[slot],reference[slot],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
                region.bufferOffset=4*bytes+slot*bytes; region.imageExtent.height=height;
                vkCmdCopyImageToBuffer(commands[slot],reference[slot],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&region);
                VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER}; host.buffer=buffer; host.size=VK_WHOLE_SIZE;
                host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
                host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
                vkCmdPipelineBarrier(commands[slot],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
                check(vkEndCommandBuffer(commands[slot])); VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                submit.commandBufferCount=1; submit.pCommandBuffers=&commands[slot]; check(vkQueueSubmit(queue,1,&submit,fences[slot]));
            }
            check(vkWaitForFences(device,2,fences,VK_TRUE,UINT64_MAX));
            for(unsigned slot=0;slot<2;++slot) {
                auto *actual=data+2*bytes+slot*bytes;
                auto *expected=data+4*bytes+slot*bytes+plan.sourceTop*rowBytes;
                require(std::memcmp(actual+copy.destinationTop*rowBytes,expected+copy.destinationTop*rowBytes,copy.height*rowBytes)==0,"All updated compact pixels, including guard, equal full-canvas output");
                for(unsigned p=copy.destinationTop*rowBytes;p<plan.destinationHeight*rowBytes;p+=4) {
                    auto *input=data+slot*bytes+plan.sourceTop*rowBytes+p;
                    require(actual[p]==input[convert?2:0] && actual[p+1]==input[1] && actual[p+2]==input[convert?0:2] && actual[p+3]==input[3],"Exact pixels and alpha with unchanged scale/orientation");
                }
                require(std::memcmp(actual+plan.subImageTop*rowBytes,data+4*bytes+slot*bytes+unsigned(crops[batch])*rowBytes,plan.visibleHeight*rowBytes)==0,"Submitted subimage corresponds to original crop");
            }
            check(vkResetFences(device,2,fences));
        }
    }
    for(auto fence:fences) vkDestroyFence(device,fence,nullptr); vkDestroyCommandPool(device,pool,nullptr);
    vkUnmapMemory(device,bufferMemory); vkDestroyBuffer(device,buffer,nullptr); vkFreeMemory(device,bufferMemory,nullptr);
    for(auto value:images) { vkDestroyImage(device,value.image,nullptr); vkFreeMemory(device,value.memory,nullptr); }
    vkDestroyDevice(device,nullptr); vkDestroyInstance(instance,nullptr);
    std::puts("PASS rectangle oracle and 64 GPU cases: blit/copy, compact/full, guard, odd dimensions, expanded/front-end/unavailable fallback, two in-flight slots and changed content");
}
