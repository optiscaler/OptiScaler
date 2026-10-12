#pragma once
#include "SysUtils.h"
#include "IFGFeature.h"

#include <vulkan/vulkan.h>

struct VkFGResource
{
    FG_ResourceType type;
    VkImage image = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t imageWidth = 0; // Whole image
    uint32_t imageHeight = 0;
    uint32_t width = 0; // Used area
    uint32_t height = 0;
    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
    FG_ResourceValidity validity = FG_ResourceValidity::ValidNow;
    int frameIndex = -1;
};

class IFGFeature_Vk : public virtual IFGFeature
{
  protected:
    struct ImageCopy
    {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        uint32_t width = 0;
        uint32_t height = 0;
    };

  private:
    ImageCopy _copies[BUFFER_COUNT][ResourceTypeCOUNT] {};

    std::vector<VkDeviceQueueCreateInfo> _queueCreateInfos;
    std::vector<std::vector<float>> _queuePriorities;

  protected:
    // Queues the output needs next to the game's, filled in by the output's constructor
    struct ExtraQueue
    {
        VkQueueFlags flags = 0;
        bool present = false;
        uint32_t family = UINT32_MAX;
        uint32_t index = 0;
        VkQueue queue = VK_NULL_HANDLE;
    };

    std::vector<ExtraQueue> _extraQueues;

    VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
    VkDevice _device = VK_NULL_HANDLE;
    VkQueue _gameQueue = VK_NULL_HANDLE;
    uint32_t _gameQueueFamily = UINT32_MAX;

    // The swapchain handle the game got from the output
    VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
    VkFormat _swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D _swapchainExtent {};

    UINT64 _fgFramePresentId = 0;
    UINT64 _lastFGFramePresentId = 0;

    std::unordered_map<FG_ResourceType, VkFGResource> _frameResources[BUFFER_COUNT] {};
    std::shared_mutex _resourceMutex[BUFFER_COUNT];

    void NewFrame() override final;
    std::optional<VkFGResource> GetResource(FG_ResourceType type, int index = -1);

    static void ImageBarrier(VkCommandBuffer cmdBuffer, VkImage image, VkFormat format, VkImageLayout oldLayout,
                             VkImageLayout newLayout);

    // Copies the resource into target in the given format, then points the resource to the copy.
    // Formats only differing in sRGB keep the stored values, others get blitted.
    bool CopyImage(VkCommandBuffer cmdBuffer, VkFGResource& resource, ImageCopy& target, VkFormat format);
    void DestroyCopy(ImageCopy& copy);

  public:
    // Called around vkCreateDevice to add the extra queues to the game's device
    void ReserveQueues(VkPhysicalDevice physicalDevice, VkDeviceCreateInfo& createInfo);
    void OnDeviceCreated(VkDevice device);

    bool HasSwapchain() const { return _swapchain != VK_NULL_HANDLE; }
    bool OwnsSwapchain(VkSwapchainKHR swapchain) const
    {
        return swapchain != VK_NULL_HANDLE && swapchain == _swapchain;
    }
    bool OwnsQueue(VkQueue queue) const;

    // The game's swapchain calls, the hooks route them here for the output's swapchain
    virtual bool CreateSwapchain(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                 const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) = 0;
    // Returns false if the swapchain isn't the output's
    virtual bool DestroySwapchain(VkDevice device, VkSwapchainKHR swapchain,
                                  const VkAllocationCallbacks* pAllocator) = 0;
    virtual VkResult GetSwapchainImages(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount,
                                        VkImage* pSwapchainImages) = 0;
    virtual VkResult AcquireNextImage(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
                                      VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex) = 0;
    virtual VkResult QueuePresent(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) = 0;
    virtual void SetHdrMetadata(VkDevice device, VkSwapchainKHR swapchain, const VkHdrMetadataEXT* pMetadata) = 0;

    virtual void EvaluateState(FG_Constants& fgConstants) = 0;

    bool SetResource(VkFGResource* inputResource);
    bool HasResource(FG_ResourceType type, int index = -1) override final;

    IFGFeature_Vk() = default;
    virtual ~IFGFeature_Vk();
};
