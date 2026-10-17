// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <vulkan/vulkan.h>
#include <array>
#include <vector>
namespace QuestXR {
struct CompositeImage {VkImage image{};VkImageLayout layout{};VkFormat format{};uint32_t width=0,height=0;VkImageUsageFlags usage=0;};
// GeneralsX @performance Codex 17/10/2026 One owned handoff draw, original world resolution and RGB.
class WorldComposite final {
public:
 WorldComposite()=default;WorldComposite(const WorldComposite&)=delete;WorldComposite&operator=(const WorldComposite&)=delete;
 bool initialize(VkPhysicalDevice physical,VkDevice device,uint32_t width,uint32_t height,VkFormat format,
  const std::array<CompositeImage,2>&color,const std::array<CompositeImage,2>&mask,const std::vector<VkImage>&destinations);
 bool matches(const std::array<CompositeImage,2>&color,const std::array<CompositeImage,2>&mask)const;
 bool record(VkCommandBuffer command,unsigned imageIndex,const std::array<CompositeImage,2>&color,const std::array<CompositeImage,2>&mask)const;
 void destroy(); // Caller drains the shared queue before destruction.
private:
 struct Destination {VkImage image{};std::array<VkImageView,2> views{};std::array<VkFramebuffer,2> frames{};};
 VkDevice m_device{};VkPipeline m_pipeline{};VkPipelineLayout m_layout{};VkDescriptorSetLayout m_setLayout{};
 VkDescriptorPool m_pool{};VkRenderPass m_pass{};VkSampler m_sampler{};
 std::array<VkDescriptorSet,2> m_sets{};std::array<VkImageView,4> m_sourceViews{};
 std::array<CompositeImage,2> m_color{},m_mask{};std::vector<Destination> m_destinations;
 uint32_t m_width=0,m_height=0;
};
}
#endif
