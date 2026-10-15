// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include "VRDevice/QuestTaaMath.h"
#include <vulkan/vulkan.h>
#include <array>
namespace QuestXR {
struct TaaImage {
    VkImage image=VK_NULL_HANDLE;
    VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
};
class TemporalAA final {
public:
    bool initialize(VkPhysicalDevice physical,VkDevice device,uint32_t family,
        const std::array<TaaImage,2> &eyes,const TaaImage &depth);
    // Caller flushes original DXVK producers and holds the same submission queue lock.
    bool process(VkQueue queue,unsigned eye,const TaaCamera &camera,uint64_t frame,uint64_t time,bool identity=false);
    void invalidate() { for(auto &eye:m_eyes)eye.history.valid=false; }
    void destroy(); // caller drains the bound queue first
    bool ready() const {return m_resolve && !m_failed;}
    bool matches(const std::array<TaaImage,2> &eyes,const TaaImage &depth) const;
    double lastGpuMs() const {return m_lastGpu;}
    uint64_t bytes() const {return m_bytes;}
    bool copiesDepth() const {return m_copyDepth;}
private:
    struct Image {VkImage image{};VkDeviceMemory memory{};VkImageView view{};};
    struct Eye {
        std::array<Image,2> images{};
        std::array<VkDescriptorSet,2> sets{};
        std::array<VkFramebuffer,2> resolveFb{};
        VkImageView sourceView{};
        VkFramebuffer applyFb{};
        TaaMath::History history{};
        unsigned output=0;
        bool initialized=false;
    };
    struct Slot {VkCommandBuffer command{};VkFence fence{};bool timed=false;};
    bool makeImage(VkFormat format,VkImageUsageFlags usage,VkImageAspectFlags aspect,Image &image);
    VkImageView makeView(VkImage image,VkFormat format,VkImageAspectFlags aspect);
    VkRenderPass makePass(VkFormat format,bool load);
    VkPipeline makePipeline(VkRenderPass pass,const uint32_t *fragment,size_t bytes,bool alpha);
    VkDevice m_device{};
    VkPhysicalDevice m_physical{};
    VkPipeline m_resolve{},m_apply{};
    VkPipelineLayout m_pipelineLayout{};
    VkDescriptorSetLayout m_setLayout{};
    VkDescriptorPool m_descriptors{};
    VkSampler m_sampler{},m_depthSampler{};
    VkRenderPass m_historyPass{},m_applyPass{};
    VkCommandPool m_pool{};
    VkQueryPool m_queries{};
    VkImageView m_depthView{};
    Image m_privateDepth{};
    std::array<TaaImage,2> m_sources{};
    TaaImage m_depth{};
    std::array<Eye,2> m_eyes{};
    std::array<Slot,6> m_slots{};
    VkPhysicalDeviceMemoryProperties m_memory{};
    uint32_t m_width=0,m_height=0,m_bits=0;
    unsigned m_nextSlot[2]{};
    float m_timestampPeriod=0;
    double m_lastGpu=-1;
    double m_eyeGpu[2]{-1,-1};
    uint64_t m_bytes=0,m_processed=0,m_resets=0,m_busy=0;
    uint64_t m_resetReasons[6]{},m_invalid=0;
    bool m_copyDepth=false,m_depthInitialized=false,m_failed=false;
};
}
#endif
