#include <pch.h>

#include "IFeature_VkwDx12.h"

#include <Config.h>
#include <SysUtils.h>

#include <proxies/DXGI_Proxy.h>
#include <proxies/D3D12_Proxy.h>

#include <hooks/VulkanwDx12_Hooks.h>
#include <with_dx12/with_dx12.h>

#include <misc/IdentifyGpu.h>
#include <misc/DeferredRelease.h>

#include <magic_enum.hpp>
#include <detours/detours.h>
#include <imgui/ImGuiNotify.hpp>

// Used Nukem's VKToDX as a base
// https://github.com/Nukem9/dlssg-to-fsr3/blob/eca4a79b4d23339a1dcf02e30b9f3bafe7901513/source/maindll/FFFrameInterpolatorVKToDX.cpp

#define ASSIGN_VK_DESC(dest, src, width, height, format)                                                               \
    dest.Width = width;                                                                                                \
    dest.Height = height;                                                                                              \
    dest.Format = format;

#define SAFE_DESTROY_VK(func, device, handle, allocator)                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (handle != VK_NULL_HANDLE)                                                                                  \
        {                                                                                                              \
            func(device, handle, allocator);                                                                           \
            handle = VK_NULL_HANDLE;                                                                                   \
        }                                                                                                              \
    } while ((void) 0, 0)

void IFeature_VkwDx12::SetVkObjectName(VkDevice device, VkObjectType objectType, uint64_t objectHandle,
                                       const char* name)
{
    static PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT = nullptr;

    if (vkSetDebugUtilsObjectNameEXT == nullptr)
        vkSetDebugUtilsObjectNameEXT = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetInstanceProcAddr(VulkanInstance, "vkSetDebugUtilsObjectNameEXT"));

    VkDebugUtilsObjectNameInfoEXT info {};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = objectType;
    info.objectHandle = objectHandle;
    info.pObjectName = name;

    vkSetDebugUtilsObjectNameEXT(device, &info);
}

static bool IsDepthFormat(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return true;

    default:
        return false;
    }
}

static bool HasStencil(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return true;

    default:
        return false;
    }
}

// Helper function to convert Vulkan formats to DXGI formats
static DXGI_FORMAT VkFormatToDxgiFormat(VkFormat vkFormat)
{
    switch (vkFormat)
    {
    // 8-bit formats
    case VK_FORMAT_R8_UNORM:
        return DXGI_FORMAT_R8_UNORM;
    case VK_FORMAT_R8_SNORM:
        return DXGI_FORMAT_R8_SNORM;
    case VK_FORMAT_R8_UINT:
        return DXGI_FORMAT_R8_UINT;
    case VK_FORMAT_R8_SINT:
        return DXGI_FORMAT_R8_SINT;
    case VK_FORMAT_R8G8_UNORM:
        return DXGI_FORMAT_R8G8_UNORM;
    case VK_FORMAT_R8G8_SNORM:
        return DXGI_FORMAT_R8G8_SNORM;
    case VK_FORMAT_R8G8_UINT:
        return DXGI_FORMAT_R8G8_UINT;
    case VK_FORMAT_R8G8_SINT:
        return DXGI_FORMAT_R8G8_SINT;
    case VK_FORMAT_R8G8B8A8_UNORM:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_R8G8B8A8_SNORM:
        return DXGI_FORMAT_R8G8B8A8_SNORM;
    case VK_FORMAT_R8G8B8A8_UINT:
        return DXGI_FORMAT_R8G8B8A8_UINT;
    case VK_FORMAT_R8G8B8A8_SINT:
        return DXGI_FORMAT_R8G8B8A8_SINT;
    case VK_FORMAT_R8G8B8A8_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case VK_FORMAT_B8G8R8A8_UNORM:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case VK_FORMAT_B8G8R8A8_SNORM:
        return DXGI_FORMAT_UNKNOWN; // Not directly supported
    case VK_FORMAT_B8G8R8A8_UINT:
        return DXGI_FORMAT_UNKNOWN; // Not directly supported
    case VK_FORMAT_B8G8R8A8_SINT:
        return DXGI_FORMAT_UNKNOWN; // Not directly supported
    case VK_FORMAT_B8G8R8A8_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_A8B8G8R8_SNORM_PACK32:
        return DXGI_FORMAT_R8G8B8A8_SNORM;
    case VK_FORMAT_A8B8G8R8_UINT_PACK32:
        return DXGI_FORMAT_R8G8B8A8_UINT;
    case VK_FORMAT_A8B8G8R8_SINT_PACK32:
        return DXGI_FORMAT_R8G8B8A8_SINT;
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

    // 16-bit formats
    case VK_FORMAT_R16_UNORM:
        return DXGI_FORMAT_R16_UNORM;
    case VK_FORMAT_R16_SNORM:
        return DXGI_FORMAT_R16_SNORM;
    case VK_FORMAT_R16_UINT:
        return DXGI_FORMAT_R16_UINT;
    case VK_FORMAT_R16_SINT:
        return DXGI_FORMAT_R16_SINT;
    case VK_FORMAT_R16_SFLOAT:
        return DXGI_FORMAT_R16_FLOAT;
    case VK_FORMAT_R16G16_UNORM:
        return DXGI_FORMAT_R16G16_UNORM;
    case VK_FORMAT_R16G16_SNORM:
        return DXGI_FORMAT_R16G16_SNORM;
    case VK_FORMAT_R16G16_UINT:
        return DXGI_FORMAT_R16G16_UINT;
    case VK_FORMAT_R16G16_SINT:
        return DXGI_FORMAT_R16G16_SINT;
    case VK_FORMAT_R16G16_SFLOAT:
        return DXGI_FORMAT_R16G16_FLOAT;
    case VK_FORMAT_R16G16B16A16_UNORM:
        return DXGI_FORMAT_R16G16B16A16_UNORM;
    case VK_FORMAT_R16G16B16A16_SNORM:
        return DXGI_FORMAT_R16G16B16A16_SNORM;
    case VK_FORMAT_R16G16B16A16_UINT:
        return DXGI_FORMAT_R16G16B16A16_UINT;
    case VK_FORMAT_R16G16B16A16_SINT:
        return DXGI_FORMAT_R16G16B16A16_SINT;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;

    // 32-bit formats
    case VK_FORMAT_R32_UINT:
        return DXGI_FORMAT_R32_UINT;
    case VK_FORMAT_R32_SINT:
        return DXGI_FORMAT_R32_SINT;
    case VK_FORMAT_R32_SFLOAT:
        return DXGI_FORMAT_R32_FLOAT;
    case VK_FORMAT_R32G32_UINT:
        return DXGI_FORMAT_R32G32_UINT;
    case VK_FORMAT_R32G32_SINT:
        return DXGI_FORMAT_R32G32_SINT;
    case VK_FORMAT_R32G32_SFLOAT:
        return DXGI_FORMAT_R32G32_FLOAT;
    case VK_FORMAT_R32G32B32_UINT:
        return DXGI_FORMAT_R32G32B32_UINT;
    case VK_FORMAT_R32G32B32_SINT:
        return DXGI_FORMAT_R32G32B32_SINT;
    case VK_FORMAT_R32G32B32_SFLOAT:
        return DXGI_FORMAT_R32G32B32_FLOAT;
    case VK_FORMAT_R32G32B32A32_UINT:
        return DXGI_FORMAT_R32G32B32A32_UINT;
    case VK_FORMAT_R32G32B32A32_SINT:
        return DXGI_FORMAT_R32G32B32A32_SINT;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;

    // Packed formats
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
        return DXGI_FORMAT_R11G11B10_FLOAT;
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case VK_FORMAT_A2R10G10B10_UINT_PACK32:
        return DXGI_FORMAT_R10G10B10A2_UINT;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case VK_FORMAT_A2B10G10R10_UINT_PACK32:
        return DXGI_FORMAT_R10G10B10A2_UINT;
    case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
        return DXGI_FORMAT_R9G9B9E5_SHAREDEXP;

    // Depth/Stencil formats
    case VK_FORMAT_D16_UNORM:
        return DXGI_FORMAT_D16_UNORM;
    case VK_FORMAT_D32_SFLOAT:
        return DXGI_FORMAT_D32_FLOAT;
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    case VK_FORMAT_X8_D24_UNORM_PACK32:
        return DXGI_FORMAT_D24_UNORM_S8_UINT; // Closest match

    // Compressed formats - BC
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        return DXGI_FORMAT_BC1_UNORM;
    case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        return DXGI_FORMAT_BC1_UNORM_SRGB;
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        return DXGI_FORMAT_BC1_UNORM;
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        return DXGI_FORMAT_BC1_UNORM_SRGB;
    case VK_FORMAT_BC2_UNORM_BLOCK:
        return DXGI_FORMAT_BC2_UNORM;
    case VK_FORMAT_BC2_SRGB_BLOCK:
        return DXGI_FORMAT_BC2_UNORM_SRGB;
    case VK_FORMAT_BC3_UNORM_BLOCK:
        return DXGI_FORMAT_BC3_UNORM;
    case VK_FORMAT_BC3_SRGB_BLOCK:
        return DXGI_FORMAT_BC3_UNORM_SRGB;
    case VK_FORMAT_BC4_UNORM_BLOCK:
        return DXGI_FORMAT_BC4_UNORM;
    case VK_FORMAT_BC4_SNORM_BLOCK:
        return DXGI_FORMAT_BC4_SNORM;
    case VK_FORMAT_BC5_UNORM_BLOCK:
        return DXGI_FORMAT_BC5_UNORM;
    case VK_FORMAT_BC5_SNORM_BLOCK:
        return DXGI_FORMAT_BC5_SNORM;
    case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        return DXGI_FORMAT_BC6H_UF16;
    case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        return DXGI_FORMAT_BC6H_SF16;
    case VK_FORMAT_BC7_UNORM_BLOCK:
        return DXGI_FORMAT_BC7_UNORM;
    case VK_FORMAT_BC7_SRGB_BLOCK:
        return DXGI_FORMAT_BC7_UNORM_SRGB;

    // Special formats
    case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
        return DXGI_FORMAT_B4G4R4A4_UNORM;
    case VK_FORMAT_B5G6R5_UNORM_PACK16:
        return DXGI_FORMAT_B5G6R5_UNORM;
    case VK_FORMAT_B5G5R5A1_UNORM_PACK16:
        return DXGI_FORMAT_B5G5R5A1_UNORM;
    case VK_FORMAT_A4R4G4B4_UNORM_PACK16:
        return DXGI_FORMAT_B4G4R4A4_UNORM; // Swizzled equivalent

    default:
        LOG_WARN("Unknown Vulkan format mapping: {} ({})", (int) vkFormat, magic_enum::enum_name(vkFormat));
        return DXGI_FORMAT_UNKNOWN;
    }
}

void IFeature_VkwDx12::ResourceBarrier(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource,
                                       D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState)
{
    if (beforeState == afterState)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = beforeState;
    barrier.Transition.StateAfter = afterState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
}

bool IFeature_VkwDx12::LoadVulkanExternalMemoryFunctions()
{
    LOG_FUNC();

    if (vkGetMemoryWin32HandlePropertiesKHR == nullptr && VulkanGDPA != nullptr)
    {
        vkGetMemoryWin32HandlePropertiesKHR =
            (PFN_vkGetMemoryWin32HandlePropertiesKHR) VulkanGDPA(VulkanDevice, "vkGetMemoryWin32HandlePropertiesKHR");
    }

    if (vkImportSemaphoreWin32HandleKHR == nullptr && VulkanGDPA != nullptr)
    {
        vkImportSemaphoreWin32HandleKHR =
            (PFN_vkImportSemaphoreWin32HandleKHR) VulkanGDPA(VulkanDevice, "vkImportSemaphoreWin32HandleKHR");
    }

    bool result = vkGetMemoryWin32HandlePropertiesKHR != nullptr && vkImportSemaphoreWin32HandleKHR != nullptr;

    if (!result)
        LOG_ERROR("Failed to load Vulkan external memory functions!");

    return result;
}

bool IFeature_VkwDx12::CreateVulkanCommandBuffers(uint32_t queueFamilyIndex)
{
    LOG_FUNC();

    if (VulkanQueueCommandBuffers.contains(queueFamilyIndex))
        return true;

    VulkanQueueCommandBuffers.emplace(queueFamilyIndex, QUERY_INDEX_BUFFERS {});

    auto& b = VulkanQueueCommandBuffers[queueFamilyIndex];

    for (uint32_t i = 0; i < VKDX12_BUFFER_COUNT; i++)
    {
        if (b.VulkanCopyCommandPool[i] == VK_NULL_HANDLE)
        {
            VkCommandPoolCreateInfo poolInfo = {};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = queueFamilyIndex;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

            VkResult result = vkCreateCommandPool(VulkanDevice, &poolInfo, nullptr, &b.VulkanCopyCommandPool[i]);
            if (result != VK_SUCCESS)
            {
                LOG_ERROR("vkCreateCommandPool error: {0:x}", (int) result);
                return false;
            }

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(VulkanDevice, VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t) b.VulkanCopyCommandPool[i],
                            "VulkanCopyCommandPool");
#endif
        }

        if (b.VulkanCopyCommandBuffer[i] == VK_NULL_HANDLE)
        {
            VkCommandBufferAllocateInfo allocInfo = {};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = b.VulkanCopyCommandPool[i];
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;

            VkResult result = vkAllocateCommandBuffers(VulkanDevice, &allocInfo, &b.VulkanCopyCommandBuffer[i]);
            if (result != VK_SUCCESS)
            {
                LOG_ERROR("vkAllocateCommandBuffers error: {0:x}", (int) result);
                return false;
            }

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(VulkanDevice, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t) b.VulkanCopyCommandBuffer[i],
                            "VulkanCopyCommandBuffer");
#endif
        }

        if (b.VulkanBarrierCommandPool[i] == VK_NULL_HANDLE)
        {
            VkCommandPoolCreateInfo poolInfo = {};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = queueFamilyIndex;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

            VkResult result = vkCreateCommandPool(VulkanDevice, &poolInfo, nullptr, &b.VulkanBarrierCommandPool[i]);
            if (result != VK_SUCCESS)
            {
                LOG_ERROR("vkCreateCommandPool error: {0:x}", (int) result);
                return false;
            }

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(VulkanDevice, VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t) b.VulkanBarrierCommandPool[i],
                            "VulkanBarrierCommandPool");
#endif
        }

        if (b.VulkanBarrierCommandBuffer[i] == VK_NULL_HANDLE)
        {
            VkCommandBufferAllocateInfo allocInfo = {};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = b.VulkanBarrierCommandPool[i];
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;

            VkResult result = vkAllocateCommandBuffers(VulkanDevice, &allocInfo, &b.VulkanBarrierCommandBuffer[i]);
            if (result != VK_SUCCESS)
            {
                LOG_ERROR("vkAllocateCommandBuffers error: {0:x}", (int) result);
                return false;
            }

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(VulkanDevice, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t) b.VulkanBarrierCommandBuffer[i],
                            "VulkanBarrierCommandBuffer");
#endif
        }
    }

    return true;
}

uint32_t IFeature_VkwDx12::FindVulkanMemoryTypeIndex(uint32_t MemoryTypeBits, VkMemoryPropertyFlags PropertyFlags)
{
    VkPhysicalDeviceMemoryProperties memoryProperties = {};
    vkGetPhysicalDeviceMemoryProperties(VulkanPhysicalDevice, &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
    {
        if (((1u << i) & MemoryTypeBits) == 0)
            continue;

        if ((memoryProperties.memoryTypes[i].propertyFlags & PropertyFlags) != PropertyFlags)
            continue;

        return i;
    }

    if (PropertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    {
        LOG_WARN("Device-local memory not available, trying without it");
        VkMemoryPropertyFlags fallbackFlags = PropertyFlags & ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
        {
            if (((1u << i) & MemoryTypeBits) == 0)
                continue;

            if ((memoryProperties.memoryTypes[i].propertyFlags & fallbackFlags) == fallbackFlags)
                return i;
        }
    }

    LOG_ERROR("No compatible memory type found! MemoryTypeBits={:x}, PropertyFlags={:x}", MemoryTypeBits,
              (uint32_t) PropertyFlags);

    return 0xFFFFFFFF;
}

bool IFeature_VkwDx12::CreateSharedTexture(const VkImageCreateInfo& ImageInfo, VkImage& VulkanResource,
                                           VkDeviceMemory& VulkanMemory, ID3D12Resource*& D3D12Resource, bool InOutput)
{
    const D3D12_HEAP_PROPERTIES d3d12HeapProperties = {
        .Type = D3D12_HEAP_TYPE_DEFAULT,
        .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
    };

    // Convert Vulkan format to DXGI format
    DXGI_FORMAT dxgiFormat = VkFormatToDxgiFormat(ImageInfo.format);
    if (dxgiFormat == DXGI_FORMAT_UNKNOWN)
    {
        LOG_ERROR("Unsupported VkFormat for D3D12 interop: {} ({})", (int) ImageInfo.format,
                  magic_enum::enum_name(ImageInfo.format));
        return false;
    }

    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    if (InOutput)
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    const D3D12_RESOURCE_DESC d3d12ResourceDesc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Alignment = 0,
        .Width = ImageInfo.extent.width,
        .Height = ImageInfo.extent.height,
        .DepthOrArraySize = static_cast<uint16_t>(ImageInfo.extent.depth),
        .MipLevels = static_cast<uint16_t>(ImageInfo.mipLevels),
        .Format = dxgiFormat,
        .SampleDesc = { .Count = 1, .Quality = 0 },
        .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
        .Flags = flags,
    };

    ID3D12Resource* createdResourceDX = nullptr;
    auto hr = _dx11on12Device->CreateCommittedResource(&d3d12HeapProperties, D3D12_HEAP_FLAG_SHARED, &d3d12ResourceDesc,
                                                       D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                       IID_PPV_ARGS(&createdResourceDX));

    if (FAILED(hr))
    {
        LOG_ERROR("Failed to create D3D12 committed resource: {0:x}", hr);
        return false;
    }

    HANDLE win32Handle = nullptr;
    hr = _dx11on12Device->CreateSharedHandle(createdResourceDX, nullptr, GENERIC_ALL, nullptr, &win32Handle);

    if (FAILED(hr))
    {
        LOG_ERROR("Failed to create shared handle: {0:x}", hr);
        createdResourceDX->Release();
        return false;
    }

    // Vulkan makes us create an image and allocate its backing memory by hand...
    //
    // "A VkExternalMemoryImageCreateInfo structure with a non-zero handleTypes field must
    // be included in the creation parameters for an image that will be bound to memory that
    // is either exported or imported."
    const VkExternalMemoryImageCreateInfo externalMemoryImageCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT,
    };

    auto imageCreateInfoCopy = ImageInfo;
    imageCreateInfoCopy.pNext = &externalMemoryImageCreateInfo;

    VkImage createdResourceVK = VK_NULL_HANDLE;
    if (vkCreateImage(VulkanDevice, &imageCreateInfoCopy, nullptr, &createdResourceVK) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create Vulkan image");
        CloseHandle(win32Handle);
        createdResourceDX->Release();
        return false;
    }

    VkMemoryRequirements memoryRequirements = {};
    vkGetImageMemoryRequirements(VulkanDevice, createdResourceVK, &memoryRequirements);

    VkMemoryWin32HandlePropertiesKHR memoryWin32HandleProperties = {};
    memoryWin32HandleProperties.sType = VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR;

    if (vkGetMemoryWin32HandlePropertiesKHR(VulkanDevice, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT,
                                            win32Handle, &memoryWin32HandleProperties) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to get memory Win32 handle properties");
        vkDestroyImage(VulkanDevice, createdResourceVK, nullptr);
        CloseHandle(win32Handle);
        createdResourceDX->Release();
        return false;
    }

    // "To import memory from a Windows handle, add a VkImportMemoryWin32HandleInfoKHR structure
    // to the pNext chain of the VkMemoryAllocateInfo structure."
    const VkMemoryDedicatedAllocateInfo dedicatedAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .image = createdResourceVK,
        .buffer = VK_NULL_HANDLE,
    };

    const VkImportMemoryWin32HandleInfoKHR importMemoryWin32HandleInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
        .pNext = &dedicatedAllocInfo,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT,
        .handle = win32Handle,
    };

    VkMemoryPropertyFlags memoryFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    uint32_t allowedBits = memoryRequirements.memoryTypeBits & memoryWin32HandleProperties.memoryTypeBits;

    uint32_t typeIndex = FindVulkanMemoryTypeIndex(allowedBits, memoryFlags);

    const VkMemoryAllocateInfo memoryAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importMemoryWin32HandleInfo,
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = typeIndex,
    };

    if (memoryAllocInfo.memoryTypeIndex == 0xFFFFFFFF)
    {
        LOG_ERROR("Failed to find compatible memory type for shared texture. Format: {}, MemoryTypeBits: {:x}",
                  magic_enum::enum_name(ImageInfo.format), memoryWin32HandleProperties.memoryTypeBits);
        vkDestroyImage(VulkanDevice, createdResourceVK, nullptr);
        CloseHandle(win32Handle);
        createdResourceDX->Release();
        return false;
    }

    VkDeviceMemory createdMemory = VK_NULL_HANDLE;
    if (vkAllocateMemory(VulkanDevice, &memoryAllocInfo, nullptr, &createdMemory) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to allocate Vulkan memory");
        vkDestroyImage(VulkanDevice, createdResourceVK, nullptr);
        CloseHandle(win32Handle);
        createdResourceDX->Release();
        return false;
    }

    if (vkBindImageMemory(VulkanDevice, createdResourceVK, createdMemory, 0) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to bind image memory");
        vkDestroyImage(VulkanDevice, createdResourceVK, nullptr);
        vkFreeMemory(VulkanDevice, createdMemory, nullptr);
        CloseHandle(win32Handle);
        createdResourceDX->Release();
        return false;
    }

    VulkanResource = createdResourceVK;
    VulkanMemory = createdMemory;
    D3D12Resource = createdResourceDX;

    CloseHandle(win32Handle);
    return true;
}

bool IFeature_VkwDx12::CopyTextureFromVkToDx12(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Resource_VK* InParam,
                                               VK_TEXTURE2D_RESOURCE_C* OutResource, ResourceCopy_Vk* InCopyShader,
                                               bool InCopy, bool InDepth)
{
    // Convert VkFormat to DXGI_FORMAT early for validation
    DXGI_FORMAT dxgiFormat = VkFormatToDxgiFormat(InParam->Resource.ImageViewInfo.Format);
    if (dxgiFormat == DXGI_FORMAT_UNKNOWN)
    {
        LOG_ERROR("Unsupported VkFormat for D3D12 interop: {} ({})", (int) InParam->Resource.ImageViewInfo.Format,
                  magic_enum::enum_name(InParam->Resource.ImageViewInfo.Format));
        return false;
    }

    // Check if this is a depth format
    bool isDepthFormat = InDepth; // IsDepthFormat(InParam->Resource.ImageViewInfo.Format);

    // Check if we need to create a new shared resource
    if (OutResource->Width != InParam->Resource.ImageViewInfo.Width ||
        OutResource->Height != InParam->Resource.ImageViewInfo.Height ||
        OutResource->Format != InParam->Resource.ImageViewInfo.Format || OutResource->VkSharedImage == VK_NULL_HANDLE)
    {
        ReleaseSharedResource(*OutResource);

        ASSIGN_VK_DESC((*OutResource), (*OutResource), InParam->Resource.ImageViewInfo.Width,
                       InParam->Resource.ImageViewInfo.Height, InParam->Resource.ImageViewInfo.Format);

        // Create image info for shared texture
        VkImageCreateInfo imageCreateInfo = {};
        imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.format = !isDepthFormat ? InParam->Resource.ImageViewInfo.Format : VK_FORMAT_R32_SFLOAT;
        imageCreateInfo.extent.width = InParam->Resource.ImageViewInfo.Width;
        imageCreateInfo.extent.height = InParam->Resource.ImageViewInfo.Height;
        imageCreateInfo.extent.depth = 1;
        imageCreateInfo.mipLevels = 1;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;

        imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (!CreateSharedTexture(imageCreateInfo, OutResource->VkSharedImage, OutResource->VkSharedMemory,
                                 OutResource->Dx12Resource, !InCopy))
        {
            if (State::Instance().isRunningOnLinux)
                ImGui::InsertNotification(
                    { ImGuiToastType::Warning, 10000,
                      "Failed to create a shared texture\nMake sure you are using at least Wine/Proton 11" });

            LOG_ERROR("Failed to create shared texture!");
            return false;
        }

        OutResource->VkSharedImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        OutResource->VkSharedImageAccess = VK_ACCESS_NONE;

        // Output is written by D3D12 first, a later transition from UNDEFINED could discard it
        if (!InCopy)
        {
            VkImageMemoryBarrier imageBarrier {};
            imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.image = OutResource->VkSharedImage;
            imageBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCmdPipelineBarrier(InCmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &imageBarrier);

            OutResource->VkSharedImageLayout = VK_IMAGE_LAYOUT_GENERAL;
        }

        // Verify the resource descriptor
        D3D12_RESOURCE_DESC actualDesc = OutResource->Dx12Resource->GetDesc();
        LOG_DEBUG("Successfully created shared D3D12 resource: {}x{}, format={} ({}) (VK: {}x{}, {} ({}))",
                  actualDesc.Width, actualDesc.Height, magic_enum::enum_name(actualDesc.Format),
                  (int) actualDesc.Format, InParam->Resource.ImageViewInfo.Width,
                  InParam->Resource.ImageViewInfo.Height, magic_enum::enum_name(InParam->Resource.ImageViewInfo.Format),
                  (int) InParam->Resource.ImageViewInfo.Format);
    }

    if (InCopy)
    {
        OutResource->VkSourceImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        OutResource->VkSourceImageAccess = VK_ACCESS_SHADER_READ_BIT;
    }
    else
    {
        OutResource->VkSourceImageLayout = VK_IMAGE_LAYOUT_GENERAL;
        OutResource->VkSourceImageAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    }

    // Copy from source to shared image if requested
    if (InCopy && InParam->Resource.ImageViewInfo.Image != VK_NULL_HANDLE)
    {
        if (isDepthFormat)
        {
            if (!DT->CanRender())
            {
                LOG_ERROR("DepthTransfer_Vk not initialized!");
                return false;
            }

            VkImageMemoryBarrier imageBarriers[2] = {};
            imageBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageBarriers[0].oldLayout = OutResource->VkSharedImageLayout;
            imageBarriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            imageBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarriers[0].image = OutResource->VkSharedImage;
            imageBarriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageBarriers[0].subresourceRange.baseMipLevel = 0;
            imageBarriers[0].subresourceRange.levelCount = 1;
            imageBarriers[0].subresourceRange.baseArrayLayer = 0;
            imageBarriers[0].subresourceRange.layerCount = 1;
            imageBarriers[0].srcAccessMask = OutResource->VkSharedImageAccess;
            imageBarriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

            imageBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageBarriers[1].oldLayout = OutResource->VkSourceImageLayout;
            imageBarriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarriers[1].image = InParam->Resource.ImageViewInfo.Image;
            imageBarriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

            if (HasStencil(InParam->Resource.ImageViewInfo.Format))
                imageBarriers[1].subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

            imageBarriers[1].subresourceRange.baseMipLevel = 0;
            imageBarriers[1].subresourceRange.levelCount = 1;
            imageBarriers[1].subresourceRange.baseArrayLayer = 0;
            imageBarriers[1].subresourceRange.layerCount = 1;
            imageBarriers[1].srcAccessMask = OutResource->VkSourceImageAccess;
            imageBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(InCmdBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 2, imageBarriers);

            // Create image views for depth transfer
            if (OutResource->VkSharedImageView == VK_NULL_HANDLE)
            {
                VkImageViewCreateInfo dstViewInfo = {};
                dstViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                dstViewInfo.image = OutResource->VkSharedImage;
                dstViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                dstViewInfo.format = VK_FORMAT_R32_SFLOAT;
                dstViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                dstViewInfo.subresourceRange.baseMipLevel = 0;
                dstViewInfo.subresourceRange.levelCount = 1;
                dstViewInfo.subresourceRange.baseArrayLayer = 0;
                dstViewInfo.subresourceRange.layerCount = 1;

                if (vkCreateImageView(VulkanDevice, &dstViewInfo, nullptr, &OutResource->VkSharedImageView) !=
                    VK_SUCCESS)
                {
                    LOG_ERROR("Failed to create destination image view!");
                    return false;
                }
            }

            // Dispatch depth transfer compute shader
            VkExtent2D extent = { InParam->Resource.ImageViewInfo.Width, InParam->Resource.ImageViewInfo.Height };
            if (!DT->Dispatch(VulkanDevice, InCmdBuffer, InParam->Resource.ImageViewInfo.ImageView,
                              OutResource->VkSharedImageView, extent))
            {
                LOG_ERROR("Failed to dispatch depth transfer!");
                return false;
            }

            imageBarriers[0].oldLayout = imageBarriers[0].newLayout;
            imageBarriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageBarriers[0].srcAccessMask = imageBarriers[0].dstAccessMask;
            imageBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            imageBarriers[1].oldLayout = imageBarriers[1].newLayout;
            imageBarriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageBarriers[1].srcAccessMask = imageBarriers[1].dstAccessMask;
            imageBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(InCmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                 0, 0, nullptr, 0, nullptr, 2, imageBarriers);

            OutResource->VkSharedImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            OutResource->VkSharedImageAccess = VK_ACCESS_SHADER_READ_BIT;
            OutResource->VkSourceImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            OutResource->VkSourceImageAccess = VK_ACCESS_SHADER_READ_BIT;
        }
        else
        {
            if (!InCopyShader->CanRender())
            {
                LOG_ERROR("ResourceCopy_Vk not initialized!");
                return false;
            }

            // Create image views for depth transfer
            if (OutResource->VkSharedImageView == VK_NULL_HANDLE)
            {
                VkImageViewCreateInfo dstViewInfo = {};
                dstViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                dstViewInfo.image = OutResource->VkSharedImage;
                dstViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                dstViewInfo.format = InParam->Resource.ImageViewInfo.Format;
                dstViewInfo.subresourceRange = InParam->Resource.ImageViewInfo.SubresourceRange;

                if (vkCreateImageView(VulkanDevice, &dstViewInfo, nullptr, &OutResource->VkSharedImageView) !=
                    VK_SUCCESS)
                {
                    LOG_ERROR("Failed to create destination image view!");
                    return false;
                }
            }

            {
                if (!Config::Instance()->VulkanUseCopyForInputs.value_or_default())
                {
                    VkImageMemoryBarrier imageBarrier = {};
                    imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    imageBarrier.oldLayout = OutResource->VkSharedImageLayout;
                    imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    imageBarrier.image = OutResource->VkSharedImage;
                    imageBarrier.subresourceRange = InParam->Resource.ImageViewInfo.SubresourceRange;
                    imageBarrier.srcAccessMask = OutResource->VkSharedImageAccess;
                    imageBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    vkCmdPipelineBarrier(InCmdBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                         &imageBarrier);

                    VkExtent2D extent = { InParam->Resource.ImageViewInfo.Width,
                                          InParam->Resource.ImageViewInfo.Height };
                    if (!InCopyShader->Dispatch(VulkanDevice, InCmdBuffer, InParam->Resource.ImageViewInfo.ImageView,
                                                OutResource->VkSharedImageView, extent))
                    {
                        LOG_ERROR("Failed to dispatch resource copy!");
                        return false;
                    }

                    imageBarrier.oldLayout = imageBarrier.newLayout;
                    imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    imageBarrier.srcAccessMask = imageBarrier.dstAccessMask;
                    imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    vkCmdPipelineBarrier(InCmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                         &imageBarrier);

                    OutResource->VkSharedImageLayout = imageBarrier.newLayout;
                    OutResource->VkSharedImageAccess = imageBarrier.dstAccessMask;
                }
                else
                {
                    VkImageMemoryBarrier imageBarriers[2] = { {}, {} };

                    imageBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    imageBarriers[0].oldLayout = OutResource->VkSourceImageLayout;
                    imageBarriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    imageBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    imageBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    imageBarriers[0].image = InParam->Resource.ImageViewInfo.Image;
                    imageBarriers[0].subresourceRange = InParam->Resource.ImageViewInfo.SubresourceRange;
                    imageBarriers[0].srcAccessMask = OutResource->VkSourceImageAccess;
                    imageBarriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

                    imageBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    imageBarriers[1].oldLayout = OutResource->VkSharedImageLayout;
                    imageBarriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    imageBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    imageBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    imageBarriers[1].image = OutResource->VkSharedImage;
                    imageBarriers[1].subresourceRange = InParam->Resource.ImageViewInfo.SubresourceRange;
                    imageBarriers[1].srcAccessMask = OutResource->VkSharedImageAccess;
                    imageBarriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

                    // Single batched barrier call for pre-copy transitions
                    vkCmdPipelineBarrier(InCmdBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, imageBarriers);

                    VkExtent3D extent = { InParam->Resource.ImageViewInfo.Width, InParam->Resource.ImageViewInfo.Height,
                                          1 };

                    VkImageCopy copyRegion = {};
                    copyRegion.extent = extent;
                    copyRegion.dstSubresource.aspectMask = imageBarriers[0].subresourceRange.aspectMask;
                    copyRegion.dstSubresource.mipLevel = imageBarriers[0].subresourceRange.baseMipLevel;
                    copyRegion.dstSubresource.baseArrayLayer = imageBarriers[0].subresourceRange.baseArrayLayer;
                    copyRegion.dstSubresource.layerCount =
                        imageBarriers[0].subresourceRange.layerCount == VK_REMAINING_ARRAY_LAYERS
                            ? 1
                            : imageBarriers[0].subresourceRange.layerCount;
                    copyRegion.srcSubresource = copyRegion.dstSubresource;

                    vkCmdCopyImage(InCmdBuffer, imageBarriers[0].image, imageBarriers[0].newLayout,
                                   imageBarriers[1].image, imageBarriers[1].newLayout, 1, &copyRegion);

                    // Restore source image to read layout
                    imageBarriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    imageBarriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    imageBarriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    imageBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                    // Pass shared image to general layout for D3D12 access
                    imageBarriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    imageBarriers[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
                    imageBarriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    imageBarriers[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

                    vkCmdPipelineBarrier(InCmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 2,
                                         imageBarriers);

                    OutResource->VkSourceImageLayout = imageBarriers[0].newLayout;
                    OutResource->VkSourceImageAccess = imageBarriers[0].dstAccessMask;
                    OutResource->VkSharedImageLayout = imageBarriers[1].newLayout;
                    OutResource->VkSharedImageAccess = imageBarriers[1].dstAccessMask;
                }
            }
        }
    }

    return true;
}

static bool NvVkResourceNotValid(NVSDK_NGX_Resource_VK* param)
{
    return param == nullptr || param->Resource.ImageViewInfo.Image == VK_NULL_HANDLE ||
           param->Resource.ImageViewInfo.ImageView == VK_NULL_HANDLE ||
           param->Resource.ImageViewInfo.Format == VK_FORMAT_UNDEFINED || param->Resource.ImageViewInfo.Width == 0 ||
           param->Resource.ImageViewInfo.Height == 0;
}

bool IFeature_VkwDx12::ProcessVulkanTextures(VkCommandBuffer InCmdList, const NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    auto frame = _frameCount % VKDX12_BUFFER_COUNT;
    LOG_DEBUG("frame: {}", frame);

    registeredVirtualCommandBuffer = VK_NULL_HANDLE;

    auto commandBufferLevel = Vulkan_wDx12::cmdBufferStateTracker.GetCommandBufferLevel(InCmdList);
    if (!commandBufferLevel.has_value())
    {
        LOG_ERROR("Could not determine command-buffer level for {:X}; refusing Vulkan w/Dx12 interop",
                  (size_t) InCmdList);
        return false;
    }

    // This interop path splits execution at a queue-submit command-buffer boundary. A secondary command buffer
    // executes inside its parent primary, so promoting it to the parent would move post-Evaluate work to the wrong
    // execution point. Supporting secondary Evaluate requires parent command-stream splitting; fail closed for now.
    if (*commandBufferLevel != VK_COMMAND_BUFFER_LEVEL_PRIMARY)
    {
        LOG_ERROR("Vulkan w/Dx12 Evaluate on secondary command buffer {:X} is unsupported; refusing unsafe submit "
                  "promotion",
                  (size_t) InCmdList);
        return false;
    }

    auto queueFamilyOpt = Vulkan_wDx12::cmdBufferStateTracker.GetCommandBufferQueueFamily(InCmdList);
    if (!queueFamilyOpt.has_value())
    {
        LOG_ERROR("Could not determine queue family for command buffer {:X}; refusing Vulkan w/Dx12 interop",
                  (size_t) InCmdList);
        return false;
    }

    const uint32_t cmdBufferQueueFamily = *queueFamilyOpt;
    LOG_DEBUG("Command buffer {:X} belongs to queue family {}", (size_t) InCmdList, cmdBufferQueueFamily);

    if (cmdBufferQueueFamily != ActiveQueueFamilyIndex)
    {
        if (ActiveQueueFamilyIndex != UINT32_MAX)
        {
            LOG_WARN("Queue family changed from {} to {}; creating matching Vulkan w/Dx12 command buffers",
                     ActiveQueueFamilyIndex, cmdBufferQueueFamily);
        }

        if (!CreateVulkanCommandBuffers(cmdBufferQueueFamily))
        {
            LOG_ERROR("Failed to create Vulkan command buffers for queue family {}", cmdBufferQueueFamily);
            return false;
        }

        ActiveQueueFamilyIndex = cmdBufferQueueFamily;
    }

    LOG_DEBUG("Upscaling command buffer: {:X}, frame: {}", (size_t) InCmdList, frame);

    // Only wait if we're more than 1 frame behind (triple buffering protection)
    uint64_t lastFrameToWaitFor = (_frameCount > 1) ? (_frameCount - 1) : 0;
    bool waitAborted = false;
    if (Dx12Fence->GetCompletedValue() < lastFrameToWaitFor)
    {
        // Only wait for N-1 frame, not current frame
        Dx12Fence->SetEventOnCompletion(lastFrameToWaitFor, Dx12FenceEvent);
        if (WaitForSingleObject(Dx12FenceEvent, 1000) != WAIT_OBJECT_0)
        {
            // Its command buffer wasn't submitted, D3D12 is still waiting for it
            size_t aborted = 0;
            for (auto fence : dx12FenceTextureCopy)
                aborted += Vulkan_wDx12::AbortPendingSubmissions(fence);

            LOG_WARN("Previous upscale didn't complete, aborted {} pending submits", aborted);
            waitAborted = aborted > 0;

            // Engine records the next upscale before submitting the previous one, every frame would stall
            // Nothing to abort means GPU is just slow (shader compiles etc.)
            if ((waitAborted && ++_abortedWaits >= 3) ||
                WaitForSingleObject(Dx12FenceEvent, waitAborted ? 1000 : INFINITE) != WAIT_OBJECT_0)
            {
                LOG_ERROR("Previous upscale is stuck, changing upscaler");
                State::Instance().changeBackend[Handle()->Id] = true;
                return false;
            }
        }
    }

    if (!waitAborted)
        _abortedWaits = 0;

    if (Dx12CommandAllocator[frame]->Reset() != S_OK ||
        Dx12CommandList[frame]->Reset(Dx12CommandAllocator[frame], nullptr) != S_OK)
    {
        LOG_ERROR("Can't reset D3D12 command list");
        return false;
    }

    commandListOpen = true;

#pragma region Extract Vulkan Resources

    NVSDK_NGX_Resource_VK* paramColor = nullptr;
    if (InParameters->Get(NVSDK_NGX_Parameter_Color, (void**) &paramColor) != NVSDK_NGX_Result_Success ||
        NvVkResourceNotValid(paramColor))
    {
        LOG_ERROR("Color not exist!!");
        paramColor = nullptr;
        return false;
    }
    else
    {
        if (ColorCopy == nullptr || ColorCopy.get() == nullptr)
            ColorCopy = std::make_unique<ResourceCopy_Vk>("ColorCopy", VulkanDevice, VulkanPhysicalDevice);
    }

    NVSDK_NGX_Resource_VK* paramMv = nullptr;
    if (InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &paramMv) != NVSDK_NGX_Result_Success ||
        NvVkResourceNotValid(paramMv))
    {
        LOG_ERROR("MotionVectors not exist!!");
        paramMv = nullptr;
        return false;
    }
    else
    {
        if (VelocityCopy == nullptr || VelocityCopy.get() == nullptr)
            VelocityCopy = std::make_unique<ResourceCopy_Vk>("VelocityCopy", VulkanDevice, VulkanPhysicalDevice);
    }

    NVSDK_NGX_Resource_VK* paramOutput = nullptr;
    if (InParameters->Get(NVSDK_NGX_Parameter_Output, (void**) &paramOutput) != NVSDK_NGX_Result_Success ||
        NvVkResourceNotValid(paramOutput))
    {
        LOG_ERROR("Output not exist!!");
        paramOutput = nullptr;
        return false;
    }
    else
    {
        if (OutCopy == nullptr || OutCopy.get() == nullptr)
            OutCopy = std::make_unique<ResourceCopy_Vk>("OutCopy", VulkanDevice, VulkanPhysicalDevice);
    }

    NVSDK_NGX_Resource_VK* paramDepth = nullptr;
    if (InParameters->Get(NVSDK_NGX_Parameter_Depth, (void**) &paramDepth) != NVSDK_NGX_Result_Success ||
        NvVkResourceNotValid(paramDepth))
    {
        LOG_ERROR("Depth not exist!!");
        paramDepth = nullptr;
        return false;
    }
    else
    {
        if (DT == nullptr || DT.get() == nullptr)
            DT = std::make_unique<DepthTransfer_Vk>("DepthTransfer", VulkanDevice, VulkanPhysicalDevice,
                                                    paramDepth->Resource.ImageViewInfo.Format);

        if (DepthCopy == nullptr || DepthCopy.get() == nullptr)
            DepthCopy = std::make_unique<ResourceCopy_Vk>("DepthCopy", VulkanDevice, VulkanPhysicalDevice);
    }

    NVSDK_NGX_Resource_VK* paramExposure = nullptr;
    if (!AutoExposure())
    {
        if (InParameters->Get(NVSDK_NGX_Parameter_ExposureTexture, (void**) &paramExposure) !=
                NVSDK_NGX_Result_Success ||
            NvVkResourceNotValid(paramExposure))
        {
            LOG_WARN("AutoExposure disabled but ExposureTexture is not exist, it may cause problems!!");
            State::Instance().autoExposure = true;
            State::Instance().changeBackend[Handle()->Id] = true;
            paramExposure = nullptr;
        }
        else
        {
            if (ExpCopy == nullptr || ExpCopy.get() == nullptr)
                ExpCopy = std::make_unique<ResourceCopy_Vk>("ExpCopy", VulkanDevice, VulkanPhysicalDevice);
        }
    }

    NVSDK_NGX_Resource_VK* paramReactiveMask = nullptr;
    if (!Config::Instance()->DisableReactiveMask.value_or(false))
    {
        if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, (void**) &paramReactiveMask) !=
                NVSDK_NGX_Result_Success ||
            NvVkResourceNotValid(paramReactiveMask) || MaskInput(paramReactiveMask) == nullptr)
        {
            paramReactiveMask = nullptr;
        }
        else
        {
            if (ReactiveCopy == nullptr || ReactiveCopy.get() == nullptr)
                ReactiveCopy = std::make_unique<ResourceCopy_Vk>("ReactiveCopy", VulkanDevice, VulkanPhysicalDevice);
        }
    }

#pragma endregion

#pragma region Copy Vulkan Textures to Shared Resources

    // Now process all textures - they will record their copy commands into the same command buffer
    if (paramColor != nullptr)
    {
        LOG_DEBUG("Color: {}x{}, format: {} ({})", paramColor->Resource.ImageViewInfo.Width,
                  paramColor->Resource.ImageViewInfo.Height,
                  magic_enum::enum_name(paramColor->Resource.ImageViewInfo.Format),
                  (int) paramColor->Resource.ImageViewInfo.Format);

        if (!CopyTextureFromVkToDx12(InCmdList, paramColor, &vkColor, ColorCopy.get(), true, false))
        {
            LOG_ERROR("Failed to copy color texture!");
            return false;
        }
        else
        {
            vkColor.VkSourceImage = paramColor->Resource.ImageViewInfo.Image;
            vkColor.VkSourceImageView = paramColor->Resource.ImageViewInfo.ImageView;

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(VulkanDevice, VK_OBJECT_TYPE_IMAGE, (uint64_t) paramColor->Resource.ImageViewInfo.Image,
                            "Color");
#endif
        }
    }

    if (paramMv != nullptr)
    {
        LOG_DEBUG("MotionVectors: {}x{}, format: {} ({})", paramMv->Resource.ImageViewInfo.Width,
                  paramMv->Resource.ImageViewInfo.Height, magic_enum::enum_name(paramMv->Resource.ImageViewInfo.Format),
                  (int) paramMv->Resource.ImageViewInfo.Format);

        if (!CopyTextureFromVkToDx12(InCmdList, paramMv, &vkMv, VelocityCopy.get(), true, false))
        {
            LOG_ERROR("Failed to copy motion vectors!");
            return false;
        }
        else
        {
            vkMv.VkSourceImage = paramMv->Resource.ImageViewInfo.Image;
            vkMv.VkSourceImageView = paramMv->Resource.ImageViewInfo.ImageView;

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(VulkanDevice, VK_OBJECT_TYPE_IMAGE, (uint64_t) paramMv->Resource.ImageViewInfo.Image,
                            "MotionVectors");
#endif
        }
    }

    if (paramOutput != nullptr)
    {
        LOG_DEBUG("Output: {}x{}, format: {} ({})", paramOutput->Resource.ImageViewInfo.Width,
                  paramOutput->Resource.ImageViewInfo.Height,
                  magic_enum::enum_name(paramOutput->Resource.ImageViewInfo.Format),
                  (int) paramOutput->Resource.ImageViewInfo.Format);

        if (!CopyTextureFromVkToDx12(InCmdList, paramOutput, &vkOut, OutCopy.get(), false, false))
        {
            LOG_ERROR("Failed to copy output texture!");
            return false;
        }
        else
        {
            vkOut.VkSourceImage = paramOutput->Resource.ImageViewInfo.Image;
            vkOut.VkSourceImageView = paramOutput->Resource.ImageViewInfo.ImageView;

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(VulkanDevice, VK_OBJECT_TYPE_IMAGE, (uint64_t) paramOutput->Resource.ImageViewInfo.Image,
                            "Output");
#endif
        }
    }

    if (paramDepth != nullptr)
    {
        LOG_DEBUG("Depth: {}x{}, format: {} ({})", paramDepth->Resource.ImageViewInfo.Width,
                  paramDepth->Resource.ImageViewInfo.Height,
                  magic_enum::enum_name(paramDepth->Resource.ImageViewInfo.Format),
                  (int) paramDepth->Resource.ImageViewInfo.Format);

        if (!CopyTextureFromVkToDx12(InCmdList, paramDepth, &vkDepth, DepthCopy.get(), true, true))
        {
            LOG_ERROR("Failed to copy depth texture!");
            return false;
        }
        else
        {
            vkDepth.VkSourceImage = paramDepth->Resource.ImageViewInfo.Image;
            vkDepth.VkSourceImageView = paramDepth->Resource.ImageViewInfo.ImageView;

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(VulkanDevice, VK_OBJECT_TYPE_IMAGE, (uint64_t) paramDepth->Resource.ImageViewInfo.Image,
                            "Depth");
#endif
        }
    }

    if (paramExposure != nullptr)
    {
        LOG_DEBUG("Exposure: {}x{}, format: {} ({})", paramExposure->Resource.ImageViewInfo.Width,
                  paramExposure->Resource.ImageViewInfo.Height,
                  magic_enum::enum_name(paramExposure->Resource.ImageViewInfo.Format),
                  (int) paramExposure->Resource.ImageViewInfo.Format);

        if (!CopyTextureFromVkToDx12(InCmdList, paramExposure, &vkExp, ExpCopy.get(), true, false))
        {
            LOG_ERROR("Failed to copy exposure texture!");
            return false;
        }
        else
        {
            vkExp.VkSourceImage = paramExposure->Resource.ImageViewInfo.Image;
            vkExp.VkSourceImageView = paramExposure->Resource.ImageViewInfo.ImageView;

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(VulkanDevice, VK_OBJECT_TYPE_IMAGE, (uint64_t) paramExposure->Resource.ImageViewInfo.Image,
                            "Exposure");
#endif
        }
    }
    else
    {
        // Evaluate passes the copy to the backend, last frame's must not be used
        ReleaseSharedResource(vkExp);
    }

    if (paramReactiveMask != nullptr)
    {
        LOG_DEBUG("ReactiveMask: {}x{}, format: {} ({})", paramReactiveMask->Resource.ImageViewInfo.Width,
                  paramReactiveMask->Resource.ImageViewInfo.Height,
                  magic_enum::enum_name(paramReactiveMask->Resource.ImageViewInfo.Format),
                  (int) paramReactiveMask->Resource.ImageViewInfo.Format);

        if (!CopyTextureFromVkToDx12(InCmdList, paramReactiveMask, &vkReactive, ReactiveCopy.get(), true, false))
        {
            LOG_ERROR("Failed to copy reactive mask!");
            return false;
        }
        else
        {
            vkReactive.VkSourceImage = paramReactiveMask->Resource.ImageViewInfo.Image;
            vkReactive.VkSourceImageView = paramReactiveMask->Resource.ImageViewInfo.ImageView;

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(VulkanDevice, VK_OBJECT_TYPE_IMAGE,
                            (uint64_t) paramReactiveMask->Resource.ImageViewInfo.Image, "ReactiveMask");
#endif
        }
    }
    else
    {
        ReleaseSharedResource(vkReactive);
    }

#pragma endregion

#pragma region Vulkan to D3D12 Synchronization

    // Create shared fence/semaphore if needed. D3D12 queue waits are intentionally deferred until CopyBackOutput,
    // after both the D3D12 and Vulkan copy-back command buffers have been recorded successfully.
    if (!CreateSharedFenceSemaphore())
    {
        LOG_ERROR("Failed to create shared fence/semaphore");
        return false;
    }

    pendingResourceCopyValue = ++_fenceValue;
    LOG_DEBUG("Prepared Vulkan resource-copy signal value: {}", pendingResourceCopyValue);

    {
        if (!VulkanQueueCommandBuffers.contains(ActiveQueueFamilyIndex))
        {
            if (!CreateVulkanCommandBuffers(ActiveQueueFamilyIndex))
            {
                LOG_ERROR("Failed to create Vulkan command buffers for queue family {}", ActiveQueueFamilyIndex);
                return false;
            }
        }

        auto& b = VulkanQueueCommandBuffers[ActiveQueueFamilyIndex];

        // An aborted upscale from two frames ago can still use it
        Vulkan_wDx12::DropPendingSubmissions(b.VulkanBarrierCommandBuffer[frame]);

        auto vkResult = vkResetCommandBuffer(b.VulkanBarrierCommandBuffer[frame], 0);
        if (vkResult != VK_SUCCESS)
        {
            LOG_ERROR("vkResetCommandBuffer error: {0:x}", (int) vkResult);
            return false;
        }

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkResult = vkBeginCommandBuffer(b.VulkanBarrierCommandBuffer[frame], &beginInfo);
        if (vkResult != VK_SUCCESS)
        {
            LOG_ERROR("vkBeginCommandBuffer error: {0:x}", (int) vkResult);
            return false;
        }

        if (!Vulkan_wDx12::RegisterVirtualCommandBuffer(InCmdList, b.VulkanBarrierCommandBuffer[frame]))
        {
            LOG_ERROR("Failed to register virtual command buffer for {:X}", (size_t) InCmdList);
            vkEndCommandBuffer(b.VulkanBarrierCommandBuffer[frame]);
            return false;
        }

        registeredVirtualCommandBuffer = b.VulkanBarrierCommandBuffer[frame];

        vk_state::ReplayParams params {};
        params.ReplayGraphicsPipeline = true;
        params.ReplayComputeToo = false;
        params.ReplayViewportScissor = true;
        params.ReplayExtendedDynamicState = true;
        params.ReplayPushConstants = true;
        params.ReplayVertexIndex = true;
        params.RequiredGraphicsSetMask = 0xFFFFFFFFu;
        params.OverrideGraphicsLayout = VK_NULL_HANDLE;

        if (!Vulkan_wDx12::cmdBufferStateTracker.CaptureAndReplay(InCmdList, b.VulkanBarrierCommandBuffer[frame],
                                                                  params))
        {
            LOG_WARN("Failed to capture and replay command buffer state or state is empty");
        }
        else
        {
            LOG_DEBUG("Successfully replayed state to virtual command buffer");
        }

        VkImageMemoryBarrier imageBarrier = {};
        imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageBarrier.oldLayout = vkOut.VkSourceImageLayout;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.image = vkOut.VkSourceImage;
        imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBarrier.subresourceRange.baseMipLevel = 0;
        imageBarrier.subresourceRange.levelCount = 1;
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = 1;
        imageBarrier.srcAccessMask = vkOut.VkSourceImageAccess;
        imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(b.VulkanBarrierCommandBuffer[frame], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier);
    }

#pragma endregion

#pragma region Transition Resources for D3D12 Processing

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(6);

    auto AddBarrier = [&](ID3D12Resource* resource, D3D12_RESOURCE_STATES afterState)
    {
        if (resource)
        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
            barrier.Transition.StateAfter = afterState;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers.push_back(barrier);
        }
    };

    // Batch all input transitions
    AddBarrier(vkColor.Dx12Resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    AddBarrier(vkMv.Dx12Resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    AddBarrier(vkDepth.Dx12Resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    AddBarrier(vkExp.Dx12Resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    AddBarrier(vkReactive.Dx12Resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    AddBarrier(vkOut.Dx12Resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // Single batched barrier call instead of 6 individual ones
    if (!barriers.empty())
        Dx12CommandList[frame]->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

#pragma endregion

    return true;
}

bool IFeature_VkwDx12::CopyBackOutput(VkCommandBuffer InCmdBuffer, const Subrect* InView)
{
    LOG_FUNC();

    const VkOffset2D viewOffset = InView ? VkOffset2D { (int32_t) InView->X, (int32_t) InView->Y } : VkOffset2D {};
    const VkExtent2D viewExtent =
        InView ? VkExtent2D { InView->Width, InView->Height } : VkExtent2D { vkOut.Width, vkOut.Height };

    auto frame = _frameCount % VKDX12_BUFFER_COUNT;
    LOG_DEBUG("frame: {}", frame);

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(6);

    auto AddBarrier = [&](ID3D12Resource* resource, D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState)
    {
        if (resource && beforeState != afterState)
        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource;
            barrier.Transition.StateBefore = beforeState;
            barrier.Transition.StateAfter = afterState;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers.push_back(barrier);
        }
    };

    AddBarrier(vkOut.Dx12Resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    AddBarrier(vkColor.Dx12Resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    AddBarrier(vkMv.Dx12Resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    AddBarrier(vkDepth.Dx12Resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    AddBarrier(vkExp.Dx12Resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    AddBarrier(vkReactive.Dx12Resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);

    if (!barriers.empty())
        Dx12CommandList[frame]->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

    if (vkOut.VkSourceImage == VK_NULL_HANDLE || vkOut.VkSharedImage == VK_NULL_HANDLE)
    {
        LOG_ERROR("Output Vulkan images are not valid for copy-back");
        return false;
    }

    if (!VulkanQueueCommandBuffers.contains(ActiveQueueFamilyIndex))
    {
        if (!CreateVulkanCommandBuffers(ActiveQueueFamilyIndex))
        {
            LOG_ERROR("Failed to create Vulkan command buffers for queue family {}", ActiveQueueFamilyIndex);
            return false;
        }
    }

    auto& b = VulkanQueueCommandBuffers[ActiveQueueFamilyIndex];

    // Record every fallible Vulkan copy-back operation before placing a wait on the D3D12 queue. This makes
    // Evaluate failures rollback-safe: an abandoned frame cannot leave the D3D12 queue permanently waiting.
    VkResult vkResult = vkResetCommandBuffer(b.VulkanCopyCommandBuffer[frame], 0);
    if (vkResult != VK_SUCCESS)
    {
        LOG_ERROR("vkResetCommandBuffer error: {0:x}", (int) vkResult);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkResult = vkBeginCommandBuffer(b.VulkanCopyCommandBuffer[frame], &beginInfo);
    if (vkResult != VK_SUCCESS)
    {
        LOG_ERROR("vkBeginCommandBuffer error: {0:x}", (int) vkResult);
        return false;
    }

    std::vector<VkImageMemoryBarrier> imageBarriers;
    imageBarriers.reserve(5);

    auto AddVkBarrier = [&](VK_TEXTURE2D_RESOURCE_C* resource)
    {
        if (resource->VkSourceImage != VK_NULL_HANDLE && resource->VkSharedImage != VK_NULL_HANDLE)
        {
            VkImageMemoryBarrier imageBarrier {};
            imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageBarrier.oldLayout = resource->VkSharedImageLayout;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.image = resource->VkSharedImage;
            imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageBarrier.subresourceRange.baseMipLevel = 0;
            imageBarrier.subresourceRange.levelCount = 1;
            imageBarrier.subresourceRange.baseArrayLayer = 0;
            imageBarrier.subresourceRange.layerCount = 1;
            imageBarrier.srcAccessMask = resource->VkSharedImageAccess;
            imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            imageBarriers.push_back(imageBarrier);

            resource->VkSharedImageLayout = imageBarrier.newLayout;
            resource->VkSharedImageAccess = imageBarrier.dstAccessMask;
        }
    };

    AddVkBarrier(&vkColor);
    AddVkBarrier(&vkDepth);
    AddVkBarrier(&vkMv);
    AddVkBarrier(&vkExp);
    AddVkBarrier(&vkReactive);

    if (!imageBarriers.empty())
    {
        vkCmdPipelineBarrier(b.VulkanCopyCommandBuffer[frame], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
                             static_cast<uint32_t>(imageBarriers.size()), imageBarriers.data());
    }

    if (!Config::Instance()->VulkanUseCopyForOutput.value_or_default())
    {
        VkImageMemoryBarrier imageBarrier = {};
        imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageBarrier.oldLayout = vkOut.VkSharedImageLayout;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.image = vkOut.VkSharedImage;
        imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBarrier.subresourceRange.baseMipLevel = 0;
        imageBarrier.subresourceRange.levelCount = 1;
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = 1;
        imageBarrier.srcAccessMask = vkOut.VkSharedImageAccess;
        imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkOut.VkSharedImageLayout = imageBarrier.newLayout;
        vkOut.VkSharedImageAccess = imageBarrier.dstAccessMask;

        vkCmdPipelineBarrier(b.VulkanCopyCommandBuffer[frame], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier);

        if (!OutCopy->CanRender())
        {
            LOG_ERROR("ResourceCopy_Vk not initialized!");
            return false;
        }

        if (vkOut.VkSharedImageView == VK_NULL_HANDLE)
        {
            VkImageViewCreateInfo srcViewInfo = {};
            srcViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            srcViewInfo.image = vkOut.VkSharedImage;
            srcViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            srcViewInfo.format = vkOut.Format;
            srcViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            srcViewInfo.subresourceRange.baseMipLevel = 0;
            srcViewInfo.subresourceRange.levelCount = 1;
            srcViewInfo.subresourceRange.baseArrayLayer = 0;
            srcViewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(VulkanDevice, &srcViewInfo, nullptr, &vkOut.VkSharedImageView) != VK_SUCCESS)
            {
                LOG_ERROR("Failed to create destination image view!");
                return false;
            }
        }

        if (!OutCopy->Dispatch(VulkanDevice, b.VulkanCopyCommandBuffer[frame], vkOut.VkSharedImageView,
                               vkOut.VkSourceImageView, viewExtent, viewOffset, viewOffset))
        {
            LOG_ERROR("Failed to dispatch resource copy!");
            return false;
        }
    }
    else
    {
        VkImageMemoryBarrier copyBarriers[2] = { {}, {} };

        copyBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        copyBarriers[0].oldLayout = vkOut.VkSharedImageLayout;
        copyBarriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        copyBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        copyBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        copyBarriers[0].image = vkOut.VkSharedImage;
        copyBarriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyBarriers[0].subresourceRange.baseMipLevel = 0;
        copyBarriers[0].subresourceRange.levelCount = 1;
        copyBarriers[0].subresourceRange.baseArrayLayer = 0;
        copyBarriers[0].subresourceRange.layerCount = 1;
        copyBarriers[0].srcAccessMask = vkOut.VkSharedImageAccess;
        copyBarriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        copyBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        copyBarriers[1].oldLayout = vkOut.VkSourceImageLayout;
        copyBarriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        copyBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        copyBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        copyBarriers[1].image = vkOut.VkSourceImage;
        copyBarriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyBarriers[1].subresourceRange.baseMipLevel = 0;
        copyBarriers[1].subresourceRange.levelCount = 1;
        copyBarriers[1].subresourceRange.baseArrayLayer = 0;
        copyBarriers[1].subresourceRange.layerCount = 1;
        copyBarriers[1].srcAccessMask = vkOut.VkSourceImageAccess;
        copyBarriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(b.VulkanCopyCommandBuffer[frame], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, copyBarriers);

        VkImageCopy copyRegion = {};
        copyRegion.extent = { viewExtent.width, viewExtent.height, 1 };
        copyRegion.srcOffset = { viewOffset.x, viewOffset.y, 0 };
        copyRegion.dstOffset = copyRegion.srcOffset;
        copyRegion.dstSubresource.aspectMask = copyBarriers[0].subresourceRange.aspectMask;
        copyRegion.dstSubresource.mipLevel = copyBarriers[0].subresourceRange.baseMipLevel;
        copyRegion.dstSubresource.baseArrayLayer = copyBarriers[0].subresourceRange.baseArrayLayer;
        copyRegion.dstSubresource.layerCount = copyBarriers[0].subresourceRange.layerCount;
        copyRegion.srcSubresource = copyRegion.dstSubresource;

        vkCmdCopyImage(b.VulkanCopyCommandBuffer[frame], copyBarriers[0].image, copyBarriers[0].newLayout,
                       copyBarriers[1].image, copyBarriers[1].newLayout, 1, &copyRegion);

        copyBarriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        copyBarriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        copyBarriers[0].srcAccessMask = copyBarriers[0].dstAccessMask;
        copyBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

        copyBarriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        copyBarriers[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        copyBarriers[1].srcAccessMask = copyBarriers[1].dstAccessMask;
        copyBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(b.VulkanCopyCommandBuffer[frame], VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 2, copyBarriers);

        vkOut.VkSharedImageLayout = copyBarriers[0].newLayout;
        vkOut.VkSharedImageAccess = copyBarriers[0].dstAccessMask;
        vkOut.VkSourceImageLayout = copyBarriers[1].newLayout;
        vkOut.VkSourceImageAccess = copyBarriers[1].dstAccessMask;
    }

    UpscalerTime->EndInterop(b.VulkanCopyCommandBuffer[frame]);

    vkResult = vkEndCommandBuffer(b.VulkanCopyCommandBuffer[frame]);
    if (vkResult != VK_SUCCESS)
    {
        LOG_ERROR("vkEndCommandBuffer error: {0:x}", (int) vkResult);
        return false;
    }

    HRESULT result = Dx12CommandList[frame]->Close();
    commandListOpen = false;
    if (result != S_OK)
    {
        LOG_ERROR("Dx12CommandList->Close failed: {0:x}", result);
        return false;
    }

    const uint64_t d3d12CompleteValue = ++_fenceValue;
    const uint64_t copyBackValue = ++_fenceValue;

    Vulkan_wDx12::PendingSubmission submission {};
    submission.submitCommandBuffer = InCmdBuffer;
    submission.resourceCopySemaphore = vkSemaphoreTextureCopy[frame];
    submission.resourceCopyFence = dx12FenceTextureCopy[frame];
    submission.resourceCopyValue = pendingResourceCopyValue;
    submission.copyBackSemaphore = vkSemaphoreCopyBack[frame];
    submission.d3d12CompleteValue = d3d12CompleteValue;
    submission.copyBackValue = copyBackValue;
    submission.copyBackCommandBuffer = b.VulkanCopyCommandBuffer[frame];
    submission.barrierCommandBuffer = b.VulkanBarrierCommandBuffer[frame];

    if (!Vulkan_wDx12::RegisterPendingSubmission(submission))
    {
        LOG_ERROR("Failed to register Vulkan w/Dx12 pending submission");
        return false;
    }

    // Queue D3D12 work only after the complete Vulkan/D3D12 transaction has been prepared and registered.
    result = Dx12CommandQueue->Wait(dx12FenceTextureCopy[frame], pendingResourceCopyValue);
    if (result != S_OK)
    {
        Vulkan_wDx12::CancelPendingSubmission(InCmdBuffer);
        LOG_ERROR("Dx12CommandQueue->Wait failed: {0:x}", result);
        return false;
    }

    ID3D12CommandList* ppCommandLists[] = { Dx12CommandList[frame] };
    Dx12CommandQueue->ExecuteCommandLists(1, ppCommandLists);

    auto CancelAndDrainQueuedWait = [&]()
    {
        Vulkan_wDx12::CancelPendingSubmission(InCmdBuffer);

        // The queue wait has already been accepted. If a later queue operation fails, removing the Vulkan-side
        // transaction without releasing this wait would permanently strand the D3D12 queue. CPU-signaling the
        // resource-copy fence is safe for the abort path: only OptiScaler's private shared-copy resources can be
        // consumed by the queued D3D12 work, and no Vulkan copy-back submit will be injected afterwards.
        HRESULT unblockResult = dx12FenceTextureCopy[frame]->Signal(pendingResourceCopyValue);
        if (unblockResult != S_OK)
        {
            LOG_ERROR("Failed to release aborted Vulkan w/Dx12 queue wait: {0:x}", unblockResult);
        }
    };

    result = Dx12CommandQueue->Signal(dx12FenceTextureCopy[frame], d3d12CompleteValue);
    if (result != S_OK)
    {
        LOG_ERROR("Dx12CommandQueue->Signal (shared fence) failed: {0:x}", result);
        CancelAndDrainQueuedWait();
        return false;
    }

    result = Dx12CommandQueue->Signal(Dx12Fence, _frameCount);
    if (result != S_OK)
    {
        LOG_ERROR("Dx12CommandQueue->Signal failed: {0:x}", result);
        CancelAndDrainQueuedWait();
        return false;
    }

    // Publish the query completion marker only after the whole transaction was prepared successfully.
    UpscalerTime->SubmitInterop(vkSemaphoreCopyBack[frame], copyBackValue);

    LOG_DEBUG("Prepared Vulkan w/Dx12 transaction: resource={}, d3d12={}, copyBack={}", pendingResourceCopyValue,
              d3d12CompleteValue, copyBackValue);
    pendingResourceCopyValue = 0;
    return true;
}

void IFeature_VkwDx12::AbortPendingInterop(VkCommandBuffer InCmdBuffer, uint32_t InFrame)
{
    if (UpscalerTime)
        UpscalerTime->AbortInterop();

    // Left open when Evaluate failed between ProcessVulkanTextures and CopyBackOutput
    if (commandListOpen && Dx12CommandList[InFrame] != nullptr)
        Dx12CommandList[InFrame]->Close();

    commandListOpen = false;

    // Another Evaluate on the same command buffer can own the mapping, CopyBackOutput cancels its own submission
    if (registeredVirtualCommandBuffer != VK_NULL_HANDLE &&
        Vulkan_wDx12::RemoveVirtualCommandBuffer(InCmdBuffer) == registeredVirtualCommandBuffer)
    {
        Vulkan_wDx12::EndCmdBuffer(registeredVirtualCommandBuffer);
    }

    registeredVirtualCommandBuffer = VK_NULL_HANDLE;

    pendingResourceCopyValue = 0;
}

// Command buffers of previous frames can still be using its objects
template <typename T> static void DeferDelete(std::unique_ptr<T>& object, VkDevice device)
{
    if (object == nullptr)
        return;

    DeferredRelease::Enqueue([raw = object.release()]() { delete raw; }, (uint64_t) device);
}

void IFeature_VkwDx12::FinishPendingWork()
{
    for (auto fence : dx12FenceTextureCopy)
        Vulkan_wDx12::AbortPendingSubmissions(fence);

    // Aborted upscales start running on D3D12 now
    if (Dx12CommandQueue != nullptr && Dx12Fence != nullptr && Dx12FenceEvent != nullptr &&
        Dx12CommandQueue->Signal(Dx12Fence, _frameCount + 1) == S_OK &&
        Dx12Fence->SetEventOnCompletion(_frameCount + 1, Dx12FenceEvent) == S_OK)
    {
        WaitForSingleObject(Dx12FenceEvent, 1000);
    }
}

void IFeature_VkwDx12::ReleaseSharedResources()
{
    LOG_FUNC();

    // The objects released below and the D3D12 upscaler destroyed after this can still be in use until then
    FinishPendingWork();

    ReleaseSharedResource(vkColor);
    ReleaseSharedResource(vkMv);
    ReleaseSharedResource(vkOut);
    ReleaseSharedResource(vkDepth);
    ReleaseSharedResource(vkReactive);
    ReleaseSharedResource(vkExp);

    // Vulkan submits of previous frames can still be using them, destroying a pool frees its command buffers
    for (auto& [index, b] : VulkanQueueCommandBuffers)
    {
        for (size_t i = 0; i < VKDX12_BUFFER_COUNT; i++)
        {
            // Game can still submit an aborted upscale, its barrier command buffer is freed with the pool
            if (b.VulkanBarrierCommandBuffer[i] != VK_NULL_HANDLE)
                Vulkan_wDx12::DropPendingSubmissions(b.VulkanBarrierCommandBuffer[i]);

            for (auto pool : { b.VulkanBarrierCommandPool[i], b.VulkanCopyCommandPool[i] })
            {
                if (pool != VK_NULL_HANDLE)
                {
                    DeferredRelease::Enqueue([device = VulkanDevice, pool]()
                                             { vkDestroyCommandPool(device, pool, nullptr); }, (uint64_t) VulkanDevice);
                }
            }
        }
    }

    VulkanQueueCommandBuffers.clear();

    ReleaseSyncResources();

    for (size_t i = 0; i < VKDX12_BUFFER_COUNT; i++)
    {
        DeferredRelease::Release(Dx12CommandList[i]);
        DeferredRelease::Release(Dx12CommandAllocator[i]);
    }

    DeferredRelease::Release(Dx12CommandQueue);
    DeferredRelease::Release(Dx12Fence);

    // The fence can still signal it if the wait above timed out
    if (Dx12FenceEvent != nullptr)
    {
        DeferredRelease::Enqueue([event = Dx12FenceEvent]() { CloseHandle(event); });
        Dx12FenceEvent = nullptr;
    }

    DeferDelete(ColorCopy, VulkanDevice);
    DeferDelete(VelocityCopy, VulkanDevice);
    DeferDelete(DT, VulkanDevice);
    DeferDelete(DepthCopy, VulkanDevice);
    DeferDelete(ReactiveCopy, VulkanDevice);
    DeferDelete(ExpCopy, VulkanDevice);
    DeferDelete(OutCopy, VulkanDevice);
    DeferDelete(OutCopy2, VulkanDevice);
}

void IFeature_VkwDx12::ReleaseSyncResources()
{
    LOG_FUNC();
    for (uint32_t i = 0; i < VKDX12_BUFFER_COUNT; i++)
    {
        // Pending submits can still wait or signal them
        if (vkSemaphoreTextureCopy[i] != VK_NULL_HANDLE || vkSemaphoreCopyBack[i] != VK_NULL_HANDLE)
        {
            DeferredRelease::Enqueue(
                [device = VulkanDevice, textureCopy = vkSemaphoreTextureCopy[i], copyBack = vkSemaphoreCopyBack[i]]()
                {
                    if (textureCopy != VK_NULL_HANDLE)
                        vkDestroySemaphore(device, textureCopy, nullptr);

                    if (copyBack != VK_NULL_HANDLE)
                        vkDestroySemaphore(device, copyBack, nullptr);
                },
                (uint64_t) VulkanDevice);
        }

        vkSemaphoreTextureCopy[i] = VK_NULL_HANDLE;
        vkSemaphoreCopyBack[i] = VK_NULL_HANDLE;

        DeferredRelease::Release(dx12FenceTextureCopy[i]);

        SAFE_CLOSE_HANDLE(vkSHForTextureCopy[i]);
    }
}

// Previous frames on both APIs can still be using it
void IFeature_VkwDx12::ReleaseSharedResource(VK_TEXTURE2D_RESOURCE_C& resource)
{
    if (resource.VkSharedImage != VK_NULL_HANDLE || resource.VkSharedImageView != VK_NULL_HANDLE ||
        resource.VkSharedMemory != VK_NULL_HANDLE)
    {
        DeferredRelease::Enqueue(
            [device = VulkanDevice, image = resource.VkSharedImage, view = resource.VkSharedImageView,
             memory = resource.VkSharedMemory]()
            {
                if (view != VK_NULL_HANDLE)
                    vkDestroyImageView(device, view, nullptr);

                if (image != VK_NULL_HANDLE)
                    vkDestroyImage(device, image, nullptr);

                if (memory != VK_NULL_HANDLE)
                    vkFreeMemory(device, memory, nullptr);
            },
            (uint64_t) VulkanDevice);
    }

    resource.VkSharedImage = VK_NULL_HANDLE;
    resource.VkSharedImageView = VK_NULL_HANDLE;
    resource.VkSharedMemory = VK_NULL_HANDLE;

    SAFE_CLOSE_HANDLE(resource.SharedHandle);

    DeferredRelease::Release(resource.Dx12Resource);
}

std::optional<LUID> IFeature_VkwDx12::VulkanDeviceLuid()
{
    auto gipa = VulkanGIPA != nullptr ? VulkanGIPA : vkGetInstanceProcAddr;

    auto getProperties2 = (PFN_vkGetPhysicalDeviceProperties2) gipa(VulkanInstance, "vkGetPhysicalDeviceProperties2");

    if (getProperties2 == nullptr)
        getProperties2 = (PFN_vkGetPhysicalDeviceProperties2) gipa(VulkanInstance, "vkGetPhysicalDeviceProperties2KHR");

    if (getProperties2 == nullptr)
        return std::nullopt;

    VkPhysicalDeviceIDProperties idProperties {};
    idProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

    VkPhysicalDeviceProperties2 properties {};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &idProperties;

    getProperties2(VulkanPhysicalDevice, &properties);

    if (!idProperties.deviceLUIDValid)
        return std::nullopt;

    LUID luid {};
    std::memcpy(&luid, idProperties.deviceLUID, sizeof(luid));
    return luid;
}

HRESULT IFeature_VkwDx12::CreateDx12Device()
{
    LOG_FUNC();

    ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
    ScopedSkipVulkanHooks skipVulkanHooks {};

    HRESULT result;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_0;

    // Shared resources only work on the Vulkan device's GPU
    const auto vkLuid = VulkanDeviceLuid();
    auto gameDevice = State::Instance().currentD3D12Device;
    const bool gameDeviceUsable =
        gameDevice != nullptr && (!vkLuid.has_value() || IsEqualLUID(gameDevice->GetAdapterLuid(), vkLuid.value()));

    if (_localDx11on12Device == nullptr &&
        (!gameDeviceUsable || (State::Instance().gameQuirks & GameQuirk::ForceCreateD3D12Device)))
    {
        IDXGIFactory2* factory = nullptr;

        if (DxgiProxy::Module() == nullptr)
            result = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
        else
            result = DxgiProxy::CreateDxgiFactory2_()(0, __uuidof(factory), &factory);

        if (result != S_OK)
        {
            LOG_ERROR("Can't create factory: {0:x}", result);
            return result;
        }

        IDXGIAdapter* hwAdapter = nullptr;
        bool adapterByLuid = false;

        if (vkLuid.has_value())
        {
            IDXGIFactory4* factory4 = nullptr;

            if (factory->QueryInterface(IID_PPV_ARGS(&factory4)) == S_OK)
            {
                adapterByLuid = factory4->EnumAdapterByLuid(vkLuid.value(), IID_PPV_ARGS(&hwAdapter)) == S_OK;

                if (!adapterByLuid)
                    hwAdapter = nullptr;

                factory4->Release();
            }
        }

        if (hwAdapter == nullptr)
        {
            if (vkLuid.has_value())
                LOG_WARN("Can't find the Vulkan device's adapter, using the hardware adapter");

            IdentifyGpu::getHardwareAdapter(factory, &hwAdapter, featureLevel);
        }

        if (hwAdapter == nullptr)
            LOG_WARN("Can't get hwAdapter, will try nullptr!");

        // WithDx12 keeps or adopts an existing device without looking at the adapter
        auto withDx12Device = WithDx12::GetD3D12Device() != nullptr ? WithDx12::GetD3D12Device() : gameDevice;
        const bool forceCreate = static_cast<bool>(State::Instance().gameQuirks & GameQuirk::ForceCreateD3D12Device);

        if (adapterByLuid && !forceCreate && withDx12Device != nullptr &&
            !IsEqualLUID(withDx12Device->GetAdapterLuid(), vkLuid.value()))
        {
            ID3D12Device* device = nullptr;
            if (CreateD3D12DeviceOnAdapter(hwAdapter, featureLevel, &device) == S_OK && device != nullptr)
            {
                LOG_INFO("Created a D3D12Device on the Vulkan device's GPU");
                _localDx11on12Device = device;
            }
        }

        if (_localDx11on12Device == nullptr)
            _localDx11on12Device = WithDx12::RequestD3D12Device(featureLevel, hwAdapter);

        if (_localDx11on12Device == nullptr)
        {
            LOG_ERROR("Can't create device!");
            return result;
        }

        _dx11on12Device = _localDx11on12Device;

        if (vkLuid.has_value() && !IsEqualLUID(_localDx11on12Device->GetAdapterLuid(), vkLuid.value()))
            LOG_WARN("D3D12Device is on a different GPU than the Vulkan device");

        if (hwAdapter != nullptr)
            hwAdapter->Release();

        if (factory != nullptr)
            factory->Release();
    }
    else
    {
        if (_localDx11on12Device != nullptr)
        {
            LOG_DEBUG("Using _localDx11on12Device");
            _dx11on12Device = _localDx11on12Device;
        }
        else
        {
            LOG_DEBUG("Using currentD3D12Device");
            _dx11on12Device = State::Instance().currentD3D12Device;
        }
    }

    if (Dx12CommandQueue == nullptr)
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = Dx12CommandListType;

        result = _dx11on12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&Dx12CommandQueue));

        if (result != S_OK || Dx12CommandQueue == nullptr)
        {
            LOG_ERROR("CreateCommandQueue result: {0:x}", result);
            return E_NOINTERFACE;
        }
    }

    for (size_t i = 0; i < VKDX12_BUFFER_COUNT; i++)
    {
        if (Dx12CommandAllocator[i] == nullptr)
        {
            result =
                _dx11on12Device->CreateCommandAllocator(Dx12CommandListType, IID_PPV_ARGS(&Dx12CommandAllocator[i]));

            if (result != S_OK)
            {
                LOG_ERROR("CreateCommandAllocator error: {:X}", result);
                return E_NOINTERFACE;
            }
        }

        if (Dx12CommandList[i] == nullptr && Dx12CommandAllocator[i] != nullptr)
        {
            // CreateCommandList
            result = _dx11on12Device->CreateCommandList(0, Dx12CommandListType, Dx12CommandAllocator[i], nullptr,
                                                        IID_PPV_ARGS(&Dx12CommandList[i]));

            if (result != S_OK)
            {
                LOG_ERROR("CreateCommandList error: {:X}", result);
                return E_NOINTERFACE;
            }

            Dx12CommandList[i]->Close();
        }
    }

    if (Dx12Fence == nullptr)
    {
        result = _dx11on12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Dx12Fence));

        if (result != S_OK)
        {
            LOG_ERROR("CreateFence error: {0:X}", result);
            return E_NOINTERFACE;
        }

        Dx12FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        if (Dx12FenceEvent == nullptr)
        {
            LOG_ERROR("CreateEvent error!");
            return E_NOINTERFACE;
        }
    }

    return S_OK;
}

IFeature_VkwDx12::IFeature_VkwDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : IFeature(InHandleId, InParameters), IFeature_Vk(InHandleId, InParameters)
{
    SetInitParameters(InParameters);
}

IFeature_VkwDx12::~IFeature_VkwDx12()
{
    if (State::Instance().isShuttingDown)
        return;

    ReleaseSharedResources();
}

bool IFeature_VkwDx12::Init(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice, VkCommandBuffer InCmdList,
                            PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                            NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (IsInited())
        return true;

    // Shared resources are already in NPSR, the backends' UE barriers would declare wrong states
    if (State::Instance().NVNGX_Engine == NVSDK_NGX_ENGINE_TYPE_UNREAL ||
        State::Instance().gameQuirks & GameQuirk::ForceUnrealEngine)
    {
        LOG_INFO("Vulkan detected, disabling UE resource barrier overrides");
        Config::Instance()->ColorResourceBarrier.set_volatile_value(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Config::Instance()->MVResourceBarrier.set_volatile_value(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    VulkanInstance = InInstance;
    VulkanPhysicalDevice = InPD;
    VulkanDevice = InDevice;
    VulkanGIPA = InGIPA;
    VulkanGDPA = InGDPA;

    // Load external memory functions
    if (!LoadVulkanExternalMemoryFunctions())
    {
        LOG_ERROR("Failed to load Vulkan external memory functions!");
        return false;
    }

    // Get queue info
    /*
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(VulkanPhysicalDevice, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(VulkanPhysicalDevice, &queueFamilyCount, queueFamilies.data());

    bool queueFamilyFound = false;
    for (uint32_t i = 0; i < queueFamilyCount; i++)
    {

        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
        {
            LOG_DEBUG("Found compute queue family index: {}", i);
            VulkanQueueFamilyIndex = i;
            queueFamilyFound = true;
        }
    }

    if (!queueFamilyFound)
    {
        LOG_ERROR("Failed to find compute queue family!");
        return false;
    }

    vkGetDeviceQueue(VulkanDevice, VulkanQueueFamilyIndex, 0, &VulkanGraphicsQueue);

    LOG_DEBUG("VulkanGraphicsQueue: {:X}", (size_t) VulkanGraphicsQueue);
    */

    // Create D3D12 device
    auto result = CreateDx12Device();

    if (result != S_OK || _dx11on12Device == nullptr)
    {
        LOG_ERROR("CreateDx12Device result: {0:x}", result);
        return false;
    }

    UpscalerTime = std::make_unique<GpuTime_Vk>(InDevice, InPD);

    SetInitParameters(InParameters);

    // Non-DLSS upscalers don't use the cmdList during Init
    // We have more than one cmdList so unsure how that would even work
    SetInit(dx12Feature->Init(_dx11on12Device, Dx12CommandList[0], InParameters));

    return IsInited();
}

bool IFeature_VkwDx12::Evaluate(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters)
{
    // Frees deferred objects even when presents aren't hooked
    DeferredRelease::Collect();

    LOG_FUNC();
    std::scoped_lock evaluateLock(EvaluateMutex);

    if (!IsInited())
        return false;

    auto frame = _frameCount % VKDX12_BUFFER_COUNT;
    auto cmdList = Dx12CommandList[frame];

    // Ends in the copy back command buffer, covers the whole interop with D3D12 upscaling in between
    // The start is recorded in the original CB, the end in the Vulkan copy-back CB.
    // Query slots are guarded by copy-back timeline completion, not frame count.
    UpscalerTime->StartInterop(InCmdBuffer);

    void* originalColor = nullptr;
    void* originalMotionVectors = nullptr;
    void* originalOutput = nullptr;
    void* originalDepth = nullptr;
    void* originalExposure = nullptr;
    void* originalReactiveMask = nullptr;
    InParameters->Get(NVSDK_NGX_Parameter_Color, &originalColor);
    InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, &originalMotionVectors);
    InParameters->Get(NVSDK_NGX_Parameter_Output, &originalOutput);
    InParameters->Get(NVSDK_NGX_Parameter_Depth, &originalDepth);
    InParameters->Get(NVSDK_NGX_Parameter_ExposureTexture, &originalExposure);
    InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, &originalReactiveMask);

    // Vulkan structs, Dx12 backends would read them as Dx12 resources. Reactive mask is shared via the bias mask
    void* originalFsrReactive = nullptr;
    void* originalFsrTransparency = nullptr;
    InParameters->Get("FSR.reactive", &originalFsrReactive);
    InParameters->Get("FSR.transparencyAndComposition", &originalFsrTransparency);

    auto RestoreVulkanParameters = [&]()
    {
        InParameters->Set(NVSDK_NGX_Parameter_Color, originalColor);
        InParameters->Set(NVSDK_NGX_Parameter_MotionVectors, originalMotionVectors);
        InParameters->Set(NVSDK_NGX_Parameter_Output, originalOutput);
        InParameters->Set(NVSDK_NGX_Parameter_Depth, originalDepth);
        InParameters->Set(NVSDK_NGX_Parameter_ExposureTexture, originalExposure);
        InParameters->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, originalReactiveMask);
        InParameters->Set("FSR.reactive", originalFsrReactive);
        InParameters->Set("FSR.transparencyAndComposition", originalFsrTransparency);
    };

    bool dx12EvalResult = false;
    bool interopPrepared = false;
    do
    {
        if (!ProcessVulkanTextures(InCmdBuffer, InParameters))
        {
            LOG_ERROR("Can't process Vulkan textures!");
            AbortPendingInterop(InCmdBuffer, frame);
            break;
        }
        interopPrepared = true;

        if (State::Instance().changeBackend[Handle()->Id])
            break;

        InParameters->Set(NVSDK_NGX_Parameter_Color, (void*) vkColor.Dx12Resource);
        InParameters->Set(NVSDK_NGX_Parameter_MotionVectors, (void*) vkMv.Dx12Resource);
        InParameters->Set(NVSDK_NGX_Parameter_Output, (void*) vkOut.Dx12Resource);
        InParameters->Set(NVSDK_NGX_Parameter_Depth, (void*) vkDepth.Dx12Resource);

        // Nullptr when there is no copy, the Vulkan struct must not reach the Dx12 backend
        if (!AutoExposure())
            InParameters->Set(NVSDK_NGX_Parameter_ExposureTexture, (void*) vkExp.Dx12Resource);

        if (!Config::Instance()->DisableReactiveMask.value_or(false))
            InParameters->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, (void*) vkReactive.Dx12Resource);

        InParameters->Set("FSR.reactive", (void*) nullptr);
        InParameters->Set("FSR.transparencyAndComposition", (void*) nullptr);

        LOG_DEBUG("Dispatch!!");
        dx12EvalResult = dx12Feature->Evaluate(cmdList, InParameters);

    } while (false);

    if (!dx12EvalResult)
    {
        if (interopPrepared)
            AbortPendingInterop(InCmdBuffer, frame);
        RestoreVulkanParameters();
        return false;
    }

    // The D3D12 feature writes only its view to the shared output, which doesn't hold the game's output.
    // Copying the whole image back would overwrite the other views.
    ReadSubrects(InParameters);
    const auto& outputRect = GetSubrect(SubrectSlot::Output);
    const bool copyView = outputRect.IsPartOf(vkOut.Width, vkOut.Height) &&
                          SubrectFits(SubrectSlot::Output, outputRect, vkOut.Width, vkOut.Height);

    if (!CopyBackOutput(InCmdBuffer, copyView ? &outputRect : nullptr))
    {
        LOG_ERROR("Can't copy output texture back!");
        AbortPendingInterop(InCmdBuffer, frame);
        RestoreVulkanParameters();
        return false;
    }

    RestoreVulkanParameters();

    _frameCount++;

    return true;
}

bool IFeature_VkwDx12::CreateSharedFenceSemaphore()
{
    LOG_FUNC();

    // Created in order, the last one exists only when all do
    if (vkSemaphoreCopyBack[VKDX12_BUFFER_COUNT - 1] != VK_NULL_HANDLE)
    {
        return true;
    }

    for (uint32_t i = 0; i < VKDX12_BUFFER_COUNT; i++)
    {
        // Create D3D12 fence with shared flag (only once)
        if (dx12FenceTextureCopy[i] == nullptr)
        {
            auto result =
                _dx11on12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&dx12FenceTextureCopy[i]));

            if (result != S_OK)
            {
                LOG_ERROR("Can't create dx12FenceTextureCopy {0:x}", result);
                return false;
            }
        }

        // Create shared handle for the fence (only once)
        if (vkSHForTextureCopy[i] == nullptr)
        {
            auto result = _dx11on12Device->CreateSharedHandle(dx12FenceTextureCopy[i], nullptr, GENERIC_ALL, nullptr,
                                                              &vkSHForTextureCopy[i]);

            if (result != S_OK)
            {
                LOG_ERROR("Can't create sharedhandle for dx12FenceTextureCopy {0:x}", result);
                return false;
            }
        }

        // Create Vulkan semaphore if it doesn't exist (only once)
        if (vkSemaphoreTextureCopy[i] == VK_NULL_HANDLE)
        {
            const VkSemaphoreTypeCreateInfo semaphoreTypeCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
                .initialValue = 0,
            };

            const VkSemaphoreCreateInfo semaphoreCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                .pNext = &semaphoreTypeCreateInfo,
            };

            if (vkCreateSemaphore(VulkanDevice, &semaphoreCreateInfo, nullptr, &vkSemaphoreTextureCopy[i]) !=
                VK_SUCCESS)
            {
                LOG_ERROR("Failed to create Vulkan semaphore");
                return false;
            }

            // Import the D3D12 fence into the Vulkan semaphore (ONLY ONCE!)
            const VkImportSemaphoreWin32HandleInfoKHR importSemaphoreInfo = {
                .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR,
                .semaphore = vkSemaphoreTextureCopy[i],
                .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT,
                .handle = vkSHForTextureCopy[i],
            };

            if (vkImportSemaphoreWin32HandleKHR(VulkanDevice, &importSemaphoreInfo) != VK_SUCCESS)
            {
                LOG_ERROR("Failed to import semaphore Win32 handle");
                SAFE_DESTROY_VK(vkDestroySemaphore, VulkanDevice, vkSemaphoreTextureCopy[i], nullptr);
                return false;
            }

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(VulkanDevice, VK_OBJECT_TYPE_SEMAPHORE, (uint64_t) vkSemaphoreTextureCopy[i],
                            "vkSemaphoreTextureCopy");
#endif

            LOG_DEBUG("Shared fence/semaphore created and imported successfully");
        }

        if (vkSemaphoreCopyBack[i] == VK_NULL_HANDLE)
        {
            const VkSemaphoreTypeCreateInfo semaphoreTypeCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
                .initialValue = 0,
            };

            const VkSemaphoreCreateInfo semaphoreCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                .pNext = &semaphoreTypeCreateInfo,
            };

            if (vkCreateSemaphore(VulkanDevice, &semaphoreCreateInfo, nullptr, &vkSemaphoreCopyBack[i]) != VK_SUCCESS)
            {
                LOG_ERROR("Failed to create Dummy Vulkan semaphore");
                return false;
            }
        }
    }

    return true;
}
