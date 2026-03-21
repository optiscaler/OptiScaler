#include "pch.h"
#include "IdentifyGpu.h"
#include "fsr4/FSR4Upgrade.h"

#include <proxies/Dxgi_Proxy.h>
#include <proxies/D3d12_Proxy.h>
#include "nvapi/NvApiTypes.h"
#include <magic_enum.hpp>

using Microsoft::WRL::ComPtr;

// Prioritize Nvidia cards that can run DLSS and are connected to a display
void sortGpus(std::vector<GpuInformation>& gpus)
{
    std::sort(gpus.begin(), gpus.end(),
              [](const GpuInformation& a, const GpuInformation& b)
              {
                  auto isPreferredNvidia = [](const GpuInformation& gpu)
                  {
                      bool isNvidia = (gpu.vendorId == VendorId::Nvidia);
                      return isNvidia && gpu.dlssCapable && !gpu.noDisplayConnected;
                  };

                  bool aIsPreferred = isPreferredNvidia(a);
                  bool bIsPreferred = isPreferredNvidia(b);

                  // If one is a preferred and the other isn't then the preferred one should be sorted first
                  if (aIsPreferred != bIsPreferred)
                  {
                      return aIsPreferred;
                  }

                  if (a.softwareAdapter)
                      return false;

                  // Fallback on VRAM amount
                  return a.dedicatedVramInBytes > b.dedicatedVramInBytes;
              });
}
std::vector<GpuInformation> IdentifyGpu::checkGpuInfo()
{
    auto localCachedInfo = std::vector<GpuInformation> {};

    DxgiProxy::Init();

    ComPtr<IDXGIFactory6> factory = nullptr;
    HRESULT result = DxgiProxy::CreateDxgiFactory1_()(__uuidof(factory), (IDXGIFactory1**) factory.GetAddressOf());

    if (result != S_OK || factory == nullptr)
    {
        // Will land here if getPrimaryGpu/getAllGpus are called from within DLL_PROCESS_ATTACH
        LOG_ERROR("Failed to create DXGI Factory, GPU info will be inaccurate!");
        return localCachedInfo;
    }

    UINT adapterIndex = 0;
    DXGI_ADAPTER_DESC1 adapterDesc {};
    ComPtr<IDXGIAdapter1> adapter;

    DXGI_GPU_PREFERENCE gpuPreference = DXGI_GPU_PREFERENCE_UNSPECIFIED;
    if (Config::Instance()->PreferDedicatedGpu.value_or_default())
        gpuPreference = DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;

    while (factory->EnumAdapterByGpuPreference(adapterIndex, gpuPreference, IID_PPV_ARGS(&adapter)) == S_OK)
    {
        if (adapter == nullptr)
        {
            adapterIndex++;
            continue;
        }

        {
            ScopedSkipSpoofing skipSpoofing {};
            result = adapter->GetDesc1(&adapterDesc);
        }

        if (result == S_OK)
        {
            GpuInformation gpuInfo;
            gpuInfo.luid = adapterDesc.AdapterLuid;
            gpuInfo.vendorId = (VendorId::Value) adapterDesc.VendorId;
            gpuInfo.deviceId = adapterDesc.DeviceId;
            gpuInfo.dedicatedVramInBytes = adapterDesc.DedicatedVideoMemory;

            std::wstring szName(adapterDesc.Description);
            gpuInfo.name = wstring_to_string(szName);

            ComPtr<IDXGIDXVKAdapter> dxvkAdapter;
            if (SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&dxvkAdapter))))
                gpuInfo.usesDxvk = true;

            // Needed to be able to query amdxc and check for vkd3d-proton
            if (gpuInfo.vendorId == VendorId::AMD || gpuInfo.usesDxvk)
            {
                D3d12Proxy::Init();
                D3d12Proxy::D3D12CreateDevice_()(adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                                                 IID_PPV_ARGS(&gpuInfo.d3d12device));
            }

            if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                gpuInfo.softwareAdapter = true;

            localCachedInfo.push_back(std::move(gpuInfo));
        }
        else
        {
            LOG_DEBUG("Can't get description of adapter: {}", adapterIndex);
        }

        adapterIndex++;
    }

    // We might be getting the correct ordering by default.
    // Trying to sort by vendor might cause issues if someone
    // has some old Nvidia card in their system for example.
    // sortGpus(localCachedInfo);

    for (auto& gpuInfo : localCachedInfo)
    {
        ComPtr<ID3D12DXVKInteropDevice> vkd3dInterop;
        if (gpuInfo.usesDxvk && SUCCEEDED(gpuInfo.d3d12device->QueryInterface(IID_PPV_ARGS(&vkd3dInterop))))
            gpuInfo.usesVkd3dProton = true;

        if (gpuInfo.vendorId == VendorId::AMD && gpuInfo.d3d12device)
        {
            auto moduleAmdxc64 = KernelBaseProxy::GetModuleHandleW_()(L"amdxc64.dll");

            if (moduleAmdxc64 == nullptr)
                moduleAmdxc64 = NtdllProxy::LoadLibraryExW_Ldr(L"amdxc64.dll", NULL, 0);

            if (moduleAmdxc64 == nullptr)
                continue;

            ComPtr<IAmdExtD3DFactory> amdExtD3DFactory = nullptr;
            auto AmdExtD3DCreateInterface = (PFN_AmdExtD3DCreateInterface) KernelBaseProxy::GetProcAddress_()(
                moduleAmdxc64, "AmdExtD3DCreateInterface");

            // Kinda questionable, may need to reconsider
            if (Config::Instance()->Fsr4ForceCapable.value_or_default())
                gpuInfo.fsr4Capable = true;

            // Query amdxc for a specific intrinsics support, FSR 4 checks more but hopefully this one is enough
            if (!gpuInfo.fsr4Capable && gpuInfo.d3d12device &&
                SUCCEEDED(AmdExtD3DCreateInterface(gpuInfo.d3d12device, IID_PPV_ARGS(&amdExtD3DFactory))))
            {
                ComPtr<IAmdExtD3DShaderIntrinsics> amdExtD3DShaderIntrinsics = nullptr;

                if (SUCCEEDED(amdExtD3DFactory->CreateInterface(gpuInfo.d3d12device,
                                                                IID_PPV_ARGS(&amdExtD3DShaderIntrinsics))))
                {
                    HRESULT float8support =
                        amdExtD3DShaderIntrinsics->CheckSupport(AmdExtD3DShaderIntrinsicsSupport_Float8Conversion);
                    gpuInfo.fsr4Capable = float8support == S_OK;
                }
            }

            // Query vkd3d-proton for extensions it's using to look for the required one for FSR 4
            if (!gpuInfo.fsr4Capable && gpuInfo.usesVkd3dProton)
            {
                UINT extensionCount = 0;

                if (SUCCEEDED(vkd3dInterop->GetDeviceExtensions(&extensionCount, nullptr)) && extensionCount > 0)
                {
                    std::vector<const char*> exts(extensionCount);

                    if (SUCCEEDED(vkd3dInterop->GetDeviceExtensions(&extensionCount, exts.data())))
                    {
                        for (UINT i = 0; i < extensionCount; i++)
                        {
                            // Only RDNA4+
                            if (!strcmp("VK_EXT_shader_float8", exts[i]))
                            {
                                gpuInfo.fsr4Capable = true;
                                break;
                            }
                        }
                    }
                }
            }

            // Pre-RDNA4 GPUs on Linux can support FSR 4 but require a special envvar
            // check for the envvar and assume everything else is also setup for FSR 4 to work on those cards
            if (!gpuInfo.fsr4Capable)
            {
                const char* envvar = getenv("DXIL_SPIRV_CONFIG");
                if (envvar && strstr(envvar, "wmma_rdna3_workaround"))
                    gpuInfo.fsr4Capable = true;
            }

            // TODO: could now try to ask amdxcffx for FSR 4 and see if it returns it
            // but our FSR 4 upgrade code call this function so it gets complicated
        }
        else if (gpuInfo.vendorId == VendorId::Nvidia)
        {
            queryNvapi(gpuInfo);
        }

        if (gpuInfo.d3d12device)
        {
            gpuInfo.d3d12device->Release();
            gpuInfo.d3d12device = nullptr;
        }
    }

    return localCachedInfo;
}

// !!! Doesn't fill out FSR 4 capability and dxvk/vkd3d-proton usages !!!
// We are using Vulkan inside DLL_PROCESS_ATTACH which unlike dxgi technically works?
// Not ideal + requires a GPU that supports Vulkan but every GPU we care about should
std::vector<GpuInformation> IdentifyGpu::checkGpuInfoVulkan()
{
    auto localCachedInfo = std::vector<GpuInformation> {};

    VkApplicationInfo appInfo {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "AdapterQuery";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "None";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto winevulkan = LoadLibraryA("winevulkan.dll");

    auto o_vkCreateInstance = vkCreateInstance;
    auto o_vkGetInstanceProcAddr = vkGetInstanceProcAddr;

    if (State::Instance().isRunningOnLinux && winevulkan)
    {
        o_vkCreateInstance = (PFN_vkCreateInstance) KernelBaseProxy::GetProcAddress_()(winevulkan, "vkCreateInstance");
        o_vkGetInstanceProcAddr =
            (PFN_vkGetInstanceProcAddr) KernelBaseProxy::GetProcAddress_()(winevulkan, "vkGetInstanceProcAddr");
    }

    VkInstance instance = VK_NULL_HANDLE;
    if (o_vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS)
    {
        LOG_ERROR("Couldn't create a Vulkan instance");
        return localCachedInfo;
    }

    auto o_vkEnumeratePhysicalDevices = vkEnumeratePhysicalDevices;
    auto o_vkGetPhysicalDeviceProperties2 = vkGetPhysicalDeviceProperties2;
    auto o_vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
    auto o_vkDestroyInstance = vkDestroyInstance;

    if (State::Instance().isRunningOnLinux && winevulkan && instance)
    {
        o_vkEnumeratePhysicalDevices =
            (PFN_vkEnumeratePhysicalDevices) o_vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
        o_vkGetPhysicalDeviceProperties2 =
            (PFN_vkGetPhysicalDeviceProperties2) o_vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2");
        o_vkGetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties) o_vkGetInstanceProcAddr(
            instance, "vkGetPhysicalDeviceMemoryProperties");
        o_vkDestroyInstance = (PFN_vkDestroyInstance) o_vkGetInstanceProcAddr(instance, "vkDestroyInstance");
    }

    uint32_t deviceCount = 0;
    o_vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        LOG_ERROR("No Vulkan devices");
        vkDestroyInstance(instance, nullptr);
        return localCachedInfo;
    }

    // ScopedSkipSpoofing skipSpoofing {};

    std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
    o_vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());

    for (auto physicalDevice : physicalDevices)
    {
        VkPhysicalDeviceIDProperties idProps {};
        idProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

        VkPhysicalDeviceProperties2 props2 {};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &idProps;

        o_vkGetPhysicalDeviceProperties2(physicalDevice, &props2);

        VkPhysicalDeviceMemoryProperties memProps {};
        o_vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

        GpuInformation gpuInfo;
        for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
        {
            if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                gpuInfo.dedicatedVramInBytes += memProps.memoryHeaps[i].size;
        }

        if (idProps.deviceLUIDValid == VK_TRUE)
            memcpy(&gpuInfo.luid, idProps.deviceLUID, VK_LUID_SIZE);

        gpuInfo.vendorId = (VendorId::Value) props2.properties.vendorID;
        gpuInfo.deviceId = props2.properties.deviceID;
        gpuInfo.name = std::string(props2.properties.deviceName);
        gpuInfo.softwareAdapter = props2.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;

        localCachedInfo.push_back(std::move(gpuInfo));
    }

    o_vkDestroyInstance(instance, nullptr);
    sortGpus(localCachedInfo);

    for (auto& gpuInfo : localCachedInfo)
    {
        if (gpuInfo.vendorId == VendorId::Nvidia)
            queryNvapi(gpuInfo);
    }

    return localCachedInfo;
}

void IdentifyGpu::queryNvapi(GpuInformation& gpuInfo)
{
    bool loadedHere = false;
    auto nvapiModule = KernelBaseProxy::GetModuleHandleW_()(L"nvapi64.dll");

    if (!nvapiModule)
    {
        nvapiModule = NtdllProxy::LoadLibraryExW_Ldr(L"nvapi64.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        loadedHere = true;
    }

    // No nvapi, should not be nvidia, possibly external spoofing
    if (!nvapiModule)
        return;

    if (auto o_NvAPI_QueryInterface =
            (PFN_NvApi_QueryInterface) KernelBaseProxy::GetProcAddress_()(nvapiModule, "nvapi_QueryInterface"))
    {
        // Check for fakenvapi in system32, assume it's not nvidia if found
        if (o_NvAPI_QueryInterface(GET_ID(Fake_InformFGState)))
            return;

        // Handle we want to grab
        NvPhysicalGpuHandle hPhysicalGpu {};

        // Grab logical GPUs to extract coresponding LUID
        auto* getLogicalGPUs = GET_INTERFACE(NvAPI_SYS_GetLogicalGPUs, o_NvAPI_QueryInterface);
        NV_LOGICAL_GPUS logicalGpus {};
        logicalGpus.version = NV_LOGICAL_GPUS_VER;
        if (getLogicalGPUs)
        {
            if (auto result = getLogicalGPUs(&logicalGpus); result != NVAPI_OK)
                LOG_ERROR("NvAPI_SYS_GetLogicalGPUs failed: {}", magic_enum::enum_name(result));
        }

        auto* getLogicalGpuInfo = GET_INTERFACE(NvAPI_GPU_GetLogicalGpuInfo, o_NvAPI_QueryInterface);

        if (getLogicalGpuInfo)
        {
            for (uint32_t i = 0; i < logicalGpus.gpuHandleCount; i++)
            {
                LUID luid;
                NV_LOGICAL_GPU_DATA logicalGpuData {};
                logicalGpuData.pOSAdapterId = &luid;
                logicalGpuData.version = NV_LOGICAL_GPU_DATA_VER;
                auto logicalGpu = logicalGpus.gpuHandleData[i].hLogicalGpu;

                if (auto result = getLogicalGpuInfo(logicalGpu, &logicalGpuData); result != NVAPI_OK)
                    LOG_ERROR("NvAPI_GPU_GetLogicalGpuInfo failed: {}", magic_enum::enum_name(result));

                // We are looking at the correct GPU for this gpuInfo.luid
                if (IsEqualLUID(luid, gpuInfo.luid) && logicalGpuData.physicalGpuCount > 0)
                {
                    if (logicalGpuData.physicalGpuCount > 1)
                        LOG_WARN("A logical GPU has more than a single physical GPU, we are only checking one");

                    hPhysicalGpu = logicalGpuData.physicalGpuHandles[0];
                }
            }
        }

        auto* getArchInfo = GET_INTERFACE(NvAPI_GPU_GetArchInfo, o_NvAPI_QueryInterface);
        gpuInfo.nvidiaArchInfo.version = NV_GPU_ARCH_INFO_VER;
        if (getArchInfo && hPhysicalGpu && getArchInfo(hPhysicalGpu, &gpuInfo.nvidiaArchInfo) != NVAPI_OK)
            LOG_ERROR("Couldn't get GPU Architecture");

        auto* getConnectedDisplayIds = GET_INTERFACE(NvAPI_GPU_GetConnectedDisplayIds, o_NvAPI_QueryInterface);
        NvU32 displayCount = 0;
        if (getConnectedDisplayIds && hPhysicalGpu &&
            getConnectedDisplayIds(hPhysicalGpu, nullptr, &displayCount, 0) == NVAPI_OK && displayCount == 0)
        {
            gpuInfo.noDisplayConnected = true;
        }
    }

    if (loadedHere)
        NtdllProxy::FreeLibrary_Ldr(nvapiModule);

    // assumes GTX16xx to be capable due to our spoofing
    if (Config::Instance()->DLSSEnabled.value_or_default())
        gpuInfo.dlssCapable = gpuInfo.nvidiaArchInfo.architecture_id >= NV_GPU_ARCHITECTURE_TU100;
}

void IdentifyGpu::getHardwareAdapter(IDXGIFactory* InFactory, IDXGIAdapter** InAdapter,
                                     D3D_FEATURE_LEVEL requiredFeatureLevel)
{
    LOG_FUNC();

    *InAdapter = nullptr;

    auto allGpus = getAllGpus();
    IDXGIFactory6* factory6 = nullptr;

    if (InFactory->QueryInterface(IID_PPV_ARGS(&factory6)) == S_OK && factory6 != nullptr)
    {
        D3d12Proxy::Init();

        for (auto gpu : allGpus)
        {
            if (*InAdapter == nullptr)
            {
                LOG_TRACE("Trying to select: {}", gpu.name);

                ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
                auto result = factory6->EnumAdapterByLuid(gpu.luid, IID_PPV_ARGS(InAdapter));
            }

            if (*InAdapter != nullptr)
            {
                // Check if the requested D3D_FEATURE_LEVEL is supported without actually creating the device
                if (SUCCEEDED(D3d12Proxy::D3D12CreateDevice_()(*InAdapter, requiredFeatureLevel, _uuidof(ID3D12Device),
                                                               nullptr)))
                {
                    break;
                }

                (*InAdapter)->Release();
                *InAdapter = nullptr;
            }
        }

        factory6->Release();
    }
}

std::vector<GpuInformation> IdentifyGpu::getAllGpus()
{
    // Static inits are thread safe
    static std::vector<GpuInformation> cache = []() { return checkGpuInfo(); }();
    return cache;
}

GpuInformation IdentifyGpu::getPrimaryGpu()
{
    auto allGpus = getAllGpus();
    return allGpus.size() > 0 ? allGpus[0] : GpuInformation {};
}

// !!! Use the Vulkan variants only inside DLL_PROCESS_ATTACH as they provide incomplete data !!!
std::vector<GpuInformation> IdentifyGpu::getAllGpusVulkan()
{
    static std::vector<GpuInformation> cache = []() { return checkGpuInfoVulkan(); }();
    return cache;
}

// !!! Use the Vulkan variants only inside DLL_PROCESS_ATTACH as they provide incomplete data !!!
GpuInformation IdentifyGpu::getPrimaryGpuVulkan()
{
    // return GpuInformation {};
    auto allGpus = getAllGpusVulkan();
    return allGpus.size() > 0 ? allGpus[0] : GpuInformation {};
}