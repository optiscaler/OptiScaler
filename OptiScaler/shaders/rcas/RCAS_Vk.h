#pragma once

#include "SysUtils.h"
#include <shaders/Shader_Vk.h>
#include "RCAS_Common.h"

class RCAS_Vk : public Shader_Vk, public RCAS_Common
{
    VkBuffer _constantBufferDA = VK_NULL_HANDLE;
    VkDeviceMemory _constantBufferMemoryDA = VK_NULL_HANDLE;
    void* _mappedConstantBufferDA = nullptr;

    VkDescriptorPool _descriptorPoolDA = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> _descriptorSetsDA;

    VkDescriptorSetLayout _descriptorSetLayoutDA = VK_NULL_HANDLE;
    VkPipelineLayout _pipelineLayoutDA = VK_NULL_HANDLE;

    VkPipeline _pipelineDA = VK_NULL_HANDLE;
    VkPipeline _pipelineDASDA = VK_NULL_HANDLE;

    void UpdateDescriptorSet(VkCommandBuffer cmdList, int setIndex, VkImageView inputView, VkImageView motionView,
                             VkImageView outputView);
    void UpdateDescriptorSetDA(VkDescriptorSet descriptorSet, VkBuffer constantBuffer, VkImageView inputView,
                               VkImageView motionView, VkImageView depthView, VkImageView outputView);

    bool DispatchRCAS(VkCommandBuffer InCmdList, RcasConstants InConstants, const VkImageInfo& InResourceInfo,
                      const VkImageInfo& InMotionVectorsInfo, const VkImageInfo& OutResourceInfo);

    // Merged Dispatch for DA and DASDA
    bool DispatchDepthAdaptive(VkCommandBuffer InCmdList, RcasConstants InConstants, const VkImageInfo& InResourceInfo,
                               const VkImageInfo& InMotionVectorsInfo, const VkImageInfo& OutResourceInfo,
                               VkImageInfo* InDepthInfo, bool isDAS);

  public:
    RCAS_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice);
    ~RCAS_Vk();

    bool Dispatch(VkDevice InDevice, VkCommandBuffer InCmdList, RcasConstants InConstants,
                  const VkImageInfo& InResourceView, const VkImageInfo& InMotionVectorsInfo,
                  const VkImageInfo& OutResourceInfo, VkImageInfo* InDepthInfo = nullptr);

    // Wrappers to maintain the original public API while using the generalized base methods
    bool CreateBufferResource(VkDevice device, VkPhysicalDevice physicalDevice, VkBuffer* buffer,
                              VkDeviceMemory* memory, VkDeviceSize size, VkBufferUsageFlags usage,
                              VkMemoryPropertyFlags properties)
    {
        return Shader_Vk::CreateBufferResource(device, physicalDevice, buffer, memory, size, usage, properties);
    }
    void SetBufferState(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize size, VkAccessFlags srcAccess,
                        VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
    {
        Shader_Vk::SetBufferState(commandBuffer, buffer, size, srcAccess, dstAccess, srcStage, dstStage);
    }

    bool CreateImageResource(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height,
                             VkFormat format, VkImageUsageFlags usage)
    {
        return Shader_Vk::CreateImageResource(width, height, format, usage);
    }

    void SetImageLayout(VkCommandBuffer cmdBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                        VkImageSubresourceRange subresourceRange)
    {
        Shader_Vk::SetImageLayout(cmdBuffer, image, oldLayout, newLayout, subresourceRange);
    }
};
