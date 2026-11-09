#pragma once
#pragma once

#include "SysUtils.h"
#include <shaders/Shader_Vk.h>
#include "RCAS_Common.h"

class RCAS_Vk : public Shader_Vk
{
  public:
    RCAS_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice);
    ~RCAS_Vk();

    bool Dispatch(VkDevice InDevice, VkCommandBuffer InCmdList, RcasConstants InConstants, VkImageView InResourceView,
                  VkImageView InMotionVectorsView, VkImageView OutResourceView, VkExtent2D OutExtent,
                  VkImageView InDepthView = VK_NULL_HANDLE);

    bool CreateBufferResource(VkDevice device, VkPhysicalDevice physicalDevice, VkBuffer* buffer,
                              VkDeviceMemory* memory, VkDeviceSize size, VkBufferUsageFlags usage,
                              VkMemoryPropertyFlags properties);
    void SetBufferState(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize size, VkAccessFlags srcAccess,
                        VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage);

    bool CreateImageResource(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height,
                             VkFormat format, VkImageUsageFlags usage);
    void ReleaseImageResource();
    void SetImageLayout(VkCommandBuffer cmdBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                        VkImageSubresourceRange subresourceRange);

    VkImageView GetImageView() const { return _intermediateImageView; }
    VkImage GetImage() const { return _intermediateImage; }

    bool CanRender() const { return _init && _pipeline != VK_NULL_HANDLE; }

  private:
    struct alignas(256) InternalConstants
    {
        float Sharpness;
        float Contrast;

        // Motion Vector Stuff
        int DynamicSharpenEnabled;
        int DisplaySizeMV;
        int Debug;

        float MotionSharpness;
        float MotionTextureScale;
        float MvScaleX;
        float MvScaleY;
        float Threshold;
        float ScaleLimit;
        int DisplayWidth;
        int DisplayHeight;
    };

    struct alignas(256) InternalConstantsDA
    {
        float Sharpness;

        int DepthIsLinear;
        int DepthIsReversed;

        float DepthScale;
        float DepthBias;

        float DepthLinearA;
        float DepthLinearB;
        float DepthLinearC;

        int DynamicSharpenEnabled;
        int DisplaySizeMV;
        int Debug;

        float MotionSharpness;
        float MotionTextureScale;
        float MvScaleX;
        float MvScaleY;
        float MotionThreshold;
        float MotionScaleLimit;

        float DepthTextureScale;

        int ClampOutput;

        int DisplayWidth;
        int DisplayHeight;
        int MotionWidth;
        int MotionHeight;
        int DepthWidth;
        int DepthHeight;
    };

    VkBuffer _constantBuffer = VK_NULL_HANDLE;
    VkDeviceMemory _constantBufferMemory = VK_NULL_HANDLE;
    VkBuffer _constantBufferDA = VK_NULL_HANDLE;
    VkBuffer _constantBufferLCDA = VK_NULL_HANDLE;
    VkDeviceMemory _constantBufferMemoryDA = VK_NULL_HANDLE;
    VkDeviceMemory _constantBufferMemoryLCDA = VK_NULL_HANDLE;
    VkSampler _nearestSampler = VK_NULL_HANDLE;
    void* _mappedConstantBuffer = nullptr;
    void* _mappedConstantBufferDA = nullptr;
    void* _mappedConstantBufferLCDA = nullptr;

    VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
    VkDescriptorPool _descriptorPoolDA = VK_NULL_HANDLE;
    VkDescriptorPool _descriptorPoolLCDA = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> _descriptorSets;
    std::vector<VkDescriptorSet> _descriptorSetsDA;
    std::vector<VkDescriptorSet> _descriptorSetsLCDA;
    uint32_t _currentSetIndex = 0;
    // Sets and constants slots are reused in turn, presents limit how far the GPU lags behind
    static const int MAX_FRAMES_IN_FLIGHT = 8;

    void CreateDescriptorSetLayout();
    void CreateDescriptorSetLayoutDA();
    void CreateDescriptorSetLayoutDASDA();
    void CreateDescriptorPool();
    void CreateDescriptorPoolDA();
    void CreateDescriptorPoolDASDA();
    void CreateDescriptorSets();
    void CreateDescriptorSetsDA();
    void CreateDescriptorSetsDASDA();
    void CreateConstantBuffer();
    void CreateConstantBufferDA();
    void CreateConstantBufferDASDA();
    void UpdateDescriptorSet(VkCommandBuffer cmdList, int setIndex, VkImageView inputView, VkImageView motionView,
                             VkImageView outputView);
    void UpdateDescriptorSetDA(VkCommandBuffer cmdList, int setIndex, VkImageView inputView, VkImageView motionView,
                               VkImageView depthView, VkImageView outputView);
    void UpdateDescriptorSetLCDA(VkCommandBuffer cmdList, int setIndex, VkImageView inputView, VkImageView motionView,
                                 VkImageView depthView, VkImageView outputView);
    static void FillMotionConstants(InternalConstants& OutConstants, const RcasConstants& InConstants);
    static void FillMotionConstants(InternalConstantsDA& OutConstants, const RcasConstants& InConstants);
    bool DispatchRCAS(VkDevice InDevice, VkCommandBuffer InCmdList, RcasConstants InConstants,
                      VkImageView InResourceView, VkImageView InMotionVectorsView, VkImageView OutResourceView,
                      VkExtent2D OutExtent);
    bool DispatchDepthAdaptive(VkDevice InDevice, VkCommandBuffer InCmdList, RcasConstants InConstants,
                               VkImageView InResourceView, VkImageView InMotionVectorsView, VkImageView OutResourceView,
                               VkExtent2D OutExtent, VkImageView InDepthView);
    bool DispatchDASDepthAdaptive(VkDevice InDevice, VkCommandBuffer InCmdList, RcasConstants InConstants,
                                  VkImageView InResourceView, VkImageView InMotionVectorsView,
                                  VkImageView OutResourceView, VkExtent2D OutExtent, VkImageView InDepthView);

    VkImageView _intermediateImageView = VK_NULL_HANDLE;
    VkImage _intermediateImage = VK_NULL_HANDLE;
    VkDeviceMemory _intermediateMemory = VK_NULL_HANDLE;
    uint32_t _width = 0;
    uint32_t _height = 0;
    VkFormat _format = VK_FORMAT_UNDEFINED;

    VkPipeline _pipelineDA = VK_NULL_HANDLE;
    VkPipelineLayout _pipelineLayoutDA = VK_NULL_HANDLE;
    VkDescriptorSetLayout _descriptorSetLayoutDA = VK_NULL_HANDLE;
    VkPipeline _pipelineDASDA = VK_NULL_HANDLE;
    VkPipelineLayout _pipelineLayoutDASDA = VK_NULL_HANDLE;
    VkDescriptorSetLayout _descriptorSetLayoutLCDA = VK_NULL_HANDLE;
};
