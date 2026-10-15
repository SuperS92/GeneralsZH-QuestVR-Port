// SPDX-License-Identifier: GPL-3.0-or-later
// Actual GPU transfer versus independent CPU pixels and full-canvas readback.
#include "VRDevice/QuestHudTransfer.h"
#include "VRDevice/QuestColorCopy.h"
#include "VRDevice/QuestTransferBatch.h"
#include "VRDevice/QuestShadowCoverage.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static void require(bool ok,const char *why) { if(!ok) { std::fprintf(stderr,"FAIL: %s\n",why); std::exit(1); } }
static void check(VkResult result) { require(result==VK_SUCCESS,"Vulkan operation"); }
struct Image { VkImage image={}; VkDeviceMemory memory={}; };
int main() {
    using namespace QuestXR;
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
        info.imageType=VK_IMAGE_TYPE_2D; info.format=format; info.extent={w,h,1}; info.mipLevels=1; info.arrayLayers=2;
        info.samples=VK_SAMPLE_COUNT_1_BIT; info.tiling=VK_IMAGE_TILING_OPTIMAL;
        info.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        check(vkCreateImage(device,&info,nullptr,&value.image)); VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device,value.image,&requirements); value.memory=allocate(requirements,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkBindImageMemory(device,value.image,value.memory,0)); images.push_back(value); return value.image;
    };
    constexpr unsigned width=17,height=513,rowBytes=width*4,bytes=rowBytes*height;
    VkBuffer buffer{}; VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size=bytes*18; bufferInfo.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
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
        b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,2};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
    };

    // Independent viewport containment oracle, including offset and invalid viewports.
    unsigned viewportCases=0;
    for(unsigned w=0;w<1024;++w) for(unsigned h:{0u,1u,513u,2640u})
        for(unsigned x:{0u,7u,16384u}) for(unsigned y:{0u,5u,16384u}) {
            ShadowCoverageRect rectangle{-1,-1,-1,-1};
            const bool valid=PlanShadowCoverage(x,y,w,h,2520,2640,rectangle);
            const bool oracle=w>0 && h>0 && uint64_t(x)+w<=2520 && uint64_t(y)+h<=2640;
            require(valid==oracle,"Viewport oracle");
            if(valid) require(rectangle.left==x && rectangle.top==y && rectangle.right==x+w && rectangle.bottom==y+h,"Exact viewport coverage without clipping the last row/column");
            else require(rectangle.left==-1 && rectangle.bottom==-1,"Invalid viewport leaves fallback untouched");
            ++viewportCases;
        }
    ShadowCoverageRect r{};
    require(!PlanShadowCoverage(UINT32_MAX,0,2,3,2520,2640,r),"Viewport overflow rejected");
    require(!PlanShadowCoverage(0,0,1,1,UINT32_MAX,2640,r),"Unbounded surface rejected");
    unsigned cases=0;
    const int crops[]={333,320,319,0,512,333,333,257};
    for(bool convert:{false,true}) {
        VkImage source[2][3]{},eyeOutput[2]{},menuFull[2]{},menuCompact[2]{};
        VkImage eyeReference[2]{},menuReferenceFull[2]{},menuReferenceCompact[2]{};
        for(unsigned slot=0;slot<2;++slot) {
            for(auto &src:source[slot]) src=image(width,height,convert?VK_FORMAT_B8G8R8A8_UNORM:VK_FORMAT_R8G8B8A8_UNORM);
            eyeOutput[slot]=image(width,height,VK_FORMAT_R8G8B8A8_UNORM);
            eyeReference[slot]=image(width,height,VK_FORMAT_R8G8B8A8_UNORM);
            menuFull[slot]=image(width,height,VK_FORMAT_R8G8B8A8_UNORM);
            menuCompact[slot]=image(width,height-height/2,VK_FORMAT_R8G8B8A8_UNORM);
            menuReferenceFull[slot]=image(width,height,VK_FORMAT_R8G8B8A8_UNORM);
            menuReferenceCompact[slot]=image(width,height-height/2,VK_FORMAT_R8G8B8A8_UNORM);
        }
        for(unsigned batch=0;batch<8;++batch) {
            const auto plan=PlanHudTransfer(height,crops[batch],batch!=5,batch!=6);
            const bool withMenu=batch!=7;
            for(unsigned slot=0;slot<2;++slot) {
                auto cmd=commands[slot];
                check(vkResetCommandBuffer(cmd,0)); VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                check(vkBeginCommandBuffer(cmd,&begin));
                for(unsigned channel=0;channel<3;++channel) {
                    auto *input=data+(slot*3+channel)*bytes;
                    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
                        unsigned p=(y*width+x)*4;
                        input[p]=(x*71+y*17+batch*23+slot*19+channel*31)&255;
                        input[p+1]=(x*13+y*97+batch*61+slot*7+channel*47)&255;
                        input[p+2]=(x*43+y*11+batch*17+slot*29+channel*83)&255;
                        input[p+3]=(x*37+y*53+batch*47+channel*5)&255;
                        if(convert) { const auto v=input[p];input[p]=input[p+2];input[p+2]=v; }
                    }
                    auto src=source[slot][channel];
                    barrier(cmd,src,batch?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
                    VkBufferImageCopy upload{};upload.bufferOffset=(slot*3+channel)*bytes;
                    upload.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};upload.imageExtent={width,height,1};
                    vkCmdCopyBufferToImage(cmd,buffer,src,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&upload);
                    barrier(cmd,src,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,
                        VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT);
                }
                ColorTransferBatch b;
                b.eyes={source[slot][0],source[slot][1]};b.layouts={VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL};
                b.eyeDestination=eyeOutput[slot];b.eyeWidth=width;b.eyeHeight=height;b.convertFormat=convert;
                b.copyMenu=withMenu;b.menu=source[slot][2];b.menuLayout=VK_IMAGE_LAYOUT_GENERAL;
                b.menuDestination=plan.compact?menuCompact[slot]:menuFull[slot];
                b.menuWidth=width;b.menuHeight=height;b.hud=plan;
                require(CanBatchColorTransfers(b),"Eligible distinct owned resources");
                auto bad=b;bad.eyes[1]=bad.eyes[0];require(!CanBatchColorTransfers(bad),"Aliased eye sources rejected");
                bad=b;bad.eyeWidth=0;require(!CanBatchColorTransfers(bad),"Zero extent rejected");
                bad=b;bad.layouts[0]=VK_IMAGE_LAYOUT_UNDEFINED;require(!CanBatchColorTransfers(bad),"Unknown source layout rejected");
                bad=b;bad.eyeDestination=bad.eyes[1];require(!CanBatchColorTransfers(bad),"Aliased destination rejected");
                bad=b;bad.eyes[0]=VK_NULL_HANDLE;require(!RecordColorTransferBatch(cmd,bad),"Invalid plan records no work");
                bad=b;bad.copyMenu=true;bad.menuDestination=bad.eyeDestination;require(!CanBatchColorTransfers(bad),"Aliased menu destination rejected");
                bad=b;bad.copyMenu=true;bad.hud.sourceTop=UINT32_MAX;require(!CanBatchColorTransfers(bad),"Invalid crop rejected");
                require(RecordColorTransferBatch(cmd,b),"Actual batched transfer helper");
                // Sequential reference uses the inherited per-image transition/copy/restore order.
                barrier(cmd,eyeReference[slot],VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
                for(unsigned eye=0;eye<2;++eye) {
                    auto src=source[slot][eye];
                    barrier(cmd,src,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
                    CopyEyeColorImage(cmd,src,eyeReference[slot],width,height,eye,convert);
                    barrier(cmd,src,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,
                        VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT);
                }
                VkImage refMenu=plan.compact?menuReferenceCompact[slot]:menuReferenceFull[slot];
                if(withMenu) {
                    barrier(cmd,refMenu,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
                    barrier(cmd,b.menu,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
                    CopyHudColorImage(cmd,b.menu,refMenu,width,plan,convert);
                    barrier(cmd,b.menu,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,
                        VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT);
                }
                for(unsigned reference=0;reference<2;++reference) {
                    auto output=reference?eyeReference[slot]:eyeOutput[slot];
                    barrier(cmd,output,reference?VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
                    for(unsigned channel=0;channel<(withMenu?3u:2u);++channel) {
                        auto target=channel==2?(reference?refMenu:b.menuDestination):output;
                        if(channel==2) barrier(cmd,target,reference?VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
                        VkBufferImageCopy read{};read.bufferOffset=((reference?12:6)+slot*3+channel)*bytes;
                        read.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,channel==2?0:channel,1};
                        read.imageExtent={width,channel==2?plan.destinationHeight:height,1};
                        vkCmdCopyImageToBuffer(cmd,target,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&read);
                    }
                }
                VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.buffer=buffer;host.size=VK_WHOLE_SIZE;
                host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
                host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
                vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
                check(vkEndCommandBuffer(cmd));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                submit.commandBufferCount=1;submit.pCommandBuffers=&commands[slot];check(vkQueueSubmit(queue,1,&submit,fences[slot]));
            }
            // Both independent slots are submitted before waiting or overwriting their input bytes.
            check(vkWaitForFences(device,2,fences,VK_TRUE,UINT64_MAX));
            for(unsigned slot=0;slot<2;++slot) {
                for(unsigned channel=0;channel<(withMenu?3u:2u);++channel) {
                    auto *actual=data+(6+slot*3+channel)*bytes;
                    auto *reference=data+(12+slot*3+channel)*bytes;
                    const unsigned rows=channel==2?plan.destinationHeight:height;
                    const unsigned top=channel==2?plan.sourceTop:0;
                    require(std::memcmp(actual,reference,rows*rowBytes)==0,"Grouped output equals sequential GPU reference, including alpha and guards");
                    for(unsigned p=0;p<rows*rowBytes;p+=4) {
                        auto *input=data+(slot*3+channel)*bytes+top*rowBytes+p;
                        require(actual[p]==input[convert?2:0] && actual[p+1]==input[1] && actual[p+2]==input[convert?0:2] && actual[p+3]==input[3],"Independent CPU color/alpha/eye/row oracle");
                    }
                }
                ++cases;
            }
            check(vkResetFences(device,2,fences));
        }
    }
    for(auto fence:fences) vkDestroyFence(device,fence,nullptr); vkDestroyCommandPool(device,pool,nullptr);
    vkUnmapMemory(device,bufferMemory); vkDestroyBuffer(device,buffer,nullptr); vkFreeMemory(device,bufferMemory,nullptr);
    for(auto value:images) { vkDestroyImage(device,value.image,nullptr); vkFreeMemory(device,value.memory,nullptr); }
    vkDestroyDevice(device,nullptr); vkDestroyInstance(instance,nullptr);
    std::printf("PASS %u exact GPU cases: grouped/sequential and CPU pixels; both eyes; BGRA/RGBA; full/compact/no HUD; changing content; two slots in flight; %u viewport oracles; invalid plans fail open\n",cases,viewportCases);
}
