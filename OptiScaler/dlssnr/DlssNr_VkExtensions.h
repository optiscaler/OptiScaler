#pragma once

// What the Neural Rendering model needs from a Vulkan device, and whether it can be arranged.
//
// The model ships a complete native Vulkan surface -- fourteen entry points, more than either D3D
// interface has -- so there is no reason for the pass to go through a D3D12 bridge on Vulkan. What
// stops it is not the model, it is the device: NGX loads its kernels through two NVIDIA vendor
// extensions that no game enables, and a Vulkan device's extension list is fixed at creation. Ask
// afterwards and the answer is no, permanently.
//
// OptiScaler already hooks vkCreateInstance and vkCreateDevice and already hands the real call a
// mutable copy of the create info, so appending to that list is what the hook is shaped for. This
// header is the list and the appending, kept in the module so it leaves with it.
//
// The names come from the model binary itself rather than from documentation: scanning
// nvngx_dlssnr.dll for VK_*_* yields exactly these.

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

namespace DlssNr::VkExt
{

// Instance level. get_physical_device_properties2 is core from Vulkan 1.1 and every game enables it
// anyway; it is listed because the model names it and a 1.0 instance would still need it.
inline const char* const kInstance[] = {
    VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
};

// Device level. The two NVX entries are the ones that matter and the reason this file exists --
// binary_import is how NGX hands the driver its cubins, and image_view_handle is how it addresses
// the textures it was given. Neither appears in a game's own list, ever.
inline const char* const kDevice[] = {
    "VK_NVX_binary_import",
    "VK_NVX_image_view_handle",
    VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
};

// Holds the merged list for as long as the create call needs it. VkDeviceCreateInfo keeps a bare
// pointer, so the storage has to outlive the call rather than the statement.
struct Merged
{
    std::vector<const char*> names;
    std::vector<std::string> owned;
};

// Everything the physical device is willing to offer, by name.
inline std::vector<std::string> SupportedDeviceExtensions(PFN_vkGetInstanceProcAddr getInstanceProcAddr,
                                                          VkInstance instance, VkPhysicalDevice physicalDevice)
{
    std::vector<std::string> out;

    if (getInstanceProcAddr == nullptr || physicalDevice == VK_NULL_HANDLE)
        return out;

    auto enumerate = (PFN_vkEnumerateDeviceExtensionProperties) getInstanceProcAddr(
        instance, "vkEnumerateDeviceExtensionProperties");

    if (enumerate == nullptr)
        return out;

    uint32_t count = 0;

    if (enumerate(physicalDevice, nullptr, &count, nullptr) != VK_SUCCESS || count == 0)
        return out;

    std::vector<VkExtensionProperties> props(count);

    if (enumerate(physicalDevice, nullptr, &count, props.data()) != VK_SUCCESS)
        return out;

    out.reserve(count);

    for (const auto& p : props)
        out.emplace_back(p.extensionName);

    return out;
}

inline bool Contains(const std::vector<std::string>& haystack, const char* needle)
{
    for (const auto& h : haystack)
    {
        if (h == needle)
            return true;
    }

    return false;
}

inline bool ListHas(const char* const* list, uint32_t count, const char* needle)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        if (list[i] != nullptr && std::string(list[i]) == needle)
            return true;
    }

    return false;
}

} // namespace DlssNr::VkExt
