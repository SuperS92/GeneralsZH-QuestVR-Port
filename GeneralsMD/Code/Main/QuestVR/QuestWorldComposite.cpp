// SPDX-License-Identifier: GPL-3.0-or-later
#include "QuestWorldComposite.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include "QuestWorldCompositeShaders.spv.h"
namespace QuestXR {
namespace {
bool rgba(VkFormat f){return f==VK_FORMAT_R8G8B8A8_UNORM||f==VK_FORMAT_B8G8R8A8_UNORM;}
VkImageView view(VkDevice d,VkImage image,VkFormat format,unsigned layer){
 VkImageViewCreateInfo i{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};i.image=image;i.viewType=VK_IMAGE_VIEW_TYPE_2D;i.format=format;
 i.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,layer,1};VkImageView v{};return vkCreateImageView(d,&i,nullptr,&v)==VK_SUCCESS?v:VK_NULL_HANDLE;
}
VkImageMemoryBarrier barrier(const CompositeImage&i,bool restore){
 VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=i.image;
 b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
 b.oldLayout=restore?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:i.layout;b.newLayout=restore?i.layout:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
 b.srcAccessMask=restore?VK_ACCESS_SHADER_READ_BIT:VK_ACCESS_MEMORY_WRITE_BIT;
 b.dstAccessMask=restore?(VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT):VK_ACCESS_SHADER_READ_BIT;return b;
}
}
bool WorldComposite::initialize(VkPhysicalDevice physical,VkDevice device,uint32_t w,uint32_t h,VkFormat format,
 const std::array<CompositeImage,2>&color,const std::array<CompositeImage,2>&mask,const std::vector<VkImage>&destinations){
 if(m_device||!physical||!device||!w||!h||w>8192||h>8192||!rgba(format)||destinations.empty())return false;
 for(unsigned e=0;e<2;++e)for(unsigned k=0;k<2;++k){const auto&i=k?mask[e]:color[e];
  if(!i.image||i.width!=w||i.height!=h||!(i.usage&VK_IMAGE_USAGE_SAMPLED_BIT)
   ||(k?i.format!=VK_FORMAT_R8_UNORM:!rgba(i.format))||i.layout==VK_IMAGE_LAYOUT_UNDEFINED)return false;
  VkFormatProperties p{};vkGetPhysicalDeviceFormatProperties(physical,i.format,&p);if(!(p.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))return false;
 }
 VkFormatProperties fp{};vkGetPhysicalDeviceFormatProperties(physical,format,&fp);if(!(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))return false;
 m_device=device;m_width=w;m_height=h;m_color=color;m_mask=mask;
 auto fail=[&](){destroy();return false;};
 std::array<VkDescriptorSetLayoutBinding,2> bindings{};for(unsigned i=0;i<2;++i)bindings[i]={i,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
 VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};sl.bindingCount=2;sl.pBindings=bindings.data();
 if(vkCreateDescriptorSetLayout(device,&sl,nullptr,&m_setLayout)!=VK_SUCCESS)return fail();
 VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&m_setLayout;
 if(vkCreatePipelineLayout(device,&pl,nullptr,&m_layout)!=VK_SUCCESS)return fail();
 VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};si.magFilter=si.minFilter=VK_FILTER_NEAREST;si.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
 si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
 if(vkCreateSampler(device,&si,nullptr,&m_sampler)!=VK_SUCCESS)return fail();
 VkAttachmentDescription at{};at.format=format;at.samples=VK_SAMPLE_COUNT_1_BIT;at.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;at.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
 at.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;at.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;at.initialLayout=at.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
 VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&ref;
 VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};ri.attachmentCount=1;ri.pAttachments=&at;ri.subpassCount=1;ri.pSubpasses=&sub;
 if(vkCreateRenderPass(device,&ri,nullptr,&m_pass)!=VK_SUCCESS)return fail();
 VkShaderModule modules[2]{};VkShaderModuleCreateInfo sh{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
 sh.codeSize=sizeof(kWorldCompositeVertex);sh.pCode=kWorldCompositeVertex;if(vkCreateShaderModule(device,&sh,nullptr,&modules[0])!=VK_SUCCESS)return fail();
 sh.codeSize=sizeof(kWorldCompositeFragment);sh.pCode=kWorldCompositeFragment;if(vkCreateShaderModule(device,&sh,nullptr,&modules[1])!=VK_SUCCESS){vkDestroyShaderModule(device,modules[0],nullptr);return fail();}
 VkPipelineShaderStageCreateInfo stages[2]={{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
 for(unsigned i=0;i<2;++i){stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;stages[i].module=modules[i];stages[i].pName="main";}
 VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
 VkViewport vp{0,0,float(w),float(h),0,1};VkRect2D sc{{0,0},{w,h}};VkPipelineViewportStateCreateInfo vs{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vs.viewportCount=vs.scissorCount=1;vs.pViewports=&vp;vs.pScissors=&sc;
 VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.lineWidth=1;
 VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
 VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};cb.attachmentCount=1;cb.pAttachments=&blend;
 VkGraphicsPipelineCreateInfo gi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};gi.stageCount=2;gi.pStages=stages;gi.pVertexInputState=&vi;gi.pInputAssemblyState=&ia;gi.pViewportState=&vs;gi.pRasterizationState=&raster;gi.pMultisampleState=&ms;gi.pColorBlendState=&cb;gi.layout=m_layout;gi.renderPass=m_pass;
 const auto result=vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&gi,nullptr,&m_pipeline);for(auto module:modules)vkDestroyShaderModule(device,module,nullptr);if(result!=VK_SUCCESS)return fail();
 VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,4};VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pi.maxSets=2;pi.poolSizeCount=1;pi.pPoolSizes=&size;
 if(vkCreateDescriptorPool(device,&pi,nullptr,&m_pool)!=VK_SUCCESS)return fail();
 VkDescriptorSetLayout layouts[2]={m_setLayout,m_setLayout};VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=m_pool;ai.descriptorSetCount=2;ai.pSetLayouts=layouts;
 if(vkAllocateDescriptorSets(device,&ai,m_sets.data())!=VK_SUCCESS)return fail();
 for(unsigned e=0;e<2;++e)for(unsigned k=0;k<2;++k){const auto&i=k?mask[e]:color[e];auto&v=m_sourceViews[e*2+k];v=view(device,i.image,i.format,0);if(!v)return fail();
  VkDescriptorImageInfo image{m_sampler,v,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=m_sets[e];write.dstBinding=k;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.pImageInfo=&image;vkUpdateDescriptorSets(device,1,&write,0,nullptr);
 }
 for(auto image:destinations){if(!image)return fail();m_destinations.emplace_back();auto&target=m_destinations.back();target.image=image;
  for(unsigned e=0;e<2;++e){target.views[e]=view(device,image,format,e);if(!target.views[e])return fail();VkFramebufferCreateInfo f{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};f.renderPass=m_pass;f.attachmentCount=1;f.pAttachments=&target.views[e];f.width=w;f.height=h;f.layers=1;if(vkCreateFramebuffer(device,&f,nullptr,&target.frames[e])!=VK_SUCCESS)return fail();}
 }
 return true;
}
bool WorldComposite::matches(const std::array<CompositeImage,2>&color,const std::array<CompositeImage,2>&mask)const{
 if(!m_pipeline)return false;
 for(unsigned e=0;e<2;++e)for(unsigned k=0;k<2;++k){const auto&a=k?mask[e]:color[e];const auto&b=k?m_mask[e]:m_color[e];
  if(a.image!=b.image||a.width!=m_width||a.height!=m_height||a.format!=b.format||!(a.usage&VK_IMAGE_USAGE_SAMPLED_BIT)||a.layout==VK_IMAGE_LAYOUT_UNDEFINED)return false;
 }return true;
}
bool WorldComposite::record(VkCommandBuffer command,unsigned index,const std::array<CompositeImage,2>&color,const std::array<CompositeImage,2>&mask)const{
 if(!command||index>=m_destinations.size()||!matches(color,mask))return false;
 std::array<VkImageMemoryBarrier,4> sources;for(unsigned e=0;e<2;++e){sources[e*2]=barrier(color[e],false);sources[e*2+1]=barrier(mask[e],false);}
 vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,4,sources.data());
 VkImageMemoryBarrier dst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};dst.image=m_destinations[index].image;dst.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;dst.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
 dst.srcQueueFamilyIndex=dst.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;dst.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dst.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,2};
 vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,1,&dst);
 for(unsigned e=0;e<2;++e){VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=m_pass;begin.framebuffer=m_destinations[index].frames[e];begin.renderArea={{0,0},{m_width,m_height}};
  vkCmdBeginRenderPass(command,&begin,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,m_pipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,m_layout,0,1,&m_sets[e],0,nullptr);vkCmdDraw(command,3,1,0,0);vkCmdEndRenderPass(command);
 }
 for(unsigned e=0;e<2;++e){sources[e*2]=barrier(color[e],true);sources[e*2+1]=barrier(mask[e],true);}
 vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,4,sources.data());
 return true;
}
void WorldComposite::destroy(){
 if(!m_device)return;
 for(auto&t:m_destinations)for(unsigned e=0;e<2;++e){if(t.frames[e])vkDestroyFramebuffer(m_device,t.frames[e],nullptr);if(t.views[e])vkDestroyImageView(m_device,t.views[e],nullptr);}
 m_destinations.clear();if(m_pipeline)vkDestroyPipeline(m_device,m_pipeline,nullptr);if(m_pool)vkDestroyDescriptorPool(m_device,m_pool,nullptr);
 for(auto v:m_sourceViews)if(v)vkDestroyImageView(m_device,v,nullptr);
 if(m_sampler)vkDestroySampler(m_device,m_sampler,nullptr);if(m_layout)vkDestroyPipelineLayout(m_device,m_layout,nullptr);if(m_setLayout)vkDestroyDescriptorSetLayout(m_device,m_setLayout,nullptr);if(m_pass)vkDestroyRenderPass(m_device,m_pass,nullptr);
 m_device={};m_pipeline={};m_pool={};m_sourceViews={};m_sampler={};m_layout={};m_setLayout={};m_pass={};m_sets={};m_color={};m_mask={};m_width=m_height=0;
}
}
#endif
