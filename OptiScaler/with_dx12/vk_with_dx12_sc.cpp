#include "pch.h"
#include "vk_with_dx12_sc.h"

#include <with_dx12/with_dx12.h>
#include <inputs/FG/VkwDx12_Inputs.h>

#include <hooks/FG_Hooks.h>
#include <hooks/DxgiFactory_Hooks.h>
#include <hooks/Vulkan_Hooks.h>
#include <proxies/DXGI_Proxy.h>
#include <misc/HiddenWindow.h>

#include <Config.h>
#include <menu/menu_common.h>

#include <vulkan/vulkan_win32.h>

#include <unordered_map>

namespace
{
std::mutex queueFamiliesMutex;
std::unordered_map<VkQueue, uint32_t> queueFamilies;

std::mutex inputSlotsMutex;
bool inputSlotPending[BUFFER_COUNT] {};

// The game's image is copied to the UNORM format of its texels, flip model swapchains have no sRGB formats
bool SwapchainFormats(VkFormat format, VkFormat& sharedFormat, DXGI_FORMAT& dxgiFormat)
{
    switch (format)
    {
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
        sharedFormat = VK_FORMAT_B8G8R8A8_UNORM;
        dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
        return true;

    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        sharedFormat = VK_FORMAT_R8G8B8A8_UNORM;
        dxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        return true;

    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        sharedFormat = format;
        dxgiFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
        return true;

    case VK_FORMAT_R16G16B16A16_SFLOAT:
        sharedFormat = format;
        dxgiFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        return true;

    default:
        return false;
    }
}

// Formats of the shared back buffers and FG inputs
DXGI_FORMAT SharedDxgiFormat(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_B8G8R8A8_UNORM:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case VK_FORMAT_R8G8B8A8_UNORM:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case VK_FORMAT_R16G16_SFLOAT:
        return DXGI_FORMAT_R16G16_FLOAT;
    case VK_FORMAT_R32G32_SFLOAT:
        return DXGI_FORMAT_R32G32_FLOAT;
    case VK_FORMAT_R32_SFLOAT:
        return DXGI_FORMAT_R32_FLOAT;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

bool ColorSpace(VkColorSpaceKHR colorSpace, DXGI_COLOR_SPACE_TYPE& dxgiColorSpace)
{
    switch (colorSpace)
    {
    case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
        dxgiColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        return true;

    case VK_COLOR_SPACE_HDR10_ST2084_EXT:
        dxgiColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        return true;

    case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
        dxgiColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
        return true;

    default:
        return false;
    }
}

void Transition(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
}

uint32_t MemoryTypeIndex(VkPhysicalDevice physicalDevice, uint32_t typeBits)
{
    VkPhysicalDeviceMemoryProperties properties {};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);

    for (auto flags : { (VkMemoryPropertyFlags) VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, (VkMemoryPropertyFlags) 0 })
    {
        for (uint32_t i = 0; i < properties.memoryTypeCount; i++)
        {
            if ((typeBits & (1u << i)) != 0 && (properties.memoryTypes[i].propertyFlags & flags) == flags)
                return i;
        }
    }

    return UINT32_MAX;
}

// A D3D12 fence imported as a timeline semaphore
bool CreateSharedFence(VkDevice device, ID3D12Device* dx12Device, ID3D12Fence*& fence, VkSemaphore& semaphore)
{
    if (FAILED(dx12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence))))
        return false;

    HANDLE handle = nullptr;
    if (FAILED(dx12Device->CreateSharedHandle(fence, nullptr, GENERIC_ALL, nullptr, &handle)))
        return false;

    VkSemaphoreTypeCreateInfo typeInfo { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;

    VkSemaphoreCreateInfo createInfo { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    createInfo.pNext = &typeInfo;

    auto importHandle =
        (PFN_vkImportSemaphoreWin32HandleKHR) VulkanHooks::GetDeviceProcAddr(device, "vkImportSemaphoreWin32HandleKHR");

    bool result = false;

    if (importHandle != nullptr && vkCreateSemaphore(device, &createInfo, nullptr, &semaphore) == VK_SUCCESS)
    {
        VkImportSemaphoreWin32HandleInfoKHR importInfo { VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR };
        importInfo.semaphore = semaphore;
        importInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
        importInfo.handle = handle;

        result = importHandle(device, &importInfo) == VK_SUCCESS;
    }

    CloseHandle(handle);
    return result;
}

// Only the parts of the game's chain that keep working on the hidden window's swapchain
struct SwapchainChain
{
    VkImageFormatListCreateInfo formatList {};
    VkSwapchainLatencyCreateInfoNV latency {};

    const void* Filter(const void* next)
    {
        const void* chain = nullptr;

        for (auto it = (const VkBaseInStructure*) next; it != nullptr; it = it->pNext)
        {
            if (it->sType == VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO)
            {
                formatList = *(const VkImageFormatListCreateInfo*) it;
                formatList.pNext = chain;
                chain = &formatList;
            }
            else if (it->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_LATENCY_CREATE_INFO_NV)
            {
                latency = *(const VkSwapchainLatencyCreateInfoNV*) it;
                latency.pNext = chain;
                chain = &latency;
            }
        }

        return chain;
    }
};

size_t PresentStructSize(VkStructureType type)
{
    switch (type)
    {
    case VK_STRUCTURE_TYPE_PRESENT_ID_KHR:
        return sizeof(VkPresentIdKHR);
    case VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR:
        return sizeof(VkPresentId2KHR);
    case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT:
        return sizeof(VkSwapchainPresentFenceInfoEXT);
    case VK_STRUCTURE_TYPE_PRESENT_REGIONS_KHR:
        return sizeof(VkPresentRegionsKHR);
    case VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE:
        return sizeof(VkPresentTimesInfoGOOGLE);
    case VK_STRUCTURE_TYPE_DEVICE_GROUP_PRESENT_INFO_KHR:
        return sizeof(VkDeviceGroupPresentInfoKHR);
    case VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT:
        return sizeof(VkFrameBoundaryEXT);
    default:
        return 0;
    }
}

// The hidden window's swapchain only has its own present mode, the rest of the present's chain is kept
struct PresentChain
{
    std::vector<std::unique_ptr<uint8_t[]>> copies;

    const void* WithoutPresentMode(const void* next)
    {
        auto found = false;

        for (auto it = (const VkBaseInStructure*) next; it != nullptr; it = it->pNext)
            found |= it->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT;

        if (!found)
            return next;

        VkBaseOutStructure* head = nullptr;
        VkBaseOutStructure* tail = nullptr;

        for (auto it = (const VkBaseInStructure*) next; it != nullptr; it = it->pNext)
        {
            if (it->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT)
                continue;

            auto size = PresentStructSize(it->sType);

            if (size == 0)
            {
                LOG_WARN("Dropping present struct {}", (UINT) it->sType);
                continue;
            }

            auto& copy = copies.emplace_back(std::make_unique<uint8_t[]>(size));
            memcpy(copy.get(), it, size);

            auto out = (VkBaseOutStructure*) copy.get();
            out->pNext = nullptr;

            if (tail != nullptr)
                tail->pNext = out;
            else
                head = out;

            tail = out;
        }

        return head;
    }
};
} // namespace

bool VkwDx12SC::Wanted()
{
    auto& state = State::Instance();

    // Every FG input, the Nvngx one has no FG output
    return state.activeFgOutput == FGOutput::XeFG && state.activeFgInput != FGInput::NoFG &&
           state.activeFgInput != FGInput::NvngxFG && !state.vulkanSkipHooks;
}

bool VkwDx12SC::CanShare(VkFormat format) { return SharedDxgiFormat(format) != DXGI_FORMAT_UNKNOWN; }

void VkwDx12SC::DeviceCreated(VkDevice device, const VkDeviceCreateInfo* pCreateInfo)
{
    std::scoped_lock lock(queueFamiliesMutex);

    for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++)
    {
        auto& info = pCreateInfo->pQueueCreateInfos[i];

        // Protected queues are only returned by vkGetDeviceQueue2
        if (info.flags != 0)
            continue;

        for (uint32_t index = 0; index < info.queueCount; index++)
        {
            VkQueue queue = VK_NULL_HANDLE;
            vkGetDeviceQueue(device, info.queueFamilyIndex, index, &queue);

            if (queue != VK_NULL_HANDLE)
                queueFamilies[queue] = info.queueFamilyIndex;
        }
    }
}

bool VkwDx12SC::CreateSwapchain(VkDevice device, VkPhysicalDevice physicalDevice, VkInstance instance, HWND hwnd,
                                const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator,
                                const Functions& functions, VkSwapchainKHR* pSwapchain, VkResult& result)
{
    std::scoped_lock lock(_mutex);

    if (pCreateInfo == nullptr || hwnd == nullptr || instance == VK_NULL_HANDLE)
        return false;

    if (_current != nullptr)
    {
        // Swapchains of the game's other windows stay its own
        if (_current->_hwnd != hwnd || _current->_device != device || pCreateInfo->oldSwapchain != _current->_swapchain)
        {
            LOG_WARN("Not the FG swapchain's window or old swapchain, creating the game's own");
            return false;
        }

        result = _current->Recreate(pCreateInfo, pAllocator);

        if (result == VK_SUCCESS)
            *pSwapchain = _current->_swapchain;

        return true;
    }

    auto sc = new VkwDx12SC();
    sc->_functions = functions;
    sc->_device = device;
    sc->_physicalDevice = physicalDevice;
    sc->_instance = instance;
    sc->_hwnd = hwnd;

    if (!sc->Init(pCreateInfo, pAllocator))
    {
        LOG_WARN("Can't present through DX12, creating the game's own swapchain");
        sc->Destroy(pAllocator);
        delete sc;
        return false;
    }

    _current = sc;
    *pSwapchain = sc->_swapchain;
    result = VK_SUCCESS;

    LOG_INFO("Created VkwDx12SC: swapchain {:X}, {}x{}, {} images, fg12 {:X}", (size_t) sc->_swapchain,
             sc->_extent.width, sc->_extent.height, sc->_swapchainImages.size(), (size_t) sc->_fgSwapChain);

    return true;
}

bool VkwDx12SC::DestroySwapchain(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator)
{
    std::scoped_lock lock(_mutex);

    if (_current == nullptr || swapchain == VK_NULL_HANDLE)
        return false;

    // The game destroys the old swapchain after creating the new one
    if (auto it = std::find(_current->_retired.begin(), _current->_retired.end(), swapchain);
        it != _current->_retired.end())
    {
        _current->WaitForIdle();
        _current->_functions.DestroySwapchain(device, swapchain, pAllocator);
        _current->_retired.erase(it);
        return true;
    }

    if (swapchain != _current->_swapchain)
        return false;

    _current->Destroy(pAllocator);
    delete _current;
    _current = nullptr;

    return true;
}

bool VkwDx12SC::OwnsSwapchain(VkSwapchainKHR swapchain)
{
    std::scoped_lock lock(_mutex);
    return _current != nullptr && swapchain != VK_NULL_HANDLE && swapchain == _current->_swapchain;
}

VkResult VkwDx12SC::QueuePresent(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    std::scoped_lock lock(_mutex);

    if (_current == nullptr)
        return VK_ERROR_SURFACE_LOST_KHR;

    return _current->Present(queue, pPresentInfo);
}

void VkwDx12SC::SetHdrMetadata(VkSwapchainKHR swapchain, const VkHdrMetadataEXT* pMetadata)
{
    std::scoped_lock lock(_mutex);

    if (_current == nullptr || swapchain != _current->_swapchain || pMetadata == nullptr ||
        _current->_fgSwapChain == nullptr)
    {
        return;
    }

    auto primary = [](float value) { return (UINT16) std::clamp(value * 50000.0f, 0.0f, 50000.0f); };

    DXGI_HDR_METADATA_HDR10 metadata {};
    metadata.RedPrimary[0] = primary(pMetadata->displayPrimaryRed.x);
    metadata.RedPrimary[1] = primary(pMetadata->displayPrimaryRed.y);
    metadata.GreenPrimary[0] = primary(pMetadata->displayPrimaryGreen.x);
    metadata.GreenPrimary[1] = primary(pMetadata->displayPrimaryGreen.y);
    metadata.BluePrimary[0] = primary(pMetadata->displayPrimaryBlue.x);
    metadata.BluePrimary[1] = primary(pMetadata->displayPrimaryBlue.y);
    metadata.WhitePoint[0] = primary(pMetadata->whitePoint.x);
    metadata.WhitePoint[1] = primary(pMetadata->whitePoint.y);
    metadata.MaxMasteringLuminance = (UINT) pMetadata->maxLuminance;
    metadata.MinMasteringLuminance = (UINT) (pMetadata->minLuminance * 10000.0f);
    metadata.MaxContentLightLevel = (UINT16) pMetadata->maxContentLightLevel;
    metadata.MaxFrameAverageLightLevel = (UINT16) pMetadata->maxFrameAverageLightLevel;

    auto result = _current->_fgSwapChain->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(metadata), &metadata);

    if (FAILED(result))
        LOG_WARN("SetHDRMetaData failed: {:X}", (UINT) result);
}

bool VkwDx12SC::InteropDevices(VkDevice& device, VkPhysicalDevice& physicalDevice, ID3D12Device*& dx12Device)
{
    std::scoped_lock lock(_mutex);

    if (_current == nullptr || _current->_fgSwapChain == nullptr)
        return false;

    device = _current->_device;
    physicalDevice = _current->_physicalDevice;
    dx12Device = _current->_dx12Device;

    return true;
}

bool VkwDx12SC::CreateSharedImage(VkDevice device, VkPhysicalDevice physicalDevice, ID3D12Device* dx12Device,
                                  VkwDx12Image& image, VkFormat format, uint32_t width, uint32_t height,
                                  VkImageUsageFlags usage)
{
    auto dxgiFormat = SharedDxgiFormat(format);

    if (dxgiFormat == DXGI_FORMAT_UNKNOWN)
    {
        LOG_ERROR("Can't share format {} with D3D12", (UINT) format);
        return false;
    }

    D3D12_HEAP_PROPERTIES heapProperties {};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = dxgiFormat;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

    if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    auto result =
        dx12Device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_SHARED, &desc, D3D12_RESOURCE_STATE_COMMON,
                                            nullptr, IID_PPV_ARGS(&image.resource));

    if (FAILED(result))
    {
        LOG_ERROR("CreateCommittedResource failed: {:X}", (UINT) result);
        return false;
    }

    HANDLE handle = nullptr;
    result = dx12Device->CreateSharedHandle(image.resource, nullptr, GENERIC_ALL, nullptr, &handle);

    if (FAILED(result))
    {
        LOG_ERROR("CreateSharedHandle failed: {:X}", (UINT) result);
        DestroySharedImage(device, image);
        return false;
    }

    VkExternalMemoryImageCreateInfo externalInfo { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
    externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;

    VkImageCreateInfo imageInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.pNext = &externalInfo;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    auto getHandleProperties = (PFN_vkGetMemoryWin32HandlePropertiesKHR) VulkanHooks::GetDeviceProcAddr(
        device, "vkGetMemoryWin32HandlePropertiesKHR");

    VkMemoryWin32HandlePropertiesKHR handleProperties { VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR };

    if (getHandleProperties == nullptr || vkCreateImage(device, &imageInfo, nullptr, &image.image) != VK_SUCCESS ||
        getHandleProperties(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT, handle, &handleProperties) !=
            VK_SUCCESS)
    {
        LOG_ERROR("Can't create the Vulkan image of the shared texture");
        CloseHandle(handle);
        DestroySharedImage(device, image);
        return false;
    }

    VkMemoryRequirements requirements {};
    vkGetImageMemoryRequirements(device, image.image, &requirements);

    VkMemoryDedicatedAllocateInfo dedicatedInfo { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
    dedicatedInfo.image = image.image;

    VkImportMemoryWin32HandleInfoKHR importInfo { VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR };
    importInfo.pNext = &dedicatedInfo;
    importInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
    importInfo.handle = handle;

    VkMemoryAllocateInfo allocateInfo { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocateInfo.pNext = &importInfo;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex =
        MemoryTypeIndex(physicalDevice, requirements.memoryTypeBits & handleProperties.memoryTypeBits);

    auto bound = allocateInfo.memoryTypeIndex != UINT32_MAX &&
                 vkAllocateMemory(device, &allocateInfo, nullptr, &image.memory) == VK_SUCCESS &&
                 vkBindImageMemory(device, image.image, image.memory, 0) == VK_SUCCESS;

    // The import doesn't take the handle's ownership
    CloseHandle(handle);

    if (!bound)
    {
        LOG_ERROR("Can't import the shared texture's memory");
        DestroySharedImage(device, image);
        return false;
    }

    if (usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT))
    {
        VkImageViewCreateInfo viewInfo { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = image.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        if (vkCreateImageView(device, &viewInfo, nullptr, &image.view) != VK_SUCCESS)
        {
            LOG_ERROR("Can't create the shared texture's view");
            DestroySharedImage(device, image);
            return false;
        }
    }

    image.format = format;
    image.width = width;
    image.height = height;
    image.layout = VK_IMAGE_LAYOUT_UNDEFINED;

    return true;
}

void VkwDx12SC::DestroySharedImage(VkDevice device, VkwDx12Image& image)
{
    if (image.view != VK_NULL_HANDLE)
        vkDestroyImageView(device, image.view, nullptr);

    if (image.image != VK_NULL_HANDLE)
        vkDestroyImage(device, image.image, nullptr);

    if (image.memory != VK_NULL_HANDLE)
        vkFreeMemory(device, image.memory, nullptr);

    if (image.resource != nullptr)
        image.resource->Release();

    image = {};
}

bool VkwDx12SC::WaitForInputSlot(int index)
{
    UINT64 value = 0;
    ID3D12Fence* fence = nullptr;

    {
        std::scoped_lock lock(_mutex);

        if (_current == nullptr || index < 0 || index >= BUFFER_COUNT)
            return false;

        fence = _current->_dxDoneFence;
        value = _current->_inputSlotValues[index];
    }

    if (fence == nullptr || fence->GetCompletedValue() >= value)
        return true;

    auto event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    auto done =
        SUCCEEDED(fence->SetEventOnCompletion(value, event)) && WaitForSingleObject(event, 1000) == WAIT_OBJECT_0;
    CloseHandle(event);

    if (!done)
        LOG_WARN("D3D12 is still using FG input slot {}", index);

    return done;
}

void VkwDx12SC::InputSlotUsed(int index)
{
    if (index < 0 || index >= BUFFER_COUNT)
        return;

    std::scoped_lock lock(inputSlotsMutex);
    inputSlotPending[index] = true;
}

bool VkwDx12SC::Init(const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator)
{
    if (!SwapchainFormats(pCreateInfo->imageFormat, _sharedFormat, _dxgiFormat))
    {
        LOG_INFO("Swapchain format {} can't be presented through DX12", (UINT) pCreateInfo->imageFormat);
        return false;
    }

    DXGI_COLOR_SPACE_TYPE dxgiColorSpace {};
    if (!ColorSpace(pCreateInfo->imageColorSpace, dxgiColorSpace) || pCreateInfo->imageArrayLayers != 1)
    {
        LOG_INFO("Swapchain color space {} can't be presented through DX12", (UINT) pCreateInfo->imageColorSpace);
        return false;
    }

    _swapchainFormat = pCreateInfo->imageFormat;
    _colorSpace = pCreateInfo->imageColorSpace;
    _gamePresentMode = pCreateInfo->presentMode;
    _extent = pCreateInfo->imageExtent;

    if (!CreateDx12Objects() || !CreateSyncObjects())
        return false;

    try
    {
        _hiddenHwnd = CreateHiddenSwapchainWindow();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Can't create the hidden window: {}", e.what());
        return false;
    }

    VkWin32SurfaceCreateInfoKHR surfaceInfo { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    surfaceInfo.hinstance = GetModuleHandleW(nullptr);
    surfaceInfo.hwnd = _hiddenHwnd;

    auto createSurface = _functions.CreateWin32Surface;

    if (createSurface == nullptr)
        createSurface =
            (PFN_vkCreateWin32SurfaceKHR) VulkanHooks::GetInstanceProcAddr(_instance, "vkCreateWin32SurfaceKHR");

    if (createSurface == nullptr || createSurface(_instance, &surfaceInfo, nullptr, &_hiddenSurface) != VK_SUCCESS)
    {
        LOG_ERROR("Can't create the hidden window's surface");
        return false;
    }

    if (CreateRealSwapchain(pCreateInfo, pAllocator, VK_NULL_HANDLE) != VK_SUCCESS || !CreateFGSwapchain())
        return false;

    if (_colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
    {
        auto result = _fgSwapChain->SetColorSpace1(dxgiColorSpace);

        if (FAILED(result))
            LOG_WARN("SetColorSpace1 failed: {:X}", (UINT) result);
    }

    return CreatePresentImages();
}

bool VkwDx12SC::CreateDx12Objects()
{
    ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
    ScopedSkipVulkanHooks skipVulkanHooks {};

    // The D3D12 device has to be on the game's GPU
    VkPhysicalDeviceIDProperties idProperties { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
    VkPhysicalDeviceProperties2 properties { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    properties.pNext = &idProperties;
    vkGetPhysicalDeviceProperties2(_physicalDevice, &properties);

    if (!idProperties.deviceLUIDValid)
    {
        LOG_ERROR("The Vulkan device has no LUID");
        return false;
    }

    HRESULT result;

    if (DxgiProxy::Module() == nullptr)
        result = CreateDXGIFactory2(0, IID_PPV_ARGS(&_factory));
    else
        result = DxgiProxy::CreateDxgiFactory2_()(0, __uuidof(IDXGIFactory4), (IDXGIFactory2**) &_factory);

    if (FAILED(result) || _factory == nullptr)
    {
        LOG_ERROR("Can't create the DXGI factory: {:X}", (UINT) result);
        return false;
    }

    LUID luid {};
    memcpy(&luid, idProperties.deviceLUID, sizeof(luid));

    IDXGIAdapter* adapter = nullptr;
    result = _factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter));

    if (FAILED(result) || adapter == nullptr)
    {
        LOG_ERROR("No DXGI adapter with the Vulkan device's LUID: {:X}", (UINT) result);
        return false;
    }

    _dx12Device = WithDx12::RequestD3D12Device(D3D_FEATURE_LEVEL_12_0, adapter);
    _dx12Queue = WithDx12::GetD3D12CommandQueue();
    adapter->Release();

    if (_dx12Device == nullptr || _dx12Queue == nullptr)
    {
        LOG_ERROR("Can't create the D3D12 device");
        return false;
    }

    IDXGIFactory5* factory5 = nullptr;
    if (SUCCEEDED(_factory->QueryInterface(IID_PPV_ARGS(&factory5))))
    {
        BOOL tearing = FALSE;
        _tearingSupported =
            SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) &&
            tearing;
        factory5->Release();
    }

    return true;
}

bool VkwDx12SC::CreateSyncObjects()
{
    _fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    if (_fenceEvent == nullptr || !CreateSharedFence(_device, _dx12Device, _vkDoneFence, _vkDone) ||
        !CreateSharedFence(_device, _dx12Device, _dxDoneFence, _dxDone))
    {
        LOG_ERROR("Can't create the shared fences");
        return false;
    }

    return true;
}

VkResult VkwDx12SC::CreateRealSwapchain(const VkSwapchainCreateInfoKHR* pCreateInfo,
                                        const VkAllocationCallbacks* pAllocator, VkSwapchainKHR oldSwapchain)
{
    // The hidden window's surface has the window's size
    SetWindowPos(_hiddenHwnd, nullptr, 0, 0, (int) pCreateInfo->imageExtent.width,
                 (int) pCreateInfo->imageExtent.height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    VkSurfaceCapabilitiesKHR caps {};
    auto result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_physicalDevice, _hiddenSurface, &caps);

    if (result != VK_SUCCESS)
    {
        LOG_ERROR("vkGetPhysicalDeviceSurfaceCapabilitiesKHR: {}", (int) result);
        return result;
    }

    if (caps.currentExtent.width != UINT32_MAX && (caps.currentExtent.width != pCreateInfo->imageExtent.width ||
                                                   caps.currentExtent.height != pCreateInfo->imageExtent.height))
    {
        LOG_ERROR("Hidden window is {}x{}, the swapchain {}x{}", caps.currentExtent.width, caps.currentExtent.height,
                  pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Its presents mustn't wait for the display, the FG swapchain does
    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(_physicalDevice, _hiddenSurface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(_physicalDevice, _hiddenSurface, &modeCount, modes.data());

    auto presentMode = VK_PRESENT_MODE_FIFO_KHR;

    for (auto mode : { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR })
    {
        if (std::find(modes.begin(), modes.end(), mode) != modes.end())
            presentMode = mode;
    }

    auto usage = pCreateInfo->imageUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    if ((caps.supportedUsageFlags & usage) != usage)
    {
        LOG_ERROR("Hidden window's swapchain doesn't support usage {:X}", (UINT) usage);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    SwapchainChain chain;

    VkSwapchainCreateInfoKHR createInfo = *pCreateInfo;
    createInfo.pNext = chain.Filter(pCreateInfo->pNext);
    createInfo.flags &= VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;
    createInfo.surface = _hiddenSurface;
    createInfo.minImageCount = std::max(pCreateInfo->minImageCount, caps.minImageCount);
    createInfo.imageUsage = usage;
    createInfo.preTransform = caps.currentTransform;
    createInfo.presentMode = presentMode;
    createInfo.oldSwapchain = oldSwapchain;

    if (caps.maxImageCount != 0)
        createInfo.minImageCount = std::min(createInfo.minImageCount, caps.maxImageCount);

    if ((caps.supportedCompositeAlpha & createInfo.compositeAlpha) == 0)
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    result = _functions.CreateSwapchain(_device, &createInfo, pAllocator, &swapchain);

    if (result != VK_SUCCESS)
    {
        LOG_ERROR("Can't create the hidden window's swapchain: {}", (int) result);
        return result;
    }

    uint32_t count = 0;
    _functions.GetSwapchainImages(_device, swapchain, &count, nullptr);
    _swapchainImages.resize(count);
    _functions.GetSwapchainImages(_device, swapchain, &count, _swapchainImages.data());

    _swapchain = swapchain;
    _swapchainFormat = pCreateInfo->imageFormat;
    _gamePresentMode = pCreateInfo->presentMode;
    _extent = pCreateInfo->imageExtent;

    LOG_DEBUG("Hidden window swapchain {:X}: {}x{}, {} images, present mode {}", (size_t) swapchain, _extent.width,
              _extent.height, count, (int) presentMode);

    return VK_SUCCESS;
}

bool VkwDx12SC::CreateFGSwapchain()
{
    DXGI_SWAP_CHAIN_DESC desc {};
    desc.BufferDesc.Width = _extent.width;
    desc.BufferDesc.Height = _extent.height;
    desc.BufferDesc.Format = _dxgiFormat;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = std::clamp((UINT) _swapchainImages.size(), 2u, 16u);
    desc.OutputWindow = _hwnd;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Flags = _tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    _fgFlags = desc.Flags;

    IDXGISwapChain* swapchain = nullptr;
    HRESULT result;

    {
        DxgiFactoryHooks::ScopedSkipFGSCCreation skipFGSCCreation {};
        result = FGHooks::CreateSwapChain(_factory, _dx12Queue, &desc, &swapchain);
    }

    if (FAILED(result) || swapchain == nullptr)
    {
        LOG_ERROR("Can't create the FG swapchain: {:X}", (UINT) result);
        return false;
    }

    swapchain->QueryInterface(IID_PPV_ARGS(&_fgSwapChain));
    swapchain->Release();

    if (_fgSwapChain == nullptr)
        return false;

    auto& state = State::Instance();
    state.currentSwapchainDesc = desc;
    state.currentD3D12Device = _dx12Device;
    state.currentCommandQueue = _dx12Queue;
    state.swapchainInteropApi = SwapchainInteropApi::VkwDx12;
    state.SCAllowTearing = (desc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0;
    state.SCLastFlags = desc.Flags;

    return true;
}

bool VkwDx12SC::ResizeFGSwapchain()
{
    DXGI_SWAP_CHAIN_DESC1 desc {};
    _fgSwapChain->GetDesc1(&desc);

    const auto count = std::clamp((UINT) _swapchainImages.size(), 2u, 16u);

    if (desc.Width == _extent.width && desc.Height == _extent.height && desc.Format == _dxgiFormat &&
        desc.BufferCount == count)
    {
        return true;
    }

    auto result = _fgSwapChain->ResizeBuffers(count, _extent.width, _extent.height, _dxgiFormat, _fgFlags);

    if (FAILED(result))
    {
        LOG_ERROR("FG swapchain ResizeBuffers failed: {:X}", (UINT) result);
        return false;
    }

    auto& state = State::Instance();
    _fgSwapChain->GetDesc(&state.currentSwapchainDesc);
    state.scChanged = true;

    if (Config::Instance()->IsFGEnabled())
    {
        state.fgResetCapturedResources = true;
        state.fgOnlyUseCapturedResources = false;
        state.fgChanged = true;
    }

    return true;
}

VkResult VkwDx12SC::Recreate(const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator)
{
    VkFormat sharedFormat;
    DXGI_FORMAT dxgiFormat;
    DXGI_COLOR_SPACE_TYPE dxgiColorSpace {};

    if (!SwapchainFormats(pCreateInfo->imageFormat, sharedFormat, dxgiFormat) ||
        !ColorSpace(pCreateInfo->imageColorSpace, dxgiColorSpace))
    {
        // The window is DX12's now, Vulkan can't have it back while the FG swapchain lives
        LOG_ERROR("Swapchain format {} or color space {} can't be presented through DX12",
                  (UINT) pCreateInfo->imageFormat, (UINT) pCreateInfo->imageColorSpace);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    WaitForIdle();
    DestroyPresentImages();

    auto oldSwapchain = _swapchain;
    auto result = CreateRealSwapchain(pCreateInfo, pAllocator, oldSwapchain);

    if (result != VK_SUCCESS)
        return result;

    _retired.push_back(oldSwapchain);
    _sharedFormat = sharedFormat;
    _dxgiFormat = dxgiFormat;

    if (!ResizeFGSwapchain() || !CreatePresentImages())
        return VK_ERROR_INITIALIZATION_FAILED;

    // Games switching HDR on or off recreate the swapchain with the new format and color space
    if (pCreateInfo->imageColorSpace != _colorSpace)
    {
        auto hr = _fgSwapChain->SetColorSpace1(dxgiColorSpace);

        if (FAILED(hr))
            LOG_WARN("SetColorSpace1 failed: {:X}", (UINT) hr);

        _colorSpace = pCreateInfo->imageColorSpace;
    }

    LOG_INFO("Recreated VkwDx12SC: swapchain {:X}, {}x{}, {} images, format {}, color space {}", (size_t) _swapchain,
             _extent.width, _extent.height, _swapchainImages.size(), (UINT) _swapchainFormat, (UINT) _colorSpace);

    return VK_SUCCESS;
}

bool VkwDx12SC::CreatePresentImages()
{
    _images.resize(_swapchainImages.size());

    for (auto& presentImage : _images)
    {
        if (!CreateSharedImage(_device, _physicalDevice, _dx12Device, presentImage.shared, _sharedFormat, _extent.width,
                               _extent.height, VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        {
            return false;
        }

        VkFenceCreateInfo fenceInfo { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VkSemaphoreCreateInfo semaphoreInfo { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

        if (vkCreateFence(_device, &fenceInfo, nullptr, &presentImage.fence) != VK_SUCCESS ||
            vkCreateSemaphore(_device, &semaphoreInfo, nullptr, &presentImage.presentSemaphore) != VK_SUCCESS)
        {
            LOG_ERROR("Can't create the present's sync objects");
            return false;
        }

        if (FAILED(_dx12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                       IID_PPV_ARGS(&presentImage.allocator))) ||
            FAILED(_dx12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, presentImage.allocator, nullptr,
                                                  IID_PPV_ARGS(&presentImage.commandList))))
        {
            LOG_ERROR("Can't create the D3D12 copy command list");
            return false;
        }

        presentImage.commandList->Close();
    }

    // Command buffers are allocated for the queue family of the present
    _commandPoolFamily = UINT32_MAX;

    return true;
}

void VkwDx12SC::DestroyPresentImages()
{
    if (_commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(_device, _commandPool, nullptr);
        _commandPool = VK_NULL_HANDLE;
    }

    for (auto& presentImage : _images)
    {
        DestroySharedImage(_device, presentImage.shared);

        if (presentImage.fence != VK_NULL_HANDLE)
            vkDestroyFence(_device, presentImage.fence, nullptr);

        if (presentImage.presentSemaphore != VK_NULL_HANDLE)
            vkDestroySemaphore(_device, presentImage.presentSemaphore, nullptr);

        if (presentImage.commandList != nullptr)
            presentImage.commandList->Release();

        if (presentImage.allocator != nullptr)
            presentImage.allocator->Release();
    }

    _images.clear();
    _commandPoolFamily = UINT32_MAX;
}

bool VkwDx12SC::PrepareCommandPool(uint32_t queueFamily)
{
    if (_commandPool != VK_NULL_HANDLE && queueFamily == _commandPoolFamily)
        return true;

    WaitForIdle();

    if (_commandPool != VK_NULL_HANDLE)
        vkDestroyCommandPool(_device, _commandPool, nullptr);

    _commandPool = VK_NULL_HANDLE;

    VkCommandPoolCreateInfo poolInfo { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily;

    if (vkCreateCommandPool(_device, &poolInfo, nullptr, &_commandPool) != VK_SUCCESS)
    {
        LOG_ERROR("Can't create the command pool");
        return false;
    }

    for (auto& presentImage : _images)
    {
        VkCommandBufferAllocateInfo allocateInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        allocateInfo.commandPool = _commandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(_device, &allocateInfo, &presentImage.commandBuffer) != VK_SUCCESS)
        {
            LOG_ERROR("Can't allocate the command buffers");
            return false;
        }
    }

    _commandPoolFamily = queueFamily;
    return true;
}

bool VkwDx12SC::WaitForDx12(UINT64 value)
{
    if (_dxDoneFence == nullptr || _dxDoneFence->GetCompletedValue() >= value)
        return true;

    if (FAILED(_dxDoneFence->SetEventOnCompletion(value, _fenceEvent)) ||
        WaitForSingleObject(_fenceEvent, 5000) != WAIT_OBJECT_0)
    {
        LOG_ERROR("D3D12 wait failed, value {}, completed {}", value, _dxDoneFence->GetCompletedValue());
        return false;
    }

    return true;
}

void VkwDx12SC::WaitForIdle()
{
    for (auto& presentImage : _images)
    {
        if (presentImage.submitted)
            vkWaitForFences(_device, 1, &presentImage.fence, VK_TRUE, 5'000'000'000ull);

        presentImage.submitted = false;
    }

    if (_dx12Queue != nullptr && _dxDoneFence != nullptr && SUCCEEDED(_dx12Queue->Signal(_dxDoneFence, ++_dxDoneValue)))
        WaitForDx12(_dxDoneValue);
}

void VkwDx12SC::Destroy(const VkAllocationCallbacks* pAllocator)
{
    WaitForIdle();
    DestroyPresentImages();

    if (_swapchain != VK_NULL_HANDLE)
        _functions.DestroySwapchain(_device, _swapchain, pAllocator);

    _swapchain = VK_NULL_HANDLE;

    if (_hiddenSurface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(_instance, _hiddenSurface, nullptr);

    _hiddenSurface = VK_NULL_HANDLE;

    auto& state = State::Instance();

    if (_fgSwapChain != nullptr)
    {
        // Like a DX12 game's last release, FGHooks then releases the FG's swapchain context
        if (auto fg = state.currentFG; fg != nullptr && fg->IsActive())
            fg->Deactivate();

        if (state.currentSwapchain == _fgSwapChain)
            state.currentSwapchain = nullptr;

        _fgSwapChain->Release();
        _fgSwapChain = nullptr;

        state.swapchainInteropApi = SwapchainInteropApi::None;
    }

    if (_vkDone != VK_NULL_HANDLE)
        vkDestroySemaphore(_device, _vkDone, nullptr);

    if (_dxDone != VK_NULL_HANDLE)
        vkDestroySemaphore(_device, _dxDone, nullptr);

    _vkDone = VK_NULL_HANDLE;
    _dxDone = VK_NULL_HANDLE;

    if (_vkDoneFence != nullptr)
        _vkDoneFence->Release();

    if (_dxDoneFence != nullptr)
        _dxDoneFence->Release();

    _vkDoneFence = nullptr;
    _dxDoneFence = nullptr;

    if (_fenceEvent != nullptr)
        CloseHandle(_fenceEvent);

    _fenceEvent = nullptr;

    if (_factory != nullptr)
        _factory->Release();

    _factory = nullptr;
}

VkResult VkwDx12SC::Present(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    const auto index = pPresentInfo->pImageIndices[0];

    uint32_t queueFamily = UINT32_MAX;
    {
        std::scoped_lock lock(queueFamiliesMutex);

        if (auto it = queueFamilies.find(queue); it != queueFamilies.end())
            queueFamily = it->second;
    }

    if (index >= _images.size() || queueFamily == UINT32_MAX || !PrepareCommandPool(queueFamily))
    {
        LOG_ERROR("Can't present image {} from queue {:X}", index, (size_t) queue);
        return VK_ERROR_OUT_OF_DATE_KHR;
    }

    auto& presentImage = _images[index];

    if (presentImage.submitted)
    {
        vkWaitForFences(_device, 1, &presentImage.fence, VK_TRUE, 5'000'000'000ull);
        vkResetFences(_device, 1, &presentImage.fence);
        presentImage.submitted = false;
    }

    if (!RecordCopy(presentImage, _swapchainImages[index]))
        return VK_ERROR_OUT_OF_DATE_KHR;

    // Waits for the game's frame and the last D3D12 copy from the shared texture
    std::vector<VkSemaphore> waits(pPresentInfo->pWaitSemaphores,
                                   pPresentInfo->pWaitSemaphores + pPresentInfo->waitSemaphoreCount);
    std::vector<uint64_t> waitValues(waits.size(), 0);
    waits.push_back(_dxDone);
    waitValues.push_back(presentImage.copiedValue);
    std::vector<VkPipelineStageFlags> waitStages(waits.size(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

    const VkSemaphore signals[] = { _vkDone, presentImage.presentSemaphore };
    const uint64_t signalValues[] = { ++_vkDoneValue, 0 };

    VkTimelineSemaphoreSubmitInfo timelineInfo { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    timelineInfo.waitSemaphoreValueCount = (uint32_t) waitValues.size();
    timelineInfo.pWaitSemaphoreValues = waitValues.data();
    timelineInfo.signalSemaphoreValueCount = 2;
    timelineInfo.pSignalSemaphoreValues = signalValues;

    VkSubmitInfo submitInfo { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.pNext = &timelineInfo;
    submitInfo.waitSemaphoreCount = (uint32_t) waits.size();
    submitInfo.pWaitSemaphores = waits.data();
    submitInfo.pWaitDstStageMask = waitStages.data();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &presentImage.commandBuffer;
    submitInfo.signalSemaphoreCount = 2;
    submitInfo.pSignalSemaphores = signals;

    auto vkResult = vkQueueSubmit(queue, 1, &submitInfo, presentImage.fence);

    if (vkResult != VK_SUCCESS)
    {
        LOG_ERROR("vkQueueSubmit: {}", (int) vkResult);
        return vkResult;
    }

    presentImage.submitted = true;

    // D3D12 takes the frame once Vulkan copied it
    if (FAILED(_dx12Queue->Wait(_vkDoneFence, _vkDoneValue)) || !CopyToFGBackBuffer(presentImage))
        return VK_ERROR_DEVICE_LOST;

    auto result = _fgSwapChain->Present(SyncInterval(), PresentFlags());

    if (auto fg = State::Instance().currentFG; SUCCEEDED(result) && fg != nullptr && fg->IsActive() && !fg->IsPaused())
        MenuCommon::FGOutputFramesPresented(fg->GetInterpolatedFrameCount() + 1);

    // Covers the FG's copies of the frame's inputs
    if (SUCCEEDED(_dx12Queue->Signal(_dxDoneFence, ++_dxDoneValue)))
    {
        std::scoped_lock lock(inputSlotsMutex);

        for (int i = 0; i < BUFFER_COUNT; i++)
        {
            if (inputSlotPending[i])
                _inputSlotValues[i] = _dxDoneValue;

            inputSlotPending[i] = false;
        }
    }

    if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET || result == DXGI_ERROR_DEVICE_HUNG)
    {
        LOG_ERROR("FG Present: {:X}", (UINT) result);
        return VK_ERROR_DEVICE_LOST;
    }

    if (FAILED(result))
        LOG_WARN("FG Present: {:X}", (UINT) result);

    // The hidden window's present gives the image back to the game
    PresentChain chain;

    VkPresentInfoKHR presentInfo = *pPresentInfo;
    presentInfo.pNext = chain.WithoutPresentMode(pPresentInfo->pNext);
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &presentImage.presentSemaphore;

    vkResult = _functions.QueuePresent(queue, &presentInfo);

    // The hidden window doesn't follow the game's, its new size is for the game to pick up
    RECT rect {};
    if (vkResult == VK_SUCCESS && GetClientRect(_hwnd, &rect) && rect.right > 0 && rect.bottom > 0 &&
        ((uint32_t) rect.right != _extent.width || (uint32_t) rect.bottom != _extent.height))
    {
        vkResult = VK_SUBOPTIMAL_KHR;
    }

    return vkResult;
}

bool VkwDx12SC::RecordCopy(PresentImage& presentImage, VkImage swapchainImage)
{
    auto commandBuffer = presentImage.commandBuffer;

    VkCommandBufferBeginInfo beginInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        return false;

    // FG inputs valid until present, before _vkDone lets D3D12 read them
    VkwDx12Inputs::RecordPresentCopies(commandBuffer, _commandPoolFamily);

    VkImageMemoryBarrier barriers[2] {};

    for (auto& barrier : barriers)
    {
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    }

    barriers[0].image = swapchainImage;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    barriers[1].image = presentImage.shared.image;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                         nullptr, 0, nullptr, 2, barriers);

    VkImageCopy region {};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.extent = { _extent.width, _extent.height, 1 };

    vkCmdCopyImage(commandBuffer, swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, presentImage.shared.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[0].dstAccessMask = 0;

    barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                         nullptr, 0, nullptr, 2, barriers);

    return vkEndCommandBuffer(commandBuffer) == VK_SUCCESS;
}

bool VkwDx12SC::CopyToFGBackBuffer(PresentImage& presentImage)
{
    // The allocator's last list is done once its copy is
    if (!WaitForDx12(presentImage.copiedValue))
        return false;

    if (FAILED(presentImage.allocator->Reset()) ||
        FAILED(presentImage.commandList->Reset(presentImage.allocator, nullptr)))
    {
        LOG_ERROR("Can't reset the D3D12 copy command list");
        return false;
    }

    ID3D12Resource* backBuffer = nullptr;
    auto result = _fgSwapChain->GetBuffer(_fgSwapChain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&backBuffer));

    if (FAILED(result) || backBuffer == nullptr)
    {
        LOG_ERROR("FG GetBuffer failed: {:X}", (UINT) result);
        presentImage.commandList->Close();
        return false;
    }

    auto commandList = presentImage.commandList;
    auto shared = presentImage.shared.resource;

    Transition(commandList, shared, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(commandList, backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);

    commandList->CopyResource(backBuffer, shared);

    Transition(commandList, backBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    Transition(commandList, shared, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);

    backBuffer->Release();

    if (FAILED(commandList->Close()))
        return false;

    ID3D12CommandList* lists[] = { commandList };
    _dx12Queue->ExecuteCommandLists(1, lists);

    if (FAILED(_dx12Queue->Signal(_dxDoneFence, ++_dxDoneValue)))
        return false;

    presentImage.copiedValue = _dxDoneValue;
    return true;
}

UINT VkwDx12SC::SyncInterval() const
{
    return _gamePresentMode == VK_PRESENT_MODE_FIFO_KHR || _gamePresentMode == VK_PRESENT_MODE_FIFO_RELAXED_KHR ? 1 : 0;
}

UINT VkwDx12SC::PresentFlags() const
{
    return _gamePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR && (_fgFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0
               ? DXGI_PRESENT_ALLOW_TEARING
               : 0;
}
