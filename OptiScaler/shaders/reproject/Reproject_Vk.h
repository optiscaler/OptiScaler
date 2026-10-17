#pragma once

#include "SysUtils.h"

#include <shaders/Shader_Vk.h>
#include "Reproject.h"

class Reproject_Vk : public Shader_Vk, public Reproject
{
  private:
    struct Image
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;      // 2D, or the cube of a cubemap
        VkImageView arrayView = VK_NULL_HANDLE; // the faces of a cubemap as a 2D array, for writing them
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct Buffer
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr; // host visible ones stay mapped
    };

    // All passes share one descriptor set layout, the main one is _pipeline
    VkPipeline _maskPipelineH = VK_NULL_HANDLE;
    VkPipeline _maskPipelineV = VK_NULL_HANDLE;
    VkPipeline _latchPipeline = VK_NULL_HANDLE;
    VkPipeline _cachePipeline = VK_NULL_HANDLE;
    VkPipeline _statsPipeline = VK_NULL_HANDLE;

    // The bilinear one is _textureSampler
    VkSampler _pointSampler = VK_NULL_HANDLE;

    Buffer _params; // a slot for each descriptor set
    VkDeviceSize _paramsStride = 0;

    // Late latching, the live input is written by the input thread
    Buffer _liveInput;
    Buffer _latched; // rotation rows + used mouse delta, written by the latch pass

    Buffer _stats;
    Buffer _statsReadback[StatsLatency];

    // Stay in VK_IMAGE_LAYOUT_GENERAL once used
    Image _depthMaskTemp;
    Image _depthMask;
    Image _cacheColor[2];
    Image _cacheGeo[2];
    Image _emptyCube; // read by the reprojection in place of the cache when there is none

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

    bool CreateImage(Image& image, VkFormat format, uint32_t width, uint32_t height, bool cube);
    void DestroyImage(Image& image);
    bool CreateBuffer(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible);
    void DestroyBuffer(Buffer& buffer);

    bool CreateMaskImages(uint32_t width, uint32_t height);
    bool CreateCacheImages(uint32_t size);
    void ToGeneral(VkCommandBuffer cmdBuffer, Image& image);

  public:
    // present, hudless, depth and velocity are read in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    // Leaves present unmodified, the result is in GetImage
    bool Dispatch(VkCommandBuffer cmdBuffer, ReprojectionParams& params, VkImageView present, VkExtent2D presentExtent,
                  VkImageView hudless, VkImageView depth, VkImageView velocity = VK_NULL_HANDLE);

    // GetImage can be called only after Dispatch
    // Expected to be in VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, the size of the present
    static constexpr VkFormat OutputFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    Reproject_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice);
    ~Reproject_Vk();
};
