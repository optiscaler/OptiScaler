#pragma once
#include "SysUtils.h"
#include "IFGFeature.h"

#include <shaders/ui_unblend/UiUnblend_Vk.h>
#include <shaders/resource_flip/RF_Vk.h>

#include <vulkan/vulkan.h>

struct VkFGResource
{
    FG_ResourceType type;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE; // Sampled view of the image, if the input or a copy has one
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
    ImageCopy _flipped[BUFFER_COUNT][ResourceTypeCOUNT] {};

    std::unique_ptr<UiUnblend_Vk> _uiUnblend;
    std::unique_ptr<RF_Vk> _depthFlip;
    std::unique_ptr<RF_Vk> _mvFlip;

    std::vector<VkDeviceQueueCreateInfo> _queueCreateInfos;
    std::vector<std::vector<float>> _queuePriorities;

    // (Re)creates the image when the format or size changed
    bool CreateCopy(ImageCopy& copy, FG_ResourceType type, VkFormat format, uint32_t width, uint32_t height,
                    VkImageUsageFlags usage);

    // Flips the copied depth or velocity of Unity games into a storage image, points the resource to it
    bool FlipResource(VkCommandBuffer cmdBuffer, VkFGResource& resource, int fIndex);

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

    // Command buffers of the output for the game queue, _fence is signaled by their submit
    VkCommandPool _commandPool[BUFFER_COUNT] {};
    VkCommandBuffer _commandBuffer[BUFFER_COUNT] {};
    VkFence _fence[BUFFER_COUNT] {};

    // Waits until the GPU is done with the previous use of the index, then begins it
    VkCommandBuffer BeginCommandBuffer(int index);
    void WaitForCommandBuffers();
    void DestroyCommandBuffers();

    std::unordered_map<FG_ResourceType, VkFGResource> _frameResources[BUFFER_COUNT] {};
    std::shared_mutex _resourceMutex[BUFFER_COUNT];

    void NewFrame() override final;
    std::optional<VkFGResource> GetResource(FG_ResourceType type, int index = -1);

    static void ImageBarrier(VkCommandBuffer cmdBuffer, VkImage image, VkFormat format, VkImageLayout oldLayout,
                             VkImageLayout newLayout);

    // Copies the resource into target in the given format, then points the resource to the copy.
    // Formats only differing in sRGB keep the stored values, others get blitted.
    bool CopyImage(VkCommandBuffer cmdBuffer, VkFGResource& resource, ImageCopy& target, VkFormat format);

    // Destroyed once in flight frames are done with it
    void DestroyCopy(ImageCopy& copy);

    // The copy SetResource made of the resource, nullptr if it points to something else
    const ImageCopy* OwnCopy(const VkFGResource& resource, int fIndex) const;

  public:
    static VkFormat WithoutSrgb(VkFormat format);

    // Called around vkCreateDevice to add what the output needs, like the extra queues, to the game's device
    virtual void PrepareDevice(VkPhysicalDevice physicalDevice, VkDeviceCreateInfo& createInfo);
    virtual void OnDeviceCreated(VkDevice device);

    bool HasSwapchain() const { return _swapchain != VK_NULL_HANDLE; }
    VkFormat SwapchainFormat() const { return _swapchainFormat; }
    VkExtent2D SwapchainExtent() const { return _swapchainExtent; }
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

    // Removes the current frame's UI copy from its hudless copy, for games blending a UI texture on the frame
    void RemoveUiFromHudless(VkCommandBuffer cmdBuffer, bool premultiplied);

    bool HasResource(FG_ResourceType type, int index = -1) override final;

    IFGFeature_Vk() = default;
    virtual ~IFGFeature_Vk();
};
