#pragma once
#include <pch.h>
#include <vulkan/vulkan.h>
#include <string>
#include <vector>

#include <shaders/Shader_Vk.h>

class ResourceCopy_Vk : public Shader_Vk
{
  private:
    static constexpr int MAX_FRAMES_IN_FLIGHT = 3;

    VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> _descriptorSets;
    int _currentSetIndex = 0;

    // VkSampler _nearestSampler = VK_NULL_HANDLE;

    // Intermediate image resources
    VkImage _intermediateImage = VK_NULL_HANDLE;
    VkImageView _intermediateImageView = VK_NULL_HANDLE;
    VkDeviceMemory _intermediateMemory = VK_NULL_HANDLE;
    uint32_t _width = 0;
    uint32_t _height = 0;
    VkFormat _format = VK_FORMAT_UNDEFINED;

    VkImageView _currentInResourceView = VK_NULL_HANDLE;
    VkImageView _currentOutResourceView = VK_NULL_HANDLE;

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

    void CreateDescriptorSetLayout();
    void CreateDescriptorPool();
    void CreateDescriptorSets();
    void UpdateDescriptorSet(VkCommandBuffer cmdList, int setIndex, VkImageView inputView, VkImageView outputView);

    bool InitializeViews(VkImageView InResourceView, VkImageView OutResourceView);

  protected:
    // Same copy pipeline with another shader that has the same bindings
    ResourceCopy_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice,
                    const std::vector<char>& InShaderCode);

  public:
    bool CreateImageResource(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height,
                             VkFormat format, VkImageUsageFlags usage);
    void ReleaseImageResource();

    bool Dispatch(VkDevice InDevice, VkCommandBuffer InCmdList, VkImageView InResourceView, VkImageView OutResourceView,
                  VkExtent2D OutExtent);

    VkImage GetImage() const { return _intermediateImage; }
    VkImageView GetImageView() const { return _intermediateImageView; }
    bool CanRender() const { return _init; }

    void SetImageLayout(VkCommandBuffer cmdBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                        VkImageSubresourceRange subresourceRange);

    ResourceCopy_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice);
    ~ResourceCopy_Vk();
};
