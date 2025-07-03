#pragma once

#include <pch.h>

#include <Config.h>

#include <proxies/Dxgi_Proxy.h>

#include <detours/detours.h>

#include <dxgi1_6.h>

#define METHOD_BASED_SPOOFING_CHECK

#ifdef METHOD_BASED_SPOOFING_CHECK
#include <DbgHelp.h>
#endif

#ifndef METHOD_BASED_SPOOFING_CHECK
// #define FILE_BASED_SPOOFING_CHECK
#endif

#pragma intrinsic(_ReturnAddress)

typedef HRESULT (*PFN_GetDesc)(IDXGIAdapter* This, DXGI_ADAPTER_DESC* pDesc);
typedef HRESULT (*PFN_GetDesc1)(IDXGIAdapter1* This, DXGI_ADAPTER_DESC1* pDesc);
typedef HRESULT (*PFN_GetDesc2)(IDXGIAdapter2* This, DXGI_ADAPTER_DESC2* pDesc);
typedef HRESULT (*PFN_GetDesc3)(IDXGIAdapter4* This, DXGI_ADAPTER_DESC3* pDesc);

typedef HRESULT (*PFN_EnumAdapterByGpuPreference)(IDXGIFactory6* This, UINT Adapter, DXGI_GPU_PREFERENCE GpuPreference,
                                                  REFIID riid, IDXGIAdapter** ppvAdapter);
typedef HRESULT (*PFN_EnumAdapterByLuid)(IDXGIFactory4* This, LUID AdapterLuid, REFIID riid, IDXGIAdapter** ppvAdapter);
typedef HRESULT (*PFN_EnumAdapters1)(IDXGIFactory1* This, UINT Adapter, IDXGIAdapter1** ppAdapter);
typedef HRESULT (*PFN_EnumAdapters)(IDXGIFactory* This, UINT Adapter, IDXGIAdapter** ppAdapter);

inline static PFN_GetDesc o_GetDesc = nullptr;
inline static PFN_GetDesc1 o_GetDesc1 = nullptr;
inline static PFN_GetDesc2 o_GetDesc2 = nullptr;
inline static PFN_GetDesc3 o_GetDesc3 = nullptr;

inline static PFN_EnumAdapters o_EnumAdapters = nullptr;
inline static PFN_EnumAdapters1 o_EnumAdapters1 = nullptr;
inline static PFN_EnumAdapterByLuid o_EnumAdapterByLuid = nullptr;
inline static PFN_EnumAdapterByGpuPreference o_EnumAdapterByGpuPreference = nullptr;

inline static DxgiProxy::PFN_CreateDxgiFactory o_CreateDxgiFactory = nullptr;
inline static DxgiProxy::PFN_CreateDxgiFactory1 o_CreateDxgiFactory1 = nullptr;
inline static DxgiProxy::PFN_CreateDxgiFactory2 o_CreateDxgiFactory2 = nullptr;

inline static bool skipHighPerfCheck = false;

void AttachToAdapter(IUnknown* unkAdapter);
void AttachToFactory(IUnknown* unkFactory);

#pragma region DXGI Adapter methods

inline static bool SkipSpoofing()
{
    auto skip = !Config::Instance()->DxgiSpoofing.value_or_default() ||
                State::Instance().skipSpoofing; // || State::Instance().isRunningOnLinux;

    if (skip)
    {
        LOG_TRACE("DxgiSpoofing: {}, skipSpoofing: {}, skipping spoofing",
                  Config::Instance()->DxgiSpoofing.value_or_default(), State::Instance().skipSpoofing);

        return true;
    }

#ifdef METHOD_BASED_SPOOFING_CHECK || FILE_BASED_SPOOFING_CHECK

    HANDLE process = GetCurrentProcess();

#ifdef FILE_BASED_SPOOFING_CHECK
    if (!skip /*&& Config::Instance()->DxgiBlacklist.has_value()*/ && process != nullptr)
    {
        skip = true;

        // File based spoofing
        SymInitialize(process, NULL, TRUE);

        // Capture stack trace
        const int maxFrames = 64;
        void* stack[maxFrames];
        USHORT frames = CaptureStackBackTrace(0, maxFrames, stack, NULL);

        size_t pos_streamline = std::string::npos;
        size_t pos_intel = std::string::npos;
        size_t pos_ffx = std::string::npos;
        size_t pos_fsr = std::string::npos;

        for (USHORT i = 0; i < frames; ++i)
        {
            DWORD64 address = (DWORD64) (stack[i]);
            DWORD64 moduleBase = SymGetModuleBase64(process, address);

            if (moduleBase)
            {
                char moduleName[MAX_PATH];
                if (GetModuleFileNameA((HINSTANCE) moduleBase, moduleName, MAX_PATH))
                {
                    std::string module(moduleName);

                    if (pos_streamline == std::string::npos)
                        pos_streamline = module.rfind("\\sl.");

                    if (pos_intel == std::string::npos)
                        pos_intel = module.rfind("\\libxe");

                    if (pos_ffx == std::string::npos)
                        pos_ffx = module.rfind("\\amd_fidelityfx");

                    if (pos_fsr == std::string::npos)
                        pos_fsr = module.rfind("\\ffx_fsr");
                }
            }
        }

        SymCleanup(process);

        //&& pos_streamline == std::string::npos;
        skip = pos_intel != std::string::npos && pos_ffx != std::string::npos && pos_fsr != std::string::npos;
#endif

#ifdef METHOD_BASED_SPOOFING_CHECK
        if (!skip && Config::Instance()->DxgiBlacklist.has_value() && process != nullptr)
        {
            skip = true;

            // Walk the call stack to find the DLL that is calling the hooked function
            void* callers[100];

            unsigned short frames = CaptureStackBackTrace(0, 100, callers, NULL);

            if (SymInitialize(process, NULL, TRUE))
            {
                SYMBOL_INFO* symbol = (SYMBOL_INFO*) calloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char), 1);

                if (symbol != nullptr)
                {
                    symbol->MaxNameLen = 255;
                    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

                    for (unsigned int i = 0; i < frames; i++)
                    {
                        if (SymFromAddr(process, (DWORD64) callers[i], 0, symbol))
                        {
                            auto sn = std::string(symbol->Name);
                            auto pos = Config::Instance()->DxgiBlacklist.value().rfind(sn);

                            LOG_DEBUG("checking for: {0} ({1})", sn, i);

                            if (pos != std::string::npos)
                            {
                                LOG_INFO("spoofing for: {0}", sn);
                                skip = false;
                                break;
                            }
                        }
                    }

                    free(symbol);
                }

                SymCleanup(process);
            }
#endif

            if (skip)
                LOG_DEBUG("skipping spoofing, blacklisting active");
        }
#endif

        return skip;
    }

    inline static HRESULT hkGetDesc3(IDXGIAdapter4 * This, DXGI_ADAPTER_DESC3 * pDesc)
    {
        auto result = o_GetDesc3(This, pDesc);

#if _DEBUG
        LOG_TRACE("result: {:X}, caller: {}", (UINT) result, Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        if (result == S_OK)
        {
            if (pDesc->VendorId != VendorId::Microsoft &&
                !State::Instance().adapterDescs.contains(pDesc->AdapterLuid.HighPart | pDesc->AdapterLuid.LowPart))
            {
                std::wstring szName(pDesc->Description);
                std::string descStr =
                    std::format("Adapter: {}, VRAM: {} MB, VendorId: {:#x}, DeviceId: {:#x}", wstring_to_string(szName),
                                pDesc->DedicatedVideoMemory / (1024 * 1024), pDesc->VendorId, pDesc->DeviceId);
                LOG_INFO("{}", descStr);
                State::Instance().adapterDescs.insert_or_assign(
                    pDesc->AdapterLuid.HighPart | pDesc->AdapterLuid.LowPart, descStr);
            }

            if (Config::Instance()->DxgiVRAM.has_value())
                pDesc->DedicatedVideoMemory = (UINT64) Config::Instance()->DxgiVRAM.value() * 1024 * 1024 * 1024;

            if (pDesc->VendorId != VendorId::Microsoft &&
                (!Config::Instance()->TargetVendorId.has_value() ||
                 Config::Instance()->TargetVendorId.value() == pDesc->VendorId) &&
                (!Config::Instance()->TargetDeviceId.has_value() ||
                 Config::Instance()->TargetDeviceId.value() == pDesc->DeviceId) &&
                Config::Instance()->DxgiSpoofing.value_or_default() && !SkipSpoofing())
            {
                pDesc->VendorId = Config::Instance()->SpoofedVendorId.value_or_default();
                pDesc->DeviceId = Config::Instance()->SpoofedDeviceId.value_or_default();

                auto szName = Config::Instance()->SpoofedGPUName.value_or_default();
                std::memset(pDesc->Description, 0, sizeof(pDesc->Description));
                std::wcscpy(pDesc->Description, szName.c_str());

#ifdef _DEBUG
                LOG_DEBUG("spoofing");
#endif
            }
        }

        AttachToAdapter(This);

        return result;
    }

    inline static HRESULT hkGetDesc2(IDXGIAdapter2 * This, DXGI_ADAPTER_DESC2 * pDesc)
    {
        auto result = o_GetDesc2(This, pDesc);

#if _DEBUG
        LOG_TRACE("result: {:X}, caller: {}", (UINT) result, Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        if (result == S_OK)
        {
            if (pDesc->VendorId != VendorId::Microsoft &&
                !State::Instance().adapterDescs.contains(pDesc->AdapterLuid.HighPart | pDesc->AdapterLuid.LowPart))
            {
                std::wstring szName(pDesc->Description);
                std::string descStr =
                    std::format("Adapter: {}, VRAM: {} MB, VendorId: {:#x}, DeviceId: {:#x}", wstring_to_string(szName),
                                pDesc->DedicatedVideoMemory / (1024 * 1024), pDesc->VendorId, pDesc->DeviceId);
                LOG_INFO("{}", descStr);
                State::Instance().adapterDescs.insert_or_assign(
                    pDesc->AdapterLuid.HighPart | pDesc->AdapterLuid.LowPart, descStr);
            }

            if (Config::Instance()->DxgiVRAM.has_value())
                pDesc->DedicatedVideoMemory = (UINT64) Config::Instance()->DxgiVRAM.value() * 1024 * 1024 * 1024;

            if (pDesc->VendorId != VendorId::Microsoft &&
                (!Config::Instance()->TargetVendorId.has_value() ||
                 Config::Instance()->TargetVendorId.value() == pDesc->VendorId) &&
                (!Config::Instance()->TargetDeviceId.has_value() ||
                 Config::Instance()->TargetDeviceId.value() == pDesc->DeviceId) &&
                Config::Instance()->DxgiSpoofing.value_or_default() && !SkipSpoofing())
            {
                pDesc->VendorId = Config::Instance()->SpoofedVendorId.value_or_default();
                pDesc->DeviceId = Config::Instance()->SpoofedDeviceId.value_or_default();

                auto szName = Config::Instance()->SpoofedGPUName.value_or_default();
                std::memset(pDesc->Description, 0, sizeof(pDesc->Description));
                std::wcscpy(pDesc->Description, szName.c_str());

#ifdef _DEBUG
                LOG_DEBUG("spoofing");
#endif
            }
        }

        AttachToAdapter(This);

        return result;
    }

    inline static HRESULT hkGetDesc1(IDXGIAdapter1 * This, DXGI_ADAPTER_DESC1 * pDesc)
    {
        auto result = o_GetDesc1(This, pDesc);

#if _DEBUG
        LOG_TRACE("result: {:X}, caller: {}", (UINT) result, Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        if (result == S_OK)
        {
            if (pDesc->VendorId != VendorId::Microsoft &&
                !State::Instance().adapterDescs.contains(pDesc->AdapterLuid.HighPart | pDesc->AdapterLuid.LowPart))
            {
                std::wstring szName(pDesc->Description);
                std::string descStr =
                    std::format("Adapter: {}, VRAM: {} MB, VendorId: {:#x}, DeviceId: {:#x}", wstring_to_string(szName),
                                pDesc->DedicatedVideoMemory / (1024 * 1024), pDesc->VendorId, pDesc->DeviceId);
                LOG_INFO("{}", descStr);
                State::Instance().adapterDescs.insert_or_assign(
                    pDesc->AdapterLuid.HighPart | pDesc->AdapterLuid.LowPart, descStr);
            }

            if (Config::Instance()->DxgiVRAM.has_value())
                pDesc->DedicatedVideoMemory = (UINT64) Config::Instance()->DxgiVRAM.value() * 1024 * 1024 * 1024;

            if (pDesc->VendorId != VendorId::Microsoft &&
                (!Config::Instance()->TargetVendorId.has_value() ||
                 Config::Instance()->TargetVendorId.value() == pDesc->VendorId) &&
                (!Config::Instance()->TargetDeviceId.has_value() ||
                 Config::Instance()->TargetDeviceId.value() == pDesc->DeviceId) &&
                Config::Instance()->DxgiSpoofing.value_or_default() && !SkipSpoofing())
            {
                pDesc->VendorId = Config::Instance()->SpoofedVendorId.value_or_default();
                pDesc->DeviceId = Config::Instance()->SpoofedDeviceId.value_or_default();

                auto szName = Config::Instance()->SpoofedGPUName.value_or_default();
                std::memset(pDesc->Description, 0, sizeof(pDesc->Description));
                std::wcscpy(pDesc->Description, szName.c_str());

#ifdef _DEBUG
                LOG_DEBUG("spoofing");
#endif
            }
        }

        AttachToAdapter(This);

        return result;
    }

    inline static HRESULT hkGetDesc(IDXGIAdapter * This, DXGI_ADAPTER_DESC * pDesc)
    {
        auto result = o_GetDesc(This, pDesc);

#if _DEBUG
        LOG_TRACE("result: {:X}, caller: {}", (UINT) result, Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        if (result == S_OK)
        {
            if (pDesc->VendorId != VendorId::Microsoft &&
                !State::Instance().adapterDescs.contains(pDesc->AdapterLuid.HighPart | pDesc->AdapterLuid.LowPart))
            {
                std::wstring szName(pDesc->Description);
                std::string descStr =
                    std::format("Adapter: {}, VRAM: {} MB, VendorId: {:#x}, DeviceId: {:#x}", wstring_to_string(szName),
                                pDesc->DedicatedVideoMemory / (1024 * 1024), pDesc->VendorId, pDesc->DeviceId);
                LOG_INFO("{}", descStr);
                State::Instance().adapterDescs.insert_or_assign(
                    pDesc->AdapterLuid.HighPart | pDesc->AdapterLuid.LowPart, descStr);
            }

            if (Config::Instance()->DxgiVRAM.has_value())
                pDesc->DedicatedVideoMemory = (UINT64) Config::Instance()->DxgiVRAM.value() * 1024 * 1024 * 1024;

            if (pDesc->VendorId != VendorId::Microsoft &&
                (!Config::Instance()->TargetVendorId.has_value() ||
                 Config::Instance()->TargetVendorId.value() == pDesc->VendorId) &&
                (!Config::Instance()->TargetDeviceId.has_value() ||
                 Config::Instance()->TargetDeviceId.value() == pDesc->DeviceId) &&
                Config::Instance()->DxgiSpoofing.value_or_default() && !SkipSpoofing())
            {
                pDesc->VendorId = Config::Instance()->SpoofedVendorId.value_or_default();
                pDesc->DeviceId = Config::Instance()->SpoofedDeviceId.value_or_default();

                auto szName = Config::Instance()->SpoofedGPUName.value_or_default();
                std::memset(pDesc->Description, 0, sizeof(pDesc->Description));
                std::wcscpy(pDesc->Description, szName.c_str());

#ifdef _DEBUG
                LOG_DEBUG("spoofing");
#endif
            }
        }

        AttachToAdapter(This);

        return result;
    }

#pragma endregion

#pragma region DXGI Factory methods

    inline static HRESULT hkEnumAdapterByGpuPreference(
        IDXGIFactory6 * This, UINT Adapter, DXGI_GPU_PREFERENCE GpuPreference, REFIID riid, IDXGIAdapter * *ppvAdapter)
    {
        AttachToFactory(This);

        State::Instance().skipDxgiLoadChecks = true;
        auto result = o_EnumAdapterByGpuPreference(This, Adapter, GpuPreference, riid, ppvAdapter);
        State::Instance().skipDxgiLoadChecks = false;

        if (result == S_OK)
            AttachToAdapter(*ppvAdapter);

#if _DEBUG
        LOG_TRACE("result: {:X}, Adapter: {}, pAdapter: {:X}, caller: {}", (UINT) result, Adapter, (size_t) *ppvAdapter,
                  Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        return result;
    }

    inline static HRESULT hkEnumAdapterByLuid(IDXGIFactory4 * This, LUID AdapterLuid, REFIID riid,
                                              IDXGIAdapter * *ppvAdapter)
    {
        AttachToFactory(This);

        State::Instance().skipDxgiLoadChecks = true;
        auto result = o_EnumAdapterByLuid(This, AdapterLuid, riid, ppvAdapter);
        State::Instance().skipDxgiLoadChecks = false;

        if (result == S_OK)
            AttachToAdapter(*ppvAdapter);

#if _DEBUG
        LOG_TRACE("result: {:X}, pAdapter: {:X}, caller: {}", (UINT) result, (size_t) *ppvAdapter,
                  Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        return result;
    }

    inline static HRESULT hkEnumAdapters1(IDXGIFactory1 * This, UINT Adapter, IDXGIAdapter1 * *ppAdapter)
    {
        AttachToFactory(This);

        HRESULT result = S_OK;

        if (!skipHighPerfCheck && Config::Instance()->PreferDedicatedGpu.value_or_default())
        {
            LOG_WARN("High perf GPU selection");

            if (Config::Instance()->PreferFirstDedicatedGpu.value_or_default() && Adapter > 0)
            {
                LOG_DEBUG("{}, returning not found", Adapter);
                return DXGI_ERROR_NOT_FOUND;
            }

            IDXGIFactory6* factory6 = nullptr;
            if (This->QueryInterface(IID_PPV_ARGS(&factory6)) == S_OK && factory6 != nullptr)
            {
                LOG_DEBUG("Trying to select high performance adapter ({})", Adapter);

                skipHighPerfCheck = true;
                State::Instance().skipDxgiLoadChecks = true;
                result = o_EnumAdapterByGpuPreference(factory6, Adapter, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                      __uuidof(IDXGIAdapter), (IDXGIAdapter**) ppAdapter);
                State::Instance().skipDxgiLoadChecks = false;
                skipHighPerfCheck = false;

                if (result != S_OK)
                {
                    LOG_ERROR("Can't get high performance adapter: {:X}, fallback to standard method", Adapter);
                    result = o_EnumAdapters1(This, Adapter, ppAdapter);
                }

                if (result == S_OK)
                {
                    DXGI_ADAPTER_DESC desc;
                    State::Instance().skipSpoofing = true;
                    if ((*ppAdapter)->GetDesc(&desc) == S_OK)
                    {
                        std::wstring name(desc.Description);
                        LOG_DEBUG("High performance adapter ({}) will be used", wstring_to_string(name));
                    }
                    else
                    {
                        LOG_DEBUG("High performance adapter (Can't get description!) will be used");
                    }
                    State::Instance().skipSpoofing = false;
                }

                factory6->Release();
            }
            else
            {
                State::Instance().skipDxgiLoadChecks = true;
                result = o_EnumAdapters1(This, Adapter, ppAdapter);
                State::Instance().skipDxgiLoadChecks = false;
            }
        }
        else
        {
            State::Instance().skipDxgiLoadChecks = true;
            result = o_EnumAdapters1(This, Adapter, ppAdapter);
            State::Instance().skipDxgiLoadChecks = false;
        }

        if (result == S_OK)
            AttachToAdapter(*ppAdapter);

#if _DEBUG
        LOG_TRACE("result: {:X}, Adapter: {}, pAdapter: {:X}, caller: {}", (UINT) result, Adapter, (size_t) *ppAdapter,
                  Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        return result;
    }

    inline static HRESULT hkEnumAdapters(IDXGIFactory * This, UINT Adapter, IDXGIAdapter * *ppAdapter)
    {
        AttachToFactory(This);

        HRESULT result = S_OK;

        if (!skipHighPerfCheck && Config::Instance()->PreferDedicatedGpu.value_or_default())
        {
            if (Config::Instance()->PreferFirstDedicatedGpu.value_or_default() && Adapter > 0)
            {
                LOG_DEBUG("{}, returning not found", Adapter);
                return DXGI_ERROR_NOT_FOUND;
            }

            IDXGIFactory6* factory6 = nullptr;
            if (This->QueryInterface(IID_PPV_ARGS(&factory6)) == S_OK && factory6 != nullptr)
            {
                LOG_DEBUG("Trying to select high performance adapter ({})", Adapter);

                skipHighPerfCheck = true;
                State::Instance().skipDxgiLoadChecks = true;
                result = o_EnumAdapterByGpuPreference(factory6, Adapter, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                      __uuidof(IDXGIAdapter), ppAdapter);
                State::Instance().skipDxgiLoadChecks = false;
                skipHighPerfCheck = false;

                if (result != S_OK)
                {
                    LOG_ERROR("Can't get high performance adapter: {:X}, fallback to standard method", Adapter);
                    result = o_EnumAdapters(This, Adapter, ppAdapter);
                }

                if (result == S_OK)
                {
                    DXGI_ADAPTER_DESC desc;
                    State::Instance().skipSpoofing = true;
                    if ((*ppAdapter)->GetDesc(&desc) == S_OK)
                    {
                        std::wstring name(desc.Description);
                        LOG_DEBUG("Adapter ({}) will be used", wstring_to_string(name));
                    }
                    else
                    {
                        LOG_ERROR("Can't get adapter description!");
                    }
                    State::Instance().skipSpoofing = false;
                }

                factory6->Release();
            }
            else
            {
                State::Instance().skipDxgiLoadChecks = true;
                result = o_EnumAdapters(This, Adapter, ppAdapter);
                State::Instance().skipDxgiLoadChecks = false;
            }
        }
        else
        {
            State::Instance().skipDxgiLoadChecks = true;
            result = o_EnumAdapters(This, Adapter, ppAdapter);
            State::Instance().skipDxgiLoadChecks = false;
        }

        if (result == S_OK)
            AttachToAdapter(*ppAdapter);

#if _DEBUG
        LOG_TRACE("result: {:X}, Adapter: {}, pAdapter: {:X}, caller: {}", (UINT) result, Adapter, (size_t) *ppAdapter,
                  Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        return result;
    }

#pragma endregion

#pragma region DXGI methods

    inline static HRESULT hkCreateDXGIFactory(REFIID riid, IDXGIFactory * *ppFactory)
    {
        HRESULT result = o_CreateDxgiFactory(riid, ppFactory);

#if _DEBUG
        LOG_TRACE("result: {:X}, caller: {}", (UINT) result, Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        if (result == S_OK)
            AttachToFactory(*ppFactory);

        return result;
    }

    inline static HRESULT hkCreateDXGIFactory1(REFIID riid, IDXGIFactory1 * *ppFactory)
    {
        HRESULT result = o_CreateDxgiFactory1(riid, ppFactory);

#if _DEBUG
        LOG_TRACE("result: {:X}, caller: {}", (UINT) result, Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        if (result == S_OK)
            AttachToFactory(*ppFactory);

        return result;
    }

    inline static HRESULT hkCreateDXGIFactory2(UINT Flags, REFIID riid, IDXGIFactory2 * *ppFactory)
    {
        HRESULT result = o_CreateDxgiFactory2(Flags, riid, ppFactory);

#if _DEBUG
        LOG_TRACE("result: {:X}, caller: {}", (UINT) result, Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        if (result == S_OK)
            AttachToFactory(*ppFactory);

        return result;
    }

#pragma endregion

#pragma region DXGI Attach methods

    inline static void AttachToAdapter(IUnknown * unkAdapter)
    {
        if (o_GetDesc != nullptr && o_GetDesc1 != nullptr && o_GetDesc2 != nullptr && o_GetDesc3 != nullptr)
            return;

        const GUID guid = { 0x907bf281, 0xea3c, 0x43b4, { 0xa8, 0xe4, 0x9f, 0x23, 0x11, 0x07, 0xb4, 0xff } };

        PVOID* pVTable = *(PVOID**) unkAdapter;

        bool dxvkStatus = State::Instance().isRunningOnDXVK;

        IDXGIAdapter* adapter = nullptr;
        bool adapterOk = unkAdapter->QueryInterface(__uuidof(IDXGIAdapter), (void**) &adapter) == S_OK;

        void* dxvkAdapter = nullptr;
        if (adapterOk && !State::Instance().isRunningOnDXVK && adapter->QueryInterface(guid, &dxvkAdapter) == S_OK)
        {
            State::Instance().isRunningOnDXVK = dxvkAdapter != nullptr;
            ((IDXGIAdapter*) dxvkAdapter)->Release();
        }

        if (State::Instance().isRunningOnDXVK != dxvkStatus)
            LOG_INFO("IDXGIVkInteropDevice interface (DXVK) {0}",
                     State::Instance().isRunningOnDXVK ? "detected" : "not detected");

        if (o_GetDesc == nullptr && adapterOk)
        {
            LOG_DEBUG("Attach to GetDesc");

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            o_GetDesc = (PFN_GetDesc) pVTable[8];

            DetourAttach(&(PVOID&) o_GetDesc, hkGetDesc);

            DetourTransactionCommit();
        }

        if (adapter != nullptr)
            adapter->Release();

        IDXGIAdapter1* adapter1 = nullptr;
        if (o_GetDesc1 == nullptr && unkAdapter->QueryInterface(__uuidof(IDXGIAdapter1), (void**) &adapter1) == S_OK)
        {
            LOG_DEBUG("Attach to GetDesc1");

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            o_GetDesc1 = (PFN_GetDesc1) pVTable[10];

            DetourAttach(&(PVOID&) o_GetDesc1, hkGetDesc1);

            DetourTransactionCommit();
        }

        if (adapter1 != nullptr)
            adapter1->Release();

        IDXGIAdapter2* adapter2 = nullptr;
        if (o_GetDesc2 == nullptr && unkAdapter->QueryInterface(__uuidof(IDXGIAdapter2), (void**) &adapter2) == S_OK)
        {
            LOG_DEBUG("Attach to GetDesc2");

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            o_GetDesc2 = (PFN_GetDesc2) pVTable[11];

            DetourAttach(&(PVOID&) o_GetDesc2, hkGetDesc2);

            DetourTransactionCommit();
        }

        if (adapter2 != nullptr)
            adapter2->Release();

        IDXGIAdapter4* adapter4 = nullptr;
        if (o_GetDesc3 == nullptr && unkAdapter->QueryInterface(__uuidof(IDXGIAdapter4), (void**) &adapter4) == S_OK)
        {
            LOG_DEBUG("Attach to GetDesc3");

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            o_GetDesc3 = (PFN_GetDesc3) pVTable[18];

            DetourAttach(&(PVOID&) o_GetDesc3, hkGetDesc3);

            DetourTransactionCommit();
        }

        if (adapter4 != nullptr)
            adapter4->Release();
    }

    inline static void AttachToFactory(IUnknown * unkFactory)
    {
        if (o_EnumAdapters != nullptr && o_EnumAdapters1 != nullptr && o_EnumAdapterByLuid != nullptr &&
            o_EnumAdapterByGpuPreference != nullptr)
            return;

        PVOID* pVTable = *(PVOID**) unkFactory;

        IDXGIFactory* factory;
        if (o_EnumAdapters == nullptr && unkFactory->QueryInterface(__uuidof(IDXGIFactory), (void**) &factory) == S_OK)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            o_EnumAdapters = (PFN_EnumAdapters) pVTable[7];

            DetourAttach(&(PVOID&) o_EnumAdapters, hkEnumAdapters);

            DetourTransactionCommit();

            factory->Release();
        }

        IDXGIFactory1* factory1;
        if (o_EnumAdapters1 == nullptr &&
            unkFactory->QueryInterface(__uuidof(IDXGIFactory1), (void**) &factory1) == S_OK)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            o_EnumAdapters1 = (PFN_EnumAdapters1) pVTable[12];

            DetourAttach(&(PVOID&) o_EnumAdapters1, hkEnumAdapters1);

            DetourTransactionCommit();

            factory1->Release();
        }

        IDXGIFactory4* factory4;
        if (o_EnumAdapterByLuid == nullptr &&
            unkFactory->QueryInterface(__uuidof(IDXGIFactory4), (void**) &factory4) == S_OK)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            o_EnumAdapterByLuid = (PFN_EnumAdapterByLuid) pVTable[26];

            DetourAttach(&(PVOID&) o_EnumAdapterByLuid, hkEnumAdapterByLuid);

            DetourTransactionCommit();

            factory4->Release();
        }

        IDXGIFactory6* factory6;
        if (o_EnumAdapterByGpuPreference == nullptr &&
            unkFactory->QueryInterface(__uuidof(IDXGIFactory6), (void**) &factory6) == S_OK)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            o_EnumAdapterByGpuPreference = (PFN_EnumAdapterByGpuPreference) pVTable[29];

            DetourAttach(&(PVOID&) o_EnumAdapterByGpuPreference, hkEnumAdapterByGpuPreference);

            DetourTransactionCommit();

            factory6->Release();
        }
    }

#pragma endregion

    inline void HookDxgiForSpoofing()
    {
        if (o_CreateDxgiFactory != nullptr)
            return;

        LOG_DEBUG("");

        o_CreateDxgiFactory = DxgiProxy::Hook_CreateDxgiFactory(hkCreateDXGIFactory);
        o_CreateDxgiFactory1 = DxgiProxy::Hook_CreateDxgiFactory1(hkCreateDXGIFactory1);
        o_CreateDxgiFactory2 = DxgiProxy::Hook_CreateDxgiFactory2(hkCreateDXGIFactory2);
    }
