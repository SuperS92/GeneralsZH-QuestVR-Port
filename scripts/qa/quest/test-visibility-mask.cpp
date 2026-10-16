// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestVisibilityMask.h"
#include "QuestTaaShaders.spv.h"
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
using namespace QuestXR;
static void need(bool v,const char *s){if(!v){std::fprintf(stderr,"FAIL %s\n",s);std::exit(1);}}
static void check(VkResult r){need(r==VK_SUCCESS,"Vulkan operation");}
static MaskPoint inversePixel(MaskPoint p,MaskFov f,unsigned w,unsigned h) {
    return {std::tan(f.left)+(p.x-.5)/w*(std::tan(f.right)-std::tan(f.left)),
        std::tan(f.up)-(p.y-.5)/h*(std::tan(f.up)-std::tan(f.down))};
}
static std::vector<MaskPoint> corners(MaskFov f,unsigned w,unsigned h,unsigned eye) {
    std::vector<MaskPoint> p;
    for(unsigned corner=0;corner<4;++corner)for(auto v:{MaskPoint{-64,-64},MaskPoint{w*(eye?.76:.8),-64},MaskPoint{-64,h*.8}}) {
        if(corner&1)v.x=w-v.x;if(corner&2)v.y=h-v.y;
        p.push_back(inversePixel(v,f,w,h));
    }
    return p;
}
// Independent area-coordinate containment oracle, separate from production edge tests.
static bool inside(MaskPoint p,MaskPoint a,MaskPoint b,MaskPoint c) {
    const double den=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);
    if(std::abs(den)<1e-9)return false;
    const double u=((b.y-c.y)*(p.x-c.x)+(c.x-b.x)*(p.y-c.y))/den;
    const double v=((c.y-a.y)*(p.x-c.x)+(a.x-c.x)*(p.y-c.y))/den;
    return u>=-1e-9&&v>=-1e-9&&u+v<=1+1e-9;
}
static bool covered(unsigned x,unsigned y,const MaskPlan &p) {
    for(auto r:p.rectangles)if(int(x)>=r.left&&int(x)<r.right&&int(y)>=r.top&&int(y)<r.bottom)return true;
    return false;
}
static void mathChecks() {
    const MaskFov f{-.7f,.9f,.85f,-.6f};std::vector<uint32_t> indices;for(unsigned i=0;i<12;++i)indices.push_back(i);
    for(unsigned w:{257u,513u,1025u})for(unsigned h:{193u,385u})for(unsigned eye:{0u,1u}) {
        const auto vertices=corners(f,w,h,eye);const auto p=PlanVisibilityMask(vertices,indices,f,w,h);
        need(p.valid&&!p.rectangles.empty(),"Conservative hidden rectangles exist");uint64_t area=0;
        std::vector<MaskPoint> projected;for(auto v:vertices)projected.push_back(ProjectMaskPoint(v,f,w,h));
        for(auto r:p.rectangles) {
            need(r.left>=0&&r.top>=0&&r.right<=int(w)&&r.bottom<=int(h)&&r.left<r.right&&r.top<r.bottom,"Rectangle bounds");
            need(r.right<=int(w*15/100)-4||r.left>=int(w)-int(w*15/100)+4||r.bottom<=int(h*15/100)-4||r.top>=int(h)-int(h*15/100)+4,"Entire Lite footprint excluded");
            area+=uint64_t(r.right-r.left)*(r.bottom-r.top);
            // Check every pixel in the expanded guard against the runtime triangle union.
            for(int y=std::max(0,r.top-int(kMaskGuard));y<std::min(int(h),r.bottom+int(kMaskGuard));++y)
                for(int x=std::max(0,r.left-int(kMaskGuard));x<std::min(int(w),r.right+int(kMaskGuard));++x) {
                    bool hidden=false;for(size_t i=0;i<indices.size();i+=3)hidden|=inside({x+.5,y+.5},projected[indices[i]],projected[indices[i+1]],projected[indices[i+2]]);
                    need(hidden,"Every guarded pixel is actually in the hidden mesh");
                }
        }
        need(area==p.pixels&&area<uint64_t(w)*h,"Exact area accounting");
        auto reversed=indices;for(size_t i=0;i<indices.size();i+=3)std::swap(reversed[i],reversed[i+2]);
        need(PlanVisibilityMask(vertices,reversed,f,w,h).pixels==area,"Both triangle windings");
        auto bad=indices;bad[0]=99999;need(!PlanVisibilityMask(vertices,bad,f,w,h).valid,"Invalid runtime indices rejected");
        bad.pop_back();need(!PlanVisibilityMask(vertices,bad,f,w,h).valid,"Incomplete triangles rejected");
        auto nan=vertices;nan[0].x=std::numeric_limits<double>::quiet_NaN();need(!PlanVisibilityMask(nan,indices,f,w,h).valid,"Nonfinite runtime vertex rejected");
    }
    need(!PlanVisibilityMask({},indices,f,513,385).valid,"Empty mask rejected");
    need(!PlanVisibilityMask(corners(f,513,385,0),indices,f,0,385).valid,"Empty eye rejected");
    MaskFov bad=f;bad.right=bad.left;need(!ValidMaskFov(bad),"Degenerate FOV rejected");
    const double l=std::tan(f.left),r=std::tan(f.right),u=std::tan(f.up),d=std::tan(f.down);
    float m[16]{};m[0]=float(2/(r-l));m[5]=float(2/(u-d));m[8]=float((r+l)/(r-l));m[9]=float((u+d)/(u-d));m[11]=-1;
    need(MaskProjectionMatches(m,f),"Asymmetric right-handed D3D projection");m[5]=-m[5];need(!MaskProjectionMatches(m,f),"Wrong vertical orientation rejected");
    m[5]=-m[5];m[8]+=.01f;need(!MaskProjectionMatches(m,f),"Wrong eye projection rejected");
    const auto empty=PlanVisibilityMask({{0,0},{0,0},{0,0}},std::vector<uint32_t>(24576,0),f,1024,1024);
    need(!empty.valid,"Pathological mesh preparation work is bounded");
    std::puts("PASS independent guarded-pixel geometry, winding, projection and malformed/work-budget oracles");
}
struct Image {VkImage image{};VkDeviceMemory memory{};VkImageView view{};};
int main(int argc,char **argv) {
    need(argc==2,"Fragment SPIR-V path");mathChecks();
    VkInstance instance{};VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};check(vkCreateInstance(&ci,nullptr,&instance));
    uint32_t n=0;check(vkEnumeratePhysicalDevices(instance,&n,nullptr));need(n>0,"GPU present");std::vector<VkPhysicalDevice> physicals(n);check(vkEnumeratePhysicalDevices(instance,&n,physicals.data()));const auto physical=physicals[0];
    VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(physical,&props);std::printf("Host GPU: %s\n",props.deviceName);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,nullptr);std::vector<VkQueueFamilyProperties> families(n);vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,families.data());unsigned family=0;while(family<n&&!(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))++family;need(family<n,"Graphics queue");
    float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;VkDevice device{};check(vkCreateDevice(physical,&di,nullptr,&device));VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);
    VkPhysicalDeviceMemoryProperties memories{};vkGetPhysicalDeviceMemoryProperties(physical,&memories);
    auto allocate=[&](VkMemoryRequirements req,VkMemoryPropertyFlags flags){unsigned i=0;while(i<memories.memoryTypeCount&&(!(req.memoryTypeBits&(1u<<i))||(memories.memoryTypes[i].propertyFlags&flags)!=flags))++i;need(i<memories.memoryTypeCount,"Memory type");VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=req.size;a.memoryTypeIndex=i;VkDeviceMemory out{};check(vkAllocateMemory(device,&a,nullptr,&out));return out;};
    auto module=[&](const uint32_t *code,size_t size){VkShaderModuleCreateInfo c{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};c.codeSize=size;c.pCode=code;VkShaderModule m{};check(vkCreateShaderModule(device,&c,nullptr,&m));return m;};
    std::ifstream file(argv[1],std::ios::binary|std::ios::ate);need(bool(file),"Shader exists");const size_t size=size_t(file.tellg());need(size&&size%4==0,"SPIR-V alignment");std::vector<uint32_t> frag(size/4);file.seekg(0);file.read(reinterpret_cast<char *>(frag.data()),size);need(bool(file),"Shader read");
    const auto vert=module(kTaaVertex,sizeof(kTaaVertex)),fragment=module(frag.data(),size);
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};VkPipelineLayout layout{};check(vkCreatePipelineLayout(device,&li,nullptr,&layout));
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=family;pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;VkCommandPool pool{};check(vkCreateCommandPool(device,&pi,nullptr,&pool));
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pool;ai.commandBufferCount=1;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;VkCommandBuffer cmd{};check(vkAllocateCommandBuffers(device,&ai,&cmd));
    unsigned cases=0;
    for(auto extent:{VkExtent2D{257,193},VkExtent2D{513,385}}) {
        const unsigned w=extent.width,h=extent.height,bytes=w*h*4;
        auto image=[&](VkFormat format,VkImageUsageFlags usage,VkImageAspectFlags aspect){Image out;VkImageCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};c.imageType=VK_IMAGE_TYPE_2D;c.format=format;c.extent={w,h,1};c.mipLevels=c.arrayLayers=1;c.samples=VK_SAMPLE_COUNT_1_BIT;c.tiling=VK_IMAGE_TILING_OPTIMAL;c.usage=usage;check(vkCreateImage(device,&c,nullptr,&out.image));VkMemoryRequirements req{};vkGetImageMemoryRequirements(device,out.image,&req);out.memory=allocate(req,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);check(vkBindImageMemory(device,out.image,out.memory,0));VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};v.image=out.image;v.viewType=VK_IMAGE_VIEW_TYPE_2D;v.format=format;v.subresourceRange={aspect,0,1,0,1};check(vkCreateImageView(device,&v,nullptr,&out.view));return out;};
        const auto color=image(VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
        const auto depth=image(VK_FORMAT_D24_UNORM_S8_UINT,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT);
        VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bc.size=bytes+w*h;bc.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;VkBuffer buffer{};check(vkCreateBuffer(device,&bc,nullptr,&buffer));VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,buffer,&req);const auto bufferMemory=allocate(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);check(vkBindBufferMemory(device,buffer,bufferMemory,0));void *mapped{};check(vkMapMemory(device,bufferMemory,0,VK_WHOLE_SIZE,0,&mapped));auto pixels=static_cast<unsigned char *>(mapped);
        VkAttachmentDescription attachments[2]{};attachments[0].format=VK_FORMAT_R8G8B8A8_UNORM;attachments[1].format=VK_FORMAT_D24_UNORM_S8_UINT;
        for(unsigned i=0;i<2;++i){attachments[i].samples=VK_SAMPLE_COUNT_1_BIT;attachments[i].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachments[i].storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachments[i].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachments[i].stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE;attachments[i].initialLayout=attachments[i].finalLayout=i?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;}
        VkAttachmentReference cr{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},dr{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&cr;sub.pDepthStencilAttachment=&dr;
        VkRenderPassCreateInfo rc{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rc.attachmentCount=2;rc.pAttachments=attachments;rc.subpassCount=1;rc.pSubpasses=&sub;VkRenderPass pass{};check(vkCreateRenderPass(device,&rc,nullptr,&pass));VkImageView views[]{color.view,depth.view};VkFramebufferCreateInfo fc{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fc.renderPass=pass;fc.attachmentCount=2;fc.pAttachments=views;fc.width=w;fc.height=h;fc.layers=1;VkFramebuffer fb{};check(vkCreateFramebuffer(device,&fc,nullptr,&fb));
        VkPipelineShaderStageCreateInfo stages[2]{};for(unsigned i=0;i<2;++i){stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;stages[i].module=i?fragment:vert;stages[i].pName="main";}
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0,0,float(w),float(h),.5f,.5f};VkRect2D scissor{{0,0},extent};VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};ds.depthTestEnable=ds.depthWriteEnable=VK_TRUE;ds.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
        VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};bs.attachmentCount=1;bs.pAttachments=&blend;
        VkGraphicsPipelineCreateInfo pc{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};pc.stageCount=2;pc.pStages=stages;pc.pVertexInputState=&vi;pc.pInputAssemblyState=&assembly;pc.pViewportState=&vp;pc.pRasterizationState=&raster;pc.pMultisampleState=&ms;pc.pDepthStencilState=&ds;pc.pColorBlendState=&bs;pc.layout=layout;pc.renderPass=pass;VkPipeline pipeline{};check(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pc,nullptr,&pipeline));
        for(unsigned eye:{0u,1u})for(bool enabled:{false,true}) {
            const MaskFov f{eye?-.9f:-.7f,eye?.7f:.9f,.85f,-.6f};std::vector<uint32_t> indices;for(unsigned i=0;i<12;++i)indices.push_back(i);
            const auto plan=PlanVisibilityMask(corners(f,w,h,eye),indices,f,w,h);need(plan.valid&&plan.pixels,"GPU fixture has hidden area");
            check(vkResetCommandPool(device,pool,0));VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};check(vkBeginCommandBuffer(cmd,&begin));
            auto barrier=[&](VkImage image,VkImageAspectFlags aspect,VkImageLayout before,VkImageLayout after){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image;b.oldLayout=before;b.newLayout=after;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={aspect,0,1,0,1};b.srcAccessMask=before==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);};
            barrier(color.image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);barrier(depth.image,VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            VkClearValue clears[2]{};clears[0].color={{0,0,1,1}};clears[1].depthStencil={1,0x5a};VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};rb.renderPass=pass;rb.framebuffer=fb;rb.renderArea=scissor;rb.clearValueCount=2;rb.pClearValues=clears;vkCmdBeginRenderPass(cmd,&rb,VK_SUBPASS_CONTENTS_INLINE);
            if(enabled) {
                VkClearAttachment clear{};clear.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;clear.clearValue.depthStencil={0,0};std::vector<VkClearRect> rectangles;
                for(auto r:plan.rectangles)rectangles.push_back({{{r.left,r.top},{unsigned(r.right-r.left),unsigned(r.bottom-r.top)}},0,1});
                vkCmdClearAttachments(cmd,1,&clear,uint32_t(rectangles.size()),rectangles.data());
            }
            vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdDraw(cmd,3,1,0,0);vkCmdEndRenderPass(cmd);
            barrier(color.image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);barrier(depth.image,VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={w,h,1};vkCmdCopyImageToBuffer(cmd,color.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);copy.bufferOffset=bytes;copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_STENCIL_BIT;vkCmdCopyImageToBuffer(cmd,depth.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
            VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.buffer=buffer;host.size=VK_WHOLE_SIZE;host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);check(vkEndCommandBuffer(cmd));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;check(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));check(vkQueueWaitIdle(queue));
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const unsigned i=y*w+x;const bool hidden=enabled&&covered(x,y,plan);need(pixels[i*4]==(hidden?0:255)&&pixels[i*4+1]==0&&pixels[i*4+2]==(hidden?255:0)&&pixels[i*4+3]==255,"Depth rejects only proven hidden pixels; visible color/alpha exact");need(pixels[bytes+i]==0x5a,"Depth-only clear preserves every stencil value");}
            ++cases;
        }
        vkDestroyPipeline(device,pipeline,nullptr);vkDestroyFramebuffer(device,fb,nullptr);vkDestroyRenderPass(device,pass,nullptr);vkUnmapMemory(device,bufferMemory);vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,bufferMemory,nullptr);
        for(auto image:{color,depth}){vkDestroyImageView(device,image.view,nullptr);vkDestroyImage(device,image.image,nullptr);vkFreeMemory(device,image.memory,nullptr);}
    }
    vkDestroyCommandPool(device,pool,nullptr);vkDestroyPipelineLayout(device,layout,nullptr);vkDestroyShaderModule(device,vert,nullptr);vkDestroyShaderModule(device,fragment,nullptr);vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);
    std::printf("PASS %u GPU visibility cases; depth rejection, exact visible color/alpha and stencil preservation\n",cases);
}
