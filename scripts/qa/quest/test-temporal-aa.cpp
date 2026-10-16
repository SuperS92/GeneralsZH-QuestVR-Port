// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the actual Vulkan filter, without game logic or an XR runtime.
#include "QuestTemporalAA.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static void require(bool ok,const char *why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static void check(VkResult result){require(result==VK_SUCCESS,"Vulkan operation");}
#include "test-taa-math.h"
#include "taa-vulkan-contract.h"
struct Allocation {VkImage image{};VkDeviceMemory memory{};};
int main() {
    using namespace QuestXR;testMath();
    VkInstance instance{};VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    check(vkCreateInstance(&instanceInfo,nullptr,&instance));uint32_t count=0;
    check(vkEnumeratePhysicalDevices(instance,&count,nullptr));require(count>0,"GPU present");
    std::vector<VkPhysicalDevice> devices(count);check(vkEnumeratePhysicalDevices(instance,&count,devices.data()));
    auto physical=devices[0];VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(physical,&properties);
    std::printf("Host GPU: %s; isolated renderer evidence only\n",properties.deviceName);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);
    std::vector<VkQueueFamilyProperties> families(count);vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
    unsigned family=0;while(family<count&&!(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))++family;
    require(family<count,"Graphics queue");float priority=1;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queueInfo.queueFamilyIndex=family;queueInfo.queueCount=1;queueInfo.pQueuePriorities=&priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};deviceInfo.queueCreateInfoCount=1;deviceInfo.pQueueCreateInfos=&queueInfo;
    VkDevice device{};check(vkCreateDevice(physical,&deviceInfo,nullptr,&device));VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);
    TaaContract::initialize(device);
    VkPhysicalDeviceMemoryProperties memories{};vkGetPhysicalDeviceMemoryProperties(physical,&memories);
    auto allocate=[&](VkMemoryRequirements r,VkMemoryPropertyFlags flags){
        unsigned type=0;while(type<memories.memoryTypeCount&&(!(r.memoryTypeBits&(1u<<type))||(memories.memoryTypes[type].propertyFlags&flags)!=flags))++type;
        require(type<memories.memoryTypeCount,"Memory type");VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.memoryTypeIndex=type;a.allocationSize=r.size;
        VkDeviceMemory m{};check(vkAllocateMemory(device,&a,nullptr,&m));return m;
    };
    constexpr unsigned w=17,h=13,bytes=w*h*4;
    VkBuffer buffer{};VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};b.size=bytes*4;b.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check(vkCreateBuffer(device,&b,nullptr,&buffer));VkMemoryRequirements requirements{};vkGetBufferMemoryRequirements(device,buffer,&requirements);
    auto memory=allocate(requirements,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);check(vkBindBufferMemory(device,buffer,memory,0));
    void *mapped{};check(vkMapMemory(device,memory,0,VK_WHOLE_SIZE,0,&mapped));auto pixels=static_cast<unsigned char*>(mapped);
    VkCommandPool pool{};VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};poolInfo.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;poolInfo.queueFamilyIndex=family;
    check(vkCreateCommandPool(device,&poolInfo,nullptr,&pool));VkCommandBuffer cmd{};VkCommandBufferAllocateInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commands.commandPool=pool;commands.commandBufferCount=1;commands.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;check(vkAllocateCommandBuffers(device,&commands,&cmd));
    auto begin=[&]{check(vkResetCommandPool(device,pool,0));VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};check(vkBeginCommandBuffer(cmd,&info));};
    auto submit=[&]{check(vkEndCommandBuffer(cmd));VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};info.commandBufferCount=1;info.pCommandBuffers=&cmd;
        check(vkQueueSubmit(queue,1,&info,VK_NULL_HANDLE));check(vkQueueWaitIdle(queue));};
    auto barrier=[&](VkImage image,VkImageAspectFlags aspect,VkImageLayout old,VkImageLayout now){
        VkImageMemoryBarrier info{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};info.image=image;info.subresourceRange={aspect,0,1,0,1};
        info.srcQueueFamilyIndex=info.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;info.oldLayout=old;info.newLayout=now;
        info.srcAccessMask=old==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;info.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&info);
    };
    unsigned cases=0;
    for(auto quality:{TaaQuality::Full,TaaQuality::Lite})
    for(auto format:{VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_B8G8R8A8_UNORM})for(auto depthFormat:{VK_FORMAT_D32_SFLOAT,VK_FORMAT_D24_UNORM_S8_UINT})for(bool copyDepth:{false,true}) {
        VkFormatProperties support{};vkGetPhysicalDeviceFormatProperties(physical,depthFormat,&support);
        if(!(support.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))continue;
        std::vector<Allocation> images;
        auto make=[&](VkFormat f,VkImageUsageFlags usage){TaaImage value;value.layout=VK_IMAGE_LAYOUT_GENERAL;
            auto &i=value.info;i.imageType=VK_IMAGE_TYPE_2D;i.format=f;i.extent={w,h,1};i.mipLevels=i.arrayLayers=1;i.samples=VK_SAMPLE_COUNT_1_BIT;i.tiling=VK_IMAGE_TILING_OPTIMAL;i.usage=usage;
            value.layout=(usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                :(copyDepth?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            Allocation a;check(vkCreateImage(device,&i,nullptr,&a.image));value.image=a.image;VkMemoryRequirements r{};vkGetImageMemoryRequirements(device,a.image,&r);
            a.memory=allocate(r,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);check(vkBindImageMemory(device,a.image,a.memory,0));images.push_back(a);return value;
        };
        const auto colorUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        std::array<TaaImage,2> eyes{make(format,colorUsage),make(format,colorUsage)};
        const auto depth=make(depthFormat,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|(copyDepth?0:VK_IMAGE_USAGE_SAMPLED_BIT));
        const VkImageAspectFlags ds=VK_IMAGE_ASPECT_DEPTH_BIT|(depthFormat==VK_FORMAT_D32_SFLOAT?0:VK_IMAGE_ASPECT_STENCIL_BIT);
        begin();for(auto eye:eyes)barrier(eye.image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_UNDEFINED,eye.layout);
        barrier(depth.image,ds,VK_IMAGE_LAYOUT_UNDEFINED,depth.layout);submit();
        TemporalAA rejected;require(!rejected.initialize(physical,device,family,eyes,depth,static_cast<TaaQuality>(0)),"Disabled quality cannot initialize a filter");auto bad=eyes;bad[1]=bad[0];require(!rejected.initialize(physical,device,family,bad,depth),"Aliased eyes rejected");
        bad=eyes;bad[1].info.usage&=~VK_IMAGE_USAGE_SAMPLED_BIT;require(!rejected.initialize(physical,device,family,bad,depth),"Unsampleable eye rejected");
        auto badDepth=depth;badDepth.layout=VK_IMAGE_LAYOUT_UNDEFINED;require(!rejected.initialize(physical,device,family,eyes,badDepth),"Unknown depth layout rejected");
        TemporalAA taa;require(taa.initialize(physical,device,family,eyes,depth,quality),"Actual TAA initializes");require(taa.copiesDepth()==copyDepth,"Actual direct/copy depth selection");
        TaaCamera camera;camera.valid=true;camera.width=w;camera.height=h;
        for(unsigned i=0;i<4;++i)camera.view[i*5]=camera.projection[i*5]=1;
        std::vector<unsigned> center;
        for(unsigned frame=1;frame<=16;++frame) {
            const bool reset=frame==1||frame==10||frame==13||frame==15; if(frame==10||frame==13||frame==15)taa.invalidate();
            if(frame==14)camera.view[12]=2.0f/w; // one pixel right in the current image
            if(frame==15) {
                camera.view[12]=0;
                std::fill(camera.projection,camera.projection+16,0);
                camera.projection[0]=camera.projection[5]=1;
                camera.projection[10]=1.25f;camera.projection[11]=1;camera.projection[14]=-.25f;
            }
            if(frame==16)camera.view[12]=1.0f/w; // depth .25 shifts 2 pixels; depth .75 shifts 1
            for(unsigned eye=0;eye<2;++eye) {
                for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
                    auto p=pixels+eye*bytes+(y*w+x)*4;std::memset(p,0,4);
                    const unsigned channel=format==VK_FORMAT_B8G8R8A8_UNORM?(eye?0:2):(eye?2:0);
                    p[channel]=frame>=10?77:((x+y)%2?180:80)+(frame%2?20:-20);p[3]=unsigned((x*13+y*7+frame*11)%256);
                    if(frame>=13) {
                        const int worldX=int(x)-(frame==14?1:frame==16?(eye?1:2):0);
                        const int base=worldX==7&&y==6?140:worldX==8&&y==6?20:
                            (worldX==6&&y==6)||(worldX==7&&y==5)?255:0;
                        p[channel]=static_cast<unsigned char>(std::max(0,base-(frame==14||frame==16?20:0)));
                    }
                }
                begin();barrier(eyes[eye].image,VK_IMAGE_ASPECT_COLOR_BIT,eyes[eye].layout,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                VkBufferImageCopy copy{};copy.bufferOffset=eye*bytes;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={w,h,1};
                vkCmdCopyBufferToImage(cmd,buffer,eyes[eye].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
                barrier(eyes[eye].image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,eyes[eye].layout);
                barrier(depth.image,ds,depth.layout,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                const VkClearDepthStencilValue value{eye?.75f:.25f,73};const VkImageSubresourceRange range{ds,0,1,0,1};
                vkCmdClearDepthStencilImage(cmd,depth.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&value,1,&range);
                barrier(depth.image,ds,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,depth.layout);submit();
                require(taa.process(queue,eye,camera,frame,1000000000ull+frame*16000000ull),"Actual same-eye TAA submission");
            }
            // Readback is test-only; the production filter has no host wait here.
            check(vkQueueWaitIdle(queue));begin();
            for(unsigned eye=0;eye<2;++eye) {
                barrier(eyes[eye].image,VK_IMAGE_ASPECT_COLOR_BIT,eyes[eye].layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                VkBufferImageCopy copy{};copy.bufferOffset=(2+eye)*bytes;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={w,h,1};
                vkCmdCopyImageToBuffer(cmd,eyes[eye].image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
                barrier(eyes[eye].image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,eyes[eye].layout);
            }
            VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);submit();
            for(unsigned eye=0;eye<2;++eye)for(unsigned i=0;i<w*h;++i) {
                auto original=pixels+eye*bytes+i*4,actual=pixels+(2+eye)*bytes+i*4;
                require(actual[3]==original[3],"Original alpha exact for every pixel");
                if(reset)require(!std::memcmp(actual,original,4),"First/reset frame raw bytes exact");
                const unsigned channel=format==VK_FORMAT_B8G8R8A8_UNORM?(eye?0:2):(eye?2:0);
                for(unsigned c=0;c<3;++c)if(c!=channel)require(actual[c]<=1,"Separate eye history and color channels");
                if(frame>=10&&frame<=12)require(std::abs(int(actual[channel])-77)<=1,"Flat replacement rejects stale history");
                if((frame==14&&i==6*w+8)||(frame==16&&i==6*w+(eye?8:9))) {
                    // Independent two-frame oracle: correct previous pixel=140,
                    // current=120, wrong un-reprojected previous pixel=20.
                    // Quantized confidence gives output about 125, versus about
                    // 95 for wrong coordinates or 120 for a disabled/reset pass.
                    require(actual[channel]>=124&&actual[channel]<=127,"GPU camera/depth reprojection selects the correct same-eye earlier pixel");
                }
            }
            const unsigned channel=format==VK_FORMAT_B8G8R8A8_UNORM?2:0;
            if(frame==8||frame==9)center.push_back(pixels[2*bytes+((h/2)*w+w/2)*4+channel]);
            ++cases;
        }
        require(center.size()==2&&std::abs(int(center[0])-int(center[1]))<35,"Temporal accumulation reduces alternating texture variation");
        check(vkQueueWaitIdle(queue));taa.destroy();
        for(auto a:images){vkDestroyImage(device,a.image,nullptr);vkFreeMemory(device,a.memory,nullptr);}
    }
    vkDestroyCommandPool(device,pool,nullptr);vkUnmapMemory(device,memory);vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,memory,nullptr);
    vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);
    std::printf("PASS %u GPU frame cases; matrix oracle, reset, alpha, stereo, depth direct/copy, temporal attenuation. Quest bridge/performance pending.\n",cases);
}
