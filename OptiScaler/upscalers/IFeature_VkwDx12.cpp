#include <pch.h>

#include "IFeature_VkwDx12.h"

#include <Config.h>
#include <SysUtils.h>

#include <proxies/DXGI_Proxy.h>
#include <proxies/D3D12_Proxy.h>

#include <hooks/VulkanwDx12_Hooks.h>
#include <with_dx12/with_dx12.h>

#include <misc/IdentifyGpu.h>

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
    case VK_FORMAT_X8_D24_UNORM_PACK32:
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
        // Cleanup existing resources
        if (OutResource->VkSharedImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(VulkanDevice, OutResource->VkSharedImage, nullptr);
            OutResource->VkSharedImage = VK_NULL_HANDLE;
        }

        if (OutResource->VkSharedImageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(VulkanDevice, OutResource->VkSharedImageView, nullptr);
            OutResource->VkSharedImageView = VK_NULL_HANDLE;
        }

        if (OutResource->VkSharedMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(VulkanDevice, OutResource->VkSharedMemory, nullptr);
            OutResource->VkSharedMemory = VK_NULL_HANDLE;
        }

        SAFE_CLOSE_HANDLE(OutResource->SharedHandle);

        SAFE_RELEASE(OutResource->Dx12Resource);

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

    auto queueFamilyOpt = Vulkan_wDx12::cmdBufferStateTracker.GetCommandBufferQueueFamily(InCmdList);

    if (queueFamilyOpt.has_value())
    {
        uint32_t cmdBufferQueueFamily = queueFamilyOpt.value();
        LOG_DEBUG("Command buffer {:X} belongs to queue family {}", (size_t) InCmdList, cmdBufferQueueFamily);

        // Check if it matches your command pools
        if (cmdBufferQueueFamily != ActiveQueueFamilyIndex)
        {
            LOG_WARN("Queue family mismatch detected! App uses family {}, we use family {}", cmdBufferQueueFamily,
                     ActiveQueueFamilyIndex);

            // Recreate command pools for the correct queue family
            if (!CreateVulkanCommandBuffers(cmdBufferQueueFamily))
            {
                LOG_ERROR("Failed to create Vulkan command buffers for queue family {}", cmdBufferQueueFamily);
                return false;
            }

            ActiveQueueFamilyIndex = cmdBufferQueueFamily;
        }
    }
    else
    {
        LOG_WARN("Could not determine queue family for command buffer {:X}, using default {}", (size_t) InCmdList,
                 ActiveQueueFamilyIndex);
    }

    LOG_DEBUG("Upscaling command buffer: {:X}, frame: {}", (size_t) InCmdList, frame);

    // Only wait if we're more than 1 frame behind (triple buffering protection)
    uint64_t lastFrameToWaitFor = (_frameCount > 1) ? (_frameCount - 1) : 0;
    if (Dx12Fence->GetCompletedValue() < lastFrameToWaitFor)
    {
        // Only wait for N-1 frame, not current frame
        Dx12Fence->SetEventOnCompletion(lastFrameToWaitFor, Dx12FenceEvent);
        WaitForSingleObject(Dx12FenceEvent, INFINITE);
    }

    Dx12CommandAllocator[frame]->Reset();
    Dx12CommandList[frame]->Reset(Dx12CommandAllocator[frame], nullptr);

    HRESULT result;

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
                NVSDK_NGX_Result_Success &&
            !NvVkResourceNotValid(paramExposure))
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
                NVSDK_NGX_Result_Success &&
            !NvVkResourceNotValid(paramReactiveMask))
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

#pragma endregion

#pragma region Vulkan to D3D12 Synchronization

    // Create shared fence/semaphore if needed
    if (!CreateSharedFenceSemaphore())
    {
        LOG_ERROR("Failed to create shared fence/semaphore");
        return false;
    }

    LOG_DEBUG("Vulkan Signal & D3D12 Wait for copy operations!");

    // Will bu used to detect correct queue submit
    // Increment fence value
    _fenceValue++;
    Vulkan_wDx12::signalValueResourceCopy = _fenceValue;

    // Signal for D3D12 to wait on
    Vulkan_wDx12::timelineInfoResourceCopy.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    Vulkan_wDx12::timelineInfoResourceCopy.signalSemaphoreValueCount = 1;
    Vulkan_wDx12::timelineInfoResourceCopy.pSignalSemaphoreValues = &Vulkan_wDx12::signalValueResourceCopy;

    // Copy resources submit info
    Vulkan_wDx12::resourceCopySubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    Vulkan_wDx12::resourceCopySubmitInfo.pNext = &Vulkan_wDx12::timelineInfoResourceCopy;
    Vulkan_wDx12::resourceCopySubmitInfo.commandBufferCount = 1;
    Vulkan_wDx12::resourceCopySubmitInfo.pCommandBuffers = &InCmdList;
    Vulkan_wDx12::resourceCopySubmitInfo.signalSemaphoreCount = 1;
    Vulkan_wDx12::resourceCopySubmitInfo.pSignalSemaphores = &vkSemaphoreTextureCopy[frame];

    LOG_DEBUG("Signaling Vulkan semaphore with value: {}", Vulkan_wDx12::signalValueResourceCopy);

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

        // std::lock_guard<std::mutex> lock(Vulkan_wDx12::cmdBufferMutex);
        Vulkan_wDx12::virtualCmdBuffer = b.VulkanBarrierCommandBuffer[frame];

        // Configure replay parameters
        vk_state::ReplayParams params {};
        params.ReplayGraphicsPipeline = true;
        params.ReplayComputeToo = false;
        params.ReplayViewportScissor = true;
        params.ReplayExtendedDynamicState = true;
        params.ReplayPushConstants = true;
        params.ReplayVertexIndex = true;
        params.RequiredGraphicsSetMask = 0xFFFFFFFFu;
        params.OverrideGraphicsLayout = VK_NULL_HANDLE;

        // Simple one-line call using cached function table
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

    // D3D12 wait on the shared fence signaled by Vulkan
    result = Dx12CommandQueue->Wait(dx12FenceTextureCopy[frame], Vulkan_wDx12::signalValueResourceCopy);
    if (result != S_OK)
    {
        LOG_ERROR("Dx12CommandQueue->Wait failed: {0:x}", result);
        return false;
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

    Vulkan_wDx12::lastCmdBuffer = InCmdList;
    return true;
}

bool IFeature_VkwDx12::CopyBackOutput()
{
    LOG_FUNC();

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

    // Transition back to COMMON for Vulkan access
    AddBarrier(vkOut.Dx12Resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    AddBarrier(vkColor.Dx12Resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    AddBarrier(vkMv.Dx12Resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    AddBarrier(vkDepth.Dx12Resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    AddBarrier(vkExp.Dx12Resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    AddBarrier(vkReactive.Dx12Resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);

    // Batched transition
    if (!barriers.empty())
        Dx12CommandList[frame]->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

    // Close and execute the command list
    HRESULT result = Dx12CommandList[frame]->Close();
    if (result != S_OK)
    {
        LOG_ERROR("Dx12CommandList->Close failed: {0:x}", result);
        return false;
    }

    ID3D12CommandList* ppCommandLists[] = { Dx12CommandList[frame] };
    Dx12CommandQueue->ExecuteCommandLists(1, ppCommandLists);

    // Signal shared fence after processing
    _fenceValue++;
    Vulkan_wDx12::signalValueD3D12 = _fenceValue;

    result = Dx12CommandQueue->Signal(dx12FenceTextureCopy[frame], Vulkan_wDx12::signalValueD3D12);
    if (result != S_OK)
    {
        LOG_ERROR("Dx12CommandQueue->Signal (shared fence) failed: {0:x}", result);
        return false;
    }

    // Signal for next frame
    result = Dx12CommandQueue->Signal(Dx12Fence, _frameCount);
    if (result != S_OK)
    {
        LOG_ERROR("Dx12CommandQueue->Signal failed: {0:x}", result);
        return false;
    }

    // D3D12 side is completed now copy back output to Vulkan image
    if (vkOut.VkSourceImage != VK_NULL_HANDLE && vkOut.VkSharedImage != VK_NULL_HANDLE)
    {
        LOG_DEBUG("Copying output from shared image back to source image");

        if (!VulkanQueueCommandBuffers.contains(ActiveQueueFamilyIndex))
        {
            if (!CreateVulkanCommandBuffers(ActiveQueueFamilyIndex))
            {
                LOG_ERROR("Failed to create Vulkan command buffers for queue family {}", ActiveQueueFamilyIndex);
                return false;
            }
        }

        auto& b = VulkanQueueCommandBuffers[ActiveQueueFamilyIndex];

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
            if (resource->VkSourceImage != VK_NULL_HANDLE)
            {
                VkImageMemoryBarrier imageBarrier {};
                imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                imageBarrier.oldLayout = resource->VkSourceImageLayout;
                imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imageBarrier.image = resource->VkSharedImage;
                imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                imageBarrier.subresourceRange.baseMipLevel = 0;
                imageBarrier.subresourceRange.levelCount = 1;
                imageBarrier.subresourceRange.baseArrayLayer = 0;
                imageBarrier.subresourceRange.layerCount = 1;
                imageBarrier.srcAccessMask = resource->VkSourceImageAccess;
                imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                imageBarriers.push_back(imageBarrier);
            }
        };

        AddVkBarrier(&vkColor);
        AddVkBarrier(&vkDepth);
        AddVkBarrier(&vkMv);
        AddVkBarrier(&vkExp);
        AddVkBarrier(&vkReactive);

        vkCmdPipelineBarrier(b.VulkanCopyCommandBuffer[frame], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
                             static_cast<uint32_t>(imageBarriers.size()), imageBarriers.data());

        if (!Config::Instance()->VulkanUseCopyForOutput.value_or_default())
        {
            // Batch Vulkan barriers
            VkImageMemoryBarrier imageBarrier = {};

            // Transition shared image to transfer src
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

            // Single batched barrier call for pre-copy transitions
            vkCmdPipelineBarrier(b.VulkanCopyCommandBuffer[frame], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier);

            if (!OutCopy->CanRender())
            {
                LOG_ERROR("ResourceCopy_Vk not initialized!");
                return false;
            }

            // Create image views for depth transfer
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

            // Dispatch resource copy compute shader
            VkExtent2D extent = { vkOut.Width, vkOut.Height };
            if (!OutCopy->Dispatch(VulkanDevice, b.VulkanCopyCommandBuffer[frame], vkOut.VkSharedImageView,
                                   vkOut.VkSourceImageView, extent))
            {
                LOG_ERROR("Failed to dispatch resource copy!");
                return false;
            }
        }
        else
        {
            VkImageMemoryBarrier imageBarriers[2] = { {}, {} };

            // Shared
            imageBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageBarriers[0].oldLayout = vkOut.VkSharedImageLayout;
            imageBarriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            imageBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarriers[0].image = vkOut.VkSharedImage;
            imageBarriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageBarriers[0].subresourceRange.baseMipLevel = 0;
            imageBarriers[0].subresourceRange.levelCount = 1;
            imageBarriers[0].subresourceRange.baseArrayLayer = 0;
            imageBarriers[0].subresourceRange.layerCount = 1;
            imageBarriers[0].srcAccessMask = vkOut.VkSharedImageAccess;
            imageBarriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            // Source
            imageBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageBarriers[1].oldLayout = vkOut.VkSourceImageLayout;
            imageBarriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarriers[1].image = vkOut.VkSourceImage;
            imageBarriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageBarriers[1].subresourceRange.baseMipLevel = 0;
            imageBarriers[1].subresourceRange.levelCount = 1;
            imageBarriers[1].subresourceRange.baseArrayLayer = 0;
            imageBarriers[1].subresourceRange.layerCount = 1;
            imageBarriers[1].srcAccessMask = vkOut.VkSourceImageAccess;
            imageBarriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            vkCmdPipelineBarrier(b.VulkanCopyCommandBuffer[frame], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, imageBarriers);

            // Copy shared to source
            VkExtent3D extent = { vkOut.Width, vkOut.Height, 1 };
            VkImageCopy copyRegion = {};
            copyRegion.extent = extent;
            copyRegion.dstSubresource.aspectMask = imageBarriers[0].subresourceRange.aspectMask;
            copyRegion.dstSubresource.mipLevel = imageBarriers[0].subresourceRange.baseMipLevel;
            copyRegion.dstSubresource.baseArrayLayer = imageBarriers[0].subresourceRange.baseArrayLayer;
            copyRegion.dstSubresource.layerCount = imageBarriers[0].subresourceRange.layerCount;
            copyRegion.srcSubresource = copyRegion.dstSubresource;

            vkCmdCopyImage(b.VulkanCopyCommandBuffer[frame], imageBarriers[0].image, imageBarriers[0].newLayout,
                           imageBarriers[1].image, imageBarriers[1].newLayout, 1, &copyRegion);

            // Shared
            imageBarriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            imageBarriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            imageBarriers[0].srcAccessMask = imageBarriers[0].dstAccessMask;
            imageBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

            imageBarriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageBarriers[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            imageBarriers[1].srcAccessMask = imageBarriers[1].dstAccessMask;
            imageBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

            vkCmdPipelineBarrier(b.VulkanCopyCommandBuffer[frame], VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 2, imageBarriers);

            vkOut.VkSharedImageLayout = imageBarriers[0].newLayout;
            vkOut.VkSharedImageAccess = imageBarriers[0].dstAccessMask;
            vkOut.VkSourceImageLayout = imageBarriers[1].newLayout;
            vkOut.VkSourceImageAccess = imageBarriers[1].dstAccessMask;
        }

        // Close virtual command buffer
        vkResult = vkEndCommandBuffer(b.VulkanCopyCommandBuffer[frame]);
        if (vkResult != VK_SUCCESS)
        {
            LOG_ERROR("vkEndCommandBuffer error: {0:x}", (int) vkResult);
            return false;
        }

        LOG_DEBUG("D3D12 Signal & Vulkan Wait!");

        _fenceValue++;
        Vulkan_wDx12::signalValueCopyBack = _fenceValue;

        // Vulkan side will wait D3D12 shared fence
        Vulkan_wDx12::copyBackTimelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        Vulkan_wDx12::copyBackTimelineInfo.pNext = nullptr;
        Vulkan_wDx12::copyBackTimelineInfo.waitSemaphoreValueCount = 1;
        Vulkan_wDx12::copyBackTimelineInfo.pWaitSemaphoreValues = &Vulkan_wDx12::signalValueD3D12;
        Vulkan_wDx12::copyBackTimelineInfo.signalSemaphoreValueCount = 1;
        Vulkan_wDx12::copyBackTimelineInfo.pSignalSemaphoreValues = &Vulkan_wDx12::signalValueCopyBack;

        // This is copy back submit queue
        Vulkan_wDx12::copyBackWaitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        Vulkan_wDx12::copyBackSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        Vulkan_wDx12::copyBackSubmitInfo.pNext = &Vulkan_wDx12::copyBackTimelineInfo;
        Vulkan_wDx12::copyBackSubmitInfo.waitSemaphoreCount = 1;
        Vulkan_wDx12::copyBackSubmitInfo.pWaitSemaphores = &vkSemaphoreTextureCopy[frame];
        Vulkan_wDx12::copyBackSubmitInfo.pWaitDstStageMask = &Vulkan_wDx12::copyBackWaitStage;
        Vulkan_wDx12::copyBackSubmitInfo.signalSemaphoreCount = 1;
        Vulkan_wDx12::copyBackSubmitInfo.pSignalSemaphores = &vkSemaphoreCopyBack[frame];
        Vulkan_wDx12::copyBackSubmitInfo.commandBufferCount = 1;
        Vulkan_wDx12::copyBackSubmitInfo.pCommandBuffers = &b.VulkanCopyCommandBuffer[frame];

        // This is for syncing with copy back
        Vulkan_wDx12::syncTimelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        Vulkan_wDx12::syncTimelineInfo.pNext = nullptr;
        Vulkan_wDx12::syncTimelineInfo.waitSemaphoreValueCount = 1;
        Vulkan_wDx12::syncTimelineInfo.pWaitSemaphoreValues = &Vulkan_wDx12::signalValueCopyBack;
        Vulkan_wDx12::syncTimelineInfo.signalSemaphoreValueCount = 0;
        Vulkan_wDx12::syncTimelineInfo.pSignalSemaphoreValues = nullptr;

        // this is for moved command buffers and signals (also for to be sure copy back completed)
        Vulkan_wDx12::syncWaitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        Vulkan_wDx12::syncSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        Vulkan_wDx12::syncSubmitInfo.pNext = &Vulkan_wDx12::syncTimelineInfo;
        Vulkan_wDx12::syncSubmitInfo.waitSemaphoreCount = 1;
        Vulkan_wDx12::syncSubmitInfo.pWaitSemaphores = &vkSemaphoreCopyBack[frame];
        Vulkan_wDx12::syncSubmitInfo.pWaitDstStageMask = &Vulkan_wDx12::syncWaitStage;
        Vulkan_wDx12::syncSubmitInfo.signalSemaphoreCount = 0;
        Vulkan_wDx12::syncSubmitInfo.pSignalSemaphores = nullptr;
        Vulkan_wDx12::syncSubmitInfo.commandBufferCount = 1;
        Vulkan_wDx12::syncSubmitInfo.pCommandBuffers = &b.VulkanBarrierCommandBuffer[frame];

        // Trigger the injection on next vkQueueSubmit
        Vulkan_wDx12::commandBufferFoundCount = 0;

        LOG_DEBUG("Output copy completed synchronously");
    }

    return true;
}

void IFeature_VkwDx12::ReleaseSharedResources()
{
    LOG_FUNC();

    // Release Vulkan resources
    SAFE_DESTROY_VK(vkDestroyImage, VulkanDevice, vkColor.VkSharedImage, nullptr);
    SAFE_DESTROY_VK(vkDestroyImage, VulkanDevice, vkMv.VkSharedImage, nullptr);
    SAFE_DESTROY_VK(vkDestroyImage, VulkanDevice, vkOut.VkSharedImage, nullptr);
    SAFE_DESTROY_VK(vkDestroyImage, VulkanDevice, vkDepth.VkSharedImage, nullptr);
    SAFE_DESTROY_VK(vkDestroyImage, VulkanDevice, vkReactive.VkSharedImage, nullptr);
    SAFE_DESTROY_VK(vkDestroyImage, VulkanDevice, vkExp.VkSharedImage, nullptr);

    SAFE_DESTROY_VK(vkDestroyImageView, VulkanDevice, vkColor.VkSharedImageView, nullptr);
    SAFE_DESTROY_VK(vkDestroyImageView, VulkanDevice, vkMv.VkSharedImageView, nullptr);
    SAFE_DESTROY_VK(vkDestroyImageView, VulkanDevice, vkOut.VkSharedImageView, nullptr);
    SAFE_DESTROY_VK(vkDestroyImageView, VulkanDevice, vkDepth.VkSharedImageView, nullptr);
    SAFE_DESTROY_VK(vkDestroyImageView, VulkanDevice, vkReactive.VkSharedImageView, nullptr);
    SAFE_DESTROY_VK(vkDestroyImageView, VulkanDevice, vkExp.VkSharedImageView, nullptr);

    SAFE_DESTROY_VK(vkFreeMemory, VulkanDevice, vkColor.VkSharedMemory, nullptr);
    SAFE_DESTROY_VK(vkFreeMemory, VulkanDevice, vkMv.VkSharedMemory, nullptr);
    SAFE_DESTROY_VK(vkFreeMemory, VulkanDevice, vkOut.VkSharedMemory, nullptr);
    SAFE_DESTROY_VK(vkFreeMemory, VulkanDevice, vkDepth.VkSharedMemory, nullptr);
    SAFE_DESTROY_VK(vkFreeMemory, VulkanDevice, vkReactive.VkSharedMemory, nullptr);
    SAFE_DESTROY_VK(vkFreeMemory, VulkanDevice, vkExp.VkSharedMemory, nullptr);

    // Release D3D12 resources
    SAFE_RELEASE(vkColor.Dx12Resource);
    SAFE_RELEASE(vkMv.Dx12Resource);
    SAFE_RELEASE(vkOut.Dx12Resource);
    SAFE_RELEASE(vkDepth.Dx12Resource);
    SAFE_RELEASE(vkReactive.Dx12Resource);
    SAFE_RELEASE(vkExp.Dx12Resource);

    // Close handles
    SAFE_CLOSE_HANDLE(vkColor.SharedHandle);
    SAFE_CLOSE_HANDLE(vkMv.SharedHandle);
    SAFE_CLOSE_HANDLE(vkOut.SharedHandle);
    SAFE_CLOSE_HANDLE(vkDepth.SharedHandle);
    SAFE_CLOSE_HANDLE(vkReactive.SharedHandle);
    SAFE_CLOSE_HANDLE(vkExp.SharedHandle);

    // Cleanup Vulkan copy command buffer
    // Loop in VulkanQueueCommandBuffers instead of hardcoding 2 command buffers, in case we have more in the future
    for (auto& [index, b] : VulkanQueueCommandBuffers)
    {
        for (size_t i = 0; i < VKDX12_BUFFER_COUNT; i++)
        {
            if (b.VulkanBarrierCommandBuffer[i] != VK_NULL_HANDLE && b.VulkanBarrierCommandPool[i] != VK_NULL_HANDLE)
            {
                vkFreeCommandBuffers(VulkanDevice, b.VulkanBarrierCommandPool[i], 1, &b.VulkanBarrierCommandBuffer[i]);
                b.VulkanBarrierCommandBuffer[i] = VK_NULL_HANDLE;
            }

            SAFE_DESTROY_VK(vkDestroyCommandPool, VulkanDevice, b.VulkanBarrierCommandPool[i], nullptr);

            if (b.VulkanCopyCommandBuffer[i] != VK_NULL_HANDLE && b.VulkanCopyCommandPool[i] != VK_NULL_HANDLE)
            {
                vkFreeCommandBuffers(VulkanDevice, b.VulkanCopyCommandPool[i], 1, &b.VulkanCopyCommandBuffer[i]);
                b.VulkanCopyCommandBuffer[i] = VK_NULL_HANDLE;
            }

            SAFE_DESTROY_VK(vkDestroyCommandPool, VulkanDevice, b.VulkanCopyCommandPool[i], nullptr);
        }
    }

    VulkanQueueCommandBuffers.clear();

    ReleaseSyncResources();

    for (size_t i = 0; i < VKDX12_BUFFER_COUNT; i++)
    {
        SAFE_RELEASE(Dx12CommandList[i]);
        SAFE_RELEASE(Dx12CommandAllocator[i]);
    }

    SAFE_RELEASE(Dx12CommandQueue);
    SAFE_RELEASE(Dx12Fence);

    SAFE_CLOSE_HANDLE(Dx12FenceEvent);

    ColorCopy.reset();
    VelocityCopy.reset();
    DT.reset();
    DepthCopy.reset();
    ReactiveCopy.reset();
    ExpCopy.reset();
    OutCopy.reset();
    OutCopy2.reset();
}

void IFeature_VkwDx12::ReleaseSyncResources()
{
    LOG_FUNC();
    for (uint32_t i = 0; i < VKDX12_BUFFER_COUNT; i++)
    {
        SAFE_DESTROY_VK(vkDestroySemaphore, VulkanDevice, vkSemaphoreTextureCopy[i], nullptr);
        SAFE_RELEASE(dx12FenceTextureCopy[i]);

        SAFE_CLOSE_HANDLE(vkSHForTextureCopy[i]);

        SAFE_DESTROY_VK(vkDestroySemaphore, VulkanDevice, vkSemaphoreCopyBack[i], nullptr);
    }
}

HRESULT IFeature_VkwDx12::CreateDx12Device()
{
    LOG_FUNC();

    ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
    ScopedSkipVulkanHooks skipVulkanHooks {};

    HRESULT result;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_0;

    if (State::Instance().currentD3D12Device == nullptr ||
        ((State::Instance().gameQuirks & GameQuirk::ForceCreateD3D12Device) && _localDx11on12Device == nullptr))
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
        IdentifyGpu::getHardwareAdapter(factory, &hwAdapter, featureLevel);

        if (hwAdapter == nullptr)
            LOG_WARN("Can't get hwAdapter, will try nullptr!");

        _localDx11on12Device = WithDx12::RequestD3D12Device(featureLevel, hwAdapter);

        if (_localDx11on12Device == nullptr)
        {
            LOG_ERROR("Can't create device!");
            return result;
        }

        _dx11on12Device = _localDx11on12Device;

        if (hwAdapter != nullptr)
        {
            DXGI_ADAPTER_DESC desc {};
            auto primaryGpu = IdentifyGpu::getPrimaryGpu();
            if (hwAdapter->GetDesc(&desc) == S_OK && !IsEqualLUID(desc.AdapterLuid, primaryGpu.luid))
            {
                LOG_WARN("D3D12Device created with non-primary GPU");
            }
        }

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

bool IFeature_VkwDx12::BaseInit(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                VkCommandBuffer InCmdList, PFN_vkGetInstanceProcAddr InGIPA,
                                PFN_vkGetDeviceProcAddr InGDPA, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (IsInited())
        return true;

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

    return true;
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

    DT.reset();
    OutputScaler.reset();
    RCAS.reset();
    Bias.reset();
}

bool IFeature_VkwDx12::CreateSharedFenceSemaphore()
{
    LOG_FUNC();

    // If already created, just return success
    if (dx12FenceTextureCopy[0] != nullptr && vkSemaphoreTextureCopy[0] != VK_NULL_HANDLE)
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
