#include "pch.h"
#include "Vulkan_Spoofing.h"

#include <Config.h>
#include <SysUtils.h>

#include <proxies/KernelBase_Proxy.h>
#include <hooks/VulkanwDx12_Hooks.h>

#include <magic_enum.hpp>

#include <detours/detours.h>

#include <vulkan/vulkan_core.h>

static std::map<std::string, bool> vkInstanceExtensions;

typedef struct VkDummyProps
{
    VkStructureType sType;
    void* pNext;
} VkDummyProps;

static PFN_vkGetPhysicalDeviceProperties o_vkGetPhysicalDeviceProperties = nullptr;
static PFN_vkGetPhysicalDeviceProperties2 o_vkGetPhysicalDeviceProperties2 = nullptr;
static PFN_vkGetPhysicalDeviceProperties2KHR o_vkGetPhysicalDeviceProperties2KHR = nullptr;
static PFN_vkGetPhysicalDeviceMemoryProperties o_vkGetPhysicalDeviceMemoryProperties = nullptr;
static PFN_vkGetPhysicalDeviceMemoryProperties2 o_vkGetPhysicalDeviceMemoryProperties2 = nullptr;
static PFN_vkGetPhysicalDeviceMemoryProperties2KHR o_vkGetPhysicalDeviceMemoryProperties2KHR = nullptr;
static PFN_vkEnumerateDeviceExtensionProperties o_vkEnumerateDeviceExtensionProperties = nullptr;
static PFN_vkEnumerateInstanceExtensionProperties o_vkEnumerateInstanceExtensionProperties = nullptr;

static uint32_t vkEnumerateInstanceExtensionPropertiesCount = 0;
static uint32_t vkEnumerateDeviceExtensionPropertiesCount = 0;
static bool vkEnumerateDeviceExtensionPropertiesListed = false;
static bool vkEnumerateInstanceExtensionPropertiesListed = false;

static bool HasExtension(size_t extensionCount, const char* const* extensions, const char* extension)
{
    for (size_t i = 0; i < extensionCount; ++i)
    {
        if (std::strcmp(extensions[i], extension) == 0)
            return true;
    }

    return false;
}

static bool HasExtension(const std::vector<const char*>& extensions, const char* extension)
{
    return HasExtension(extensions.size(), extensions.data(), extension);
}

static bool SupportsExtension(const std::vector<VkExtensionProperties>& extensions, const char* extension)
{
    for (const auto& supported : extensions)
    {
        if (std::strcmp(supported.extensionName, extension) == 0)
            return true;
    }

    return false;
}

static bool HasExtension(const VkDeviceCreateInfo* pCreateInfo, const char* extension)
{
    return HasExtension(pCreateInfo->enabledExtensionCount, pCreateInfo->ppEnabledExtensionNames, extension);
}

template <typename T> static const T* FindFeatureStruct(const void* pNext, VkStructureType sType)
{
    for (auto* next = static_cast<const VkBaseInStructure*>(pNext); next != nullptr; next = next->pNext)
    {
        if (next->sType == sType)
            return reinterpret_cast<const T*>(next);
    }

    return nullptr;
}

template <typename... Features> static const void* CopyFeatureChain(const void* pNext, Features&... features)
{
    if (pNext == nullptr)
        return nullptr;

    const auto* next = static_cast<const VkBaseInStructure*>(pNext);
    const void* copy = pNext;
    const auto copyFeature = [&]<typename T>(T& feature)
    {
        if (next->sType != feature.sType)
            return false;

        feature = *reinterpret_cast<const T*>(next);
        feature.pNext = const_cast<void*>(CopyFeatureChain(next->pNext, features...));
        copy = &feature;
        return true;
    };

    if (!(copyFeature(features) || ...))
        LOG_DEBUG("Leaving Vulkan feature chain unchanged from sType {}", static_cast<int>(next->sType));

    return copy;
}

template <typename T>
static T GetSupportedFeatureStruct(PFN_vkGetPhysicalDeviceFeatures2 getFeatures2, VkPhysicalDevice physicalDevice,
                                   const T& feature)
{
    T supported {};
    supported.sType = feature.sType;

    VkPhysicalDeviceFeatures2 features { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    features.pNext = &supported;
    getFeatures2(physicalDevice, &features);
    return supported;
}

template <typename T>
static bool IsFeatureSupported(PFN_vkGetPhysicalDeviceFeatures2 getFeatures2, VkPhysicalDevice physicalDevice,
                               const T& feature, VkBool32 T::* member)
{
    return GetSupportedFeatureStruct(getFeatures2, physicalDevice, feature).*member == VK_TRUE;
}

struct ModifiedFeature
{
    VkBool32* value = nullptr;
    VkBool32 original = VK_FALSE;
};

static void EnableFeatureValue(std::vector<ModifiedFeature>& modifiedFeatures, VkBool32& enabled, VkBool32 supported,
                               bool restoreAfterCreate)
{
    if (supported != VK_TRUE || enabled == VK_TRUE)
        return;

    if (restoreAfterCreate)
        modifiedFeatures.push_back({ &enabled, enabled });

    enabled = VK_TRUE;
}

template <typename T>
static void EnableExistingFeature(std::vector<ModifiedFeature>& modifiedFeatures, T* features, T& ownedFeature,
                                  VkBool32 T::* member, VkBool32 supported)
{
    if (features == nullptr)
        return;

    EnableFeatureValue(modifiedFeatures, features->*member, supported, features != &ownedFeature);
}

template <typename T>
static void EnableSupportedFeature(VkDeviceCreateInfo* pCreateInfo, std::vector<ModifiedFeature>& modifiedFeatures,
                                   T& feature, VkBool32 T::* member, VkBool32 supported)
{
    if (supported != VK_TRUE)
        return;

    auto* existingFeature = const_cast<T*>(FindFeatureStruct<T>(pCreateInfo->pNext, feature.sType));
    if (existingFeature == nullptr)
    {
        feature.pNext = const_cast<void*>(pCreateInfo->pNext);
        pCreateInfo->pNext = &feature;
        existingFeature = &feature;
    }

    EnableExistingFeature(modifiedFeatures, existingFeature, feature, member, supported);
}

template <typename T, typename... Members>
static void EnableFeatureStruct(VkDeviceCreateInfo* pCreateInfo, PFN_vkGetPhysicalDeviceFeatures2 getFeatures2,
                                VkPhysicalDevice physicalDevice, std::vector<ModifiedFeature>& modifiedFeatures,
                                T& feature, Members... members)
{
    const auto supported = GetSupportedFeatureStruct(getFeatures2, physicalDevice, feature);
    (EnableSupportedFeature(pCreateInfo, modifiedFeatures, feature, members, supported.*members), ...);
}

inline static void hkvkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice,
                                                         VkPhysicalDeviceMemoryProperties* pMemoryProperties)
{
    o_vkGetPhysicalDeviceMemoryProperties(physicalDevice, pMemoryProperties);

    if (pMemoryProperties == nullptr)
        return;

    for (size_t i = 0; i < pMemoryProperties->memoryHeapCount; i++)
    {
        if (pMemoryProperties->memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
        {
            uint64_t newMemSize = (uint64_t) Config::Instance()->VulkanVRAM.value() * 1073741824; // 1024 * 1024 * 1024
            pMemoryProperties->memoryHeaps[i].size = newMemSize;
        }
    }
}

inline static void hkvkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice,
                                                          VkPhysicalDeviceMemoryProperties2* pMemoryProperties)
{
    o_vkGetPhysicalDeviceMemoryProperties2(physicalDevice, pMemoryProperties);

    if (pMemoryProperties == nullptr ||
        pMemoryProperties->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2)
        return;

    for (size_t i = 0; i < pMemoryProperties->memoryProperties.memoryHeapCount; i++)
    {
        if (pMemoryProperties->memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
        {
            uint64_t newMemSize = (uint64_t) Config::Instance()->VulkanVRAM.value() * 1073741824; // 1024 * 1024 * 1024
            pMemoryProperties->memoryProperties.memoryHeaps[i].size = newMemSize;
        }
    }
}

inline static void hkvkGetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice physicalDevice,
                                                             VkPhysicalDeviceMemoryProperties2* pMemoryProperties)
{
    o_vkGetPhysicalDeviceMemoryProperties2(physicalDevice, pMemoryProperties);

    if (pMemoryProperties == nullptr ||
        pMemoryProperties->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2_KHR)
        return;

    for (size_t i = 0; i < pMemoryProperties->memoryProperties.memoryHeapCount; i++)
    {
        if (pMemoryProperties->memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
        {
            uint64_t newMemSize = (uint64_t) Config::Instance()->VulkanVRAM.value() * 1073741824; // 1024 * 1024 * 1024
            pMemoryProperties->memoryProperties.memoryHeaps[i].size = newMemSize;
        }
    }
}

inline static void hkvkGetPhysicalDeviceProperties(VkPhysicalDevice physical_device,
                                                   VkPhysicalDeviceProperties* properties)
{
    o_vkGetPhysicalDeviceProperties(physical_device, properties);

    auto targetVendorIdMatches = !Config::Instance()->TargetVendorId.has_value() ||
                                 Config::Instance()->TargetVendorId.value() == properties->vendorID;

    auto targetDeviceIdMatches = !Config::Instance()->TargetDeviceId.has_value() ||
                                 Config::Instance()->TargetDeviceId.value() == properties->deviceID;

    // Spoof
    if (Config::Instance()->VulkanSpoofing.value_or_default() && !SkipVulkanSpoofing() && targetVendorIdMatches &&
        targetDeviceIdMatches)
    {
        auto deviceName = wstring_to_string(Config::Instance()->SpoofedGPUName.value_or_default());
        std::strcpy(properties->deviceName, deviceName.c_str());

        properties->vendorID = Config::Instance()->SpoofedVendorId.value_or_default();
        properties->deviceID = Config::Instance()->SpoofedDeviceId.value_or_default();
        properties->driverVersion = VK_MAKE_API_VERSION(999, 99, 0, 0);

        LOG_DEBUG("Spoofed");
    }
    else
    {
        LOG_DEBUG("Skipping spoofing");
    }
}

inline static void hkvkGetPhysicalDeviceProperties2(VkPhysicalDevice phys_dev, VkPhysicalDeviceProperties2* properties2)
{
    o_vkGetPhysicalDeviceProperties2(phys_dev, properties2);

    auto targetVendorIdMatches = !Config::Instance()->TargetVendorId.has_value() ||
                                 Config::Instance()->TargetVendorId.value() == properties2->properties.vendorID;

    auto targetDeviceIdMatches = !Config::Instance()->TargetDeviceId.has_value() ||
                                 Config::Instance()->TargetDeviceId.value() == properties2->properties.deviceID;

    // Spoof
    if (Config::Instance()->VulkanSpoofing.value_or_default() && !SkipVulkanSpoofing() && targetVendorIdMatches &&
        targetDeviceIdMatches)
    {
        auto deviceName = wstring_to_string(Config::Instance()->SpoofedGPUName.value_or_default());
        std::strcpy(properties2->properties.deviceName, deviceName.c_str());
        properties2->properties.vendorID = Config::Instance()->SpoofedVendorId.value_or_default();
        properties2->properties.deviceID = Config::Instance()->SpoofedDeviceId.value_or_default();
        properties2->properties.driverVersion = VK_MAKE_API_VERSION(999, 99, 0, 0);

        // If spoofing Nvidia
        if (Config::Instance()->SpoofedVendorId.value_or_default() == VendorId::Nvidia)
        {
            auto next = (VkDummyProps*) properties2->pNext;

            while (next != nullptr)
            {
                if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES)
                {
                    auto ddp = (VkPhysicalDeviceDriverProperties*) (void*) next;
                    ddp->driverID = VK_DRIVER_ID_NVIDIA_PROPRIETARY;
                    std::strcpy(ddp->driverName, "NVIDIA");
                    std::strcpy(ddp->driverInfo, "999.99");
                }

                next = (VkDummyProps*) next->pNext;
            }
        }

        LOG_DEBUG("Spoofed");
    }
    else
    {
        LOG_DEBUG("Skipping spoofing");
    }
}

inline static void hkvkGetPhysicalDeviceProperties2KHR(VkPhysicalDevice phys_dev,
                                                       VkPhysicalDeviceProperties2* properties2)
{
    o_vkGetPhysicalDeviceProperties2KHR(phys_dev, properties2);

    auto targetVendorIdMatches = !Config::Instance()->TargetVendorId.has_value() ||
                                 Config::Instance()->TargetVendorId.value() == properties2->properties.vendorID;

    auto targetDeviceIdMatches = !Config::Instance()->TargetDeviceId.has_value() ||
                                 Config::Instance()->TargetDeviceId.value() == properties2->properties.deviceID;

    // Spoof
    if (Config::Instance()->VulkanSpoofing.value_or_default() && !SkipVulkanSpoofing() && targetVendorIdMatches &&
        targetDeviceIdMatches)
    {
        auto deviceName = wstring_to_string(Config::Instance()->SpoofedGPUName.value_or_default());
        std::strcpy(properties2->properties.deviceName, deviceName.c_str());
        properties2->properties.vendorID = Config::Instance()->SpoofedVendorId.value_or_default();
        properties2->properties.deviceID = Config::Instance()->SpoofedDeviceId.value_or_default();
        properties2->properties.driverVersion = VK_MAKE_API_VERSION(999, 99, 0, 0);

        // If spoofing Nvidia
        if (Config::Instance()->SpoofedVendorId.value_or_default() == VendorId::Nvidia)
        {
            auto next = (VkDummyProps*) properties2->pNext;

            while (next != nullptr)
            {
                if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES)
                {
                    auto ddp = (VkPhysicalDeviceDriverProperties*) (void*) next;
                    ddp->driverID = VK_DRIVER_ID_NVIDIA_PROPRIETARY;
                    std::strcpy(ddp->driverName, "NVIDIA");
                    std::strcpy(ddp->driverInfo, "999.99");
                }

                next = (VkDummyProps*) next->pNext;
            }
        }

        LOG_DEBUG("Spoofed");
    }
    else
    {
        LOG_DEBUG("Skipping spoofing");
    }
}

inline static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    LOG_TRACE("{}", pCallbackData->pMessage);
    return VK_FALSE; // return VK_TRUE to abort calls that triggered validation errors
}

VkResult VulkanSpoofing::hkvkCreateInstance(VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,
                                            VkInstance* pInstance)
{
    if (State::Instance().creatingD3DDevice)
    {
        LOG_INFO("Skipping because DXVK/VKD3D is creating a D3D device");
        return VK_SUCCESS;
    }

    if (pCreateInfo == nullptr)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (vkInstanceExtensions.size() == 0)
    {
        auto enumarate = o_vkEnumerateInstanceExtensionProperties;
        if (o_vkEnumerateInstanceExtensionProperties == nullptr)
        {
            if (vulkanModule == nullptr)
                vulkanModule = KernelBaseProxy::GetModuleHandleA_()("vulkan-1.dll");

            if (vulkanModule != nullptr)
            {
                enumarate = (PFN_vkEnumerateInstanceExtensionProperties) KernelBaseProxy::GetProcAddress_()(
                    vulkanModule, "vkEnumerateInstanceExtensionProperties");
            }

            if (enumarate == nullptr)
            {
                enumarate = vkEnumerateInstanceExtensionProperties;
            }
        }

        if (enumarate != nullptr)
        {
            LOG_INFO("vkInstanceExtensions is empty, enumerating instance extensions");
            vkEnumerateInstanceExtensionPropertiesListed = true;
            vkEnumerateInstanceExtensionPropertiesCount = 0;

            enumarate(VK_NULL_HANDLE, &vkEnumerateInstanceExtensionPropertiesCount, VK_NULL_HANDLE);
            std::vector<VkExtensionProperties> extensions(vkEnumerateInstanceExtensionPropertiesCount);
            enumarate(VK_NULL_HANDLE, &vkEnumerateInstanceExtensionPropertiesCount, extensions.data());
            for (const auto& ext : extensions)
            {
                vkInstanceExtensions[ext.extensionName] = true;
                LOG_DEBUG("  {}", ext.extensionName);
            }
        }
    }

    if (pCreateInfo->pApplicationInfo != nullptr && pCreateInfo->pApplicationInfo->pApplicationName != nullptr)
    {
        LOG_DEBUG("ApplicationName: {}", pCreateInfo->pApplicationInfo->pApplicationName);
    }

    static std::vector<const char*> newExtensionList;
    newExtensionList.clear();

    LOG_DEBUG("Extensions ({}):", pCreateInfo->enabledExtensionCount);
    for (size_t i = 0; i < pCreateInfo->enabledExtensionCount; i++)
    {
        LOG_DEBUG("  {}", pCreateInfo->ppEnabledExtensionNames[i]);
        newExtensionList.push_back(pCreateInfo->ppEnabledExtensionNames[i]);
    }

    const auto addExtension = [&](const char* extension)
    {
        if (HasExtension(newExtensionList, extension))
        {
            LOG_DEBUG("  {} already enabled", extension);
            return true;
        }

        if (!vkInstanceExtensions.contains(extension))
        {
            LOG_DEBUG("  {} not supported, skipping", extension);
            return false;
        }

        LOG_DEBUG("  Adding {}", extension);
        newExtensionList.push_back(extension);
        return true;
    };

    LOG_INFO("Adding FFX Vulkan extensions");
    addExtension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    addExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    LOG_INFO("Adding Vulkan w/Dx12 extensions");
    addExtension(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
    addExtension(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);

    LOG_DEBUG("Layer count: {}", pCreateInfo->enabledLayerCount);
    for (size_t i = 0; i < pCreateInfo->enabledLayerCount; i++)
        LOG_DEBUG("  {}", pCreateInfo->ppEnabledLayerNames[i]);

#ifdef VULKAN_DEBUG_LAYER
    debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

    debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

    debugCreateInfo.pfnUserCallback = &VulkanDebugCallback;

    static std::vector<const char*> newLayerList;
    newLayerList.clear();
    newLayerList.push_back("VK_LAYER_KHRONOS_validation");

    for (size_t i = 0; i < pCreateInfo->enabledLayerCount; i++)
        newLayerList.push_back(pCreateInfo->ppEnabledLayerNames[i]);

    pCreateInfo->enabledLayerCount = static_cast<uint32_t>(newLayerList.size());
    pCreateInfo->ppEnabledLayerNames = newLayerList.data();

    auto next = (VkDummyProps*) pCreateInfo;

    while (next->pNext != nullptr)
    {
        next = (VkDummyProps*) next->pNext;
    }

    next->pNext = &debugCreateInfo;

    newExtensionList.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    pCreateInfo->enabledExtensionCount = static_cast<uint32_t>(newExtensionList.size());
    pCreateInfo->ppEnabledExtensionNames = newExtensionList.data();

    return VK_SUCCESS;
}

struct VulkanDeviceFeatureState::Impl
{
    VkDeviceCreateInfo* pCreateInfo = nullptr;
    PFN_vkGetPhysicalDeviceFeatures2 getFeatures2 = nullptr;
    uint32_t apiVersion = VK_API_VERSION_1_0;
    std::vector<ModifiedFeature> modifiedFeatures;

    VkPhysicalDeviceFeatures2 features2 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    VkPhysicalDeviceVulkan12Features vulkan12Features { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceVulkan13Features vulkan13Features { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceVulkan14Features vulkan14Features { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES };
    VkPhysicalDeviceFeatures coreFeatures {};
    VkPhysicalDeviceShaderFloat16Int8Features shaderFloat16Int8 {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES
    };
    VkPhysicalDevice8BitStorageFeatures storage8Bit { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES };
    VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexing {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES
    };
    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddress {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES
    };
    VkPhysicalDeviceVulkanMemoryModelFeatures vulkanMemoryModel {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES
    };
    VkPhysicalDeviceSynchronization2Features synchronization2 {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES
    };
    VkPhysicalDeviceSubgroupSizeControlFeatures subgroupSizeControl {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES
    };
    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBuffer {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT
    };
    VkPhysicalDeviceShaderFloat8FeaturesEXT shaderFloat8 {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT8_FEATURES_EXT
    };
    VkPhysicalDeviceCooperativeMatrixFeaturesKHR cooperativeMatrix {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR
    };
    VkPhysicalDeviceCooperativeMatrix2FeaturesNV cooperativeMatrix2 {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_2_FEATURES_NV
    };
    VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR computeShaderDerivatives {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR
    };

    Impl(VkDeviceCreateInfo* pCreateInfo, PFN_vkGetPhysicalDeviceFeatures2 getFeatures2)
        : pCreateInfo(pCreateInfo), getFeatures2(getFeatures2)
    {
    }

    ~Impl()
    {
        for (auto modifiedFeature = modifiedFeatures.rbegin(); modifiedFeature != modifiedFeatures.rend();
             ++modifiedFeature)
            *modifiedFeature->value = modifiedFeature->original;
    }

    void CopyOwnedFeatureChain()
    {
        pCreateInfo->pNext = CopyFeatureChain(
            pCreateInfo->pNext, features2, vulkan12Features, vulkan13Features, shaderFloat16Int8, storage8Bit,
            descriptorIndexing, bufferDeviceAddress, vulkanMemoryModel, synchronization2, subgroupSizeControl,
            descriptorBuffer, shaderFloat8, cooperativeMatrix, cooperativeMatrix2, computeShaderDerivatives);
    }

    bool CanUpgradeExtBufferDeviceAddress(VkPhysicalDevice physicalDevice)
    {
        if (getFeatures2 == nullptr)
            return false;

        const auto* extFeatures = FindFeatureStruct<VkPhysicalDeviceBufferDeviceAddressFeaturesEXT>(
            pCreateInfo->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT);
        if (extFeatures != nullptr)
        {
            LOG_INFO("Preserving VK_EXT_buffer_device_address because the application supplied its EXT feature struct "
                     "(bufferDeviceAddress={}, captureReplay={}, multiDevice={})",
                     extFeatures->bufferDeviceAddress == VK_TRUE,
                     extFeatures->bufferDeviceAddressCaptureReplay == VK_TRUE,
                     extFeatures->bufferDeviceAddressMultiDevice == VK_TRUE);
            return false;
        }

        const auto supported = GetSupportedFeatureStruct(getFeatures2, physicalDevice, bufferDeviceAddress);
        if (supported.bufferDeviceAddress != VK_TRUE)
        {
            LOG_DEBUG("Not upgrading VK_EXT_buffer_device_address because KHR/core bufferDeviceAddress is unsupported");
            return false;
        }

        return true;
    }

    void EnableCoreFeatures(VkPhysicalDevice physicalDevice)
    {
        VkPhysicalDeviceFeatures2 supported { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        getFeatures2(physicalDevice, &supported);

        if (const auto* existing = FindFeatureStruct<VkPhysicalDeviceFeatures2>(
                pCreateInfo->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2))
        {
            auto* features = const_cast<VkPhysicalDeviceFeatures2*>(existing);
            EnableFeatureValue(modifiedFeatures, features->features.shaderInt16, supported.features.shaderInt16,
                               features != &features2);
            EnableFeatureValue(modifiedFeatures, features->features.shaderStorageImageWriteWithoutFormat,
                               supported.features.shaderStorageImageWriteWithoutFormat, features != &features2);
            return;
        }

        if (pCreateInfo->pEnabledFeatures != nullptr)
            coreFeatures = *pCreateInfo->pEnabledFeatures;

        if (supported.features.shaderInt16 == VK_TRUE)
            coreFeatures.shaderInt16 = VK_TRUE;

        if (supported.features.shaderStorageImageWriteWithoutFormat == VK_TRUE)
            coreFeatures.shaderStorageImageWriteWithoutFormat = VK_TRUE;

        pCreateInfo->pEnabledFeatures = &coreFeatures;
    }

    void EnableVulkan12Features(VkPhysicalDevice physicalDevice)
    {
        if (apiVersion >= VK_API_VERSION_1_2)
        {
            const auto supported = GetSupportedFeatureStruct(getFeatures2, physicalDevice, vulkan12Features);

            if (const auto* existing = FindFeatureStruct<VkPhysicalDeviceVulkan12Features>(
                    pCreateInfo->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES))
            {
                auto* features = const_cast<VkPhysicalDeviceVulkan12Features*>(existing);
                EnableExistingFeature(modifiedFeatures, features, vulkan12Features,
                                      &VkPhysicalDeviceVulkan12Features::shaderFloat16, supported.shaderFloat16);
                EnableExistingFeature(modifiedFeatures, features, vulkan12Features,
                                      &VkPhysicalDeviceVulkan12Features::shaderInt8, supported.shaderInt8);
                EnableExistingFeature(modifiedFeatures, features, vulkan12Features,
                                      &VkPhysicalDeviceVulkan12Features::storageBuffer8BitAccess,
                                      supported.storageBuffer8BitAccess);
                EnableExistingFeature(modifiedFeatures, features, vulkan12Features,
                                      &VkPhysicalDeviceVulkan12Features::runtimeDescriptorArray,
                                      supported.runtimeDescriptorArray);
                EnableExistingFeature(modifiedFeatures, features, vulkan12Features,
                                      &VkPhysicalDeviceVulkan12Features::descriptorBindingPartiallyBound,
                                      supported.descriptorBindingPartiallyBound);
                EnableExistingFeature(modifiedFeatures, features, vulkan12Features,
                                      &VkPhysicalDeviceVulkan12Features::shaderSampledImageArrayNonUniformIndexing,
                                      supported.shaderSampledImageArrayNonUniformIndexing);
                EnableExistingFeature(modifiedFeatures, features, vulkan12Features,
                                      &VkPhysicalDeviceVulkan12Features::shaderStorageImageArrayNonUniformIndexing,
                                      supported.shaderStorageImageArrayNonUniformIndexing);
                EnableExistingFeature(modifiedFeatures, features, vulkan12Features,
                                      &VkPhysicalDeviceVulkan12Features::shaderStorageBufferArrayNonUniformIndexing,
                                      supported.shaderStorageBufferArrayNonUniformIndexing);
                EnableExistingFeature(modifiedFeatures, features, vulkan12Features,
                                      &VkPhysicalDeviceVulkan12Features::vulkanMemoryModel,
                                      supported.vulkanMemoryModel);
                if (!HasExtension(pCreateInfo, VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))
                    EnableExistingFeature(modifiedFeatures, features, vulkan12Features,
                                          &VkPhysicalDeviceVulkan12Features::bufferDeviceAddress,
                                          supported.bufferDeviceAddress);

                return;
            }

            EnableSupportedFeature(pCreateInfo, modifiedFeatures, shaderFloat16Int8,
                                   &VkPhysicalDeviceShaderFloat16Int8Features::shaderFloat16, supported.shaderFloat16);
            EnableSupportedFeature(pCreateInfo, modifiedFeatures, shaderFloat16Int8,
                                   &VkPhysicalDeviceShaderFloat16Int8Features::shaderInt8, supported.shaderInt8);
            EnableSupportedFeature(pCreateInfo, modifiedFeatures, storage8Bit,
                                   &VkPhysicalDevice8BitStorageFeatures::storageBuffer8BitAccess,
                                   supported.storageBuffer8BitAccess);
            EnableSupportedFeature(pCreateInfo, modifiedFeatures, descriptorIndexing,
                                   &VkPhysicalDeviceDescriptorIndexingFeatures::runtimeDescriptorArray,
                                   supported.runtimeDescriptorArray);
            EnableSupportedFeature(pCreateInfo, modifiedFeatures, descriptorIndexing,
                                   &VkPhysicalDeviceDescriptorIndexingFeatures::descriptorBindingPartiallyBound,
                                   supported.descriptorBindingPartiallyBound);
            EnableSupportedFeature(
                pCreateInfo, modifiedFeatures, descriptorIndexing,
                &VkPhysicalDeviceDescriptorIndexingFeatures::shaderSampledImageArrayNonUniformIndexing,
                supported.shaderSampledImageArrayNonUniformIndexing);
            EnableSupportedFeature(
                pCreateInfo, modifiedFeatures, descriptorIndexing,
                &VkPhysicalDeviceDescriptorIndexingFeatures::shaderStorageImageArrayNonUniformIndexing,
                supported.shaderStorageImageArrayNonUniformIndexing);
            EnableSupportedFeature(
                pCreateInfo, modifiedFeatures, descriptorIndexing,
                &VkPhysicalDeviceDescriptorIndexingFeatures::shaderStorageBufferArrayNonUniformIndexing,
                supported.shaderStorageBufferArrayNonUniformIndexing);
            if (!HasExtension(pCreateInfo, VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))
            {
                EnableSupportedFeature(pCreateInfo, modifiedFeatures, bufferDeviceAddress,
                                       &VkPhysicalDeviceBufferDeviceAddressFeatures::bufferDeviceAddress,
                                       supported.bufferDeviceAddress);
            }
            EnableSupportedFeature(pCreateInfo, modifiedFeatures, vulkanMemoryModel,
                                   &VkPhysicalDeviceVulkanMemoryModelFeatures::vulkanMemoryModel,
                                   supported.vulkanMemoryModel);
            return;
        }

        if (HasExtension(pCreateInfo, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME))
            EnableFeatureStruct(pCreateInfo, getFeatures2, physicalDevice, modifiedFeatures, shaderFloat16Int8,
                                &VkPhysicalDeviceShaderFloat16Int8Features::shaderFloat16,
                                &VkPhysicalDeviceShaderFloat16Int8Features::shaderInt8);

        if (HasExtension(pCreateInfo, VK_KHR_8BIT_STORAGE_EXTENSION_NAME))
            EnableFeatureStruct(pCreateInfo, getFeatures2, physicalDevice, modifiedFeatures, storage8Bit,
                                &VkPhysicalDevice8BitStorageFeatures::storageBuffer8BitAccess);

        if (HasExtension(pCreateInfo, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME))
            EnableFeatureStruct(
                pCreateInfo, getFeatures2, physicalDevice, modifiedFeatures, descriptorIndexing,
                &VkPhysicalDeviceDescriptorIndexingFeatures::runtimeDescriptorArray,
                &VkPhysicalDeviceDescriptorIndexingFeatures::descriptorBindingPartiallyBound,
                &VkPhysicalDeviceDescriptorIndexingFeatures::shaderSampledImageArrayNonUniformIndexing,
                &VkPhysicalDeviceDescriptorIndexingFeatures::shaderStorageImageArrayNonUniformIndexing,
                &VkPhysicalDeviceDescriptorIndexingFeatures::shaderStorageBufferArrayNonUniformIndexing);

        if (HasExtension(pCreateInfo, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) &&
            !HasExtension(pCreateInfo, VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))
            EnableFeatureStruct(pCreateInfo, getFeatures2, physicalDevice, modifiedFeatures, bufferDeviceAddress,
                                &VkPhysicalDeviceBufferDeviceAddressFeatures::bufferDeviceAddress);

        if (HasExtension(pCreateInfo, VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME))
            EnableFeatureStruct(pCreateInfo, getFeatures2, physicalDevice, modifiedFeatures, vulkanMemoryModel,
                                &VkPhysicalDeviceVulkanMemoryModelFeatures::vulkanMemoryModel);
    }

    void EnableVulkan13Features(VkPhysicalDevice physicalDevice)
    {
        if (apiVersion >= VK_API_VERSION_1_3)
        {
            const auto supported = GetSupportedFeatureStruct(getFeatures2, physicalDevice, vulkan13Features);

            if (const auto* existing = FindFeatureStruct<VkPhysicalDeviceVulkan13Features>(
                    pCreateInfo->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES))
            {
                auto* features = const_cast<VkPhysicalDeviceVulkan13Features*>(existing);
                EnableExistingFeature(modifiedFeatures, features, vulkan13Features,
                                      &VkPhysicalDeviceVulkan13Features::synchronization2, supported.synchronization2);
                EnableExistingFeature(modifiedFeatures, features, vulkan13Features,
                                      &VkPhysicalDeviceVulkan13Features::subgroupSizeControl,
                                      supported.subgroupSizeControl);
                EnableExistingFeature(modifiedFeatures, features, vulkan13Features,
                                      &VkPhysicalDeviceVulkan13Features::computeFullSubgroups,
                                      supported.computeFullSubgroups);

                return;
            }

            EnableSupportedFeature(pCreateInfo, modifiedFeatures, synchronization2,
                                   &VkPhysicalDeviceSynchronization2Features::synchronization2,
                                   supported.synchronization2);
            EnableSupportedFeature(pCreateInfo, modifiedFeatures, subgroupSizeControl,
                                   &VkPhysicalDeviceSubgroupSizeControlFeatures::subgroupSizeControl,
                                   supported.subgroupSizeControl);
            EnableSupportedFeature(pCreateInfo, modifiedFeatures, subgroupSizeControl,
                                   &VkPhysicalDeviceSubgroupSizeControlFeatures::computeFullSubgroups,
                                   supported.computeFullSubgroups);
            return;
        }

        if (HasExtension(pCreateInfo, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME))
            EnableFeatureStruct(pCreateInfo, getFeatures2, physicalDevice, modifiedFeatures, synchronization2,
                                &VkPhysicalDeviceSynchronization2Features::synchronization2);

        if (HasExtension(pCreateInfo, VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME))
            EnableFeatureStruct(pCreateInfo, getFeatures2, physicalDevice, modifiedFeatures, subgroupSizeControl,
                                &VkPhysicalDeviceSubgroupSizeControlFeatures::subgroupSizeControl,
                                &VkPhysicalDeviceSubgroupSizeControlFeatures::computeFullSubgroups);
    }

    void EnableVulkan14Features(VkPhysicalDevice physicalDevice)
    {
        if (!HasExtension(pCreateInfo, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME))
            return;

        const auto* existing = FindFeatureStruct<VkPhysicalDeviceVulkan14Features>(
            pCreateInfo->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES);
        if (existing == nullptr)
            return;

        auto* features = const_cast<VkPhysicalDeviceVulkan14Features*>(existing);
        const auto supported = GetSupportedFeatureStruct(getFeatures2, physicalDevice, vulkan14Features);
        EnableExistingFeature(modifiedFeatures, features, vulkan14Features,
                              &VkPhysicalDeviceVulkan14Features::pushDescriptor, supported.pushDescriptor);
    }

    void EnableExtensionFeatures(VkPhysicalDevice physicalDevice)
    {
        const auto* vulkan12Features = FindFeatureStruct<VkPhysicalDeviceVulkan12Features>(
            pCreateInfo->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
        const auto* bufferDeviceAddressFeatures = FindFeatureStruct<VkPhysicalDeviceBufferDeviceAddressFeatures>(
            pCreateInfo->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES);
        const bool isBufferDeviceAddressEnabled =
            vulkan12Features != nullptr
                ? vulkan12Features->bufferDeviceAddress == VK_TRUE
                : bufferDeviceAddressFeatures != nullptr && bufferDeviceAddressFeatures->bufferDeviceAddress == VK_TRUE;
        if (HasExtension(pCreateInfo, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME) && isBufferDeviceAddressEnabled &&
            !HasExtension(pCreateInfo, VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) &&
            !HasExtension(pCreateInfo, VK_AMD_SHADER_FRAGMENT_MASK_EXTENSION_NAME))
            EnableFeatureStruct(pCreateInfo, getFeatures2, physicalDevice, modifiedFeatures, descriptorBuffer,
                                &VkPhysicalDeviceDescriptorBufferFeaturesEXT::descriptorBuffer);

        if (HasExtension(pCreateInfo, VK_EXT_SHADER_FLOAT8_EXTENSION_NAME))
            EnableFeatureStruct(pCreateInfo, getFeatures2, physicalDevice, modifiedFeatures, shaderFloat8,
                                &VkPhysicalDeviceShaderFloat8FeaturesEXT::shaderFloat8,
                                &VkPhysicalDeviceShaderFloat8FeaturesEXT::shaderFloat8CooperativeMatrix);

        if (HasExtension(pCreateInfo, VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME))
            EnableFeatureStruct(pCreateInfo, getFeatures2, physicalDevice, modifiedFeatures, cooperativeMatrix,
                                &VkPhysicalDeviceCooperativeMatrixFeaturesKHR::cooperativeMatrix);

        if (HasExtension(pCreateInfo, VK_NV_COOPERATIVE_MATRIX_2_EXTENSION_NAME))
            EnableFeatureStruct(pCreateInfo, getFeatures2, physicalDevice, modifiedFeatures, cooperativeMatrix2,
                                &VkPhysicalDeviceCooperativeMatrix2FeaturesNV::cooperativeMatrixConversions);

        if (HasExtension(pCreateInfo, VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME))
            EnableFeatureStruct(pCreateInfo, getFeatures2, physicalDevice, modifiedFeatures, computeShaderDerivatives,
                                &VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR::computeDerivativeGroupQuads,
                                &VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR::computeDerivativeGroupLinear);
    }

    void EnableFeatures(VkPhysicalDevice physicalDevice)
    {
        if (getFeatures2 == nullptr)
            return;

        CopyOwnedFeatureChain();
        EnableCoreFeatures(physicalDevice);
        EnableVulkan12Features(physicalDevice);
        EnableVulkan13Features(physicalDevice);
        EnableVulkan14Features(physicalDevice);
        EnableExtensionFeatures(physicalDevice);
    }
};

VulkanDeviceFeatureState::VulkanDeviceFeatureState(VkDeviceCreateInfo* pCreateInfo,
                                                   PFN_vkGetPhysicalDeviceFeatures2 getFeatures2)
    : impl(std::make_unique<Impl>(pCreateInfo, getFeatures2))
{
}

VulkanDeviceFeatureState::~VulkanDeviceFeatureState() = default;

VkResult VulkanSpoofing::hkvkCreateDevice(VkPhysicalDevice physicalDevice, VkDeviceCreateInfo* pCreateInfo,
                                          const VkAllocationCallbacks* pAllocator, VkDevice* pDevice,
                                          VulkanDeviceFeatureState* featureState, uint32_t requestedApiVersion)
{
    if (State::Instance().creatingD3DDevice)
    {
        LOG_INFO("Skipping because DXVK/VKD3D is creating a D3D device");
        return VK_SUCCESS;
    }

    LOG_FUNC();

    std::vector<VkExtensionProperties> supportedDeviceExtensions;
    auto enumerateDeviceExtensions = o_vkEnumerateDeviceExtensionProperties;
    if (enumerateDeviceExtensions == nullptr)
    {
        if (vulkanModule == nullptr)
            vulkanModule = KernelBaseProxy::GetModuleHandleA_()("vulkan-1.dll");

        if (vulkanModule != nullptr)
        {
            enumerateDeviceExtensions = (PFN_vkEnumerateDeviceExtensionProperties) KernelBaseProxy::GetProcAddress_()(
                vulkanModule, "vkEnumerateDeviceExtensionProperties");
        }

        if (enumerateDeviceExtensions == nullptr)
            enumerateDeviceExtensions = vkEnumerateDeviceExtensionProperties;
    }

    bool supportedDeviceExtensionsValid = false;
    if (enumerateDeviceExtensions != nullptr)
    {
        while (true)
        {
            uint32_t extensionCount = 0;
            auto result = enumerateDeviceExtensions(physicalDevice, nullptr, &extensionCount, nullptr);
            if (result != VK_SUCCESS)
                break;

            supportedDeviceExtensions.resize(extensionCount);
            if (extensionCount == 0)
            {
                supportedDeviceExtensionsValid = true;
                break;
            }

            result =
                enumerateDeviceExtensions(physicalDevice, nullptr, &extensionCount, supportedDeviceExtensions.data());
            if (result == VK_SUCCESS)
            {
                supportedDeviceExtensions.resize(extensionCount);
                supportedDeviceExtensionsValid = true;
                break;
            }

            if (result != VK_INCOMPLETE)
                break;
        }
    }

    if (!supportedDeviceExtensionsValid)
    {
        supportedDeviceExtensions.clear();
        LOG_WARN("Unable to obtain a complete Vulkan device extension list, preserving requested extensions and "
                 "skipping optional extension injection");
    }

    VkPhysicalDeviceProperties properties {};
    const auto getPhysicalDeviceProperties =
        o_vkGetPhysicalDeviceProperties != nullptr ? o_vkGetPhysicalDeviceProperties : vkGetPhysicalDeviceProperties;
    getPhysicalDeviceProperties(physicalDevice, &properties);

    const bool requestedApiVersionKnown = requestedApiVersion != 0;
    const uint32_t effectiveApiVersion =
        requestedApiVersionKnown
            ? (requestedApiVersion < properties.apiVersion ? requestedApiVersion : properties.apiVersion)
            : VK_API_VERSION_1_0;

    if (!requestedApiVersionKnown)
        LOG_WARN("Using conservative Vulkan 1.0 capability path because the instance API version is unknown");

    static std::vector<const char*> newExtensionList;
    newExtensionList.clear();

    LOG_DEBUG("Checking extensions and removing Streamline ones");
    for (size_t i = 0; i < pCreateInfo->enabledExtensionCount; i++)
    {
        auto extName = pCreateInfo->ppEnabledExtensionNames[i];

        if (Config::Instance()->VulkanExtensionSpoofing.value_or_default())
        {
            auto binaryImport = std::strcmp(extName, VK_NVX_BINARY_IMPORT_EXTENSION_NAME) == 0;
            auto imgViewHandle = std::strcmp(extName, VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME) == 0;
            auto bufferDeviceAddr = std::strcmp(extName, VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) == 0;
            auto mvPerViewAttr = std::strcmp(extName, VK_NVX_MULTIVIEW_PER_VIEW_ATTRIBUTES_EXTENSION_NAME) == 0;
            auto nvLowLatency = std::strcmp(extName, VK_NV_LOW_LATENCY_EXTENSION_NAME) == 0;
            auto nvOpticalFlow = std::strcmp(extName, VK_NV_OPTICAL_FLOW_EXTENSION_NAME) == 0;
            auto nvPresentMeter = std::strcmp(extName, VK_NV_PRESENT_METERING_EXTENSION_NAME) == 0;
            const bool spoofedExtension = binaryImport || imgViewHandle || bufferDeviceAddr || mvPerViewAttr ||
                                          nvLowLatency || nvOpticalFlow || nvPresentMeter;

            if (spoofedExtension && supportedDeviceExtensionsValid &&
                !SupportsExtension(supportedDeviceExtensions, extName))
            {
                LOG_DEBUG("Removing unsupported spoofed extension {}", extName);
                continue;
            }
        }

        // LOG_DEBUG("Adding {}", extName);
        newExtensionList.push_back(extName);
    }

    const VkPhysicalDeviceVulkan12Features* vulkan12Features = nullptr;
    const VkPhysicalDeviceVulkan14Features* vulkan14Features = nullptr;
    bool isKhrOrCoreBufferDeviceAddressEnabled = false;
    for (auto* next = static_cast<const VkBaseInStructure*>(pCreateInfo->pNext); next != nullptr; next = next->pNext)
    {
        if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES)
        {
            vulkan12Features = reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(next);
            isKhrOrCoreBufferDeviceAddressEnabled |= vulkan12Features->bufferDeviceAddress == VK_TRUE;
        }
        else if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES)
        {
            isKhrOrCoreBufferDeviceAddressEnabled |=
                reinterpret_cast<const VkPhysicalDeviceBufferDeviceAddressFeatures*>(next)->bufferDeviceAddress ==
                VK_TRUE;
        }
        else if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES)
        {
            vulkan14Features = reinterpret_cast<const VkPhysicalDeviceVulkan14Features*>(next);
        }
    }

    const auto addExtension = [&](const char* extension)
    {
        if (std::strcmp(extension, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) == 0 &&
            HasExtension(newExtensionList, VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))
            return false;
        if (std::strcmp(extension, VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) == 0 &&
            (isKhrOrCoreBufferDeviceAddressEnabled || effectiveApiVersion >= VK_API_VERSION_1_2 ||
             HasExtension(newExtensionList, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)))
            return false;
        if (std::strcmp(extension, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME) == 0 &&
            HasExtension(newExtensionList, VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))
            return false;
        if (std::strcmp(extension, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) == 0 && vulkan12Features != nullptr &&
            vulkan12Features->descriptorIndexing != VK_TRUE)
            return false;
        if (std::strcmp(extension, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) == 0 && vulkan14Features != nullptr &&
            vulkan14Features->pushDescriptor != VK_TRUE &&
            (featureState == nullptr ||
             !IsFeatureSupported(featureState->impl->getFeatures2, physicalDevice, featureState->impl->vulkan14Features,
                                 &VkPhysicalDeviceVulkan14Features::pushDescriptor)))
        {
            LOG_DEBUG("  {} cannot be enabled because VkPhysicalDeviceVulkan14Features::pushDescriptor is unsupported",
                      extension);
            return false;
        }

        if (HasExtension(newExtensionList, extension))
        {
            LOG_DEBUG("  {} already enabled", extension);
            return true;
        }

        if (!supportedDeviceExtensionsValid)
        {
            LOG_DEBUG("  {} support unknown, skipping optional injection", extension);
            return false;
        }

        if (!SupportsExtension(supportedDeviceExtensions, extension))
        {
            LOG_DEBUG("  {} not supported, skipping", extension);
            return false;
        }

        LOG_DEBUG("  Adding {}", extension);
        newExtensionList.push_back(extension);
        return true;
    };

    if (featureState != nullptr && effectiveApiVersion < VK_API_VERSION_1_2 &&
        HasExtension(newExtensionList, VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) &&
        SupportsExtension(supportedDeviceExtensions, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) &&
        featureState->impl->CanUpgradeExtBufferDeviceAddress(physicalDevice))
    {
        std::erase_if(newExtensionList, [](const char* extension)
                      { return std::strcmp(extension, VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) == 0; });

        if (!HasExtension(newExtensionList, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))
            newExtensionList.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

        LOG_INFO("Replaced unused VK_EXT_buffer_device_address with VK_KHR_buffer_device_address");
    }

    LOG_INFO("Adding NVNGX Vulkan extensions");
    if (effectiveApiVersion >= VK_API_VERSION_1_1 || addExtension(VK_KHR_MULTIVIEW_EXTENSION_NAME))
        addExtension(VK_NVX_MULTIVIEW_PER_VIEW_ATTRIBUTES_EXTENSION_NAME);
    else
        LOG_DEBUG("  {} dependency unavailable, skipping {}", VK_KHR_MULTIVIEW_EXTENSION_NAME,
                  VK_NVX_MULTIVIEW_PER_VIEW_ATTRIBUTES_EXTENSION_NAME);
    addExtension(VK_NV_LOW_LATENCY_EXTENSION_NAME);
    addExtension(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);

    if (!HasExtension(newExtensionList, VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) &&
        effectiveApiVersion < VK_API_VERSION_1_2 && !addExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))
        addExtension(VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

    addExtension(VK_NVX_BINARY_IMPORT_EXTENSION_NAME);
    addExtension(VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME);

    LOG_INFO("Adding FFX Vulkan extensions");
    static constexpr const char* ffxExtensions[] = {
        VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,        VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
        VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
    };

    for (const auto* extension : ffxExtensions)
        addExtension(extension);

    if (featureState != nullptr)
    {
        static constexpr const char* ffxFeatureExtensions[] = {
            VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,          VK_EXT_SHADER_FLOAT8_EXTENSION_NAME,
            VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME,         VK_NV_COOPERATIVE_MATRIX_2_EXTENSION_NAME,
            VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME,
        };

        for (const auto* extension : ffxFeatureExtensions)
            addExtension(extension);

        if (effectiveApiVersion < VK_API_VERSION_1_2)
        {
            static constexpr const char* vulkan12FallbackExtensions[] = {
                VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME, VK_KHR_8BIT_STORAGE_EXTENSION_NAME,
                VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
                VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME, VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
            };

            for (const auto* extension : vulkan12FallbackExtensions)
                addExtension(extension);
        }

        if (effectiveApiVersion < VK_API_VERSION_1_3)
        {
            addExtension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
            addExtension(VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME);
        }
    }

    LOG_INFO("Adding XeSS Vulkan extensions");
    addExtension(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);

    addExtension(VK_KHR_SHADER_INTEGER_DOT_PRODUCT_EXTENSION_NAME);

    addExtension(VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME);

    LOG_INFO("Adding Vk w/Dx12 Vulkan extensions");

    addExtension(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);

    addExtension(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);

#ifdef USE_QUEUE_SUBMIT_2_KHR
    LOG_INFO("Adding QueueSubmit2 Vulkan extensions");
    addExtension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
#endif

    if (State::Instance().vkAntiLagSupported)
    {
        LOG_INFO("Adding AntiLag extension");
        addExtension(VK_AMD_ANTI_LAG_EXTENSION_NAME);
    }

    pCreateInfo->enabledExtensionCount = static_cast<uint32_t>(newExtensionList.size());
    pCreateInfo->ppEnabledExtensionNames = newExtensionList.data();

    if (featureState != nullptr)
    {
        featureState->impl->apiVersion = effectiveApiVersion;
        featureState->impl->EnableFeatures(physicalDevice);
    }

    LOG_DEBUG("Final extension count: {0}", pCreateInfo->enabledExtensionCount);

    // We already listing extensions above, no need for this
    LOG_DEBUG("Extensions:");

    for (size_t i = 0; i < pCreateInfo->enabledExtensionCount; i++)
        LOG_DEBUG("  {0}", pCreateInfo->ppEnabledExtensionNames[i]);

    return VK_SUCCESS;
}

inline static VkResult hkvkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName,
                                                              uint32_t* pPropertyCount,
                                                              VkExtensionProperties* pProperties)
{
    LOG_FUNC();

    uint32_t count = 0;

    if (pPropertyCount != nullptr)
        count = *pPropertyCount;

    if (pProperties == nullptr)
        count = 0;

    auto result = o_vkEnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties);

    if (result != VK_SUCCESS)
    {
        LOG_ERROR("o_vkEnumerateDeviceExtensionProperties({}) result: {:X}", count, (UINT) result);
        return result;
    }

    if (Config::Instance()->VulkanExtensionSpoofing.value_or_default() && !SkipVulkanSpoofing())
    {
        // Count query, modify and add 5 to final count
        if (pProperties == nullptr && pPropertyCount != nullptr && count == 0)
        {
            if (State::Instance().activeFgInput == FGInput::DLSSG ||
                State::Instance().activeFgInput == FGInput::NvngxFG)
                *pPropertyCount += 7;
            else
                *pPropertyCount += 5;

            vkEnumerateDeviceExtensionPropertiesCount = *pPropertyCount;
            LOG_TRACE("vkEnumerateDeviceExtensionProperties count: {}", *pPropertyCount);
            return result;
        }

        // If this is request of our modified count query (count == vkEnumerateDeviceExtensionPropertiesCount)
        if (pProperties != nullptr && pPropertyCount != nullptr && *pPropertyCount > 0 &&
            count == vkEnumerateDeviceExtensionPropertiesCount)
        {
            // Set back modified extension count
            *pPropertyCount = count;

            // And fill extension info at the end
            VkExtensionProperties bi { VK_NVX_BINARY_IMPORT_EXTENSION_NAME, VK_NVX_BINARY_IMPORT_SPEC_VERSION };
            memcpy(&pProperties[*pPropertyCount - 1], &bi, sizeof(VkExtensionProperties));

            VkExtensionProperties ivh { VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME,
                                        VK_NVX_IMAGE_VIEW_HANDLE_SPEC_VERSION };
            memcpy(&pProperties[*pPropertyCount - 2], &ivh, sizeof(VkExtensionProperties));

            VkExtensionProperties mpva { VK_NVX_MULTIVIEW_PER_VIEW_ATTRIBUTES_EXTENSION_NAME,
                                         VK_NVX_MULTIVIEW_PER_VIEW_ATTRIBUTES_SPEC_VERSION };
            memcpy(&pProperties[*pPropertyCount - 3], &mpva, sizeof(VkExtensionProperties));

            VkExtensionProperties ll { VK_NV_LOW_LATENCY_EXTENSION_NAME, VK_NV_LOW_LATENCY_SPEC_VERSION };
            memcpy(&pProperties[*pPropertyCount - 4], &ll, sizeof(VkExtensionProperties));

            VkExtensionProperties bda { VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
                                        VK_EXT_BUFFER_DEVICE_ADDRESS_SPEC_VERSION };
            memcpy(&pProperties[*pPropertyCount - 5], &bda, sizeof(VkExtensionProperties));

            if (State::Instance().activeFgInput == FGInput::DLSSG ||
                State::Instance().activeFgInput == FGInput::NvngxFG)
            {
                VkExtensionProperties of { VK_NV_OPTICAL_FLOW_EXTENSION_NAME, VK_NV_OPTICAL_FLOW_SPEC_VERSION };
                memcpy(&pProperties[*pPropertyCount - 6], &of, sizeof(VkExtensionProperties));

                VkExtensionProperties pm { VK_NV_PRESENT_METERING_EXTENSION_NAME, VK_NV_PRESENT_METERING_SPEC_VERSION };
                memcpy(&pProperties[*pPropertyCount - 7], &pm, sizeof(VkExtensionProperties));
            }
        }
        else
        {
            LOG_DEBUG("Not adding any extensions!");
        }
    }

    if (!vkEnumerateDeviceExtensionPropertiesListed && count != 0)
    {
        vkEnumerateDeviceExtensionPropertiesListed = true;

        LOG_DEBUG("Device extensions returned:");
        for (uint32_t i = 0; i < *pPropertyCount; ++i)
            LOG_DEBUG("  {}", pProperties[i].extensionName);
    }

    LOG_FUNC_RESULT(result);

    return result;
}

inline static VkResult hkvkEnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount,
                                                                VkExtensionProperties* pProperties)
{
    LOG_FUNC();

    auto count = *pPropertyCount;

    if (pProperties == nullptr)
        count = 0;

    auto result = o_vkEnumerateInstanceExtensionProperties(pLayerName, pPropertyCount, pProperties);

    if (result != VK_SUCCESS)
    {
        LOG_ERROR("o_vkEnumerateInstanceExtensionProperties({}) result: {:X}", count, (UINT) result);
        return result;
    }

    if (Config::Instance()->VulkanExtensionSpoofing.value_or_default() && !SkipVulkanSpoofing())
    {
        if (pLayerName == nullptr && pProperties == nullptr && count == 0)
        {
            LOG_TRACE("hkvkEnumerateDeviceExtensionProperties count: {}", vkEnumerateDeviceExtensionPropertiesCount);
            return result;
        }
    }

    if (pPropertyCount != nullptr && pProperties != nullptr && count != 0)
    {
        if (!vkEnumerateInstanceExtensionPropertiesListed)
        {
            vkEnumerateInstanceExtensionPropertiesListed = true;

            LOG_DEBUG("Extensions returned:");
            for (size_t i = 0; i < *pPropertyCount; i++)
            {
                LOG_DEBUG("  {}", pProperties[i].extensionName);
                vkInstanceExtensions.insert_or_assign(std::string(pProperties[i].extensionName), true);
            }
        }
    }

    LOG_FUNC_RESULT(result);

    return result;
}

PFN_vkVoidFunction VulkanSpoofing::hkvkGetInstanceProcAddr(const PFN_vkVoidFunction orgFunc, const char* pName)
{
    auto procName = std::string(pName);

    auto result = Vulkan_wDx12::GetInstanceProcAddr(orgFunc, pName);
    if (result != VK_NULL_HANDLE)
        return result;

    if (Config::Instance()->VulkanSpoofing.value_or_default())
    {
        if (procName == std::string("vkGetPhysicalDeviceProperties"))
        {
            if (o_vkGetPhysicalDeviceProperties == nullptr)
                o_vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties) orgFunc;

            LOG_DEBUG("vkGetPhysicalDeviceProperties");
            return (PFN_vkVoidFunction) hkvkGetPhysicalDeviceProperties;
        }
        else if (procName == std::string("vkGetPhysicalDeviceProperties2"))
        {
            if (o_vkGetPhysicalDeviceProperties2 == nullptr)
                o_vkGetPhysicalDeviceProperties2 = (PFN_vkGetPhysicalDeviceProperties2) orgFunc;

            LOG_DEBUG("vkGetPhysicalDeviceProperties2");
            return (PFN_vkVoidFunction) hkvkGetPhysicalDeviceProperties2;
        }
        else if (procName == std::string("vkGetPhysicalDeviceProperties2KHR"))
        {
            if (o_vkGetPhysicalDeviceProperties2KHR == nullptr)
                o_vkGetPhysicalDeviceProperties2KHR = (PFN_vkGetPhysicalDeviceProperties2KHR) orgFunc;

            LOG_DEBUG("vkGetPhysicalDeviceProperties2KHR");
            return (PFN_vkVoidFunction) hkvkGetPhysicalDeviceProperties2KHR;
        }
    }

    if (Config::Instance()->VulkanExtensionSpoofing.value_or_default())
    {
        if (procName == std::string("vkEnumerateInstanceExtensionProperties"))
        {
            if (o_vkEnumerateInstanceExtensionProperties == nullptr)
                o_vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties) orgFunc;

            LOG_DEBUG("vkEnumerateInstanceExtensionProperties");
            return (PFN_vkVoidFunction) hkvkEnumerateInstanceExtensionProperties;
        }
        else if (procName == std::string("vkEnumerateDeviceExtensionProperties"))
        {
            if (o_vkEnumerateDeviceExtensionProperties == nullptr)
                o_vkEnumerateDeviceExtensionProperties = (PFN_vkEnumerateDeviceExtensionProperties) orgFunc;

            LOG_DEBUG("vkEnumerateDeviceExtensionProperties");
            return (PFN_vkVoidFunction) hkvkEnumerateDeviceExtensionProperties;
        }
    }

    if (Config::Instance()->VulkanVRAM.has_value())
    {
        if (procName == std::string("vkGetPhysicalDeviceMemoryProperties"))
        {
            if (o_vkGetPhysicalDeviceMemoryProperties == nullptr)
                o_vkGetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties) orgFunc;

            LOG_DEBUG("vkGetPhysicalDeviceMemoryProperties");
            return (PFN_vkVoidFunction) hkvkGetPhysicalDeviceMemoryProperties;
        }
        else if (procName == std::string("vkGetPhysicalDeviceMemoryProperties2"))
        {
            if (o_vkGetPhysicalDeviceMemoryProperties2 == nullptr)
                o_vkGetPhysicalDeviceMemoryProperties2 = (PFN_vkGetPhysicalDeviceMemoryProperties2) orgFunc;

            LOG_DEBUG("vkGetPhysicalDeviceMemoryProperties2");
            return (PFN_vkVoidFunction) hkvkGetPhysicalDeviceMemoryProperties2;
        }
        else if (procName == std::string("vkGetPhysicalDeviceMemoryProperties2KHR"))
        {
            if (o_vkGetPhysicalDeviceMemoryProperties2KHR == nullptr)
                o_vkGetPhysicalDeviceMemoryProperties2KHR = (PFN_vkGetPhysicalDeviceMemoryProperties2KHR) orgFunc;

            LOG_DEBUG("vkGetPhysicalDeviceMemoryProperties2KHR");
            return (PFN_vkVoidFunction) hkvkGetPhysicalDeviceMemoryProperties2KHR;
        }
    }

    return orgFunc;
}

PFN_vkVoidFunction VulkanSpoofing::hkvkGetDeviceProcAddr(const PFN_vkVoidFunction orgFunc, const char* pName)
{
    auto procName = std::string(pName);

    auto result = Vulkan_wDx12::GetDeviceProcAddr(orgFunc, pName);
    if (result != VK_NULL_HANDLE)
        return result;

    if (Config::Instance()->VulkanSpoofing.value_or_default())
    {
        if (procName == std::string("vkGetPhysicalDeviceProperties"))
        {
            if (o_vkGetPhysicalDeviceProperties == nullptr)
                o_vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties) orgFunc;

            LOG_DEBUG("vkGetPhysicalDeviceProperties");
            return (PFN_vkVoidFunction) hkvkGetPhysicalDeviceProperties;
        }
        else if (procName == std::string("vkGetPhysicalDeviceProperties2"))
        {
            if (o_vkGetPhysicalDeviceProperties2 == nullptr)
                o_vkGetPhysicalDeviceProperties2 = (PFN_vkGetPhysicalDeviceProperties2) orgFunc;

            LOG_DEBUG("vkGetPhysicalDeviceProperties2");
            return (PFN_vkVoidFunction) hkvkGetPhysicalDeviceProperties2;
        }
        else if (procName == std::string("vkGetPhysicalDeviceProperties2KHR"))
        {
            if (o_vkGetPhysicalDeviceProperties2KHR == nullptr)
                o_vkGetPhysicalDeviceProperties2KHR = (PFN_vkGetPhysicalDeviceProperties2KHR) orgFunc;

            LOG_DEBUG("vkGetPhysicalDeviceProperties2KHR");
            return (PFN_vkVoidFunction) hkvkGetPhysicalDeviceProperties2KHR;
        }
    }

    if (Config::Instance()->VulkanExtensionSpoofing.value_or_default())
    {
        if (procName == std::string("vkEnumerateInstanceExtensionProperties"))
        {
            if (o_vkEnumerateInstanceExtensionProperties == nullptr)
                o_vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties) orgFunc;

            LOG_DEBUG("vkEnumerateInstanceExtensionProperties");
            return (PFN_vkVoidFunction) hkvkEnumerateInstanceExtensionProperties;
        }
        else if (procName == std::string("vkEnumerateDeviceExtensionProperties"))
        {
            if (o_vkEnumerateDeviceExtensionProperties == nullptr)
                o_vkEnumerateDeviceExtensionProperties = (PFN_vkEnumerateDeviceExtensionProperties) orgFunc;

            LOG_DEBUG("vkEnumerateDeviceExtensionProperties");
            return (PFN_vkVoidFunction) hkvkEnumerateDeviceExtensionProperties;
        }
    }

    if (Config::Instance()->VulkanVRAM.has_value())
    {
        if (procName == std::string("vkGetPhysicalDeviceMemoryProperties"))
        {
            if (o_vkGetPhysicalDeviceMemoryProperties == nullptr)
                o_vkGetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties) orgFunc;

            LOG_DEBUG("vkGetPhysicalDeviceMemoryProperties");
            return (PFN_vkVoidFunction) hkvkGetPhysicalDeviceMemoryProperties;
        }
        else if (procName == std::string("vkGetPhysicalDeviceMemoryProperties2"))
        {
            if (o_vkGetPhysicalDeviceMemoryProperties2 == nullptr)
                o_vkGetPhysicalDeviceMemoryProperties2 = (PFN_vkGetPhysicalDeviceMemoryProperties2) orgFunc;

            LOG_DEBUG("vkGetPhysicalDeviceMemoryProperties2");
            return (PFN_vkVoidFunction) hkvkGetPhysicalDeviceMemoryProperties2;
        }
        else if (procName == std::string("vkGetPhysicalDeviceMemoryProperties2KHR"))
        {
            if (o_vkGetPhysicalDeviceMemoryProperties2KHR == nullptr)
                o_vkGetPhysicalDeviceMemoryProperties2KHR = (PFN_vkGetPhysicalDeviceMemoryProperties2KHR) orgFunc;

            LOG_DEBUG("vkGetPhysicalDeviceMemoryProperties2KHR");
            return (PFN_vkVoidFunction) hkvkGetPhysicalDeviceMemoryProperties2KHR;
        }
    }

    return orgFunc;
}

void VulkanSpoofing::HookForVulkanSpoofing(HMODULE vulkanModule)
{
    Vulkan_wDx12::Hook(vulkanModule);

    if (Config::Instance()->VulkanSpoofing.value_or_default() && o_vkGetPhysicalDeviceProperties == nullptr)
    {
        FARPROC address = nullptr;

        address = KernelBaseProxy::GetProcAddress_()(vulkanModule, "vkGetPhysicalDeviceProperties");
        o_vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties) address;

        address = KernelBaseProxy::GetProcAddress_()(vulkanModule, "vkGetPhysicalDeviceProperties2");
        o_vkGetPhysicalDeviceProperties2 = (PFN_vkGetPhysicalDeviceProperties2) address;

        address = KernelBaseProxy::GetProcAddress_()(vulkanModule, "vkGetPhysicalDeviceProperties2KHR");
        o_vkGetPhysicalDeviceProperties2KHR = (PFN_vkGetPhysicalDeviceProperties2KHR) address;

        if (o_vkGetPhysicalDeviceProperties != nullptr)
        {
            LOG_INFO("Attaching Vulkan device spoofing hooks");

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (o_vkGetPhysicalDeviceProperties)
                DetourAttach(&(PVOID&) o_vkGetPhysicalDeviceProperties, hkvkGetPhysicalDeviceProperties);

            if (o_vkGetPhysicalDeviceProperties2)
                DetourAttach(&(PVOID&) o_vkGetPhysicalDeviceProperties2, hkvkGetPhysicalDeviceProperties2);

            if (o_vkGetPhysicalDeviceProperties2KHR)
                DetourAttach(&(PVOID&) o_vkGetPhysicalDeviceProperties2KHR, hkvkGetPhysicalDeviceProperties2KHR);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("Failed to attach Vulkan device spoofing hooks: {:X}", detourResult);
                o_vkGetPhysicalDeviceProperties = nullptr;
                o_vkGetPhysicalDeviceProperties2 = nullptr;
                o_vkGetPhysicalDeviceProperties2KHR = nullptr;
            }
        }
    }
}

void VulkanSpoofing::HookForVulkanExtensionSpoofing(HMODULE vulkanModule)
{
    if (o_vkEnumerateInstanceExtensionProperties == nullptr)
    {
        FARPROC address = nullptr;

        if (Config::Instance()->VulkanExtensionSpoofing.value_or_default())
        {
            address = KernelBaseProxy::GetProcAddress_()(vulkanModule, "vkEnumerateInstanceExtensionProperties");
            o_vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties) address;

            address = KernelBaseProxy::GetProcAddress_()(vulkanModule, "vkEnumerateDeviceExtensionProperties");
            o_vkEnumerateDeviceExtensionProperties = (PFN_vkEnumerateDeviceExtensionProperties) address;
        }

        if (o_vkEnumerateInstanceExtensionProperties != nullptr || o_vkEnumerateDeviceExtensionProperties != nullptr)
        {
            LOG_INFO("Attaching Vulkan extensions spoofing hooks");

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (o_vkEnumerateInstanceExtensionProperties)
                DetourAttach(&(PVOID&) o_vkEnumerateInstanceExtensionProperties,
                             hkvkEnumerateInstanceExtensionProperties);

            if (o_vkEnumerateDeviceExtensionProperties)
                DetourAttach(&(PVOID&) o_vkEnumerateDeviceExtensionProperties, hkvkEnumerateDeviceExtensionProperties);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("Failed to attach Vulkan extensions spoofing hooks: {:X}", detourResult);
                o_vkEnumerateInstanceExtensionProperties = nullptr;
                o_vkEnumerateDeviceExtensionProperties = nullptr;
            }
        }
    }
}

void VulkanSpoofing::HookForVulkanVRAMSpoofing(HMODULE vulkanModule)
{
    if (Config::Instance()->VulkanVRAM.has_value() && o_vkGetPhysicalDeviceMemoryProperties == nullptr)
    {
        FARPROC address = nullptr;

        address = KernelBaseProxy::GetProcAddress_()(vulkanModule, "vkGetPhysicalDeviceMemoryProperties");
        o_vkGetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties) address;

        address = KernelBaseProxy::GetProcAddress_()(vulkanModule, "vkGetPhysicalDeviceMemoryProperties2");
        o_vkGetPhysicalDeviceMemoryProperties2 = (PFN_vkGetPhysicalDeviceMemoryProperties2) address;

        address = KernelBaseProxy::GetProcAddress_()(vulkanModule, "vkGetPhysicalDeviceMemoryProperties2KHR");
        o_vkGetPhysicalDeviceMemoryProperties2KHR = (PFN_vkGetPhysicalDeviceMemoryProperties2KHR) address;

        if (o_vkGetPhysicalDeviceMemoryProperties != nullptr || o_vkGetPhysicalDeviceMemoryProperties2 != nullptr ||
            o_vkGetPhysicalDeviceMemoryProperties2KHR != nullptr)
        {
            LOG_INFO("Attaching Vulkan VRAM spoofing hooks");

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (o_vkGetPhysicalDeviceMemoryProperties != nullptr)
                DetourAttach(&(PVOID&) o_vkGetPhysicalDeviceMemoryProperties, hkvkGetPhysicalDeviceMemoryProperties);

            if (o_vkGetPhysicalDeviceMemoryProperties2 != nullptr)
                DetourAttach(&(PVOID&) o_vkGetPhysicalDeviceMemoryProperties2, hkvkGetPhysicalDeviceMemoryProperties2);

            if (o_vkGetPhysicalDeviceMemoryProperties2KHR != nullptr)
                DetourAttach(&(PVOID&) o_vkGetPhysicalDeviceMemoryProperties2KHR,
                             hkvkGetPhysicalDeviceMemoryProperties2KHR);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("Failed to attach Vulkan VRAM spoofing hooks: {:X}", detourResult);
                o_vkGetPhysicalDeviceMemoryProperties = nullptr;
                o_vkGetPhysicalDeviceMemoryProperties2 = nullptr;
                o_vkGetPhysicalDeviceMemoryProperties2KHR = nullptr;
            }
        }
    }
}
