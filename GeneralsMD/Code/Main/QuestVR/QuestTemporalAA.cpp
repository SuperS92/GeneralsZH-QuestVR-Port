// SPDX-License-Identifier: GPL-3.0-or-later
#include "QuestTemporalAA.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include "QuestTaaShaders.spv.h"
#include <android/log.h>
#include <algorithm>
#include <vector>
namespace QuestXR {
namespace {
// GeneralsX @tweak Codex 16/10/2026 Match pinned RDR Lite defaults. Test 10's
// 8/4/2 cap rejected more history at subpixel motion than the 10/5/1 reference.
struct Push {float reproject[16]{};uint32_t info[4]{};float tune[4]{10,5,1,0};};
static_assert(sizeof(Push)==96,"Shader push layout");
const char *qualityName(TaaQuality quality) {
    return quality==TaaQuality::Lite?"lite":quality==TaaQuality::Medium?"medium":"full";
}
// RDR reference: TaaPass.cpp, 423788baa4959d21d5057243ad31f5c8b40f9f28.
// GeneralsX @performance Codex 16/10/2026 Group independent image dependencies without weakening their scopes.
struct BarrierBatch {
    std::array<VkImageMemoryBarrier,8> images{};
    unsigned count=0;
    void add(VkImage image,VkImageAspectFlags aspects,VkImageLayout before,VkImageLayout after,VkAccessFlags src,VkAccessFlags dst) {
        auto &b=images[count++];b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask=src;b.dstAccessMask=dst;b.oldLayout=before;b.newLayout=after;b.image=image;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={aspects,0,1,0,1};
    }
    void flush(VkCommandBuffer command) {
        if(count)vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0,0,nullptr,0,nullptr,count,images.data());
        count=0;
    }
};
VkImageAspectFlags depthAspects(VkFormat format) {
    return VK_IMAGE_ASPECT_DEPTH_BIT | (format==VK_FORMAT_D32_SFLOAT?0:VK_IMAGE_ASPECT_STENCIL_BIT);
}
bool depthFormat(VkFormat format) {
    return format==VK_FORMAT_D24_UNORM_S8_UINT || format==VK_FORMAT_D32_SFLOAT_S8_UINT || format==VK_FORMAT_D32_SFLOAT;
}
void barrier(VkCommandBuffer command,VkImage image,VkImageAspectFlags aspects,
    VkImageLayout before,VkImageLayout after,VkAccessFlags src,VkAccessFlags dst) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask=src;b.dstAccessMask=dst;b.oldLayout=before;b.newLayout=after;b.image=image;
    b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange={aspects,0,1,0,1};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0,0,nullptr,0,nullptr,1,&b);
}
}
VkImageView TemporalAA::makeView(VkImage image,VkFormat format,VkImageAspectFlags aspect) {
    VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    v.image=image;v.viewType=VK_IMAGE_VIEW_TYPE_2D;v.format=format;
    v.subresourceRange={aspect,0,1,0,1};VkImageView view{};
    return vkCreateImageView(m_device,&v,nullptr,&view)==VK_SUCCESS?view:VK_NULL_HANDLE;
}
bool TemporalAA::makeImage(VkFormat format,VkImageUsageFlags usage,VkImageAspectFlags aspect,Image &image) {
    VkImageCreateInfo i{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    i.imageType=VK_IMAGE_TYPE_2D;i.format=format;i.extent={m_width,m_height,1};
    i.mipLevels=i.arrayLayers=1;i.samples=VK_SAMPLE_COUNT_1_BIT;i.tiling=VK_IMAGE_TILING_OPTIMAL;i.usage=usage;
    if(vkCreateImage(m_device,&i,nullptr,&image.image)!=VK_SUCCESS)return false;
    VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(m_device,image.image,&requirements);
    unsigned type=0;
    while(type<m_memory.memoryTypeCount && (!(requirements.memoryTypeBits&(1u<<type))
        || !(m_memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)))++type;
    if(type==m_memory.memoryTypeCount)return false;
    VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.memoryTypeIndex=type;a.allocationSize=requirements.size;
    if(vkAllocateMemory(m_device,&a,nullptr,&image.memory)!=VK_SUCCESS)return false;
    m_bytes+=requirements.size;
    if(vkBindImageMemory(m_device,image.image,image.memory,0)!=VK_SUCCESS)return false;
    image.view=makeView(image.image,format,aspect);return image.view!=VK_NULL_HANDLE;
}
VkRenderPass TemporalAA::makePass(VkFormat format,bool load) {
    VkAttachmentDescription a{};a.format=format;a.samples=VK_SAMPLE_COUNT_1_BIT;
    a.loadOp=load?VK_ATTACHMENT_LOAD_OP_LOAD:VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    a.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.initialLayout=a.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&ref;
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount=1;info.pAttachments=&a;info.subpassCount=1;info.pSubpasses=&sub;VkRenderPass pass{};
    return vkCreateRenderPass(m_device,&info,nullptr,&pass)==VK_SUCCESS?pass:VK_NULL_HANDLE;
}
VkPipeline TemporalAA::makePipeline(VkRenderPass pass,const uint32_t *fragment,size_t bytes,bool alpha) {
    VkShaderModule modules[2]{};
    VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader.codeSize=sizeof(kTaaVertex);shader.pCode=kTaaVertex;
    if(vkCreateShaderModule(m_device,&shader,nullptr,&modules[0])!=VK_SUCCESS)return VK_NULL_HANDLE;
    shader.codeSize=bytes;shader.pCode=fragment;
    if(vkCreateShaderModule(m_device,&shader,nullptr,&modules[1])!=VK_SUCCESS) {
        vkDestroyShaderModule(m_device,modules[0],nullptr);return VK_NULL_HANDLE;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    for(unsigned i=0;i<2;++i){stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;stages[i].module=modules[i];stages[i].pName="main";}
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewport.viewportCount=viewport.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState mask{};mask.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT
        |(alpha?VK_COLOR_COMPONENT_A_BIT:0);
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=1;blend.pAttachments=&mask;
    VkDynamicState dynamicStates[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dynamicStates;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount=2;info.pStages=stages;info.pVertexInputState=&vertex;info.pInputAssemblyState=&assembly;
    info.pViewportState=&viewport;info.pRasterizationState=&raster;info.pMultisampleState=&ms;info.pDepthStencilState=&ds;
    info.pColorBlendState=&blend;info.pDynamicState=&dynamic;info.layout=m_pipelineLayout;info.renderPass=pass;
    VkPipeline pipeline{};const auto status=vkCreateGraphicsPipelines(m_device,VK_NULL_HANDLE,1,&info,nullptr,&pipeline);
    for(auto module:modules)vkDestroyShaderModule(m_device,module,nullptr);
    return status==VK_SUCCESS?pipeline:VK_NULL_HANDLE;
}
bool TemporalAA::matches(const std::array<TaaImage,2> &eyes,const TaaImage &depth) const {
    return eyes[0].image==m_sources[0].image && eyes[1].image==m_sources[1].image && depth.image==m_depth.image
        && eyes[0].layout==m_sources[0].layout && eyes[1].layout==m_sources[1].layout && depth.layout==m_depth.layout;
}
bool TemporalAA::initialize(VkPhysicalDevice physical,VkDevice device,uint32_t family,
    const std::array<TaaImage,2> &eyes,const TaaImage &depth,TaaQuality quality) {
    if(m_device || (quality!=TaaQuality::Lite && quality!=TaaQuality::Medium && quality!=TaaQuality::Full))return false;
    m_quality=quality;
    m_device=device;m_physical=physical;m_sources=eyes;m_depth=depth;
    const auto fail=[&]{destroy();return false;};
    if(!physical||!device)return fail();
    const auto format=eyes[0].info.format;
    m_width=eyes[0].info.extent.width;m_height=eyes[0].info.extent.height;
    if(!m_width||!m_height||m_width>16384||m_height>16384
        ||(format!=VK_FORMAT_R8G8B8A8_UNORM && format!=VK_FORMAT_B8G8R8A8_UNORM))return fail();
    for(const auto &eye:eyes)if(!eye.image || eye.info.format!=format || eye.info.extent.width!=m_width || eye.info.extent.height!=m_height
        ||eye.info.samples!=VK_SAMPLE_COUNT_1_BIT||eye.info.arrayLayers!=1||eye.info.mipLevels!=1||eye.info.imageType!=VK_IMAGE_TYPE_2D
        ||(eye.info.usage&(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))!=(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
        ||eye.info.extent.depth!=1||eye.info.tiling!=VK_IMAGE_TILING_OPTIMAL
        ||(eye.layout!=VK_IMAGE_LAYOUT_GENERAL && eye.layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            && eye.layout!=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL))return fail();
    if(eyes[0].image==eyes[1].image || !depth.image || depth.image==eyes[0].image || depth.image==eyes[1].image
        ||!depthFormat(depth.info.format)||depth.info.samples!=VK_SAMPLE_COUNT_1_BIT
        ||depth.info.extent.width!=m_width||depth.info.extent.height!=m_height||depth.info.arrayLayers!=1||depth.info.mipLevels!=1
        ||depth.info.imageType!=VK_IMAGE_TYPE_2D||depth.info.extent.depth!=1||depth.info.tiling!=VK_IMAGE_TILING_OPTIMAL
        ||!(depth.info.usage&VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
        ||(depth.layout!=VK_IMAGE_LAYOUT_GENERAL && depth.layout!=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            && depth.layout!=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL))return fail();
    uint32_t count=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);
    std::vector<VkQueueFamilyProperties> families(count);vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
    if(family>=count||!(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))return fail();
    VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(physical,&properties);
    m_timestampPeriod=properties.limits.timestampPeriod;m_bits=families[family].timestampValidBits;
    VkFormatProperties colorFeatures{},depthFeatures{};vkGetPhysicalDeviceFormatProperties(physical,format,&colorFeatures);
    vkGetPhysicalDeviceFormatProperties(physical,depth.info.format,&depthFeatures);
    const VkFormatFeatureFlags needed=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if((colorFeatures.optimalTilingFeatures&needed)!=needed || !(depthFeatures.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))return fail();
    m_copyDepth=!(depth.info.usage&VK_IMAGE_USAGE_SAMPLED_BIT);
    if(m_copyDepth && !(depth.info.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT))return fail();
    vkGetPhysicalDeviceMemoryProperties(physical,&m_memory);
    VkDescriptorSetLayoutBinding bindings[3]{};
    for(unsigned i=0;i<3;++i){bindings[i].binding=i;bindings[i].descriptorCount=1;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;bindings[i].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;}
    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};setInfo.bindingCount=3;setInfo.pBindings=bindings;
    if(vkCreateDescriptorSetLayout(device,&setInfo,nullptr,&m_setLayout)!=VK_SUCCESS)return fail();
    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(Push)};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&m_setLayout;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;
    if(vkCreatePipelineLayout(device,&layout,nullptr,&m_pipelineLayout)!=VK_SUCCESS)return fail();
    m_historyPass=makePass(format,false);m_applyPass=makePass(format,true);
    if(!m_historyPass||!m_applyPass)return fail();
    // GeneralsX @performance Codex 16/10/2026 Only create the selected quality pipeline; Full shader stays unchanged.
    m_resolve=m_quality==TaaQuality::Lite
        ?makePipeline(m_historyPass,kTaaResolveCompact,sizeof(kTaaResolveCompact),true)
        :m_quality==TaaQuality::Medium?makePipeline(m_historyPass,kTaaResolveFast,sizeof(kTaaResolveFast),true)
            :makePipeline(m_historyPass,kTaaResolve,sizeof(kTaaResolve),true);
    m_apply=makePipeline(m_applyPass,kTaaApply,sizeof(kTaaApply),false);
    if(!m_resolve||!m_apply)return fail();
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.minFilter=sampler.magFilter=VK_FILTER_LINEAR;
    sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if(vkCreateSampler(device,&sampler,nullptr,&m_sampler)!=VK_SUCCESS)return fail();
    sampler.minFilter=sampler.magFilter=VK_FILTER_NEAREST;
    if(vkCreateSampler(device,&sampler,nullptr,&m_depthSampler)!=VK_SUCCESS)return fail();
    if(m_copyDepth) {
        if(!makeImage(depth.info.format,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_DEPTH_BIT,m_privateDepth))return fail();
        m_depthView=m_privateDepth.view;
    } else m_depthView=makeView(depth.image,depth.info.format,VK_IMAGE_ASPECT_DEPTH_BIT);
    if(!m_depthView)return fail();
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,12};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=4;pool.poolSizeCount=1;pool.pPoolSizes=&poolSize;
    if(vkCreateDescriptorPool(device,&pool,nullptr,&m_descriptors)!=VK_SUCCESS)return fail();
    const auto framebuffer=[&](VkRenderPass pass,VkImageView view,VkFramebuffer &out) {
        VkFramebufferCreateInfo f{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};f.renderPass=pass;f.attachmentCount=1;f.pAttachments=&view;f.width=m_width;f.height=m_height;f.layers=1;
        return vkCreateFramebuffer(device,&f,nullptr,&out)==VK_SUCCESS;
    };
    for(unsigned e=0;e<2;++e) {
        auto &eye=m_eyes[e];eye.sourceView=makeView(eyes[e].image,format,VK_IMAGE_ASPECT_COLOR_BIT);
        if(!eye.sourceView || !framebuffer(m_applyPass,eye.sourceView,eye.applyFb))return fail();
        for(unsigned h=0;h<2;++h) {
            if(!makeImage(format,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT,eye.images[h])
                ||!framebuffer(m_historyPass,eye.images[h].view,eye.resolveFb[h]))return fail();
        }
        VkDescriptorSetLayout layouts[2]{m_setLayout,m_setLayout};
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocate.descriptorPool=m_descriptors;allocate.descriptorSetCount=2;allocate.pSetLayouts=layouts;
        if(vkAllocateDescriptorSets(device,&allocate,eye.sets.data())!=VK_SUCCESS)return fail();
        for(unsigned h=0;h<2;++h) {
            const VkDescriptorImageInfo images[]={{m_sampler,eye.sourceView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                // The private copy is sampled, not an attachment. VUID 01210
                // forbids DEPTH_STENCIL_READ_ONLY for that image usage.
                {m_depthSampler,m_depthView,m_copyDepth?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
                {m_sampler,eye.images[h].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
            VkWriteDescriptorSet writes[3]{};
            for(unsigned b=0;b<3;++b){writes[b].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[b].dstSet=eye.sets[h];writes[b].dstBinding=b;
                writes[b].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[b].descriptorCount=1;writes[b].pImageInfo=&images[b];}
            vkUpdateDescriptorSets(device,3,writes,0,nullptr);
        }
    }
    VkCommandPoolCreateInfo commands{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};commands.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;commands.queueFamilyIndex=family;
    if(vkCreateCommandPool(device,&commands,nullptr,&m_pool)!=VK_SUCCESS)return fail();
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocate.commandPool=m_pool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=6;
    VkCommandBuffer buffers[6]{};if(vkAllocateCommandBuffers(device,&allocate,buffers)!=VK_SUCCESS)return fail();
    for(unsigned i=0;i<6;++i) {
        m_slots[i].command=buffers[i];VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};fence.flags=VK_FENCE_CREATE_SIGNALED_BIT;
        if(vkCreateFence(device,&fence,nullptr,&m_slots[i].fence)!=VK_SUCCESS)return fail();
    }
    if(m_bits && m_timestampPeriod>0) {
        VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};query.queryType=VK_QUERY_TYPE_TIMESTAMP;query.queryCount=12;
        if(vkCreateQueryPool(device,&query,nullptr,&m_queries)!=VK_SUCCESS)m_queries=VK_NULL_HANDLE;
    }
    __android_log_print(ANDROID_LOG_INFO,"GeneralsXR","Quest TAA ready: %ux%u depth_copy=%d memory_bytes=%llu; HUD/pointer excluded",
        m_width,m_height,m_copyDepth,(unsigned long long)m_bytes);
    __android_log_print(ANDROID_LOG_INFO,"GeneralsXR",
        "Quest TAA quality: mode=%s history_taps=%u relaxed_color=%u coordinates_highp=1 coverage=%s depth_region=%s barriers=%s timing_stride=%u",
        qualityName(m_quality),m_quality==TaaQuality::Full?5u:1u,m_quality==TaaQuality::Full?0u:1u,
        m_quality==TaaQuality::Lite?"center70x70":"full",m_quality==TaaQuality::Lite?"center_plus_gather_border":"full",
        m_quality==TaaQuality::Lite?"grouped":"original",m_quality==TaaQuality::Lite?16u:1u);
    return true;
}
// GeneralsX @performance Codex 16/10/2026 Bundle the lower-bandwidth centered Lite path; preserve Medium/Full recording.
void TemporalAA::recordLite(VkCommandBuffer cmd,unsigned index,unsigned output,unsigned input,const float *reproject,bool reset) {
    auto &eye=m_eyes[index];const auto color=m_sources[index];const auto aspects=depthAspects(m_depth.info.format);
    // 70% of width and height, as in RDR Lite. Integer margins are symmetric.
    const uint32_t mx=m_width*15/100,my=m_height*15/100;
    const VkRect2D area{{int32_t(mx),int32_t(my)},{m_width-2*mx,m_height-2*my}};
    Push push{};std::copy(reproject,reproject+16,push.reproject);
    push.info[0]=m_width;push.info[1]=m_height;push.info[2]=reset?1:0;push.info[3]=mx|(my<<16);
    BarrierBatch batch;
    batch.add(color.image,VK_IMAGE_ASPECT_COLOR_BIT,color.layout,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    if(!reset && m_copyDepth) {
        batch.add(m_depth.image,aspects,m_depth.layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        batch.add(m_privateDepth.image,aspects,m_depthInitialized?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,m_depthInitialized?VK_ACCESS_SHADER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);
    } else if(!m_copyDepth)batch.add(m_depth.image,aspects,m_depth.layout,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    else if(!m_depthInitialized)batch.add(m_privateDepth.image,aspects,VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,0,VK_ACCESS_SHADER_READ_BIT);
    if(!eye.initialized)batch.add(eye.images[input].image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,0,VK_ACCESS_SHADER_READ_BIT);
    batch.add(eye.images[output].image,VK_IMAGE_ASPECT_COLOR_BIT,
        eye.initialized?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        eye.initialized?VK_ACCESS_SHADER_READ_BIT:0,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    batch.flush(cmd);
    if(!reset && m_copyDepth) {
        // Unlike a full-eye copy, transfer only texels the centered shader can read.
        // textureGather at p+1 also reads the next row/column: include that border.
        VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1};
        copy.srcOffset=copy.dstOffset={int32_t(mx),int32_t(my),0};
        copy.extent={std::min(m_width-mx,area.extent.width+1),std::min(m_height-my,area.extent.height+1),1};
        vkCmdCopyImage(cmd,m_depth.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,m_privateDepth.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        batch.add(m_privateDepth.image,aspects,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
        batch.flush(cmd);
    }
    const VkViewport viewport{0,0,float(m_width),float(m_height),0,1};
    const auto draw=[&](VkRenderPass pass,VkFramebuffer fb,VkPipeline pipeline,VkDescriptorSet set) {
        VkRenderPassBeginInfo r{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};r.renderPass=pass;r.framebuffer=fb;r.renderArea=area;
        vkCmdBeginRenderPass(cmd,&r,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        vkCmdSetViewport(cmd,0,1,&viewport);vkCmdSetScissor(cmd,0,1,&area);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,m_pipelineLayout,0,1,&set,0,nullptr);
        vkCmdPushConstants(cmd,m_pipelineLayout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof push,&push);
        vkCmdDraw(cmd,3,1,0,0);vkCmdEndRenderPass(cmd);
    };
    draw(m_historyPass,eye.resolveFb[output],m_resolve,eye.sets[input]);
    batch.add(eye.images[output].image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    if(!reset) {
        batch.add(color.image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        batch.flush(cmd);
        // Retain LOAD and RGB-only writes: alpha and all peripheral pixels remain exact.
        draw(m_applyPass,eye.applyFb,m_apply,eye.sets[output]);
        batch.add(color.image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,color.layout,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT);
    } else batch.add(color.image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,color.layout,
        VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT);
    if(!reset || !m_copyDepth)batch.add(m_depth.image,aspects,m_copyDepth?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        m_depth.layout,m_copyDepth?VK_ACCESS_TRANSFER_READ_BIT:VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    batch.flush(cmd);
}
bool TemporalAA::process(VkQueue queue,unsigned index,const TaaCamera &camera,uint64_t frame,uint64_t time,bool identity) {
    if(!ready()||!queue||index>1||camera.width!=m_width||camera.height!=m_height)return false;
    auto &eye=m_eyes[index];Push push{};TaaMath::History next;
    const auto reason=TaaMath::Plan(camera,eye.history,frame,time,push.reproject,next);
    if(reason==TaaMath::Reset::Invalid){
        eye.history.valid=false;
        if(++m_invalid<=3)__android_log_print(ANDROID_LOG_WARN,"GeneralsXR","Quest TAA invalid camera/depth plan: eye=%u frame=%llu",index,(unsigned long long)frame);
        return false;
    }
    const unsigned slotIndex=index*3+m_nextSlot[index]%3;auto &slot=m_slots[slotIndex];
    const auto status=vkGetFenceStatus(m_device,slot.fence);
    if(status!=VK_SUCCESS) {eye.history.valid=false;++m_busy;if(status!=VK_NOT_READY)m_failed=true;return false;}
    if(slot.timed) {
        uint64_t ticks[2]{};
        if(vkGetQueryPoolResults(m_device,m_queries,slotIndex*2,2,sizeof ticks,ticks,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT)==VK_SUCCESS) {
            uint64_t delta=ticks[1]-ticks[0];if(m_bits<64)delta&=(uint64_t(1)<<m_bits)-1;
            m_lastGpu=double(delta)*m_timestampPeriod/1000000.0;
            m_eyeGpu[index]=m_lastGpu;
        }
        slot.timed=false;
    }
    if(vkResetCommandBuffer(slot.command,0)!=VK_SUCCESS){m_failed=true;return false;}
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if(vkBeginCommandBuffer(slot.command,&begin)!=VK_SUCCESS){m_failed=true;return false;}
    auto cmd=slot.command;const auto color=m_sources[index];const auto aspects=depthAspects(m_depth.info.format);
    // Match RDR's sparse timing cadence in Lite; never change the Medium/Full reference path.
    const bool timed=m_queries && (m_quality!=TaaQuality::Lite || frame%16==0);
    if(timed){vkCmdResetQueryPool(cmd,m_queries,slotIndex*2,2);vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,m_queries,slotIndex*2);}
    if(m_quality==TaaQuality::Lite) {
        push.info[2]=(identity||reason!=TaaMath::Reset::None)?1:0;
        recordLite(cmd,index,eye.output,1-eye.output,push.reproject,push.info[2]!=0);
    } else {
    barrier(cmd,color.image,VK_IMAGE_ASPECT_COLOR_BIT,color.layout,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    if(m_copyDepth) {
        barrier(cmd,m_depth.image,aspects,m_depth.layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        barrier(cmd,m_privateDepth.image,aspects,m_depthInitialized?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,m_depthInitialized?VK_ACCESS_SHADER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);
        VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1};copy.extent={m_width,m_height,1};
        vkCmdCopyImage(cmd,m_depth.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,m_privateDepth.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier(cmd,m_privateDepth.image,aspects,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
        barrier(cmd,m_depth.image,aspects,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,m_depth.layout,VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    } else barrier(cmd,m_depth.image,aspects,m_depth.layout,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    if(!eye.initialized)for(auto &history:eye.images)
        barrier(cmd,history.image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,0,VK_ACCESS_SHADER_READ_BIT);
    const unsigned output=eye.output,input=1-output;
    barrier(cmd,eye.images[output].image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    push.info[0]=m_width;push.info[1]=m_height;push.info[2]=(identity||reason!=TaaMath::Reset::None)?1:0;
    const VkViewport viewport{0,0,float(m_width),float(m_height),0,1};const VkRect2D area{{0,0},{m_width,m_height}};
    const auto draw=[&](VkRenderPass pass,VkFramebuffer fb,VkPipeline pipeline,VkDescriptorSet set) {
        VkRenderPassBeginInfo r{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};r.renderPass=pass;r.framebuffer=fb;r.renderArea=area;
        vkCmdBeginRenderPass(cmd,&r,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        vkCmdSetViewport(cmd,0,1,&viewport);vkCmdSetScissor(cmd,0,1,&area);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,m_pipelineLayout,0,1,&set,0,nullptr);
        vkCmdPushConstants(cmd,m_pipelineLayout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof push,&push);
        vkCmdDraw(cmd,3,1,0,0);vkCmdEndRenderPass(cmd);
    };
    draw(m_historyPass,eye.resolveFb[output],m_resolve,eye.sets[input]);
    barrier(cmd,eye.images[output].image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    if(!push.info[2]) {
        barrier(cmd,color.image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        draw(m_applyPass,eye.applyFb,m_apply,eye.sets[output]);
        barrier(cmd,color.image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,color.layout,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT);
    } else barrier(cmd,color.image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,color.layout,
        VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT);
    if(!m_copyDepth)barrier(cmd,m_depth.image,aspects,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,m_depth.layout,
        VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    }
    if(timed)vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,m_queries,slotIndex*2+1);
    if(vkEndCommandBuffer(cmd)!=VK_SUCCESS||vkResetFences(m_device,1,&slot.fence)!=VK_SUCCESS){m_failed=true;return false;}
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
    if(vkQueueSubmit(queue,1,&submit,slot.fence)!=VK_SUCCESS){m_failed=true;return false;}
    ++m_nextSlot[index];eye.history=next;eye.output=1-eye.output;eye.initialized=true;
    m_depthInitialized=true;
    slot.timed=timed;
    ++m_processed;if(push.info[2])++m_resets;
    ++m_resetReasons[unsigned(reason)];
    if(m_processed%240==0)__android_log_print(ANDROID_LOG_INFO,"GeneralsXR",
        "Quest TAA Lite: eyes=%llu resets=%llu busy=%llu gpu_left_ms=%.3f gpu_right_ms=%.3f gpu_pair_ms=%.3f depth_copy=%d memory_bytes=%llu full=%u",
        (unsigned long long)m_processed,(unsigned long long)m_resets,(unsigned long long)m_busy,m_eyeGpu[0],m_eyeGpu[1],
        m_eyeGpu[0]>=0&&m_eyeGpu[1]>=0?m_eyeGpu[0]+m_eyeGpu[1]:-1,m_copyDepth,(unsigned long long)m_bytes,m_quality==TaaQuality::Lite?0u:1u);
    if(m_processed%240==0)__android_log_print(ANDROID_LOG_INFO,"GeneralsXR",
        "Quest TAA history: reused_eyes=%llu first=%llu gap=%llu camera=%llu projection=%llu invalid=%llu tune=10,5,1 mode=%s (GPU span includes dependencies)",
        (unsigned long long)m_resetReasons[0],(unsigned long long)m_resetReasons[1],(unsigned long long)m_resetReasons[2],
        (unsigned long long)m_resetReasons[3],(unsigned long long)m_resetReasons[4],(unsigned long long)m_invalid,
        qualityName(m_quality));
    return true;
}
void TemporalAA::destroy() {
    if(!m_device)return;
    if(m_queries)vkDestroyQueryPool(m_device,m_queries,nullptr);
    for(auto &s:m_slots)if(s.fence)vkDestroyFence(m_device,s.fence,nullptr);
    if(m_pool)vkDestroyCommandPool(m_device,m_pool,nullptr);
    if(m_descriptors)vkDestroyDescriptorPool(m_device,m_descriptors,nullptr);
    for(auto &eye:m_eyes) {
        if(eye.applyFb)vkDestroyFramebuffer(m_device,eye.applyFb,nullptr);
        for(auto fb:eye.resolveFb)if(fb)vkDestroyFramebuffer(m_device,fb,nullptr);
        if(eye.sourceView)vkDestroyImageView(m_device,eye.sourceView,nullptr);
        for(auto image:eye.images) {
            if(image.view)vkDestroyImageView(m_device,image.view,nullptr);
            if(image.image)vkDestroyImage(m_device,image.image,nullptr);
            if(image.memory)vkFreeMemory(m_device,image.memory,nullptr);
        }
    }
    if(!m_copyDepth && m_depthView)vkDestroyImageView(m_device,m_depthView,nullptr);
    if(m_privateDepth.view)vkDestroyImageView(m_device,m_privateDepth.view,nullptr);
    if(m_privateDepth.image)vkDestroyImage(m_device,m_privateDepth.image,nullptr);
    if(m_privateDepth.memory)vkFreeMemory(m_device,m_privateDepth.memory,nullptr);
    if(m_resolve)vkDestroyPipeline(m_device,m_resolve,nullptr);
    if(m_apply)vkDestroyPipeline(m_device,m_apply,nullptr);
    if(m_historyPass)vkDestroyRenderPass(m_device,m_historyPass,nullptr);
    if(m_applyPass)vkDestroyRenderPass(m_device,m_applyPass,nullptr);
    if(m_sampler)vkDestroySampler(m_device,m_sampler,nullptr);
    if(m_depthSampler)vkDestroySampler(m_device,m_depthSampler,nullptr);
    if(m_pipelineLayout)vkDestroyPipelineLayout(m_device,m_pipelineLayout,nullptr);
    if(m_setLayout)vkDestroyDescriptorSetLayout(m_device,m_setLayout,nullptr);
    *this=TemporalAA{};
}
}
#endif
