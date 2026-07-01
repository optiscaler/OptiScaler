#pragma once

#include "SysUtils.h"
#include "Util.h"
#include "Config.h"
#include "Logger.h"

#include <proxies/Ntdll_Proxy.h>
#include <proxies/KernelBase_Proxy.h>

#include <inputs/FfxApi_Dx12.h>
#include <inputs/FfxApi_Vk.h>

#include <fsr4/FSR4ModelSelection.h>

#include "ffx_api.h"
#include <detours/detours.h>
#include <ffx_framegeneration.h>
#include <ffx_upscale.h>

#include <scanner/scanner.h>

#include <magic_enum.hpp>

// A mess to be able to import both
#define FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_WAITCALLBACK FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_WAITCALLBACK_DX12
#define FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_FRAMEPACINGTUNING FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_FRAMEPACINGTUNING_DX12

#include <dx12/ffx_api_dx12.h>

#undef FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_WAITCALLBACK
#undef FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_FRAMEPACINGTUNING

#define FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_WAITCALLBACK FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_WAITCALLBACK_VK
#define FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_FRAMEPACINGTUNING FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_FRAMEPACINGTUNING_VK

#include <vk/ffx_api_vk.h>

#undef FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_WAITCALLBACK
#undef FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_FRAMEPACINGTUNING

enum class FFXStructType
{
    General,
    Upscaling,
    FG,
    SwapchainDX12,
    SwapchainVulkan,
    Denoiser,
    RadianceCache,
    Unknown,
};

struct FfxModule
{
    HMODULE dll = nullptr;
    feature_version version { 0, 0, 0 };
    std::wstring filePath;

    bool skipCreateCalls = false;
    bool skipConfigureCalls = false;
    bool skipQueryCalls = false;
    bool skipDispatchCalls = false;

    bool isLoader = false;
    bool hooked = false;

    PfnFfxCreateContext CreateContext = nullptr;
    PfnFfxDestroyContext DestroyContext = nullptr;
    PfnFfxConfigure Configure = nullptr;
    PfnFfxQuery Query = nullptr;
    PfnFfxDispatch Dispatch = nullptr;
};

using AmdInt8Check = uint8_t(__fastcall*)(void* a1, ID3D12Device* device);

class FfxApiProxy
{
  private:
    inline static FfxModule main_dx12;
    inline static FfxModule upscaling_dx12;
    inline static FfxModule fg_dx12;
    inline static FfxModule denoiser_dx12;
    inline static FfxModule radiance_dx12;
    inline static FfxModule main_vk;

    inline static FfxModule main_dx12_hooked;
    inline static FfxModule upscaling_dx12_hooked;
    inline static FfxModule fg_dx12_hooked;
    inline static FfxModule denoiser_dx12_hooked;
    inline static FfxModule radiance_dx12_hooked;
    inline static FfxModule main_vk_hooked;

    inline static ankerl::unordered_dense::map<ffxContext, FFXStructType> contextToType;

    inline static bool _skipDestroyCalls = false;

    inline static AmdInt8Check o_amdInt8Check = nullptr;
    inline static uint8_t hkAmdInt8Check(void* a1, ID3D12Device* device)
    {
        LOG_DEBUG("Called with a1: {:X}, device: {:X}", (uintptr_t) a1, (uintptr_t) device);
        return 1;
    }

    static inline void parse_version(const char* version_str, feature_version* _version)
    {
        const char* p = version_str;

        // Skip non-digits at front
        while (*p)
        {
            if (isdigit((unsigned char) p[0]))
            {
                if (sscanf(p, "%u.%u.%u", &_version->major, &_version->minor, &_version->patch) == 3)
                    return;
            }
            ++p;
        }

        LOG_WARN("can't parse {0}", version_str);
    }

    static bool IsLoader(const std::wstring& filePath)
    {
        auto size = std::filesystem::file_size(filePath);

        // < 1 MB
        return size < 1048576;
    }

  public:
    static HMODULE Dx12Module() { return main_dx12.dll; }
    static HMODULE Dx12Module_SR() { return upscaling_dx12.dll; }
    static HMODULE Dx12Module_FG() { return fg_dx12.dll; }
    static HMODULE Dx12Module_Denoiser() { return denoiser_dx12.dll; }
    static HMODULE Dx12Module_Radiance() { return radiance_dx12.dll; }

    static std::wstring Dx12Module_Path() { return main_dx12.filePath; }
    static std::wstring Dx12Module_SR_Path() { return upscaling_dx12.filePath; }
    static std::wstring Dx12Module_FG_Path() { return fg_dx12.filePath; }
    static std::wstring Dx12Module_Denoiser_Path() { return denoiser_dx12.filePath; }
    static std::wstring Dx12Module_Radiance_Path() { return radiance_dx12.filePath; }

    static bool IsFGReady() { return (main_dx12.dll && !main_dx12.isLoader) || fg_dx12.dll != nullptr; }
    static bool IsSRReady() { return (main_dx12.dll && !main_dx12.isLoader) || upscaling_dx12.dll != nullptr; }
    static bool IsDenoiserReady() { return (main_dx12.dll && !main_dx12.isLoader) || denoiser_dx12.dll != nullptr; }
    static bool IsRadianceReady() { return (main_dx12.dll && !main_dx12.isLoader) || radiance_dx12.dll != nullptr; }

    static FFXStructType GetType(ffxStructType_t type)
    {
        switch (type & FFX_API_EFFECT_MASK) // type without the specific effect
        {
        case FFX_API_EFFECT_ID_GENERAL:
            return FFXStructType::General;

        case FFX_API_EFFECT_ID_UPSCALE:
            return FFXStructType::Upscaling;

        case FFX_API_EFFECT_ID_FRAMEGENERATION:
            return FFXStructType::FG;

        case FFX_API_EFFECT_ID_FRAMEGENERATIONSWAPCHAIN_DX12:
            return FFXStructType::SwapchainDX12;

        case FFX_API_EFFECT_ID_FGSC_VK:
            return FFXStructType::SwapchainVulkan;

        case 0x00050000u:
            return FFXStructType::Denoiser;

        case 0x00060000u:
            return FFXStructType::RadianceCache;

        default:
            return FFXStructType::Unknown;
        }
    }

    // Can't directly check for type when query is used
    // might apply to FFX_API_DESC_TYPE_OVERRIDE_VERSION as well
    static FFXStructType GetIndirectType(ffxQueryDescHeader* header)
    {
        ffxStructType_t type = header->type;

        if (header->type == FFX_API_QUERY_DESC_TYPE_GET_VERSIONS ||
            header->type == FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION)
        {
            type = header[1].type;
        }

        return GetType(type);
    }

    static bool InitFfxDx12(HMODULE module = nullptr)
    {
        // if dll already loaded
        if (main_dx12.dll != nullptr && main_dx12.CreateContext != nullptr)
            return true;

        spdlog::info("");

        // This is module loaded by game, need to hook it to use inputs
        if (module != nullptr && main_dx12_hooked.dll == nullptr)
        {
            main_dx12_hooked.dll = module;
        }

        // Try loading Opti's dlls first
        if (main_dx12.dll == nullptr)
        {
            std::vector<std::wstring> dllNames = { L"amd_fidelityfx_loader_dx12.dll", L"amd_fidelityfx_dx12.dll" };

            auto optiPath = Config::Instance()->MainDllPath.value();

            for (size_t i = 0; i < dllNames.size(); i++)
            {
                LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

                auto overridePath = Config::Instance()->FfxDx12Path.value_or(L"");

                if (main_dx12_hooked.dll == nullptr)
                {
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &main_dx12_hooked.dll, &main_dx12.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &memModule, &main_dx12.dll);
                }

                if (main_dx12.dll != nullptr)
                {
                    break;
                }
            }
        }

        // Can't find Opti dlls, use just loaded module
        if (main_dx12.dll == nullptr && main_dx12_hooked.dll != nullptr)
        {
            main_dx12 = main_dx12_hooked;
        }

        if (main_dx12.dll != nullptr)
        {
            wchar_t modulePath[MAX_PATH];
            DWORD len = GetModuleFileNameW(main_dx12.dll, modulePath, MAX_PATH);
            main_dx12.filePath = std::wstring(modulePath);

            LOG_INFO("Loaded from {}", wstring_to_string(main_dx12.filePath));

            // hacky but works for now
            main_dx12.isLoader = IsLoader(main_dx12.filePath);

            if (!main_dx12.isLoader)
            {
                FSR4ModelSelection::Hook(main_dx12.dll, FSR4Source::SDK);
            }
        }

        // Get methods of Opti's dlls
        if (main_dx12.dll != nullptr && main_dx12.Configure == nullptr)
        {
            main_dx12.Configure = (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(main_dx12.dll, "ffxConfigure");
            main_dx12.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(main_dx12.dll, "ffxCreateContext");
            main_dx12.DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(main_dx12.dll, "ffxDestroyContext");
            main_dx12.Dispatch = (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(main_dx12.dll, "ffxDispatch");
            main_dx12.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(main_dx12.dll, "ffxQuery");
        }

        // Get methods of game's dlls
        if (main_dx12_hooked.dll != nullptr && main_dx12_hooked.Configure == nullptr)
        {
            main_dx12_hooked.Configure =
                (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(main_dx12_hooked.dll, "ffxConfigure");
            main_dx12_hooked.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(main_dx12_hooked.dll, "ffxCreateContext");
            main_dx12_hooked.DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(main_dx12_hooked.dll, "ffxDestroyContext");
            main_dx12_hooked.Dispatch =
                (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(main_dx12_hooked.dll, "ffxDispatch");
            main_dx12_hooked.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(main_dx12_hooked.dll, "ffxQuery");
        }
        else if (main_dx12_hooked.Configure == nullptr)
        {
            main_dx12_hooked = main_dx12;
        }

        // Hook game's dlls
        if (Config::Instance()->EnableFfxInputs.value_or_default() && main_dx12_hooked.dll != nullptr &&
            main_dx12_hooked.CreateContext != nullptr && !main_dx12_hooked.hooked)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (main_dx12_hooked.Configure != nullptr)
                DetourAttach(&(PVOID&) main_dx12_hooked.Configure, ffxConfigure_Dx12);

            if (main_dx12_hooked.CreateContext != nullptr)
                DetourAttach(&(PVOID&) main_dx12_hooked.CreateContext, ffxCreateContext_Dx12);

            if (main_dx12_hooked.DestroyContext != nullptr)
                DetourAttach(&(PVOID&) main_dx12_hooked.DestroyContext, ffxDestroyContext_Dx12);

            if (main_dx12_hooked.Dispatch != nullptr)
                DetourAttach(&(PVOID&) main_dx12_hooked.Dispatch, ffxDispatch_Dx12);

            if (main_dx12_hooked.Query != nullptr)
                DetourAttach(&(PVOID&) main_dx12_hooked.Query, ffxQuery_Dx12);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("DetourTransactionCommit result: {:X}", detourResult);
                main_dx12_hooked.Configure = nullptr;
                main_dx12_hooked.CreateContext = nullptr;
                main_dx12_hooked.DestroyContext = nullptr;
                main_dx12_hooked.Dispatch = nullptr;
                main_dx12_hooked.Query = nullptr;
            }
            else
            {
                // If we are hooking Opti's dlls then we need to use original method ptrs
                if (main_dx12_hooked.dll == main_dx12.dll)
                    main_dx12 = main_dx12_hooked;

                main_dx12_hooked.hooked = true;
                State::Instance().fsrHooks = true;
            }
        }

        InitFfxDx12_SR();
        InitFfxDx12_FG();
        InitFfxDx12_Denoiser();
        InitFfxDx12_Radiance();

        bool loadResult = main_dx12.CreateContext != nullptr || upscaling_dx12.CreateContext != nullptr ||
                          fg_dx12.CreateContext != nullptr || denoiser_dx12.CreateContext != nullptr ||
                          radiance_dx12.CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (!loadResult)
            main_dx12.dll = nullptr;

        return loadResult;
    }

    static bool InitFfxDx12_SR(HMODULE module = nullptr)
    {
        // if dll already loaded
        if (upscaling_dx12.dll != nullptr && upscaling_dx12.CreateContext != nullptr)
            return true;

        spdlog::info("");

        // This is module loaded by game, need to hook it to use inputs
        if (module != nullptr && upscaling_dx12_hooked.dll == nullptr)
        {
            upscaling_dx12_hooked.dll = module;
        }

        if (upscaling_dx12.dll == nullptr)
        {
            // Try new api first
            std::vector<std::wstring> dllNames = { L"amd_fidelityfx_upscaler_dx12.dll" };

            auto optiPath = Config::Instance()->MainDllPath.value();

            for (size_t i = 0; i < dllNames.size(); i++)
            {
                LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

                auto overridePath = Config::Instance()->FfxDx12SRPath.value_or(L"");

                if (upscaling_dx12_hooked.dll == nullptr)
                {
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &upscaling_dx12_hooked.dll,
                                           &upscaling_dx12.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &memModule, &upscaling_dx12.dll);
                }

                if (upscaling_dx12.dll != nullptr)
                {
                    break;
                }
            }
        }

        // Can't find Opti dlls, use just module loaded by game
        if (upscaling_dx12.dll == nullptr && upscaling_dx12_hooked.dll != nullptr)
        {
            upscaling_dx12 = upscaling_dx12_hooked;
        }

        if (upscaling_dx12.dll != nullptr)
        {
            FSR4ModelSelection::Hook(upscaling_dx12.dll, FSR4Source::SDK);

            wchar_t modulePath[MAX_PATH];
            DWORD len = GetModuleFileNameW(upscaling_dx12.dll, modulePath, MAX_PATH);
            upscaling_dx12.filePath = std::wstring(modulePath);

            LOG_INFO("Loaded from {}", wstring_to_string(upscaling_dx12.filePath));
        }

        if (upscaling_dx12.dll != nullptr && upscaling_dx12.Configure == nullptr)
        {
            upscaling_dx12.Configure =
                (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(upscaling_dx12.dll, "ffxConfigure");
            upscaling_dx12.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(upscaling_dx12.dll, "ffxCreateContext");
            upscaling_dx12.DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(upscaling_dx12.dll, "ffxDestroyContext");
            upscaling_dx12.Dispatch =
                (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(upscaling_dx12.dll, "ffxDispatch");
            upscaling_dx12.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(upscaling_dx12.dll, "ffxQuery");
        }

        if (upscaling_dx12_hooked.dll != nullptr && upscaling_dx12_hooked.Configure == nullptr)
        {
            upscaling_dx12_hooked.Configure =
                (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(upscaling_dx12_hooked.dll, "ffxConfigure");
            upscaling_dx12_hooked.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(upscaling_dx12_hooked.dll, "ffxCreateContext");
            upscaling_dx12_hooked.DestroyContext = (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(
                upscaling_dx12_hooked.dll, "ffxDestroyContext");
            upscaling_dx12_hooked.Dispatch =
                (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(upscaling_dx12_hooked.dll, "ffxDispatch");
            upscaling_dx12_hooked.Query =
                (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(upscaling_dx12_hooked.dll, "ffxQuery");
        }
        else if (upscaling_dx12_hooked.Configure == nullptr)
        {
            upscaling_dx12_hooked = upscaling_dx12;
        }

        if (Config::Instance()->EnableFfxInputs.value_or_default() && upscaling_dx12_hooked.dll != nullptr &&
            upscaling_dx12_hooked.CreateContext != nullptr && !upscaling_dx12_hooked.hooked)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (upscaling_dx12_hooked.Configure != nullptr)
                DetourAttach(&(PVOID&) upscaling_dx12_hooked.Configure, ffxConfigure_Dx12);

            if (upscaling_dx12_hooked.CreateContext != nullptr)
                DetourAttach(&(PVOID&) upscaling_dx12_hooked.CreateContext, ffxCreateContext_Dx12);

            if (upscaling_dx12_hooked.DestroyContext != nullptr)
                DetourAttach(&(PVOID&) upscaling_dx12_hooked.DestroyContext, ffxDestroyContext_Dx12);

            if (upscaling_dx12_hooked.Dispatch != nullptr)
                DetourAttach(&(PVOID&) upscaling_dx12_hooked.Dispatch, ffxDispatch_Dx12);

            if (upscaling_dx12_hooked.Query != nullptr)
                DetourAttach(&(PVOID&) upscaling_dx12_hooked.Query, ffxQuery_Dx12);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("DetourTransactionCommit result: {:X}", detourResult);
                upscaling_dx12_hooked.Configure = nullptr;
                upscaling_dx12_hooked.CreateContext = nullptr;
                upscaling_dx12_hooked.DestroyContext = nullptr;
                upscaling_dx12_hooked.Dispatch = nullptr;
                upscaling_dx12_hooked.Query = nullptr;
            }
            else
            {
                if (upscaling_dx12.dll == upscaling_dx12_hooked.dll)
                    upscaling_dx12 = upscaling_dx12_hooked;

                upscaling_dx12_hooked.hooked = true;
                State::Instance().fsrHooks = true;
            }
        }

        bool loadResult = upscaling_dx12.CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (!loadResult)
            upscaling_dx12.dll = nullptr;

        if (Config::Instance()->Fsr4ForceEnableInt8.value_or_default())
        {
            const char* pattern =
                "48 83 EC 48 48 8B C2 48 85 D2 74 54 48 8D 54 24 20 48 8B C8 E8 ? ? ? ? 81 7C 24 34 91 00 00 00 44 0F "
                "B6 C0 75 23 8B 54 24 30 8D 4A FF 83 F9 0E 76 13 8D 4A E0 81 F9 DE 00 00 00 76 08 8D 4A F0 83 F9 0F";

            auto o_amdInt8Check = (AmdInt8Check) scanner::GetAddress(upscaling_dx12.dll, pattern);

            if (o_amdInt8Check != nullptr)
            {
                LOG_INFO("Found AmdInt8Check at {:X}", (size_t) o_amdInt8Check);

                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                if (o_amdInt8Check != nullptr)
                    DetourAttach(&(PVOID&) o_amdInt8Check, hkAmdInt8Check);

                auto detourResult = DetourTransactionCommit();
                if (detourResult != NO_ERROR)
                {
                    LOG_ERROR("DetourTransactionCommit result: {:X}", detourResult);
                    o_amdInt8Check = nullptr;
                }
            }
        }

        return loadResult;
    }

    static bool InitFfxDx12_FG(HMODULE module = nullptr)
    {
        // if dll already loaded
        if (fg_dx12.dll != nullptr && fg_dx12.CreateContext != nullptr)
            return true;

        spdlog::info("");

        // This is module loaded by game, need to hook it to use inputs
        if (module != nullptr && fg_dx12_hooked.dll == nullptr)
        {
            fg_dx12_hooked.dll = module;
        }

        if (fg_dx12.dll == nullptr)
        {
            // Try new api first
            std::vector<std::wstring> dllNames = { L"amd_fidelityfx_framegeneration_dx12.dll" };

            auto optiPath = Config::Instance()->MainDllPath.value();

            for (size_t i = 0; i < dllNames.size(); i++)
            {
                LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

                auto overridePath = Config::Instance()->FfxDx12FGPath.value_or(L"");

                if (fg_dx12_hooked.dll == nullptr)
                {
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &fg_dx12_hooked.dll, &fg_dx12.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &memModule, &fg_dx12.dll);
                }

                if (fg_dx12.dll != nullptr)
                {
                    break;
                }
            }
        }

        // Can't find Opti dlls, use just module loaded by game
        if (fg_dx12.dll == nullptr && fg_dx12_hooked.dll != nullptr)
        {
            fg_dx12 = fg_dx12_hooked;
        }

        if (fg_dx12.dll != nullptr)
        {
            wchar_t modulePath[MAX_PATH];
            DWORD len = GetModuleFileNameW(fg_dx12.dll, modulePath, MAX_PATH);
            fg_dx12.filePath = std::wstring(modulePath);

            LOG_INFO("Loaded from {}", wstring_to_string(fg_dx12.filePath));
        }

        if (fg_dx12.dll != nullptr && fg_dx12.Configure == nullptr)
        {
            fg_dx12.Configure = (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(fg_dx12.dll, "ffxConfigure");
            fg_dx12.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(fg_dx12.dll, "ffxCreateContext");
            fg_dx12.DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(fg_dx12.dll, "ffxDestroyContext");
            fg_dx12.Dispatch = (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(fg_dx12.dll, "ffxDispatch");
            fg_dx12.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(fg_dx12.dll, "ffxQuery");
        }

        if (fg_dx12_hooked.dll != nullptr && fg_dx12_hooked.Configure == nullptr)
        {
            fg_dx12_hooked.Configure =
                (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(fg_dx12_hooked.dll, "ffxConfigure");
            fg_dx12_hooked.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(fg_dx12_hooked.dll, "ffxCreateContext");
            fg_dx12_hooked.DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(fg_dx12_hooked.dll, "ffxDestroyContext");
            fg_dx12_hooked.Dispatch =
                (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(fg_dx12_hooked.dll, "ffxDispatch");
            fg_dx12_hooked.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(fg_dx12_hooked.dll, "ffxQuery");
        }
        else if (fg_dx12_hooked.Configure == nullptr)
        {
            fg_dx12_hooked = fg_dx12;
        }

        if (Config::Instance()->EnableFfxInputs.value_or_default() && fg_dx12_hooked.dll != nullptr &&
            fg_dx12_hooked.CreateContext != nullptr && !fg_dx12_hooked.hooked)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (fg_dx12_hooked.Configure != nullptr)
                DetourAttach(&(PVOID&) fg_dx12_hooked.Configure, ffxConfigure_Dx12);

            if (fg_dx12_hooked.CreateContext != nullptr)
                DetourAttach(&(PVOID&) fg_dx12_hooked.CreateContext, ffxCreateContext_Dx12);

            if (fg_dx12_hooked.DestroyContext != nullptr)
                DetourAttach(&(PVOID&) fg_dx12_hooked.DestroyContext, ffxDestroyContext_Dx12);

            if (fg_dx12_hooked.Dispatch != nullptr)
                DetourAttach(&(PVOID&) fg_dx12_hooked.Dispatch, ffxDispatch_Dx12);

            if (fg_dx12_hooked.Query != nullptr)
                DetourAttach(&(PVOID&) fg_dx12_hooked.Query, ffxQuery_Dx12);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("DetourTransactionCommit result: {:X}", detourResult);
                fg_dx12.Configure = nullptr;
                fg_dx12.CreateContext = nullptr;
                fg_dx12.DestroyContext = nullptr;
                fg_dx12.Dispatch = nullptr;
                fg_dx12.Query = nullptr;
            }
            else
            {
                if (fg_dx12.dll == fg_dx12_hooked.dll)
                    fg_dx12 = fg_dx12_hooked;

                fg_dx12_hooked.hooked = true;
                State::Instance().fsrHooks = true;
            }
        }

        bool loadResult = fg_dx12.CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (!loadResult)
            fg_dx12.dll = nullptr;

        return loadResult;
    }

    static bool InitFfxDx12_Denoiser(HMODULE module = nullptr)
    {
        // if dll already loaded
        if (denoiser_dx12.dll != nullptr && denoiser_dx12.CreateContext != nullptr)
            return true;

        spdlog::info("");

        // This is module loaded by game, need to hook it to use inputs
        if (module != nullptr && denoiser_dx12_hooked.dll == nullptr)
        {
            denoiser_dx12_hooked.dll = module;
        }

        if (denoiser_dx12.dll == nullptr)
        {
            // Try new api first
            std::vector<std::wstring> dllNames = { L"amd_fidelityfx_denoiser_dx12.dll" };

            auto optiPath = Config::Instance()->MainDllPath.value();

            for (size_t i = 0; i < dllNames.size(); i++)
            {
                LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

                auto overridePath = Config::Instance()->FfxDx12RRPath.value_or(L"");

                if (denoiser_dx12_hooked.dll == nullptr)
                {
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &denoiser_dx12_hooked.dll,
                                           &denoiser_dx12.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &memModule, &denoiser_dx12.dll);
                }

                if (denoiser_dx12.dll != nullptr)
                {
                    break;
                }
            }
        }

        // Can't find Opti dlls, use just module loaded by game
        if (denoiser_dx12.dll == nullptr && denoiser_dx12_hooked.dll != nullptr)
        {
            denoiser_dx12 = denoiser_dx12_hooked;
        }

        if (denoiser_dx12.dll != nullptr)
        {
            wchar_t modulePath[MAX_PATH];
            DWORD len = GetModuleFileNameW(denoiser_dx12.dll, modulePath, MAX_PATH);
            denoiser_dx12.filePath = std::wstring(modulePath);

            LOG_INFO("Loaded from {}", wstring_to_string(denoiser_dx12.filePath));
        }

        if (denoiser_dx12.dll != nullptr && denoiser_dx12.Configure == nullptr)
        {
            denoiser_dx12.Configure =
                (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(denoiser_dx12.dll, "ffxConfigure");
            denoiser_dx12.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(denoiser_dx12.dll, "ffxCreateContext");
            denoiser_dx12.DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(denoiser_dx12.dll, "ffxDestroyContext");
            denoiser_dx12.Dispatch =
                (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(denoiser_dx12.dll, "ffxDispatch");
            denoiser_dx12.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(denoiser_dx12.dll, "ffxQuery");
        }

        if (denoiser_dx12_hooked.dll != nullptr && denoiser_dx12_hooked.Configure == nullptr)
        {
            denoiser_dx12_hooked.Configure =
                (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(denoiser_dx12_hooked.dll, "ffxConfigure");
            denoiser_dx12_hooked.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(denoiser_dx12_hooked.dll, "ffxCreateContext");
            denoiser_dx12_hooked.DestroyContext = (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(
                denoiser_dx12_hooked.dll, "ffxDestroyContext");
            denoiser_dx12_hooked.Dispatch =
                (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(denoiser_dx12_hooked.dll, "ffxDispatch");
            denoiser_dx12_hooked.Query =
                (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(denoiser_dx12_hooked.dll, "ffxQuery");
        }
        else if (denoiser_dx12_hooked.Configure == nullptr)
        {
            denoiser_dx12_hooked = denoiser_dx12;
        }

        if (Config::Instance()->EnableFfxInputs.value_or_default() && denoiser_dx12_hooked.dll != nullptr &&
            denoiser_dx12_hooked.CreateContext != nullptr && !denoiser_dx12_hooked.hooked)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (denoiser_dx12_hooked.Configure != nullptr)
                DetourAttach(&(PVOID&) denoiser_dx12_hooked.Configure, ffxConfigure_Dx12);

            if (denoiser_dx12_hooked.CreateContext != nullptr)
                DetourAttach(&(PVOID&) denoiser_dx12_hooked.CreateContext, ffxCreateContext_Dx12);

            if (denoiser_dx12_hooked.DestroyContext != nullptr)
                DetourAttach(&(PVOID&) denoiser_dx12_hooked.DestroyContext, ffxDestroyContext_Dx12);

            if (denoiser_dx12_hooked.Dispatch != nullptr)
                DetourAttach(&(PVOID&) denoiser_dx12_hooked.Dispatch, ffxDispatch_Dx12);

            if (denoiser_dx12_hooked.Query != nullptr)
                DetourAttach(&(PVOID&) denoiser_dx12_hooked.Query, ffxQuery_Dx12);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("DetourTransactionCommit result: {:X}", detourResult);
                denoiser_dx12.Configure = nullptr;
                denoiser_dx12.CreateContext = nullptr;
                denoiser_dx12.DestroyContext = nullptr;
                denoiser_dx12.Dispatch = nullptr;
                denoiser_dx12.Query = nullptr;
            }
            else
            {
                if (denoiser_dx12.dll == denoiser_dx12_hooked.dll)
                    denoiser_dx12 = denoiser_dx12_hooked;

                denoiser_dx12_hooked.hooked = true;
                State::Instance().fsrHooks = true;
            }
        }

        bool loadResult = denoiser_dx12.CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (!loadResult)
            denoiser_dx12.dll = nullptr;

        return loadResult;
    }

    static bool InitFfxDx12_Radiance(HMODULE module = nullptr)
    {
        // if dll already loaded
        if (radiance_dx12.dll != nullptr && radiance_dx12.CreateContext != nullptr)
            return true;

        spdlog::info("");

        // This is module loaded by game, need to hook it to use inputs
        if (module != nullptr && radiance_dx12_hooked.dll == nullptr)
        {
            radiance_dx12.dll = module;
        }

        if (radiance_dx12.dll == nullptr)
        {
            // Try new api first
            std::vector<std::wstring> dllNames = { L"amd_fidelityfx_radiancecache_dx12.dll" };

            auto optiPath = Config::Instance()->MainDllPath.value();

            for (size_t i = 0; i < dllNames.size(); i++)
            {
                LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

                auto overridePath = Config::Instance()->FfxDx12RCPath.value_or(L"");

                if (radiance_dx12_hooked.dll == nullptr)
                {
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &radiance_dx12_hooked.dll,
                                           &radiance_dx12.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &memModule, &radiance_dx12.dll);
                }

                if (radiance_dx12.dll != nullptr)
                {
                    break;
                }
            }
        }

        // Can't find Opti dlls, use just module loaded by game
        if (radiance_dx12.dll == nullptr && radiance_dx12_hooked.dll != nullptr)
        {
            radiance_dx12 = radiance_dx12_hooked;
        }

        if (radiance_dx12.dll != nullptr)
        {
            wchar_t modulePath[MAX_PATH];
            DWORD len = GetModuleFileNameW(denoiser_dx12.dll, modulePath, MAX_PATH);
            radiance_dx12.filePath = std::wstring(modulePath);

            LOG_INFO("Loaded from {}", wstring_to_string(radiance_dx12.filePath));
        }

        if (radiance_dx12.dll != nullptr && radiance_dx12.Configure == nullptr)
        {
            radiance_dx12.Configure =
                (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(radiance_dx12.dll, "ffxConfigure");
            radiance_dx12.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(radiance_dx12.dll, "ffxCreateContext");
            radiance_dx12.DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(radiance_dx12.dll, "ffxDestroyContext");
            radiance_dx12.Dispatch =
                (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(radiance_dx12.dll, "ffxDispatch");
            radiance_dx12.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(radiance_dx12.dll, "ffxQuery");
        }

        if (radiance_dx12_hooked.dll != nullptr && radiance_dx12_hooked.Configure == nullptr)
        {
            radiance_dx12_hooked.Configure =
                (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(radiance_dx12_hooked.dll, "ffxConfigure");
            radiance_dx12_hooked.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(radiance_dx12_hooked.dll, "ffxCreateContext");
            radiance_dx12_hooked.DestroyContext = (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(
                radiance_dx12_hooked.dll, "ffxDestroyContext");
            radiance_dx12_hooked.Dispatch =
                (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(radiance_dx12_hooked.dll, "ffxDispatch");
            radiance_dx12_hooked.Query =
                (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(radiance_dx12_hooked.dll, "ffxQuery");
        }
        else if (radiance_dx12_hooked.Configure == nullptr)
        {
            radiance_dx12_hooked = radiance_dx12;
        }

        if (Config::Instance()->EnableFfxInputs.value_or_default() && radiance_dx12_hooked.dll != nullptr &&
            radiance_dx12_hooked.CreateContext != nullptr && !radiance_dx12_hooked.hooked)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (radiance_dx12_hooked.Configure != nullptr)
                DetourAttach(&(PVOID&) radiance_dx12_hooked.Configure, ffxConfigure_Dx12);

            if (radiance_dx12_hooked.CreateContext != nullptr)
                DetourAttach(&(PVOID&) radiance_dx12_hooked.CreateContext, ffxCreateContext_Dx12);

            if (radiance_dx12_hooked.DestroyContext != nullptr)
                DetourAttach(&(PVOID&) radiance_dx12_hooked.DestroyContext, ffxDestroyContext_Dx12);

            if (radiance_dx12_hooked.Dispatch != nullptr)
                DetourAttach(&(PVOID&) radiance_dx12_hooked.Dispatch, ffxDispatch_Dx12);

            if (radiance_dx12_hooked.Query != nullptr)
                DetourAttach(&(PVOID&) radiance_dx12_hooked.Query, ffxQuery_Dx12);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("DetourTransactionCommit result: {:X}", detourResult);
                radiance_dx12.Configure = nullptr;
                radiance_dx12.CreateContext = nullptr;
                radiance_dx12.DestroyContext = nullptr;
                radiance_dx12.Dispatch = nullptr;
                radiance_dx12.Query = nullptr;
            }
            else
            {
                if (radiance_dx12.dll == radiance_dx12_hooked.dll)
                    radiance_dx12 = radiance_dx12_hooked;

                radiance_dx12_hooked.hooked = true;
                State::Instance().fsrHooks = true;
            }
        }

        bool loadResult = radiance_dx12.CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (!loadResult)
            radiance_dx12.dll = nullptr;

        return loadResult;
    }

    static feature_version VersionDx12()
    {
        if (main_dx12.version.major == 0 && main_dx12.Query != nullptr /* && device != nullptr*/)
        {
            ffxQueryDescGetVersions versionQuery {};
            versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            uint64_t versionCount = 0;
            versionQuery.outputCount = &versionCount;

            auto queryResult = main_dx12.Query(nullptr, &versionQuery.header);

            // get number of versions for allocation
            if (versionCount > 0 && queryResult == FFX_API_RETURN_OK)
            {

                std::vector<uint64_t> versionIds;
                std::vector<const char*> versionNames;
                versionIds.resize(versionCount);
                versionNames.resize(versionCount);
                versionQuery.versionIds = versionIds.data();
                versionQuery.versionNames = versionNames.data();

                // fill version ids and names arrays.
                queryResult = main_dx12.Query(nullptr, &versionQuery.header);

                if (queryResult == FFX_API_RETURN_OK)
                {
                    parse_version(versionNames[0], &main_dx12.version);
                    LOG_INFO("FfxApi Dx12 version: {}.{}.{}", main_dx12.version.major, main_dx12.version.minor,
                             main_dx12.version.patch);
                }
                else
                {
                    LOG_WARN("main_dx12.Query 2 result: {}", (UINT) queryResult);
                }
            }
            else
            {
                LOG_WARN("main_dx12.Query result: {}", (UINT) queryResult);
            }
        }

        if (main_dx12.version.major == 0 && upscaling_dx12.Query != nullptr)
            main_dx12.version = VersionDx12_SR();

        if (main_dx12.version.major == 0 && fg_dx12.Query != nullptr)
            main_dx12.version = VersionDx12_FG();

        if (denoiser_dx12.version.major == 0 && denoiser_dx12.Query != nullptr)
            denoiser_dx12.version = VersionDx12_RR();

        if (radiance_dx12.version.major == 0 && radiance_dx12.Query != nullptr)
            radiance_dx12.version = VersionDx12_RC();

        return main_dx12.version;
    }

    static feature_version VersionDx12_SR()
    {
        if (upscaling_dx12.Query == nullptr)
            return VersionDx12();

        if (upscaling_dx12.version.major == 0)
        {
            ffxQueryDescGetVersions versionQuery {};
            versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            uint64_t versionCount = 0;
            versionQuery.outputCount = &versionCount;

            auto queryResult = upscaling_dx12.Query(nullptr, &versionQuery.header);

            // get number of versions for allocation
            if (versionCount > 0 && queryResult == FFX_API_RETURN_OK)
            {

                std::vector<uint64_t> versionIds;
                std::vector<const char*> versionNames;
                versionIds.resize(versionCount);
                versionNames.resize(versionCount);
                versionQuery.versionIds = versionIds.data();
                versionQuery.versionNames = versionNames.data();

                // fill version ids and names arrays.
                queryResult = upscaling_dx12.Query(nullptr, &versionQuery.header);

                if (queryResult == FFX_API_RETURN_OK)
                {
                    parse_version(versionNames[0], &upscaling_dx12.version);
                    LOG_INFO("FfxApi Dx12 SR version: {}.{}.{}", upscaling_dx12.version.major,
                             upscaling_dx12.version.minor, upscaling_dx12.version.patch);
                }
                else
                {
                    LOG_WARN("main_dx12.Query 2 result: {}", (UINT) queryResult);
                }
            }
            else
            {
                LOG_WARN("main_dx12.Query result: {}", (UINT) queryResult);
            }
        }

        return upscaling_dx12.version;
    }

    static feature_version VersionDx12_FG()
    {
        if (fg_dx12.Query == nullptr)
            return VersionDx12();

        if (fg_dx12.version.major == 0)
        {
            ffxQueryDescGetVersions versionQuery {};
            versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
            uint64_t versionCount = 0;
            versionQuery.outputCount = &versionCount;

            auto queryResult = fg_dx12.Query(nullptr, &versionQuery.header);

            // get number of versions for allocation
            if (versionCount > 0 && queryResult == FFX_API_RETURN_OK)
            {

                std::vector<uint64_t> versionIds;
                std::vector<const char*> versionNames;
                versionIds.resize(versionCount);
                versionNames.resize(versionCount);
                versionQuery.versionIds = versionIds.data();
                versionQuery.versionNames = versionNames.data();

                // fill version ids and names arrays.
                queryResult = fg_dx12.Query(nullptr, &versionQuery.header);

                if (queryResult == FFX_API_RETURN_OK)
                {
                    parse_version(versionNames[0], &fg_dx12.version);
                    LOG_INFO("FfxApi Dx12 FG version: {}.{}.{}", fg_dx12.version.major, fg_dx12.version.minor,
                             fg_dx12.version.patch);
                }
                else
                {
                    LOG_WARN("main_dx12.Query 2 result: {}", (UINT) queryResult);
                }
            }
            else
            {
                LOG_WARN("main_dx12.Query result: {}", (UINT) queryResult);
            }
        }

        return fg_dx12.version;
    }

    static feature_version VersionDx12_RR()
    {
        if (denoiser_dx12.Query == nullptr)
            return VersionDx12();

        if (denoiser_dx12.version.major == 0)
        {
            ffxQueryDescGetVersions versionQuery {};
            versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            uint64_t versionCount = 0;
            versionQuery.outputCount = &versionCount;

            auto queryResult = denoiser_dx12.Query(nullptr, &versionQuery.header);

            // get number of versions for allocation
            if (versionCount > 0 && queryResult == FFX_API_RETURN_OK)
            {

                std::vector<uint64_t> versionIds;
                std::vector<const char*> versionNames;
                versionIds.resize(versionCount);
                versionNames.resize(versionCount);
                versionQuery.versionIds = versionIds.data();
                versionQuery.versionNames = versionNames.data();

                // fill version ids and names arrays.
                queryResult = denoiser_dx12.Query(nullptr, &versionQuery.header);

                if (queryResult == FFX_API_RETURN_OK)
                {
                    parse_version(versionNames[0], &denoiser_dx12.version);
                    LOG_INFO("FfxApi Dx12 SR version: {}.{}.{}", denoiser_dx12.version.major,
                             denoiser_dx12.version.minor, denoiser_dx12.version.patch);
                }
                else
                {
                    LOG_WARN("main_dx12.Query 2 result: {}", (UINT) queryResult);
                }
            }
            else
            {
                LOG_WARN("main_dx12.Query result: {}", (UINT) queryResult);
            }
        }

        return denoiser_dx12.version;
    }

    static feature_version VersionDx12_RC()
    {
        if (radiance_dx12.Query == nullptr)
            return VersionDx12();

        if (radiance_dx12.version.major == 0)
        {
            ffxQueryDescGetVersions versionQuery {};
            versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            uint64_t versionCount = 0;
            versionQuery.outputCount = &versionCount;

            auto queryResult = radiance_dx12.Query(nullptr, &versionQuery.header);

            // get number of versions for allocation
            if (versionCount > 0 && queryResult == FFX_API_RETURN_OK)
            {

                std::vector<uint64_t> versionIds;
                std::vector<const char*> versionNames;
                versionIds.resize(versionCount);
                versionNames.resize(versionCount);
                versionQuery.versionIds = versionIds.data();
                versionQuery.versionNames = versionNames.data();

                // fill version ids and names arrays.
                queryResult = radiance_dx12.Query(nullptr, &versionQuery.header);

                if (queryResult == FFX_API_RETURN_OK)
                {
                    parse_version(versionNames[0], &radiance_dx12.version);
                    LOG_INFO("FfxApi Dx12 SR version: {}.{}.{}", radiance_dx12.version.major,
                             radiance_dx12.version.minor, radiance_dx12.version.patch);
                }
                else
                {
                    LOG_WARN("main_dx12.Query 2 result: {}", (UINT) queryResult);
                }
            }
            else
            {
                LOG_WARN("main_dx12.Query result: {}", (UINT) queryResult);
            }
        }

        return radiance_dx12.version;
    }

    static ffxReturnCode_t D3D12_CreateContext(ffxContext* context, ffxCreateContextDescHeader* desc,
                                               const ffxAllocationCallbacks* memCb)
    {
        auto type = GetType(desc->type);
        auto isFg = type == FFXStructType::FG || type == FFXStructType::SwapchainDX12;

        ffxReturnCode_t result = FFX_API_RETURN_ERROR;

        if (isFg && fg_dx12.dll != nullptr)
        {
            LOG_DEBUG("Creating with fg_dx12");
            result = fg_dx12.CreateContext(context, desc, memCb);
            contextToType[*context] = type;
            LOG_DEBUG("Created with fg_dx12: {:X}", (size_t) *context);
            return result;
        }
        else if (type == FFXStructType::Upscaling && upscaling_dx12.dll != nullptr)
        {
            LOG_DEBUG("Creating with upscaling_dx12");
            result = upscaling_dx12.CreateContext(context, desc, memCb);
            contextToType[*context] = type;
            LOG_DEBUG("Created with upscaling_dx12: {:X}", (size_t) *context);
            return result;
        }
        else if (type == FFXStructType::Denoiser && denoiser_dx12.dll != nullptr)
        {
            LOG_DEBUG("Creating with denoiser_dx12");
            result = denoiser_dx12.CreateContext(context, desc, memCb);
            contextToType[*context] = type;
            LOG_DEBUG("Created with denoiser_dx12: {:X}", (size_t) *context);
            return result;
        }
        else if (type == FFXStructType::RadianceCache && radiance_dx12.dll != nullptr)
        {
            LOG_DEBUG("Creating with radiance_dx12");
            result = radiance_dx12.CreateContext(context, desc, memCb);
            contextToType[*context] = type;
            LOG_DEBUG("Created with radiance_dx12: {:X}", (size_t) *context);
            return result;
        }

        const auto skipFG = isFg && fg_dx12.skipQueryCalls;
        const auto skipUpscaling = type == FFXStructType::Upscaling && upscaling_dx12.skipQueryCalls;

        if (main_dx12.dll != nullptr && !skipFG && !skipUpscaling)
        {
            LOG_DEBUG("Creating with main_dx12");

            if (isFg)
                fg_dx12.skipCreateCalls = true;
            else if (type == FFXStructType::Upscaling)
                upscaling_dx12.skipCreateCalls = true;

            result = main_dx12.CreateContext(context, desc, memCb);

            if (isFg)
                fg_dx12.skipCreateCalls = false;
            else if (type == FFXStructType::Upscaling)
                upscaling_dx12.skipCreateCalls = false;

            return result;
        }

        return FFX_API_RETURN_NO_PROVIDER;
    }

    static ffxReturnCode_t D3D12_DestroyContext(ffxContext* context, const ffxAllocationCallbacks* memCb)
    {
        ffxReturnCode_t result = FFX_API_RETURN_ERROR;
        auto type = FFXStructType::Unknown;

        if (contextToType.contains(*context))
        {
            type = contextToType[*context];
            LOG_DEBUG("Found context type mapping: {}", magic_enum::enum_name(type));
            contextToType.erase(*context);
        }
        else
        {
            LOG_DEBUG("No context type mapping found, defaulting to Unknown");
        }

        switch (type)
        {
        case FFXStructType::General:
            LOG_DEBUG("Destroying with main_dx12");
            if (main_dx12.dll != nullptr)
                result = main_dx12.DestroyContext(context, memCb);
            break;

        case FFXStructType::Upscaling:
            LOG_DEBUG("Destroying with upscaling_dx12");
            if (upscaling_dx12.dll != nullptr)
                result = upscaling_dx12.DestroyContext(context, memCb);
            break;

        case FFXStructType::FG:
        case FFXStructType::SwapchainDX12:
            LOG_DEBUG("Destroying with fg_dx12");
            if (fg_dx12.dll != nullptr)
                result = fg_dx12.DestroyContext(context, memCb);
            break;

        case FFXStructType::Denoiser:
            LOG_DEBUG("Destroying with denoiser_dx12");
            if (denoiser_dx12.dll != nullptr)
                result = denoiser_dx12.DestroyContext(context, memCb);
            break;

        case FFXStructType::RadianceCache:
            LOG_DEBUG("Destroying with radiance_dx12");
            if (radiance_dx12.dll != nullptr)
                result = radiance_dx12.DestroyContext(context, memCb);
            break;

        default:
            break;
        }

        if (result == FFX_API_RETURN_OK)
        {
            LOG_DEBUG("Destroyed with mapped module");
            return result;
        }

        if (main_dx12.dll != nullptr && !_skipDestroyCalls)
        {
            LOG_DEBUG("Destroying with main_dx12");
            _skipDestroyCalls = true;
            result = main_dx12.DestroyContext(context, memCb);
            _skipDestroyCalls = false;
        }

        if (result == FFX_API_RETURN_OK)
        {
            LOG_DEBUG("Destroyed with main_dx12");
            return result;
        }

        if (upscaling_dx12.dll != nullptr)
        {
            LOG_DEBUG("Destroying with upscaling_dx12");
            result = upscaling_dx12.DestroyContext(context, memCb);
        }

        if (result == FFX_API_RETURN_OK)
        {
            LOG_DEBUG("Destroyed with upscaling_dx12");
            return result;
        }

        if (fg_dx12.dll != nullptr)
        {
            LOG_DEBUG("Destroying with fg_dx12");
            result = fg_dx12.DestroyContext(context, memCb);
        }

        if (result == FFX_API_RETURN_OK)
        {
            LOG_DEBUG("Destroyed with fg_dx12");
            return result;
        }

        LOG_ERROR("Failed to destroy context in any module");
        return FFX_API_RETURN_NO_PROVIDER;
    }

    static ffxReturnCode_t D3D12_Configure(ffxContext* context, const ffxConfigureDescHeader* desc)
    {
        auto type = GetType(desc->type);

        if (type == FFXStructType::General && contextToType.contains(*context))
            type = contextToType[*context];

        auto isFg = type == FFXStructType::FG || type == FFXStructType::SwapchainDX12;

        if (isFg && fg_dx12.dll != nullptr)
            return fg_dx12.Configure(context, desc);
        else if (type == FFXStructType::Upscaling && upscaling_dx12.dll != nullptr)
            return upscaling_dx12.Configure(context, desc);
        else if (type == FFXStructType::Denoiser && denoiser_dx12.dll != nullptr)
            return denoiser_dx12.Configure(context, desc);
        else if (type == FFXStructType::RadianceCache && radiance_dx12.dll != nullptr)
            return radiance_dx12.Configure(context, desc);

        const auto skipFG = isFg && fg_dx12.skipQueryCalls;
        const auto skipUpscaling = type == FFXStructType::Upscaling && upscaling_dx12.skipQueryCalls;

        if (main_dx12.dll != nullptr && !skipFG && !skipUpscaling)
        {
            if (isFg)
                fg_dx12.skipConfigureCalls = true;
            else if (type == FFXStructType::Upscaling)
                upscaling_dx12.skipConfigureCalls = true;

            auto result = main_dx12.Configure(context, desc);

            if (isFg)
                fg_dx12.skipConfigureCalls = false;
            else if (type == FFXStructType::Upscaling)
                upscaling_dx12.skipConfigureCalls = false;

            return result;
        }

        return FFX_API_RETURN_NO_PROVIDER;
    }

    static ffxReturnCode_t D3D12_Query(ffxContext* context, ffxQueryDescHeader* desc)
    {
        auto type = GetIndirectType(desc);
        auto isFg = type == FFXStructType::FG || type == FFXStructType::SwapchainDX12;

        if (isFg && fg_dx12.dll != nullptr)
            return fg_dx12.Query(context, desc);
        else if (type == FFXStructType::Upscaling && upscaling_dx12.dll != nullptr)
            return upscaling_dx12.Query(context, desc);
        else if (type == FFXStructType::Denoiser && denoiser_dx12.dll != nullptr)
            return denoiser_dx12.Query(context, desc);
        else if (type == FFXStructType::RadianceCache && radiance_dx12.dll != nullptr)
            return radiance_dx12.Query(context, desc);

        const auto skipFG = isFg && fg_dx12.skipQueryCalls;
        const auto skipUpscaling = type == FFXStructType::Upscaling && upscaling_dx12.skipQueryCalls;

        if (main_dx12.dll != nullptr && !skipFG && !skipUpscaling)
        {
            if (isFg)
                fg_dx12.skipQueryCalls = true;
            else if (type == FFXStructType::Upscaling)
                upscaling_dx12.skipQueryCalls = true;

            auto result = main_dx12.Query(context, desc);

            if (isFg)
                fg_dx12.skipQueryCalls = false;
            else if (type == FFXStructType::Upscaling)
                upscaling_dx12.skipQueryCalls = false;

            return result;
        }

        return FFX_API_RETURN_NO_PROVIDER;
    }

    static ffxReturnCode_t D3D12_Dispatch(ffxContext* context, const ffxDispatchDescHeader* desc)
    {
        auto type = GetType(desc->type);
        auto isFg = type == FFXStructType::FG || type == FFXStructType::SwapchainDX12;

        if (isFg && fg_dx12.dll != nullptr)
            return fg_dx12.Dispatch(context, desc);
        else if (type == FFXStructType::Upscaling && upscaling_dx12.dll != nullptr)
            return upscaling_dx12.Dispatch(context, desc);
        else if (type == FFXStructType::Denoiser && denoiser_dx12.dll != nullptr)
            return denoiser_dx12.Dispatch(context, desc);
        else if (type == FFXStructType::RadianceCache && radiance_dx12.dll != nullptr)
            return radiance_dx12.Dispatch(context, desc);

        const auto skipFG = isFg && fg_dx12.skipQueryCalls;
        const auto skipUpscaling = type == FFXStructType::Upscaling && upscaling_dx12.skipQueryCalls;

        if (main_dx12.dll != nullptr && !skipFG && !skipUpscaling)
        {
            if (isFg)
                fg_dx12.skipDispatchCalls = true;
            else if (type == FFXStructType::Upscaling)
                upscaling_dx12.skipDispatchCalls = true;

            auto result = main_dx12.Dispatch(context, desc);

            if (isFg)
                fg_dx12.skipDispatchCalls = false;
            else if (type == FFXStructType::Upscaling)
                upscaling_dx12.skipDispatchCalls = false;

            return result;
        }

        return FFX_API_RETURN_NO_PROVIDER;
    }

    static HMODULE VkModule() { return main_vk.dll; }
    static std::wstring VkModule_Path() { return main_vk.filePath; }

    static bool InitFfxVk(HMODULE module = nullptr)
    {
        // if dll already loaded
        if (main_vk.dll != nullptr && main_vk.CreateContext != nullptr)
            return true;

        spdlog::info("");

        if (module != nullptr)
            main_vk.dll = module;

        if (main_vk.dll == nullptr)
        {
            // Try new api first
            std::vector<std::wstring> dllNames = { L"amd_fidelityfx_vk.dll" };

            auto optiPath = Config::Instance()->MainDllPath.value();

            for (size_t i = 0; i < dllNames.size(); i++)
            {
                LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

                auto overridePath = Config::Instance()->FfxVkPath.value_or(L"");

                if (main_vk_hooked.dll == nullptr)
                {
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &main_vk_hooked.dll, &main_vk.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &memModule, &main_vk.dll);
                }

                if (main_vk.dll != nullptr)
                {
                    break;
                }
            }
        }

        if (main_vk.dll != nullptr)
        {
            wchar_t modulePath[MAX_PATH];
            DWORD len = GetModuleFileNameW(main_vk.dll, modulePath, MAX_PATH);
            main_vk.filePath = std::wstring(modulePath);

            LOG_INFO("Loaded from {}", wstring_to_string(main_vk.filePath));
        }

        if (main_vk.dll != nullptr && main_vk.CreateContext == nullptr)
        {
            main_vk.Configure = (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(main_vk.dll, "ffxConfigure");
            main_vk.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(main_vk.dll, "ffxCreateContext");
            main_vk.DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(main_vk.dll, "ffxDestroyContext");
            main_vk.Dispatch = (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(main_vk.dll, "ffxDispatch");
            main_vk.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(main_vk.dll, "ffxQuery");

            if (Config::Instance()->EnableFfxInputs.value_or_default() && main_vk.CreateContext != nullptr)
            {
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                if (main_vk.Configure != nullptr)
                    DetourAttach(&(PVOID&) main_vk.Configure, ffxConfigure_Vk);

                if (main_vk.CreateContext != nullptr)
                    DetourAttach(&(PVOID&) main_vk.CreateContext, ffxCreateContext_Vk);

                if (main_vk.DestroyContext != nullptr)
                    DetourAttach(&(PVOID&) main_vk.DestroyContext, ffxDestroyContext_Vk);

                if (main_vk.Dispatch != nullptr)
                    DetourAttach(&(PVOID&) main_vk.Dispatch, ffxDispatch_Vk);

                if (main_vk.Query != nullptr)
                    DetourAttach(&(PVOID&) main_vk.Query, ffxQuery_Vk);

                auto detourResult = DetourTransactionCommit();
                if (detourResult != NO_ERROR)
                {
                    LOG_ERROR("DetourTransactionCommit result: {:X}", detourResult);
                    main_vk.Configure = nullptr;
                    main_vk.CreateContext = nullptr;
                    main_vk.DestroyContext = nullptr;
                    main_vk.Dispatch = nullptr;
                    main_vk.Query = nullptr;
                }
                else
                {
                    State::Instance().fsrHooks = true;
                }
            }
        }

        bool loadResult = main_vk.CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (loadResult)
            VersionVk();
        else
            main_vk.dll = nullptr;

        return loadResult;
    }

    static feature_version VersionVk()
    {
        if (main_vk.version.major == 0 && main_vk.Query != nullptr)
        {
            ffxQueryDescGetVersions versionQuery {};
            versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            uint64_t versionCount = 0;
            versionQuery.outputCount = &versionCount;

            auto queryResult = main_vk.Query(nullptr, &versionQuery.header);

            // get number of versions for allocation
            if (versionCount > 0 && queryResult == FFX_API_RETURN_OK)
            {

                std::vector<uint64_t> versionIds;
                std::vector<const char*> versionNames;
                versionIds.resize(versionCount);
                versionNames.resize(versionCount);
                versionQuery.versionIds = versionIds.data();
                versionQuery.versionNames = versionNames.data();

                queryResult = main_vk.Query(nullptr, &versionQuery.header);

                if (queryResult == FFX_API_RETURN_OK)
                {
                    parse_version(versionNames[0], &main_vk.version);
                    LOG_INFO("FfxApi Vulkan version: {}.{}.{}", main_vk.version.major, main_vk.version.minor,
                             main_vk.version.patch);
                }
                else
                {
                    LOG_WARN("main_vk.Query 2 result: {}", (UINT) queryResult);
                }
            }
            else
            {
                LOG_WARN("main_vk.Query result: {}", (UINT) queryResult);
            }
        }

        return main_vk.version;
    }

    static PfnFfxCreateContext VULKAN_CreateContext() { return main_vk.CreateContext; }
    static PfnFfxDestroyContext VULKAN_DestroyContext() { return main_vk.DestroyContext; }
    static PfnFfxConfigure VULKAN_Configure() { return main_vk.Configure; }
    static PfnFfxQuery VULKAN_Query() { return main_vk.Query; }
    static PfnFfxDispatch VULKAN_Dispatch() { return main_vk.Dispatch; }

    static std::string ReturnCodeToString(ffxReturnCode_t result)
    {
        switch (result)
        {
        case FFX_API_RETURN_OK:
            return "The operation was successful.";
        case FFX_API_RETURN_ERROR:
            return "An error occurred that is not further specified.";
        case FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE:
            return "The structure type given was not recognized for the function or context with which it was used. "
                   "This is likely a programming error.";
        case FFX_API_RETURN_ERROR_RUNTIME_ERROR:
            return "The underlying runtime (e.g. D3D12, Vulkan) or effect returned an error code.";
        case FFX_API_RETURN_NO_PROVIDER:
            return "No provider was found for the given structure type. This is likely a programming error.";
        case FFX_API_RETURN_ERROR_MEMORY:
            return "A memory allocation failed.";
        case FFX_API_RETURN_ERROR_PARAMETER:
            return "A parameter was invalid, e.g. a null pointer, empty resource or out-of-bounds enum value.";
        default:
            return "Unknown";
        }
    }

    static std::string GetTypeName(ffxStructType_t type)
    {
        switch ((uint64_t) type)
        {
        // General
        case 1u:
            return std::format("CONFIGURE_DESC_TYPE_GLOBALDEBUG1 ({:X})", type);

        case 2u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12 ({:X})", type);

        case 3u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_BACKEND_VK ({:X})", type);

        case 4u:
            return std::format("QUERY_DESC_TYPE_GET_VERSIONS ({:X})", type);

        case 5u:
            return std::format("DESC_TYPE_OVERRIDE_VERSION ({:X})", type);

        case 6u:
            return std::format("QUERY_DESC_TYPE_GET_PROVIDER_VERSION ({:X})", type);

        // Upscaling
        case 0x00010000u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_UPSCALE ({:X})", type);

        case 0x00010001u:
            return std::format("DISPATCH_DESC_TYPE_UPSCALE ({:X})", type);

        case 0x00010002u:
            return std::format("QUERY_DESC_TYPE_UPSCALE_GETUPSCALERATIOFROMQUALITYMODE ({:X})", type);

        case 0x00010003u:
            return std::format("QUERY_DESC_TYPE_UPSCALE_GETRENDERRESOLUTIONFROMQUALITYMODE ({:X})", type);

        case 0x00010004u:
            return std::format("QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT ({:X})", type);

        case 0x00010005u:
            return std::format("QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET ({:X})", type);

        case 0x00010006u:
            return std::format("DISPATCH_DESC_TYPE_UPSCALE_GENERATEREACTIVEMASK ({:X})", type);

        case 0x00010007u:
            return std::format("CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE ({:X})", type);

        case 0x00010008u:
            return std::format("QUERY_DESC_TYPE_UPSCALE_GPU_MEMORY_USAGE ({:X})", type);

        case 0x00010009u:
            return std::format("QUERY_DESC_TYPE_UPSCALE_GPU_MEMORY_USAGE_V2 ({:X})", type);

        case 0x0001000au:
            return std::format("QUERY_DESC_TYPE_UPSCALE_GET_RESOURCE_REQUIREMENTS ({:X})", type);

        // FG
        case 0x00020001u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION ({:X})", type);

        case 0x00020005u:
            return std::format("CALLBACK_DESC_TYPE_FRAMEGENERATION_PRESENT ({:X})", type);

        case 0x00020003u:
            return std::format("DISPATCH_DESC_TYPE_FRAMEGENERATION ({:X})", type);

        case 0x00020002u:
            return std::format("CONFIGURE_DESC_TYPE_FRAMEGENERATION ({:X})", type);

        case 0x00020004u:
            return std::format("DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE ({:X})", type);

        case 0x00020006u:
            return std::format("CONFIGURE_DESC_TYPE_FRAMEGENERATION_KEYVALUE ({:X})", type);

        case 0x00020007u:
            return std::format("QUERY_DESC_TYPE_FRAMEGENERATION_GPU_MEMORY_USAGE ({:X})", type);

        case 0x00020008u:
            return std::format("CONFIGURE_DESC_TYPE_FRAMEGENERATION_REGISTERDISTORTIONRESOURCE ({:X})", type);

        case 0x00020009u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_HUDLESS ({:X})", type);

        case 0x0002000au:
            return std::format("DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO ({:X})", type);

        case 0x0002000bu:
            return std::format("QUERY_DESC_TYPE_FRAMEGENERATION_GPU_MEMORY_USAGE_V2 ({:X})", type);

        case 0x0002000cu:
            return std::format("DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2 ({:X})", type);

        // DX12 Swapchain
        case 0x00030001u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WRAP_DX12 ({:X})", type);

        case 0x00030005u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_NEW_DX12 ({:X})", type);

        case 0x00030006u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12 ({:X})", type);

        case 0x00030002u:
            return std::format("CONFIGURE_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_REGISTERUIRESOURCE_DX12 ({:X})", type);

        case 0x00030003u:
            return std::format("QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_INTERPOLATIONCOMMANDLIST_DX12 ({:X})", type);

        case 0x00030004u:
            return std::format("QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_INTERPOLATIONTEXTURE_DX12 ({:X})", type);

        case 0x00030007u:
            return std::format("DISPATCH_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WAIT_FOR_PRESENTS_DX12 ({:X})", type);

        case 0x00030008u:
            return std::format("CONFIGURE_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_KEYVALUE_DX12 ({:X})", type);

        case 0x00030009u:
            return std::format("QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_GPU_MEMORY_USAGE_DX12 ({:X})", type);

        case 0x0003000au:
            return std::format("QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_GPU_MEMORY_USAGE_DX12_V2 ({:X})", type);

        // Vulkan Swapchain
        case 0x00040001u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK ({:X})", type);

        case 0x00040002u:
            return std::format("CONFIGURE_DESC_TYPE_FGSWAPCHAIN_REGISTERUIRESOURCE_VK ({:X})", type);

        case 0x00040003u:
            return std::format("QUERY_DESC_TYPE_FGSWAPCHAIN_INTERPOLATIONCOMMANDLIST_VK ({:X})", type);

        case 0x00040004u:
            return std::format("QUERY_DESC_TYPE_FGSWAPCHAIN_INTERPOLATIONTEXTURE_VK ({:X})", type);

        case 0x00040007u:
            return std::format("DISPATCH_DESC_TYPE_FGSWAPCHAIN_WAIT_FOR_PRESENTS_VK ({:X})", type);

        case 0x00040008u:
            return std::format("CONFIGURE_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_KEYVALUE_VK ({:X})", type);

        case 0x00040009u:
            return std::format("QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_GPU_MEMORY_USAGE_VK ({:X})", type);

        case 0x00040005u:
            return std::format("QUERY_DESC_TYPE_FGSWAPCHAIN_FUNCTIONS_VK ({:X})", type);

        case 0x00040010u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_MODE_VK ({:X})", type);

        // Denoiser
        case 0x00050001u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_DENOISER ({:X})", type);

        case 0x00050002u:
            return std::format("DISPATCH_DESC_TYPE_DENOISER ({:X})", type);

        case 0x00050003u:
            return std::format("DISPATCH_DESC_INPUT_4_SIGNALS_TYPE_DENOISER ({:X})", type);

        case 0x00050004u:
            return std::format("DISPATCH_DESC_INPUT_2_SIGNALS_TYPE_DENOISER ({:X})", type);

        case 0x00050005u:
            return std::format("DISPATCH_DESC_INPUT_1_SIGNAL_TYPE_DENOISER ({:X})", type);

        case 0x00050007u:
            return std::format("DISPATCH_DESC_INPUT_DOMINANT_LIGHT_TYPE_DENOISER ({:X})", type);

        case 0x00050008u:
            return std::format("CONFIGURE_DESC_TYPE_DENOISER_SETTINGS ({:X})", type);

        case 0x00050009u:
            return std::format("QUERY_DESC_TYPE_DENOISER_GPU_MEMORY_USAGE ({:X})", type);

        case 0x0005000au:
            return std::format("QUERY_DESC_TYPE_DENOISER_GET_VERSION ({:X})", type);

        case 0x0005000bu:
            return std::format("QUERY_DESC_TYPE_DENOISER_GET_DEFAULT_SETTINGS ({:X})", type);

        // Radiance Cache
        case 0x00060002u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_RADIANCECACHE ({:X})", type);

        case 0x00060004u:
            return std::format("DISPATCH_DESC_TYPE_RADIANCECACHE ({:X})", type);

        case 0x00060005u:
            return std::format("DEBUG_DISPATCH_DESC_TYPE_RADIANCECACHE ({:X})", type);
        }

        return std::format("??? ({:X})", type);
    }
};
