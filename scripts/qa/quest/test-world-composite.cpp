// SPDX-License-Identifier: GPL-3.0-or-later
#include "QuestWorldComposite.h"
#include <vulkan/vulkan.h>
#include <array>
#include <vector>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
static void need(bool x,const char*m){if(!x){std::fprintf(stderr,"FAIL %s\n",m);std::exit(1);}}
static void ck(VkResult x){need(x==VK_SUCCESS,"Vulkan call");}
int main(int argc,char**argv){need(argc==2,"shader directory");
 VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};ai.apiVersion=VK_API_VERSION_1_1;
 VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ii.pApplicationInfo=&ai;VkInstance in;ck(vkCreateInstance(&ii,nullptr,&in));
 uint32_t n=0;ck(vkEnumeratePhysicalDevices(in,&n,nullptr));need(n,"GPU");std::vector<VkPhysicalDevice> ps(n);ck(vkEnumeratePhysicalDevices(in,&n,ps.data()));auto p=ps[0];
 VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(p,&props);std::printf("Host GPU: %s\n",props.deviceName);
 vkGetPhysicalDeviceQueueFamilyProperties(p,&n,nullptr);std::vector<VkQueueFamilyProperties> qs(n);vkGetPhysicalDeviceQueueFamilyProperties(p,&n,qs.data());unsigned family=0;while(!(qs.at(family).queueFlags&VK_QUEUE_GRAPHICS_BIT))++family;
 VkPhysicalDeviceFeatures features{};vkGetPhysicalDeviceFeatures(p,&features);need(features.independentBlend,"independent attachment blend");features={};features.independentBlend=VK_TRUE;
 float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
 VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;di.pEnabledFeatures=&features;VkDevice d;ck(vkCreateDevice(p,&di,nullptr,&d));VkQueue q;vkGetDeviceQueue(d,family,0,&q);
 VkPhysicalDeviceMemoryProperties mp;vkGetPhysicalDeviceMemoryProperties(p,&mp);
 auto alloc=[&](VkMemoryRequirements req,VkMemoryPropertyFlags flags){unsigned t=0;while(t<mp.memoryTypeCount&&(!(req.memoryTypeBits&(1u<<t))||(mp.memoryTypes[t].propertyFlags&flags)!=flags))++t;need(t<mp.memoryTypeCount,"memory type");VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=req.size;a.memoryTypeIndex=t;VkDeviceMemory m;ck(vkAllocateMemory(d,&a,nullptr,&m));return m;};

 constexpr unsigned W=17,H=13,P=W*H;unsigned cases=0;
 VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pci.queueFamilyIndex=family;pci.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;VkCommandPool pool;ck(vkCreateCommandPool(d,&pci,nullptr,&pool));
 VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cai.commandPool=pool;cai.commandBufferCount=1;cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;VkCommandBuffer cmd;ck(vkAllocateCommandBuffers(d,&cai,&cmd));
 VkBuffer buffer;VkDeviceMemory bufferMemory;void *mapped;
 VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=8192;bi.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;ck(vkCreateBuffer(d,&bi,nullptr,&buffer));VkMemoryRequirements br;vkGetBufferMemoryRequirements(d,buffer,&br);bufferMemory=alloc(br,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);ck(vkBindBufferMemory(d,buffer,bufferMemory,0));ck(vkMapMemory(d,bufferMemory,0,8192,0,&mapped));
 for(bool bgraSource:{false,true})for(bool bgraDestination:{false,true}){
  const VkFormat srcFormat=bgraSource?VK_FORMAT_B8G8R8A8_UNORM:VK_FORMAT_R8G8B8A8_UNORM;
  const VkFormat dstFormat=bgraDestination?VK_FORMAT_B8G8R8A8_UNORM:VK_FORMAT_R8G8B8A8_UNORM;
  std::array<VkImage,6> images{};std::array<VkDeviceMemory,6> memory{};
  std::array<QuestXR::CompositeImage,2> colors{},masks{};
  for(unsigned i=0;i<6;++i){VkImageCreateInfo im{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};im.imageType=VK_IMAGE_TYPE_2D;im.format=i<2?srcFormat:i<4?VK_FORMAT_R8_UNORM:dstFormat;im.extent={W,H,1};im.mipLevels=1;im.arrayLayers=i<4?1:2;im.samples=VK_SAMPLE_COUNT_1_BIT;im.tiling=VK_IMAGE_TILING_OPTIMAL;im.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;ck(vkCreateImage(d,&im,nullptr,&images[i]));VkMemoryRequirements req;vkGetImageMemoryRequirements(d,images[i],&req);memory[i]=alloc(req,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);ck(vkBindImageMemory(d,images[i],memory[i],0));if(i<2)colors[i]={images[i],VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,im.format,W,H,im.usage};else if(i<4)masks[i-2]={images[i],VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,im.format,W,H,im.usage};}
  QuestXR::WorldComposite composite;
  auto bad=colors;bad[0].usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;need(!composite.initialize(p,d,W,H,dstFormat,bad,masks,{images[4],images[5]}),"unsupported sampled usage fallback");
  need(composite.initialize(p,d,W,H,dstFormat,colors,masks,{images[4],images[5]}),"production composite initialization");
  need(!composite.record(VK_NULL_HANDLE,0,colors,masks)&&!composite.record(cmd,2,colors,masks),"invalid record rejects without commands");
  for(unsigned frame=0;frame<32;++frame){auto data=static_cast<unsigned char*>(mapped);
   for(unsigned e=0;e<2;++e)for(unsigned i=0;i<P;++i){unsigned offset=e*P*4+i*4;data[offset]=(i*7+e*39+frame)%256;data[offset+1]=(i*13+e*3+frame*11)%256;data[offset+2]=(i*19+e*97+frame*2)%256;data[offset+3]=13+e;data[P*8+e*((P+3)&~3u)+i]=(i*5+frame*17+e*111)%256;}
   ck(vkResetCommandBuffer(cmd,0));VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};ck(vkBeginCommandBuffer(cmd,&begin));
   for(unsigned i=0;i<4;++i){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=images[i];b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.oldLayout=frame?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.srcAccessMask=frame?VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT:0;b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);VkBufferImageCopy copy{};copy.bufferOffset=i<2?i*P*4:P*8+(i-2)*((P+3)&~3u);copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={W,H,1};vkCmdCopyBufferToImage(cmd,buffer,images[i],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);}
   need(composite.record(cmd,frame%2,colors,masks),"production two-eye handoff recorded");
   VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=images[4+frame%2];b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,2};b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
   VkBufferImageCopy copy{};copy.bufferOffset=4096;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,2};copy.imageExtent={W,H,1};vkCmdCopyImageToBuffer(cmd,images[4+frame%2],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
   VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);ck(vkEndCommandBuffer(cmd));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;ck(vkQueueSubmit(q,1,&submit,VK_NULL_HANDLE));ck(vkQueueWaitIdle(q));
   for(unsigned e=0;e<2;++e)for(unsigned i=0;i<P;++i){unsigned src=e*P*4+i*4,dst=4096+e*P*4+i*4;need(data[dst]==data[src+(bgraSource!=bgraDestination?2:0)]&&data[dst+1]==data[src+1]&&data[dst+2]==data[src+(bgraSource!=bgraDestination?0:2)],"RGB byte exact including format conversion");need(data[dst+3]==data[P*8+e*((P+3)&~3u)+i],"same-eye R8 coverage, no stale opposite-eye mask");}++cases;
  }
  composite.destroy();composite.destroy();for(unsigned i=0;i<6;++i){vkDestroyImage(d,images[i],nullptr);vkFreeMemory(d,memory[i],nullptr);}
 }
 vkUnmapMemory(d,bufferMemory);vkDestroyBuffer(d,buffer,nullptr);vkFreeMemory(d,bufferMemory,nullptr);vkDestroyCommandPool(d,pool,nullptr);vkDestroyDevice(d,nullptr);vkDestroyInstance(in,nullptr);
 std::printf("PASS %u actual GPU world composite cases: paired R8 masks, RGBA/BGRA conversions, full odd-size canvas, reusable destinations, resource cleanup and fallback\n",cases);
}
