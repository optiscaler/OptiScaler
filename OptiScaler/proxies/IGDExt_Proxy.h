#pragma once

#include "SysUtils.h"

#include <d3d12.h>

class IGDExtProxy
{
  private:
    struct INTCExtensionContext;

    struct INTCDeviceInfo
    {
        uint32_t GPUMaxFreq;
        uint32_t GPUMinFreq;
        uint32_t GTGeneration;
        uint32_t EUCount;
        uint32_t PackageTDP;
        uint32_t MaxFillRate;
        wchar_t GTGenerationName[64];
    };

    struct INTCExtensionVersion
    {
        uint32_t HWFeatureLevel; ///< HW Feature Level, based on the Intel HW Platform
        uint32_t APIVersion;     ///< API Version
        uint32_t Revision;       ///< Revision number
    };

    struct INTCExtensionInfo
    {
        INTCExtensionVersion RequestedExtensionVersion; ///< [in] Intel Extension Framework interface version requested

        INTCDeviceInfo IntelDeviceInfo;      ///< [out] Intel Graphics Device detailed information
        const wchar_t* pDeviceDriverDesc;    ///< [out] Intel Graphics Driver description
        const wchar_t* pDeviceDriverVersion; ///< [out] Intel Graphics Driver version string
        uint32_t DeviceDriverBuildNumber;    ///< [out] Intel Graphics Driver build number
    };

    struct INTCExtensionAppInfo
    {
        const wchar_t* pApplicationName; ///< [in] Application name
        uint32_t ApplicationVersion;     ///< [in] Application version
        const wchar_t* pEngineName;      ///< [in] Engine name
        uint32_t EngineVersion;          ///< [in] Engine version
    };

    struct INTCAppInfoVersion
    {
        union
        {
            struct
            {
                uint32_t major;
                uint32_t minor;
                uint32_t patch;
                uint32_t reserved;
            };
            uint8_t raw[16];
        };
    };

    struct INTCExtensionAppInfo1
    {
        const wchar_t* pApplicationName;       ///< [in] Application name
        INTCAppInfoVersion ApplicationVersion; ///< [in] Application version
        const wchar_t* pEngineName;            ///< [in] Engine name
        INTCAppInfoVersion EngineVersion;      ///< [in] Engine version
    };

    enum INTC_D3D12_FEATURES
    {
        INTC_D3D12_FEATURE_D3D12_OPTIONS1,
        INTC_D3D12_FEATURE_D3D12_OPTIONS2
    };

    struct INTC_D3D12_FEATURE
    {
        BOOL EmulatedTyped64bitAtomics;
    };

    struct INTC_D3D12_RESOURCE_DESC
    {
        union
        {
            D3D12_RESOURCE_DESC* pD3D12Desc;
        };

        // Reserved Resources Texture2D Array with Mip Packing
        BOOL Texture2DArrayMipPack;
    };

    struct INTC_D3D12_RESOURCE_DESC_0001 : INTC_D3D12_RESOURCE_DESC
    {
        // Emulated Typed 64bit Atomics
        BOOL EmulatedTyped64bitAtomics;
    };

    struct INTC_D3D12_FEATURE_DATA_D3D12_OPTIONS1
    {
        BOOL XMXEnabled;
        BOOL DLBoostEnabled;
        BOOL EmulatedTyped64bitAtomics;
    };

    struct INTC_D3D12_FEATURE_DATA_D3D12_OPTIONS2
    {
        BOOL SIMD16Required;
        BOOL LSCSupported;
        BOOL LegacyTranslationRequired;
    };

    typedef HRESULT (*PFN_INTC_D3D12_GetSupportedVersions)(ID3D12Device* pDevice,
                                                           INTCExtensionVersion* pSupportedExtVersions,
                                                           uint32_t* pSupportedExtVersionsCount);

    typedef HRESULT (*PFN_INTC_D3D12_CreateDeviceExtensionContext1)(ID3D12Device* pDevice,
                                                                    INTCExtensionContext** ppExtensionContext,
                                                                    INTCExtensionInfo* pExtensionInfo,
                                                                    INTCExtensionAppInfo1* pExtensionAppInfo);

    typedef HRESULT (*PFN_INTC_DestroyDeviceExtensionContext)(INTCExtensionContext** ppExtensionContext);

    typedef HRESULT (*PFN_INTC_D3D12_CheckFeatureSupport)(INTCExtensionContext* pExtensionContext,
                                                          INTC_D3D12_FEATURES Feature, void* pFeatureSupportData,
                                                          UINT FeatureSupportDataSize);

    typedef HRESULT (*PFN_INTC_D3D12_SetFeatureSupport)(INTCExtensionContext* pExtensionContext,
                                                        INTC_D3D12_FEATURE* pFeature);

    typedef D3D12_RESOURCE_ALLOCATION_INFO (*PFN_INTC_D3D12_GetResourceAllocationInfo)(
        INTCExtensionContext* pExtensionContext, UINT visibleMask, UINT numResourceDescs,
        const INTC_D3D12_RESOURCE_DESC_0001* pResourceDescs);

    typedef HRESULT (*PFN_INTC_D3D12_CreateCommittedResource)(INTCExtensionContext* pExtensionContext,
                                                              const D3D12_HEAP_PROPERTIES* pHeapProperties,
                                                              D3D12_HEAP_FLAGS HeapFlags,
                                                              const INTC_D3D12_RESOURCE_DESC_0001* pDesc,
                                                              D3D12_RESOURCE_STATES InitialResourceState,
                                                              const D3D12_CLEAR_VALUE* pOptimizedClearValue,
                                                              REFIID riidResource, void** ppvResource);

    typedef HRESULT (*PFN_INTC_D3D12_CreatePlacedResource)(INTCExtensionContext* pExtensionContext, ID3D12Heap* pHeap,
                                                           UINT64 HeapOffset,
                                                           const INTC_D3D12_RESOURCE_DESC_0001* pDesc,
                                                           D3D12_RESOURCE_STATES InitialState,
                                                           const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid,
                                                           void** ppvResource);

    inline static PFN_INTC_D3D12_GetSupportedVersions _INTC_D3D12_GetSupportedVersions = nullptr;
    inline static PFN_INTC_D3D12_CreateDeviceExtensionContext1 _INTC_D3D12_CreateDeviceExtensionContext1 = nullptr;
    inline static PFN_INTC_DestroyDeviceExtensionContext _INTC_DestroyDeviceExtensionContext = nullptr;
    inline static PFN_INTC_D3D12_CheckFeatureSupport _INTC_D3D12_CheckFeatureSupport = nullptr;
    inline static PFN_INTC_D3D12_SetFeatureSupport _INTC_D3D12_SetFeatureSupport = nullptr;
    inline static PFN_INTC_D3D12_CreateCommittedResource _INTC_D3D12_CreateCommittedResource = nullptr;
    inline static PFN_INTC_D3D12_CreatePlacedResource _INTC_D3D12_CreatePlacedResource = nullptr;
    inline static PFN_INTC_D3D12_GetResourceAllocationInfo _INTC_D3D12_GetResourceAllocationInfo = nullptr;

    inline static HMODULE _module = nullptr;
    inline static INTCExtensionContext* _context = nullptr;
    inline static bool _atomicSupportEnabled = false;

    static bool CreateContext(ID3D12Device* device)
    {
        const INTCExtensionVersion atomicVersion = { 4, 8, 0 };

        if (_module == nullptr)
        {
            LOG_ERROR("igdext64.dll is not loaded!");
            return false;
        }

        if (_context != nullptr)
            DestroyContext();

        INTCExtensionVersion* extensionsVersions = nullptr;
        uint32_t extensionsVersionCount = 0;

        bool foundRequestedVersion = false;

        if (_INTC_D3D12_GetSupportedVersions(device, nullptr, &extensionsVersionCount) == S_OK)
        {
            extensionsVersions = new INTCExtensionVersion[extensionsVersionCount] {};
        }

        INTCExtensionInfo extInfo {};

        if (_INTC_D3D12_GetSupportedVersions(device, extensionsVersions, &extensionsVersionCount) == S_OK &&
            extensionsVersions != nullptr)
        {
            for (uint32_t i = 0; i < extensionsVersionCount; i++)
            {
                if ((extensionsVersions[i].HWFeatureLevel >= atomicVersion.HWFeatureLevel) &&
                    (extensionsVersions[i].APIVersion >= atomicVersion.APIVersion) &&
                    (extensionsVersions[i].Revision >= atomicVersion.Revision))
                {
                    LOG_DEBUG("Intel Extensions loaded requested version: {}.{}.{}",
                              extensionsVersions[i].HWFeatureLevel, extensionsVersions[i].APIVersion,
                              extensionsVersions[i].Revision);

                    foundRequestedVersion = true;
                    extInfo.RequestedExtensionVersion = extensionsVersions[i];
                    break;
                }
            }
        }

        if (!foundRequestedVersion)
            return false;

        INTCExtensionAppInfo1 appInfo {};

        appInfo.pApplicationName = string_to_wstring(State::Instance().gameName.empty() ? State::Instance().gameExe
                                                                                        : State::Instance().gameName)
                                       .c_str();

        appInfo.pApplicationName = L"Unreal Engine";
        appInfo.EngineVersion = { 5, 2, 0, 0 };

        const HRESULT result = _INTC_D3D12_CreateDeviceExtensionContext1(device, &_context, &extInfo, &appInfo);

        if (result == S_OK)
        {
            LOG_INFO("_INTC_D3D12_CreateDeviceExtensionContext1 success!");
        }
        else
        {
            LOG_ERROR("_INTC_D3D12_CreateDeviceExtensionContext1 error: {:X}", (UINT) result);

            if (_context != nullptr)
                DestroyContext();
        }

        if (extensionsVersions != nullptr)
            delete[] extensionsVersions;

        return true;
    }

    static bool EnableAtomic64Support()
    {
        if (_atomicSupportEnabled)
            return true;

        if (_context == nullptr)
            return false;

        if (_module == nullptr)
        {
            LOG_ERROR("igdext64.dll is not loaded!");
            return false;
        }

        INTC_D3D12_FEATURE_DATA_D3D12_OPTIONS1 featureSupportData;
        auto result = _INTC_D3D12_CheckFeatureSupport(_context, INTC_D3D12_FEATURE_D3D12_OPTIONS1, &featureSupportData,
                                                      sizeof(featureSupportData));

        LOG_INFO("_INTC_D3D12_CheckFeatureSupport result: {:X}", (UINT) result);

        if (result != S_OK)
            return false;

        if (featureSupportData.EmulatedTyped64bitAtomics)
        {
            INTC_D3D12_FEATURE feature;
            feature.EmulatedTyped64bitAtomics = true;

            result = _INTC_D3D12_SetFeatureSupport(_context, &feature);
            LOG_INFO("_INTC_D3D12_SetFeatureSupport result: {:X}", (UINT) result);

            _atomicSupportEnabled = (result == S_OK);

            if (!_atomicSupportEnabled)
                DestroyContext();
        }
        else
        {
            LOG_WARN("EmulatedTyped64bitAtomics is not needed!");
            _atomicSupportEnabled = false;
        }

        return _atomicSupportEnabled;
    }

  public:
    static void Init(HMODULE module)
    {
        if (module == nullptr)
            return;

        if (_module != nullptr)
            return;

        LOG_INFO("Init with module: {:X}", (size_t) module);

        _module = module;

        _INTC_D3D12_GetSupportedVersions =
            (PFN_INTC_D3D12_GetSupportedVersions) GetProcAddress(module, "_INTC_D3D12_GetSupportedVersions");

        _INTC_D3D12_CreateDeviceExtensionContext1 = (PFN_INTC_D3D12_CreateDeviceExtensionContext1) GetProcAddress(
            module, "_INTC_D3D12_CreateDeviceExtensionContext1");

        _INTC_DestroyDeviceExtensionContext =
            (PFN_INTC_DestroyDeviceExtensionContext) GetProcAddress(module, "_INTC_DestroyDeviceExtensionContext");

        _INTC_D3D12_CheckFeatureSupport =
            (PFN_INTC_D3D12_CheckFeatureSupport) GetProcAddress(module, "_INTC_D3D12_CheckFeatureSupport");

        _INTC_D3D12_SetFeatureSupport =
            (PFN_INTC_D3D12_SetFeatureSupport) GetProcAddress(module, "_INTC_D3D12_SetFeatureSupport");

        _INTC_D3D12_CreateCommittedResource =
            (PFN_INTC_D3D12_CreateCommittedResource) GetProcAddress(module, "_INTC_D3D12_CreateCommittedResource");

        _INTC_D3D12_CreatePlacedResource =
            (PFN_INTC_D3D12_CreatePlacedResource) GetProcAddress(module, "_INTC_D3D12_CreatePlacedResource");

        _INTC_D3D12_GetResourceAllocationInfo =
            (PFN_INTC_D3D12_GetResourceAllocationInfo) GetProcAddress(module, "_INTC_D3D12_GetResourceAllocationInfo");
    }

    static void EnableAtomicSupport(ID3D12Device* device)
    {
        if (_atomicSupportEnabled)
            return;

        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};

        if (CreateContext(device))
        {
            if (!EnableAtomic64Support())
            {
                DestroyContext();
                Config::Instance()->UESpoofIntelAtomics64.set_volatile_value(false);
            }
        }
    }

    static void DestroyContext()
    {
        if (_context == nullptr)
            return;

        if (_module == nullptr)
        {
            LOG_ERROR("igdext64.dll is not loaded!");
            return;
        }

        auto result = _INTC_DestroyDeviceExtensionContext(&_context);
        _context = nullptr;
        _atomicSupportEnabled = false;

        LOG_INFO("_INTC_DestroyDeviceExtensionContext result: {:X}", (UINT) result);
    }

    static HRESULT CreateCommitedResource(const D3D12_HEAP_PROPERTIES* pHeapProperties, D3D12_HEAP_FLAGS HeapFlags,
                                          D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialResourceState,
                                          const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riidResource,
                                          void** ppvResource)
    {
        if (_context == nullptr)
            return E_INVALIDARG;

        if (_module == nullptr)
        {
            LOG_ERROR("igdext64.dll is not loaded!");
            return E_INVALIDARG;
        }

        if (!_atomicSupportEnabled)
            return E_INVALIDARG;

        INTC_D3D12_RESOURCE_DESC_0001 localDesc {};
        localDesc.pD3D12Desc = pDesc;
        localDesc.EmulatedTyped64bitAtomics = true;
        localDesc.Texture2DArrayMipPack = false;

        auto result =
            _INTC_D3D12_CreateCommittedResource(_context, pHeapProperties, HeapFlags, &localDesc, InitialResourceState,
                                                pOptimizedClearValue, riidResource, ppvResource);

        LOG_DEBUG("_INTC_D3D12_CreateCommittedResource result: {:X}", (UINT) result);

        return result;
    }

    static HRESULT CreatePlacedResource(ID3D12Heap* pHeap, UINT64 HeapOffset, D3D12_RESOURCE_DESC* pDesc,
                                        D3D12_RESOURCE_STATES InitialState,
                                        const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource)
    {
        if (_context == nullptr)
            return E_INVALIDARG;

        if (_module == nullptr)
        {
            LOG_ERROR("igdext64.dll is not loaded!");
            return E_INVALIDARG;
        }

        if (!_atomicSupportEnabled)
            return E_INVALIDARG;

        INTC_D3D12_RESOURCE_DESC_0001 localDesc {};
        localDesc.pD3D12Desc = pDesc;
        localDesc.EmulatedTyped64bitAtomics = true;
        localDesc.Texture2DArrayMipPack = false;

        auto result = _INTC_D3D12_CreatePlacedResource(_context, pHeap, HeapOffset, &localDesc, InitialState,
                                                       pOptimizedClearValue, riid, ppvResource);

        LOG_DEBUG("_INTC_D3D12_CreatePlacedResource result: {:X}", (UINT) result);

        return result;
    }

    static D3D12_RESOURCE_ALLOCATION_INFO GetResourceAllocationInfo(UINT visibleMask, UINT numResourceDescs,
                                                                    const D3D12_RESOURCE_DESC* pResourceDescs)
    {
        if (_context == nullptr)
            return {};

        if (_module == nullptr)
        {
            LOG_ERROR("igdext64.dll is not loaded!");
            return {};
        }

        if (!_atomicSupportEnabled)
            return {};

        INTC_D3D12_RESOURCE_DESC_0001 localDesc {};
        D3D12_RESOURCE_DESC localD3D12Desc = *pResourceDescs;
        localDesc.pD3D12Desc = &localD3D12Desc;
        localDesc.EmulatedTyped64bitAtomics = true;

        auto result = _INTC_D3D12_GetResourceAllocationInfo(_context, visibleMask, numResourceDescs, &localDesc);

        return result;
    }
};
