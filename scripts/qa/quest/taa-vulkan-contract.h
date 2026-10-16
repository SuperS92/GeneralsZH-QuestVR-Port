// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only interception of actual image/barrier/descriptor calls. This narrow
// usage/layout check is not a substitute for Khronos validation layers.
#pragma once
#include <map>
namespace TaaContract {
inline std::map<VkImage,bool> externalImages;
inline unsigned preciseBarriers=0;
inline PFN_vkCreateImage createImage=nullptr;
inline PFN_vkCreateImageView createView=nullptr;
inline PFN_vkCmdPipelineBarrier barrier=nullptr;
inline PFN_vkUpdateDescriptorSets descriptors=nullptr;
inline PFN_vkCmdCopyImage copyImage=nullptr;
inline PFN_vkCreateGraphicsPipelines createPipelines=nullptr;
inline PFN_vkCmdBindPipeline bindPipeline=nullptr;
inline PFN_vkCmdBindDescriptorSets bindSets=nullptr;
inline PFN_vkCmdDraw draw=nullptr;
inline std::map<VkImage,VkImageUsageFlags> usage;
inline std::map<VkImage,VkImageLayout> layouts;
inline std::map<VkImage,VkExtent3D> extents;
inline std::map<VkImageView,VkImage> views;
inline std::map<VkPipeline,bool> resolvePipeline;
inline std::map<VkDescriptorSet,std::array<VkDescriptorImageInfo,4>> sets;
inline std::map<VkPipeline,unsigned> resolveBindings;
inline std::map<VkCommandBuffer,VkPipeline> boundPipeline;
inline std::map<VkCommandBuffer,VkDescriptorSet> boundSet;
inline uint64_t barrierCalls=0,copyCalls=0,copyPixels=0;
inline uint64_t depthCopies=0,colorCopies=0,depthPixels=0;
inline void initialize(VkDevice device) {
    createImage=reinterpret_cast<PFN_vkCreateImage>(vkGetDeviceProcAddr(device,"vkCreateImage"));
    createView=reinterpret_cast<PFN_vkCreateImageView>(vkGetDeviceProcAddr(device,"vkCreateImageView"));
    barrier=reinterpret_cast<PFN_vkCmdPipelineBarrier>(vkGetDeviceProcAddr(device,"vkCmdPipelineBarrier"));
    descriptors=reinterpret_cast<PFN_vkUpdateDescriptorSets>(vkGetDeviceProcAddr(device,"vkUpdateDescriptorSets"));
    copyImage=reinterpret_cast<PFN_vkCmdCopyImage>(vkGetDeviceProcAddr(device,"vkCmdCopyImage"));
    createPipelines=reinterpret_cast<PFN_vkCreateGraphicsPipelines>(vkGetDeviceProcAddr(device,"vkCreateGraphicsPipelines"));
    bindPipeline=reinterpret_cast<PFN_vkCmdBindPipeline>(vkGetDeviceProcAddr(device,"vkCmdBindPipeline"));
    bindSets=reinterpret_cast<PFN_vkCmdBindDescriptorSets>(vkGetDeviceProcAddr(device,"vkCmdBindDescriptorSets"));
    draw=reinterpret_cast<PFN_vkCmdDraw>(vkGetDeviceProcAddr(device,"vkCmdDraw"));
    require(createImage&&createView&&barrier&&descriptors&&copyImage,"Real Vulkan contract entry points");
    require(createPipelines&&bindPipeline&&bindSets&&draw,"Real Vulkan draw contract entry points");
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
    if(result==VK_SUCCESS){TaaContract::usage[*image]=info->usage;TaaContract::extents[*image]=info->extent;TaaContract::layouts[*image]=info->initialLayout;}return result;
}
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice device,const VkImageViewCreateInfo *info,const VkAllocationCallbacks *allocator,VkImageView *view) {
    const auto result=TaaContract::createView(device,info,allocator,view);
    if(result==VK_SUCCESS)TaaContract::views[*view]=info->image;return result;
}
extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer command,VkPipelineStageFlags source,VkPipelineStageFlags destination,VkDependencyFlags flags,
    uint32_t memoryCount,const VkMemoryBarrier *memory,uint32_t bufferCount,const VkBufferMemoryBarrier *buffers,uint32_t imageCount,const VkImageMemoryBarrier *images) {
    ++TaaContract::barrierCalls;
    if(imageCount&&!(source&VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)&&!(destination&VK_PIPELINE_STAGE_ALL_COMMANDS_BIT))++TaaContract::preciseBarriers;
    const auto supports=[](VkPipelineStageFlags stage,VkAccessFlags access) {
        if(stage&VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)return true;
        if((access&(VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT))&&!(stage&VK_PIPELINE_STAGE_TRANSFER_BIT))return false;
        if((access&VK_ACCESS_SHADER_READ_BIT)&&!(stage&(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)))return false;
        if((access&(VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT))&&!(stage&VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT))return false;
        if((access&(VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT))
            &&!(stage&(VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT)))return false;
        return !(access&(VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT));
    };
    for(unsigned i=0;i<imageCount;++i){
        const auto &b=images[i];
        const bool externalEdge=(b.srcAccessMask&(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT))
            ||(b.dstAccessMask&(VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT))
            ||((b.dstAccessMask&VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)&&(b.dstAccessMask&VK_ACCESS_TRANSFER_READ_BIT));
        if(TaaContract::externalImages.count(b.image)&&externalEdge) {
            if(!(source&VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)||!(destination&VK_PIPELINE_STAGE_ALL_COMMANDS_BIT))
                std::fprintf(stderr,"Boundary stages %x -> %x access %x -> %x layout %u -> %u\n",source,destination,b.srcAccessMask,b.dstAccessMask,b.oldLayout,b.newLayout);
            require((source&VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)&&(destination&VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),"DXVK producer/consumer boundaries remain conservative");
        }
        require(supports(source,b.srcAccessMask)&&supports(destination,b.dstAccessMask),"Barrier access masks supported by execution stages");TaaContract::checkLayout(b.image,b.oldLayout);TaaContract::checkLayout(b.image,b.newLayout);
        require(b.oldLayout==VK_IMAGE_LAYOUT_UNDEFINED||TaaContract::layouts.at(b.image)==b.oldLayout,"Barrier old layout matches recorded state");
        TaaContract::layouts[b.image]=b.newLayout;
    }
    TaaContract::barrier(command,source,destination,flags,memoryCount,memory,bufferCount,buffers,imageCount,images);
}
extern "C" VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice device,uint32_t count,const VkWriteDescriptorSet *writes,uint32_t copies,const VkCopyDescriptorSet *copy) {
    for(unsigned i=0;i<count;++i)if(writes[i].descriptorType==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        for(unsigned d=0;d<writes[i].descriptorCount;++d) {
            const auto &image=writes[i].pImageInfo[d];const auto view=TaaContract::views.find(image.imageView);
            require(view!=TaaContract::views.end(),"Descriptor uses a known actual view");TaaContract::checkLayout(view->second,image.imageLayout);
            require(writes[i].dstBinding+d<4,"Fixture descriptor binding");
            TaaContract::sets[writes[i].dstSet][writes[i].dstBinding+d]=image;
        }
    TaaContract::descriptors(device,count,writes,copies,copy);
}
extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(VkCommandBuffer command,VkImage source,VkImageLayout sourceLayout,
    VkImage destination,VkImageLayout destinationLayout,uint32_t count,const VkImageCopy *regions) {
    ++TaaContract::copyCalls;
    TaaContract::checkLayout(source,sourceLayout);TaaContract::checkLayout(destination,destinationLayout);
    for(unsigned i=0;i<count;++i) {
        const auto &r=regions[i];const auto src=TaaContract::extents.at(source),dst=TaaContract::extents.at(destination);
        require(r.srcOffset.x>=0&&r.srcOffset.y>=0&&r.dstOffset.x>=0&&r.dstOffset.y>=0,"Copy offsets nonnegative");
        require(unsigned(r.srcOffset.x)+r.extent.width<=src.width&&unsigned(r.srcOffset.y)+r.extent.height<=src.height
            &&unsigned(r.dstOffset.x)+r.extent.width<=dst.width&&unsigned(r.dstOffset.y)+r.extent.height<=dst.height,"Depth copy inside actual image bounds");
        // A cropped copy must include the p+1 gather footprint of every resolved pixel.
        const bool depth=(r.srcSubresource.aspectMask&VK_IMAGE_ASPECT_DEPTH_BIT)!=0;
        if(depth) {++TaaContract::depthCopies;TaaContract::depthPixels+=uint64_t(r.extent.width)*r.extent.height;}
        else ++TaaContract::colorCopies;
        if(depth&&(unsigned(r.srcOffset.x)>0||unsigned(r.srcOffset.y)>0)) {
            const unsigned mx=src.width*15/100,my=src.height*15/100;
            require(r.srcOffset.x==int(mx)&&r.srcOffset.y==int(my)&&r.dstOffset.x==int(mx)&&r.dstOffset.y==int(my),"Cropped depth preserves original pixel coordinates");
            require(unsigned(r.srcOffset.x)+r.extent.width>=std::min(src.width,src.width-mx+1)
                &&unsigned(r.srcOffset.y)+r.extent.height>=std::min(src.height,src.height-my+1),"Cropped depth covers complete gather border");
        }
        TaaContract::copyPixels+=uint64_t(r.extent.width)*r.extent.height;
    }
    TaaContract::copyImage(command,source,sourceLayout,destination,destinationLayout,count,regions);
}
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice device,VkPipelineCache cache,uint32_t count,
    const VkGraphicsPipelineCreateInfo *infos,const VkAllocationCallbacks *allocator,VkPipeline *pipelines) {
    const auto result=TaaContract::createPipelines(device,cache,count,infos,allocator,pipelines);
    if(result==VK_SUCCESS)for(unsigned i=0;i<count;++i) {
        // Fixture resolve writes confidence alpha; apply writes only RGB.
        TaaContract::resolvePipeline[pipelines[i]]=(infos[i].pColorBlendState->pAttachments[0].colorWriteMask&VK_COLOR_COMPONENT_A_BIT)!=0;
        TaaContract::resolveBindings[pipelines[i]]=infos[i].pColorBlendState->attachmentCount==2?4:3;
        if(infos[i].pColorBlendState->attachmentCount==2)
            require(!std::memcmp(&infos[i].pColorBlendState->pAttachments[0],&infos[i].pColorBlendState->pAttachments[1],sizeof(VkPipelineColorBlendAttachmentState)),"MRT blend states identical without independentBlend feature");
    }
    return result;
}
extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer command,VkPipelineBindPoint point,VkPipeline pipeline) {
    TaaContract::boundPipeline[command]=pipeline;TaaContract::bindPipeline(command,point,pipeline);
}
extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer command,VkPipelineBindPoint point,VkPipelineLayout layout,
    uint32_t first,uint32_t count,const VkDescriptorSet *sets,uint32_t dynamicCount,const uint32_t *offsets) {
    require(first==0&&count==1,"Fixture binds one descriptor set");TaaContract::boundSet[command]=sets[0];
    TaaContract::bindSets(command,point,layout,first,count,sets,dynamicCount,offsets);
}
extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer command,uint32_t vertices,uint32_t instances,uint32_t firstVertex,uint32_t firstInstance) {
    const auto &set=TaaContract::sets.at(TaaContract::boundSet.at(command));
    const bool resolve=TaaContract::resolvePipeline.at(TaaContract::boundPipeline.at(command));
    const unsigned count=resolve?TaaContract::resolveBindings.at(TaaContract::boundPipeline.at(command)):3;
    for(unsigned binding=resolve?0:2;binding<count;++binding) {
        const auto &descriptor=set[binding];const auto image=TaaContract::views.at(descriptor.imageView);
        require(TaaContract::layouts.at(image)==descriptor.imageLayout,"Every statically used descriptor has the promised layout even on reset");
    }
    TaaContract::draw(command,vertices,instances,firstVertex,firstInstance);
}
