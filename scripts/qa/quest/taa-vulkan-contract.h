// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only interception of actual image/barrier/descriptor calls. This narrow
// usage/layout check is not a substitute for Khronos validation layers.
#pragma once
#include <map>
namespace TaaContract {
inline PFN_vkCreateImage createImage=nullptr;
inline PFN_vkCreateImageView createView=nullptr;
inline PFN_vkCmdPipelineBarrier barrier=nullptr;
inline PFN_vkUpdateDescriptorSets descriptors=nullptr;
inline std::map<VkImage,VkImageUsageFlags> usage;
inline std::map<VkImageView,VkImage> views;
inline void initialize(VkDevice device) {
    createImage=reinterpret_cast<PFN_vkCreateImage>(vkGetDeviceProcAddr(device,"vkCreateImage"));
    createView=reinterpret_cast<PFN_vkCreateImageView>(vkGetDeviceProcAddr(device,"vkCreateImageView"));
    barrier=reinterpret_cast<PFN_vkCmdPipelineBarrier>(vkGetDeviceProcAddr(device,"vkCmdPipelineBarrier"));
    descriptors=reinterpret_cast<PFN_vkUpdateDescriptorSets>(vkGetDeviceProcAddr(device,"vkUpdateDescriptorSets"));
    require(createImage&&createView&&barrier&&descriptors,"Real Vulkan contract entry points");
}
inline void checkLayout(VkImage image,VkImageLayout layout) {
    const auto found=usage.find(image);require(found!=usage.end(),"Contract knows actual image creation usage");
    const auto flags=found->second;
    switch(layout) {
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
        require(flags&VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,"VUID 01209/01210: depth attachment layout needs attachment usage");break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        require(flags&(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT),"VUID 01211: sampled layout usage");break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        require(flags&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,"VUID 01208: color attachment usage");break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        require(flags&VK_IMAGE_USAGE_TRANSFER_SRC_BIT,"VUID 01212: transfer source usage");break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        require(flags&VK_IMAGE_USAGE_TRANSFER_DST_BIT,"VUID 01213: transfer destination usage");break;
    default:break;
    }
}
}
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice device,const VkImageCreateInfo *info,const VkAllocationCallbacks *allocator,VkImage *image) {
    const auto result=TaaContract::createImage(device,info,allocator,image);
    if(result==VK_SUCCESS)TaaContract::usage[*image]=info->usage;return result;
}
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice device,const VkImageViewCreateInfo *info,const VkAllocationCallbacks *allocator,VkImageView *view) {
    const auto result=TaaContract::createView(device,info,allocator,view);
    if(result==VK_SUCCESS)TaaContract::views[*view]=info->image;return result;
}
extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer command,VkPipelineStageFlags source,VkPipelineStageFlags destination,VkDependencyFlags flags,
    uint32_t memoryCount,const VkMemoryBarrier *memory,uint32_t bufferCount,const VkBufferMemoryBarrier *buffers,uint32_t imageCount,const VkImageMemoryBarrier *images) {
    for(unsigned i=0;i<imageCount;++i){TaaContract::checkLayout(images[i].image,images[i].oldLayout);TaaContract::checkLayout(images[i].image,images[i].newLayout);}
    TaaContract::barrier(command,source,destination,flags,memoryCount,memory,bufferCount,buffers,imageCount,images);
}
extern "C" VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice device,uint32_t count,const VkWriteDescriptorSet *writes,uint32_t copies,const VkCopyDescriptorSet *copy) {
    for(unsigned i=0;i<count;++i)if(writes[i].descriptorType==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        for(unsigned d=0;d<writes[i].descriptorCount;++d) {
            const auto &image=writes[i].pImageInfo[d];const auto view=TaaContract::views.find(image.imageView);
            require(view!=TaaContract::views.end(),"Descriptor uses a known actual view");TaaContract::checkLayout(view->second,image.imageLayout);
        }
    TaaContract::descriptors(device,count,writes,copies,copy);
}
