#include "pch.h"
#include "Reprojection_Vk.h"

#include <Config.h>
#include <State.h>
#include <hooks/Vulkan_Hooks.h>

#include <magic_enum.hpp>

// Straight to the driver, the game's call already went through the hooks to get here
template <typename T> static T Original(const char* name) { return (T) VulkanHooks::OriginalSwapchainFunction(name); }

bool Reprojection_Vk::CreateSwapchain(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                      const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
{
    if (device != _device || _gameQueue == VK_NULL_HANDLE)
    {
        LOG_WARN("Device or queue for the reprojection is missing");
        return false;
    }

    if (pCreateInfo->oldSwapchain != VK_NULL_HANDLE && !OwnsSwapchain(pCreateInfo->oldSwapchain))
    {
        LOG_WARN("Old swapchain isn't a reprojection one");
        return false;
    }

    // The presented image is copied out for the shaders and the result is blitted back
    constexpr VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VkSurfaceCapabilitiesKHR surfaceCaps {};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_physicalDevice, pCreateInfo->surface, &surfaceCaps);

    VkFormatProperties formatProperties {};
    vkGetPhysicalDeviceFormatProperties(_physicalDevice, pCreateInfo->imageFormat, &formatProperties);

    const bool canReproject = (surfaceCaps.supportedUsageFlags & usage) == usage &&
                              (formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) != 0;

    auto createInfo = *pCreateInfo;

    if (canReproject)
        createInfo.imageUsage |= usage;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    auto result =
        Original<PFN_vkCreateSwapchainKHR>("vkCreateSwapchainKHR")(device, &createInfo, pAllocator, &swapchain);

    if (result != VK_SUCCESS)
    {
        LOG_ERROR("vkCreateSwapchainKHR error: {}", magic_enum::enum_name(result));
        return false;
    }

    // Only one swapchain can exist for a window, the game destroys the old one through the hooks
    ReleaseSwapchainResources();

    _swapchain = swapchain;
    _swapchainFormat = pCreateInfo->imageFormat;
    _swapchainExtent = pCreateInfo->imageExtent;
    _canReproject = canReproject;

    uint32_t imageCount = 0;
    auto getImages = Original<PFN_vkGetSwapchainImagesKHR>("vkGetSwapchainImagesKHR");
    getImages(device, _swapchain, &imageCount, nullptr);
    _swapchainImages.resize(imageCount);
    getImages(device, _swapchain, &imageCount, _swapchainImages.data());

    _reprojectedSemaphores.resize(imageCount);

    for (auto& semaphore : _reprojectedSemaphores)
    {
        VkSemaphoreCreateInfo semaphoreInfo { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

        if (vkCreateSemaphore(_device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS)
        {
            LOG_ERROR("vkCreateSemaphore failed, reprojection is off");
            _canReproject = false;
        }
    }

    LOG_INFO("Format: {}, extent: {}x{}, images: {}, can reproject: {}", (UINT) _swapchainFormat,
             _swapchainExtent.width, _swapchainExtent.height, imageCount, _canReproject);

    *pSwapchain = _swapchain;
    return true;
}

bool Reprojection_Vk::DestroySwapchain(VkDevice device, VkSwapchainKHR swapchain,
                                       const VkAllocationCallbacks* pAllocator)
{
    if (!OwnsSwapchain(swapchain))
        return false;

    ReleaseSwapchainResources();
    Original<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR")(device, swapchain, pAllocator);
    _swapchain = VK_NULL_HANDLE;

    return true;
}

VkResult Reprojection_Vk::GetSwapchainImages(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount,
                                             VkImage* pSwapchainImages)
{
    return Original<PFN_vkGetSwapchainImagesKHR>("vkGetSwapchainImagesKHR")(device, swapchain, pSwapchainImageCount,
                                                                            pSwapchainImages);
}

VkResult Reprojection_Vk::AcquireNextImage(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
                                           VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex)
{
    return Original<PFN_vkAcquireNextImageKHR>("vkAcquireNextImageKHR")(device, swapchain, timeout, semaphore, fence,
                                                                        pImageIndex);
}

void Reprojection_Vk::SetHdrMetadata(VkDevice device, VkSwapchainKHR swapchain, const VkHdrMetadataEXT* pMetadata)
{
    if (auto setHdrMetadata = Original<PFN_vkSetHdrMetadataEXT>("vkSetHdrMetadataEXT"))
        setHdrMetadata(device, 1, &swapchain, pMetadata);
}

VkResult Reprojection_Vk::QueuePresent(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    auto present = Original<PFN_vkQueuePresentKHR>("vkQueuePresentKHR");

    VkSemaphore reprojected = VK_NULL_HANDLE;
    {
        OwnedLockGuard lock(Mutex, 2);
        reprojected = ReprojectPresent(queue, pPresentInfo);
    }

    if (reprojected == VK_NULL_HANDLE)
        return present(queue, pPresentInfo);

    // The reprojection waits for the game's semaphores, the present only for the reprojection
    auto presentInfo = *pPresentInfo;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &reprojected;

    return present(queue, &presentInfo);
}

VkSemaphore Reprojection_Vk::ReprojectPresent(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    auto mouseDeltaSinceSim = InputCollection::getInstance().readDeltaSinceSim(_frameCount);
    _timeSinceSimStart = Util::GetTimestamp() - mouseDeltaSinceSim.startTimestampNs;

    _reprojectionActive = IsActive();

    // Our command pool is for the game queue's family
    if (!IsActive() || IsPaused() || !_canReproject || queue != _gameQueue || pPresentInfo->swapchainCount != 1)
        return VK_NULL_HANDLE;

    auto fIndex = GetIndex();
    auto hudless = GetResource(FG_ResourceType::HudlessColor, fIndex);
    auto depth = GetResource(FG_ResourceType::Depth, fIndex);
    auto velocity = GetResource(FG_ResourceType::Velocity, fIndex);

    if (!hudless || !depth || hudless->view == VK_NULL_HANDLE || depth->view == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    if (_reproject == nullptr)
        _reproject = std::make_unique<Reproject_Vk>("Reproject", _device, _physicalDevice);

    if (!_reproject->IsInit())
        return VK_NULL_HANDLE;

    if (velocity && velocity->view == VK_NULL_HANDLE)
        velocity.reset();

    FilloutData data {};
    FilloutReprojectionData(data, fIndex);
    data.depthWidth = depth->width;
    data.depthHeight = depth->height;

    if (velocity)
    {
        data.hasMotionVectors = true;
        data.mvWidth = velocity->width;
        data.mvHeight = velocity->height;
    }

    ReprojectionParams params {};
    _reproject->FilloutStruct(data, params);

    auto commandIndex = _commandIndex++ % BUFFER_COUNT;
    auto cmdBuffer = BeginCommandBuffer(commandIndex);

    if (cmdBuffer == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    auto imageIndex = pPresentInfo->pImageIndices[0];
    auto swapchainImage = _swapchainImages[imageIndex];

    VkFGResource presented { FG_ResourceType::Color, swapchainImage };
    presented.format = _swapchainFormat;
    presented.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    presented.imageWidth = presented.width = _swapchainExtent.width;
    presented.imageHeight = presented.height = _swapchainExtent.height;

    // The begun command buffer is just reset on the next use
    if (!CopyImage(cmdBuffer, presented, _presentCopy, _swapchainFormat))
        return VK_NULL_HANDLE;

    // Hudless differing from the present only in sRGB is read with the present's encoding, like on DX12
    if (hudless->format != _swapchainFormat && WithoutSrgb(hudless->format) == WithoutSrgb(_swapchainFormat) &&
        !CopyImage(cmdBuffer, *hudless, _hudlessCopy, _swapchainFormat))
    {
        return VK_NULL_HANDLE;
    }

    std::vector<VkFGResource*> inputs = { &presented, &*hudless, &*depth };

    if (velocity)
        inputs.push_back(&*velocity);

    const auto readLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    for (auto input : inputs)
        ImageBarrier(cmdBuffer, input->image, input->format, input->layout, readLayout);

    bool dispatched = _reproject->Dispatch(cmdBuffer, params, presented.view, _swapchainExtent, hudless->view,
                                           depth->view, velocity ? velocity->view : VK_NULL_HANDLE);

    for (auto input : inputs)
        ImageBarrier(cmdBuffer, input->image, input->format, readLayout, input->layout);

    if (!dispatched)
        return VK_NULL_HANDLE;

    // Only the reprojected area, the rest stays as the game made it
    VkImageBlit region {};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = region.srcSubresource;
    region.srcOffsets[1] = { (int32_t) std::min(params.ScreenWidth, _swapchainExtent.width),
                             (int32_t) std::min(params.ScreenHeight, _swapchainExtent.height), 1 };
    region.dstOffsets[1] = region.srcOffsets[1];

    ImageBarrier(cmdBuffer, swapchainImage, _swapchainFormat, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    vkCmdBlitImage(cmdBuffer, _reproject->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapchainImage,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST);

    ImageBarrier(cmdBuffer, swapchainImage, _swapchainFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    vkEndCommandBuffer(cmdBuffer);

    std::vector<VkPipelineStageFlags> waitStages(pPresentInfo->waitSemaphoreCount, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

    VkSubmitInfo submitInfo { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.waitSemaphoreCount = pPresentInfo->waitSemaphoreCount;
    submitInfo.pWaitSemaphores = pPresentInfo->pWaitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages.data();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &_reprojectedSemaphores[imageIndex];

    vkResetFences(_device, 1, &_fence[commandIndex]);

    if (auto result = vkQueueSubmit(queue, 1, &submitInfo, _fence[commandIndex]); result != VK_SUCCESS)
    {
        LOG_ERROR("vkQueueSubmit error: {}", magic_enum::enum_name(result));
        return VK_NULL_HANDLE;
    }

    _reprojectionActive = !_reproject->IsBypassed();

    return _reprojectedSemaphores[imageIndex];
}

void Reprojection_Vk::ReleaseSwapchainResources()
{
    if (_device == VK_NULL_HANDLE)
        return;

    // The semaphores might still be waited on by a present
    vkDeviceWaitIdle(_device);

    for (auto semaphore : _reprojectedSemaphores)
    {
        if (semaphore != VK_NULL_HANDLE)
            vkDestroySemaphore(_device, semaphore, nullptr);
    }

    _reprojectedSemaphores.clear();
    _swapchainImages.clear();
    _canReproject = false;

    DestroyCopy(_presentCopy);
    DestroyCopy(_hudlessCopy);
}

bool Reprojection_Vk::Present()
{
    // Done in QueuePresent, it needs the presented image
    return false;
}

void Reprojection_Vk::Activate()
{
    if (!_isActive)
    {
        UpdateTarget();
        _isActive = true;
    }
}

void Reprojection_Vk::Deactivate() { _isActive = false; }

void Reprojection_Vk::DestroyFGContext()
{
    Deactivate();

    if (_device == VK_NULL_HANDLE)
        return;

    WaitForCommandBuffers();

    // Stops the raw input hooks too
    _reproject.reset();
}

bool Reprojection_Vk::ReleaseSwapchain(HWND hwnd)
{
    ReleaseSwapchainResources();
    return true;
}

bool Reprojection_Vk::Shutdown()
{
    DestroyFGContext();
    ReleaseSwapchainResources();
    DestroyCommandBuffers();
    return true;
}

std::optional<double> Reprojection_Vk::ReadGpuTime(void* commandQueue)
{
    if (_reproject && _reproject->IsInit())
        return _reproject->ReadGpuTime();

    return std::nullopt;
}

void Reprojection_Vk::EvaluateState(FG_Constants& fgConstants)
{
    OwnedLockGuard lock(Mutex, 555);

    auto& state = State::Instance();

    if (state.isShuttingDown)
        return;

    _constants = fgConstants;

    if (Config::Instance()->IsFGEnabled())
    {
        if (!IsPaused() && !IsActive())
            Activate();
    }
    else
    {
        Deactivate();
    }

    if (state.fgChanged)
    {
        state.fgChanged = false;

        // Pause for 10 frames
        UpdateTarget();
    }

    state.scChanged = false;
}
