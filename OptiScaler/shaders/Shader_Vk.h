#pragma once

#include "SysUtils.h"
#include <vulkan/vulkan.h>
#include <string>
#include <vector>

// Matches NVSDK_NGX_ImageViewInfo_VK
struct VkImageInfo
{
    VkImageView ImageView;
    VkImage Image;
    VkImageSubresourceRange SubresourceRange;
    VkFormat Format;
    unsigned int Width;
    unsigned int Height;
};

class Shader_Vk
{
  protected:
    std::string _name = "";
    bool _init = false;

    VkDevice _device = VK_NULL_HANDLE;
    VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;

    // Pipeline Layouts
    VkPipeline _pipeline = VK_NULL_HANDLE;
    VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout _descriptorSetLayout = VK_NULL_HANDLE;

    // Descriptors
    VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> _descriptorSets;
    uint32_t _currentSetIndex = 0;
    uint32_t _maxFramesInFlight = 3;

    // Common Resources
    VkBuffer _constantBuffer = VK_NULL_HANDLE;
    VkDeviceMemory _constantBufferMemory = VK_NULL_HANDLE;
    void* _mappedConstantBuffer = nullptr;
    VkSampler _textureSampler = VK_NULL_HANDLE;

    // Intermediate Image (For shaders that output to a temp texture)
    VkImageView _intermediateImageView = VK_NULL_HANDLE;
    VkImage _intermediateImage = VK_NULL_HANDLE;
    VkDeviceMemory _intermediateMemory = VK_NULL_HANDLE;
    uint32_t _width = 0;
    uint32_t _height = 0;
    VkFormat _format = VK_FORMAT_UNDEFINED;

    // Base Static Helpers
    static uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter,
                                   VkMemoryPropertyFlags properties);
    static bool CreateComputePipeline(VkDevice device, VkPipelineLayout pipelineLayout, VkPipeline* pipeline,
                                      const std::vector<char>& shaderCode, const char* entryPoint = "CSMain");
    static bool CreateBufferResource(VkDevice device, VkPhysicalDevice physicalDevice, VkBuffer* buffer,
                                     VkDeviceMemory* memory, VkDeviceSize size, VkBufferUsageFlags usage,
                                     VkMemoryPropertyFlags properties);
    static void SetBufferState(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize size,
                               VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage,
                               VkPipelineStageFlags dstStage);

    // Setup Helpers
    VkDescriptorSetLayoutBinding CreateBinding(uint32_t binding, VkDescriptorType descriptorType,
                                               uint32_t descriptorCount = 1,
                                               VkShaderStageFlags stageFlags = VK_SHADER_STAGE_COMPUTE_BIT);
    void CreateLayouts(const std::vector<VkDescriptorSetLayoutBinding>& bindings);
    void CreateDescriptorPool(const std::vector<VkDescriptorPoolSize>& poolSizes, uint32_t maxSets);
    void CreateDescriptorSets(VkDescriptorSetLayout descriptorSetLayout, VkDescriptorPool descriptorPool,
                              std::vector<VkDescriptorSet>& descriptorSets);

    void CreateConstantBuffer(VkDeviceSize bufferSize);
    void CreateSampler(VkFilter filter = VK_FILTER_LINEAR,
                       VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    // Image Resource Helpers
    bool CreateImageResource(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage);
    void ReleaseImageResource();
    void SetImageLayout(VkCommandBuffer cmdBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                        VkImageSubresourceRange subresourceRange);

  public:
    bool IsInit() const { return _init; }
    bool CanRender() const { return _init && _pipeline != VK_NULL_HANDLE; }

    VkImageView GetImageView() const { return _intermediateImageView; }
    VkImage GetImage() const { return _intermediateImage; }

    Shader_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice);
    virtual ~Shader_Vk();
};