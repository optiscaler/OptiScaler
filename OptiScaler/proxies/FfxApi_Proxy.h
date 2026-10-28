#pragma once

#include "SysUtils.h"
#include "Util.h"
#include "Config.h"
#include "Logger.h"

#include <proxies/Ntdll_Proxy.h>
#include <proxies/KernelBase_Proxy.h>

#include <inputs/FfxApi_Dx12.h>
#include <inputs/FfxApi_Vk.h>

#include <fsr4/FSR4DriverProviderDx12.h>
#include <fsr4/FSR4DriverProviderVk.h>
#include <fsr4/FSR4Models.h>
#include <fsr4/FSR4Presets.h>
#include <upscalers/CustomUpscalers.h>

#include "ffx_api.h"
#include "FfxModule.h"
#include <detours/detours.h>
#include <ffx_framegeneration.h>
#include <ffx_upscale.h>

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
#include <imgui/ImGuiNotify.hpp>

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

// Upscaler dll from custom/<name>/ffx or ffx_vk, only used by Opti and never hooked for inputs
struct CustomFfxModule
{
    std::string name;
    FfxModule module;
    State::FfxVersionList versions;
};

class FfxApiProxy
{
  private:
    inline static FfxModule main_dx12;
    inline static FfxModule upscaling_dx12;
    inline static FfxModule fg_dx12;
    inline static FfxModule denoiser_dx12;
    inline static FfxModule radiance_dx12;
    inline static FfxModule main_vk;
    inline static FfxModule upscaling_vk;
    inline static FfxModule fg_vk;

    // amd_fidelityfx_vk.dll next to the loader, for the effects whose SDK 2.x dll is missing
    inline static FfxModule legacy_vk;

    inline static FfxModule main_dx12_hooked;
    inline static FfxModule upscaling_dx12_hooked;
    inline static FfxModule fg_dx12_hooked;
    inline static FfxModule denoiser_dx12_hooked;
    inline static FfxModule radiance_dx12_hooked;
    inline static FfxModule main_vk_hooked;
    inline static FfxModule upscaling_vk_hooked;
    inline static FfxModule fg_vk_hooked;

    inline static ankerl::unordered_dense::map<ffxContext, FFXStructType> contextToType;

    // Vulkan contexts made by the driver, later calls with them go to the same module
    inline static std::mutex vkDriverMutex;
    inline static ankerl::unordered_dense::map<ffxContext, FfxModule*> vkDriverContexts;

    // std::map so the pointers handed out stay valid
    inline static std::map<std::string, CustomFfxModule> custom_dx12;
    inline static std::map<std::string, CustomFfxModule> custom_vk;
    inline static std::recursive_mutex customMutex;
    inline static ankerl::unordered_dense::map<ffxContext, FfxModule*> customContexts;

    static FfxModule* CustomContextModule(ffxContext* context)
    {
        if (context == nullptr)
            return nullptr;

        std::scoped_lock lock(customMutex);
        auto it = customContexts.find(*context);
        return it != customContexts.end() ? it->second : nullptr;
    }

    // Custom upscaler dlls can have the names of the SDK dlls, so looking one up by name can find them instead
    static bool IsCustomModule(HMODULE module)
    {
        if (module == nullptr)
            return false;

        std::scoped_lock lock(customMutex);
        auto matches = [module](const auto& custom) { return custom.second.module.dll == module; };
        return std::ranges::any_of(custom_dx12, matches) || std::ranges::any_of(custom_vk, matches);
    }

    // Util::LoadProxyLibrary that never returns a custom upscaler dll, those can't be hooked for inputs
    static void LoadSdkLibrary(const std::wstring& name, const std::wstring& optiPath, const std::wstring& overridePath,
                               HMODULE* memoryModule, HMODULE* loadedModule)
    {
        Util::LoadProxyLibrary(name, optiPath, overridePath, memoryModule, loadedModule);

        if (IsCustomModule(*memoryModule))
        {
            LOG_DEBUG("Ignoring {} in memory, it's a custom upscaler", wstring_to_string(name));
            *memoryModule = nullptr;
        }

        if (IsCustomModule(*loadedModule))
        {
            LOG_DEBUG("Ignoring loaded {}, it's a custom upscaler", wstring_to_string(name));
            *loadedModule = nullptr;
        }
    }

    inline static bool _skipDestroyCalls = false;

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

    // FFX modules that Opti creates its upscalers with, custom ones included
    static bool IsUpscalerModule(HMODULE module)
    {
        return module != nullptr &&
               (module == main_dx12.dll || module == upscaling_dx12.dll || IsCustomUpscalerModule(module));
    }

    static bool IsCustomUpscalerModule(HMODULE module)
    {
        if (module == nullptr)
            return false;

        std::scoped_lock lock(customMutex);
        return std::ranges::any_of(custom_dx12,
                                   [module](const auto& custom) { return custom.second.module.dll == module; });
    }

    // Module that runs the DX12 upscaler with this version id
    static HMODULE UpscalerModule(uint64_t versionId)
    {
        if (versionId != 0 && versionId == FSR4DriverProviderDx12::VersionId())
            return FSR4DriverProviderDx12::LoadedModule();

        return upscaling_dx12.dll != nullptr ? upscaling_dx12.dll : main_dx12.dll;
    }

    static std::wstring Dx12Module_Path() { return main_dx12.filePath; }
    static std::wstring Dx12Module_SR_Path() { return upscaling_dx12.filePath; }
    static std::wstring Dx12Module_FG_Path() { return fg_dx12.filePath; }
    static std::wstring Dx12Module_Denoiser_Path() { return denoiser_dx12.filePath; }
    static std::wstring Dx12Module_Radiance_Path() { return radiance_dx12.filePath; }

    static bool IsFGReady(bool sendNotification = true)
    {
        bool result = (main_dx12.dll && !main_dx12.isLoader) || fg_dx12.dll != nullptr;

        if (!result && sendNotification)
            ImGui::InsertNotification({ ImGuiToastType::Error, 10000,
                                        "Can't load amd_fidelityfx_dx12\nDid you forget to extract that dll?" });

        return result;
    }

    static bool IsSRReady(bool sendNotification = true)
    {
        bool result = (main_dx12.dll && !main_dx12.isLoader) || upscaling_dx12.dll != nullptr;

        if (!result && sendNotification)
            ImGui::InsertNotification({ ImGuiToastType::Error, 10000,
                                        "Can't load amd_fidelityfx_dx12\nDid you forget to extract that dll?" });

        return result;
    }

    static bool IsDenoiserReady(bool sendNotification = true)
    {
        bool result = (main_dx12.dll && !main_dx12.isLoader) || denoiser_dx12.dll != nullptr;

        if (!result && sendNotification)
            ImGui::InsertNotification({ ImGuiToastType::Error, 10000,
                                        "Can't load amd_fidelityfx_dx12\nDid you forget to extract that dll?" });

        return result;
    }

    static bool IsRadianceReady(bool sendNotification = true)
    {
        bool result = (main_dx12.dll && !main_dx12.isLoader) || radiance_dx12.dll != nullptr;

        if (!result && sendNotification)
            ImGui::InsertNotification({ ImGuiToastType::Error, 10000,
                                        "Can't load amd_fidelityfx_dx12\nDid you forget to extract that dll?" });

        return result;
    }

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
        if (module != nullptr && main_dx12_hooked.dll == nullptr && !IsCustomModule(module))
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
                    LoadSdkLibrary(dllNames[i], optiPath, overridePath, &main_dx12_hooked.dll, &main_dx12.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    LoadSdkLibrary(dllNames[i], optiPath, overridePath, &memModule, &main_dx12.dll);
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
                FSR4Presets::Hook(main_dx12.dll);
                FSR4Models::Hook(main_dx12.dll);
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
        if (module != nullptr && upscaling_dx12_hooked.dll == nullptr && !IsCustomModule(module))
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
                    LoadSdkLibrary(dllNames[i], optiPath, overridePath, &upscaling_dx12_hooked.dll,
                                   &upscaling_dx12.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    LoadSdkLibrary(dllNames[i], optiPath, overridePath, &memModule, &upscaling_dx12.dll);
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
            FSR4Presets::Hook(upscaling_dx12.dll);
            FSR4Models::Hook(upscaling_dx12.dll);

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

        return loadResult;
    }

    static bool InitFfxDx12_FG(HMODULE module = nullptr)
    {
        // if dll already loaded
        if (fg_dx12.dll != nullptr && fg_dx12.CreateContext != nullptr)
            return true;

        spdlog::info("");

        // This is module loaded by game, need to hook it to use inputs
        if (module != nullptr && fg_dx12_hooked.dll == nullptr && !IsCustomModule(module))
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
                    LoadSdkLibrary(dllNames[i], optiPath, overridePath, &fg_dx12_hooked.dll, &fg_dx12.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    LoadSdkLibrary(dllNames[i], optiPath, overridePath, &memModule, &fg_dx12.dll);
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
        if (module != nullptr && denoiser_dx12_hooked.dll == nullptr && !IsCustomModule(module))
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
                    LoadSdkLibrary(dllNames[i], optiPath, overridePath, &denoiser_dx12_hooked.dll, &denoiser_dx12.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    LoadSdkLibrary(dllNames[i], optiPath, overridePath, &memModule, &denoiser_dx12.dll);
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
        if (module != nullptr && radiance_dx12_hooked.dll == nullptr && !IsCustomModule(module))
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
                    LoadSdkLibrary(dllNames[i], optiPath, overridePath, &radiance_dx12_hooked.dll, &radiance_dx12.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    LoadSdkLibrary(dllNames[i], optiPath, overridePath, &memModule, &radiance_dx12.dll);
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

    // Loading is only tried once per name, nullptr if it failed
    static CustomFfxModule* LoadCustomUpscalerDx12(const std::string& name)
    {
        return LoadCustomUpscaler(custom_dx12, name, CustomUpscalers::FfxDllPath(name));
    }

    static CustomFfxModule* LoadCustomUpscalerVk(const std::string& name)
    {
        return LoadCustomUpscaler(custom_vk, name, CustomUpscalers::FfxVkDllPath(name));
    }

    // Already loaded custom upscaler, doesn't try loading it
    static CustomFfxModule* FindCustomUpscalerDx12(const std::string& name)
    {
        return FindCustomUpscaler(custom_dx12, name);
    }

    static CustomFfxModule* FindCustomUpscalerVk(const std::string& name)
    {
        return FindCustomUpscaler(custom_vk, name);
    }

    // Context made by the given module, later calls with it go to the same module
    static ffxReturnCode_t D3D12_CreateContext(FfxModule* module, ffxContext* context, ffxCreateContextDescHeader* desc,
                                               const ffxAllocationCallbacks* memCb)
    {
        auto result = module->CreateContext(context, desc, memCb);

        if (result == FFX_API_RETURN_OK)
        {
            std::scoped_lock lock(customMutex);
            customContexts[*context] = module;
        }

        return result;
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
                    main_dx12.version.parse_version(versionNames[0]);
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
                    upscaling_dx12.version.parse_version(versionNames[0]);
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
                    fg_dx12.version.parse_version(versionNames[0]);
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
                    denoiser_dx12.version.parse_version(versionNames[0]);
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
                    radiance_dx12.version.parse_version(versionNames[0]);
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

        if (auto overrideVersion = FindOverrideVersion(desc); type == FFXStructType::Upscaling && overrideVersion &&
                                                              *overrideVersion == FSR4DriverProviderDx12::VersionId())
        {
            return FSR4DriverProviderDx12::CreateContext(context, desc, memCb);
        }

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
        if (FSR4DriverProviderDx12::OwnsContext(context))
            return FSR4DriverProviderDx12::DestroyContext(context, memCb);

        if (auto custom = CustomContextModule(context))
        {
            std::scoped_lock lock(customMutex);
            customContexts.erase(*context);
            return custom->DestroyContext(context, memCb);
        }

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
        if (FSR4DriverProviderDx12::OwnsContext(context))
            return FSR4DriverProviderDx12::Configure(context, desc);

        if (auto custom = CustomContextModule(context))
            return custom->Configure(context, desc);

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
        if (FSR4DriverProviderDx12::OwnsContext(context))
            return FSR4DriverProviderDx12::Query(context, desc);

        if (auto custom = CustomContextModule(context))
            return custom->Query(context, desc);

        if (desc->type == FFX_API_QUERY_DESC_TYPE_GET_VERSIONS && GetIndirectType(desc) == FFXStructType::Upscaling)
        {
            return D3D12_QueryUpscalerVersions(reinterpret_cast<ffxQueryDescGetVersions*>(desc),
                                               FSR4DriverProviderDx12::Listing::WhenNewestUpscaler);
        }

        return D3D12_QuerySdk(context, desc);
    }

    // Versions of the FFX SDK upscalers and the upscaler of the driver
    static ffxReturnCode_t D3D12_QueryUpscalerVersions(ffxQueryDescGetVersions* desc,
                                                       FSR4DriverProviderDx12::Listing driverListing)
    {
        ffxQueryDescGetVersions sdkQuery = *desc;
        uint64_t sdkCount = 0;
        sdkQuery.outputCount = &sdkCount;
        sdkQuery.versionIds = nullptr;
        sdkQuery.versionNames = nullptr;

        if (auto result = D3D12_QuerySdk(nullptr, &sdkQuery.header); result != FFX_API_RETURN_OK)
            return result;

        std::vector<uint64_t> ids(sdkCount);
        std::vector<const char*> names(sdkCount);
        sdkQuery.versionIds = ids.data();
        sdkQuery.versionNames = names.data();

        if (auto result = D3D12_QuerySdk(nullptr, &sdkQuery.header); result != FFX_API_RETURN_OK)
            return result;

        ids.resize(sdkCount);
        names.resize(sdkCount);

        FSR4DriverProviderDx12::AddToVersionList(ids, names, driverListing);

        if (desc->outputCount == nullptr)
            return FFX_API_RETURN_ERROR_PARAMETER;

        // Same contract as FFX, a non-zero count is the capacity of the arrays
        auto count = ids.size();
        if (*desc->outputCount != 0)

        {
            count = std::min<size_t>(count, *desc->outputCount);

            for (size_t i = 0; i < count; i++)
            {
                if (desc->versionIds != nullptr)
                    desc->versionIds[i] = ids[i];

                if (desc->versionNames != nullptr)
                    desc->versionNames[i] = names[i];
            }
        }

        *desc->outputCount = count;
        return FFX_API_RETURN_OK;
    }

    template <typename QueryFn>
    static ffxReturnCode_t QueryUpscalerVersionsWithoutFsr4(ffxQueryDescGetVersions* desc, QueryFn query)
    {
        if (desc->outputCount == nullptr)
            return FFX_API_RETURN_ERROR_PARAMETER;

        ffxQueryDescGetVersions fullQuery = *desc;
        uint64_t fullCount = 0;
        fullQuery.outputCount = &fullCount;
        fullQuery.versionIds = nullptr;
        fullQuery.versionNames = nullptr;

        if (auto result = query(&fullQuery.header); result != FFX_API_RETURN_OK)
            return result;

        std::vector<uint64_t> ids(fullCount);
        std::vector<const char*> names(fullCount);
        fullQuery.versionIds = ids.data();
        fullQuery.versionNames = names.data();

        if (auto result = query(&fullQuery.header); result != FFX_API_RETURN_OK)
            return result;

        // Same contract as FFX, a non-zero count is the capacity of the arrays
        auto capacity = *desc->outputCount;
        uint64_t count = 0;

        for (size_t i = 0; i < fullCount && i < ids.size(); i++)
        {
            if (names[i] != nullptr && std::atoi(names[i]) >= 4)
            {
                LOG_DEBUG("Hiding upscaler version {} from the game", names[i]);
                continue;
            }

            if (capacity != 0)
            {
                if (count >= capacity)
                    break;

                if (desc->versionIds != nullptr)
                    desc->versionIds[count] = ids[i];

                if (desc->versionNames != nullptr)
                    desc->versionNames[count] = names[i];
            }

            count++;
        }

        *desc->outputCount = count;
        return FFX_API_RETURN_OK;
    }

  private:
    static std::optional<uint64_t> FindOverrideVersion(const ffxApiHeader* desc)
    {
        for (auto header = desc; header != nullptr; header = header->pNext)
        {
            if (header->type == FFX_API_DESC_TYPE_OVERRIDE_VERSION)
                return reinterpret_cast<const ffxOverrideVersion*>(header)->versionId;
        }

        return std::nullopt;
    }

    static ffxReturnCode_t D3D12_QuerySdk(ffxContext* context, ffxQueryDescHeader* desc)
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

  public:
    static ffxReturnCode_t D3D12_Dispatch(ffxContext* context, const ffxDispatchDescHeader* desc)
    {
        if (FSR4DriverProviderDx12::OwnsContext(context))
            return FSR4DriverProviderDx12::Dispatch(context, desc);

        if (auto custom = CustomContextModule(context))
            return custom->Dispatch(context, desc);

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
    static HMODULE VkModule_SR() { return upscaling_vk.dll; }
    static HMODULE VkModule_FG() { return fg_vk.dll; }

    static std::wstring VkModule_Path() { return main_vk.filePath; }
    static std::wstring VkModule_SR_Path() { return upscaling_vk.filePath; }
    static std::wstring VkModule_FG_Path() { return fg_vk.filePath; }

    static bool VkModule_BackendV2() { return main_vk.vkBackendV2; }

    // Module that Opti runs its Vulkan upscaler with, the upscaler dll of SDK 2.x when it's loaded
    static FfxModule* VkFfxModule_SR()
    {
        auto module = VkEffectModule(upscaling_vk);
        return module != nullptr ? module : &main_vk;
    }

    // Module that Opti runs its Vulkan frame generation and swapchain with
    static FfxModule* VkFfxModule_FG()
    {
        auto module = VkEffectModule(fg_vk);
        return module != nullptr ? module : &main_vk;
    }

    // Module that runs the Vulkan upscaler with this version id
    static HMODULE UpscalerModuleVk(uint64_t versionId)
    {
        if (versionId != 0 && FSR4DriverProviderVk::IsAvailable() && versionId == FSR4DriverProviderVk::VersionId())
            return FSR4DriverProviderVk::Module()->dll;

        return VkFfxModule_SR()->dll;
    }

    static bool IsSRReadyVk()
    {
        return (main_vk.dll != nullptr && !main_vk.isLoader) || VkEffectModule(upscaling_vk) != nullptr;
    }

    static bool IsFGReadyVk()
    {
        return (main_vk.dll != nullptr && !main_vk.isLoader) || VkEffectModule(fg_vk) != nullptr;
    }

    static bool InitFfxVk(HMODULE module = nullptr)
    {
        InitFfxVkModule(main_vk, main_vk_hooked, module, { L"amd_fidelityfx_loader_vk.dll", L"amd_fidelityfx_vk.dll" },
                        Config::Instance()->FfxVkPath.value_or(L""));

        InitFfxVk_SR();
        InitFfxVk_FG();

        if (main_vk.isLoader && (upscaling_vk.dll == nullptr || fg_vk.dll == nullptr))
            InitFfxVkLegacy();

        bool loadResult = main_vk.CreateContext != nullptr || upscaling_vk.CreateContext != nullptr ||
                          fg_vk.CreateContext != nullptr || legacy_vk.CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (loadResult)
            VersionVk();

        return loadResult;
    }

    static bool InitFfxVk_SR(HMODULE module = nullptr)
    {
        // if dll already loaded
        if (upscaling_vk.dll != nullptr && upscaling_vk.CreateContext != nullptr)
            return true;

        if (!InitFfxVkModule(upscaling_vk, upscaling_vk_hooked, module, { L"amd_fidelityfx_upscaler_vk.dll" },
                             Config::Instance()->FfxVkSRPath.value_or(L"")))
        {
            return false;
        }

        FSR4Presets::Hook(upscaling_vk.dll);
        FSR4Models::Hook(upscaling_vk.dll);

        return true;
    }

    static bool InitFfxVk_FG(HMODULE module = nullptr)
    {
        return InitFfxVkModule(fg_vk, fg_vk_hooked, module, { L"amd_fidelityfx_framegeneration_vk.dll" },
                               Config::Instance()->FfxVkFGPath.value_or(L""));
    }

    static feature_version VersionVk()
    {
        if (main_vk.version.major == 0)
            QueryVersionVk(main_vk, FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE, "");

        if (main_vk.version.major == 0 && VkEffectModule(upscaling_vk) != nullptr)
            main_vk.version = VersionVk_SR();

        if (main_vk.version.major == 0 && VkEffectModule(fg_vk) != nullptr)
            main_vk.version = VersionVk_FG();

        return main_vk.version;
    }

    static feature_version VersionVk_SR()
    {
        auto module = VkEffectModule(upscaling_vk);
        if (module == nullptr)
            return VersionVk();

        return QueryVersionVk(*module, FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE, "SR ");
    }

    static feature_version VersionVk_FG()
    {
        auto module = VkEffectModule(fg_vk);
        if (module == nullptr)
            return VersionVk();

        return QueryVersionVk(*module, FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION, "FG ");
    }

    static ffxReturnCode_t VULKAN_CreateContext(ffxContext* context, ffxCreateContextDescHeader* desc,
                                                const ffxAllocationCallbacks* memCb)
    {
        auto type = GetType(desc->type);

        if (auto overrideVersion = FindOverrideVersion(desc); type == FFXStructType::Upscaling && overrideVersion &&
                                                              FSR4DriverProviderVk::IsAvailable() &&
                                                              *overrideVersion == FSR4DriverProviderVk::VersionId())
        {
            auto driver = FSR4DriverProviderVk::Module();
            auto result = CreateContextVk(driver, context, desc, memCb);

            if (result == FFX_API_RETURN_OK)
            {
                std::scoped_lock lock(vkDriverMutex);
                vkDriverContexts[*context] = driver;
            }

            return result;
        }

        auto isFg = type == FFXStructType::FG || type == FFXStructType::SwapchainVulkan;

        ffxReturnCode_t result = FFX_API_RETURN_ERROR;

        if (auto fg = VkEffectModule(fg_vk); isFg && fg != nullptr)
        {
            LOG_DEBUG("Creating with {}", VkModuleName(fg));
            result = CreateContextVk(fg, context, desc, memCb);
            contextToType[*context] = type;
            LOG_DEBUG("Created with {}: {:X}", VkModuleName(fg), (size_t) *context);
            return result;
        }
        else if (auto upscaling = VkEffectModule(upscaling_vk);
                 type == FFXStructType::Upscaling && upscaling != nullptr)
        {
            LOG_DEBUG("Creating with {}", VkModuleName(upscaling));
            result = CreateContextVk(upscaling, context, desc, memCb);
            contextToType[*context] = type;
            LOG_DEBUG("Created with {}: {:X}", VkModuleName(upscaling), (size_t) *context);
            return result;
        }

        const auto skipFG = isFg && fg_vk.skipCreateCalls;
        const auto skipUpscaling = type == FFXStructType::Upscaling && upscaling_vk.skipCreateCalls;

        if (main_vk.dll != nullptr && !skipFG && !skipUpscaling)
        {
            LOG_DEBUG("Creating with main_vk");

            if (isFg)
                fg_vk.skipCreateCalls = true;
            else if (type == FFXStructType::Upscaling)
                upscaling_vk.skipCreateCalls = true;

            result = CreateContextVk(&main_vk, context, desc, memCb);

            if (isFg)
                fg_vk.skipCreateCalls = false;
            else if (type == FFXStructType::Upscaling)
                upscaling_vk.skipCreateCalls = false;

            return result;
        }

        return FFX_API_RETURN_NO_PROVIDER;
    }

    static ffxReturnCode_t VULKAN_DestroyContext(ffxContext* context, const ffxAllocationCallbacks* memCb)
    {
        if (auto driver = VkDriverContextModule(context))
        {
            {
                std::scoped_lock lock(vkDriverMutex);
                vkDriverContexts.erase(*context);
            }

            return driver->DestroyContext(context, memCb);
        }

        ffxReturnCode_t result = FFX_API_RETURN_ERROR;
        auto type = FFXStructType::Unknown;

        if (contextToType.contains(*context))
        {
            type = contextToType[*context];
            LOG_DEBUG("Found context type mapping: {}", magic_enum::enum_name(type));
            contextToType.erase(*context);
        }

        FfxModule* mapped = nullptr;

        switch (type)
        {
        case FFXStructType::Upscaling:
            mapped = VkEffectModule(upscaling_vk);
            break;

        case FFXStructType::FG:
        case FFXStructType::SwapchainVulkan:
            mapped = VkEffectModule(fg_vk);
            break;

        default:
            break;
        }

        if (mapped != nullptr)
        {
            LOG_DEBUG("Destroying with {}", VkModuleName(mapped));
            result = mapped->DestroyContext(context, memCb);
        }

        if (result == FFX_API_RETURN_OK)
            return result;

        if (main_vk.dll != nullptr && !_skipDestroyCalls)
        {
            LOG_DEBUG("Destroying with main_vk");
            _skipDestroyCalls = true;
            result = main_vk.DestroyContext(context, memCb);
            _skipDestroyCalls = false;
        }

        if (result == FFX_API_RETURN_OK)
            return result;

        if (upscaling_vk.dll != nullptr)
        {
            LOG_DEBUG("Destroying with upscaling_vk");
            result = upscaling_vk.DestroyContext(context, memCb);
        }

        if (result == FFX_API_RETURN_OK)
            return result;

        if (fg_vk.dll != nullptr)
        {
            LOG_DEBUG("Destroying with fg_vk");
            result = fg_vk.DestroyContext(context, memCb);
        }

        if (result == FFX_API_RETURN_OK)
            return result;

        if (legacy_vk.dll != nullptr)
        {
            LOG_DEBUG("Destroying with legacy_vk");
            result = legacy_vk.DestroyContext(context, memCb);
        }

        if (result == FFX_API_RETURN_OK)
            return result;

        LOG_ERROR("Failed to destroy context in any module");
        return FFX_API_RETURN_NO_PROVIDER;
    }

    static ffxReturnCode_t VULKAN_Configure(ffxContext* context, const ffxConfigureDescHeader* desc)
    {
        if (auto driver = VkDriverContextModule(context))
            return driver->Configure(context, desc);

        auto type = GetType(desc->type);

        if (type == FFXStructType::General && context != nullptr && contextToType.contains(*context))
            type = contextToType[*context];

        auto isFg = type == FFXStructType::FG || type == FFXStructType::SwapchainVulkan;

        if (auto fg = VkEffectModule(fg_vk); isFg && fg != nullptr)
            return fg->Configure(context, desc);
        else if (auto upscaling = VkEffectModule(upscaling_vk);
                 type == FFXStructType::Upscaling && upscaling != nullptr)
            return upscaling->Configure(context, desc);

        const auto skipFG = isFg && fg_vk.skipConfigureCalls;
        const auto skipUpscaling = type == FFXStructType::Upscaling && upscaling_vk.skipConfigureCalls;

        if (main_vk.dll != nullptr && !skipFG && !skipUpscaling)
        {
            if (isFg)
                fg_vk.skipConfigureCalls = true;
            else if (type == FFXStructType::Upscaling)
                upscaling_vk.skipConfigureCalls = true;

            auto result = main_vk.Configure(context, desc);

            if (isFg)
                fg_vk.skipConfigureCalls = false;
            else if (type == FFXStructType::Upscaling)
                upscaling_vk.skipConfigureCalls = false;

            return result;
        }

        return FFX_API_RETURN_NO_PROVIDER;
    }

    static ffxReturnCode_t VULKAN_Query(ffxContext* context, ffxQueryDescHeader* desc)
    {
        if (auto driver = VkDriverContextModule(context))
            return driver->Query(context, desc);

        if (desc->type == FFX_API_QUERY_DESC_TYPE_GET_VERSIONS && GetIndirectType(desc) == FFXStructType::Upscaling)
        {
            return VULKAN_QueryUpscalerVersions(reinterpret_cast<ffxQueryDescGetVersions*>(desc),
                                                DriverListing::WhenNewestUpscaler);
        }

        return VULKAN_QuerySdk(context, desc);
    }

    // Versions of the FFX SDK upscalers and the upscaler of the driver
    static ffxReturnCode_t VULKAN_QueryUpscalerVersions(ffxQueryDescGetVersions* desc, DriverListing driverListing)
    {
        if (desc->outputCount == nullptr)
            return FFX_API_RETURN_ERROR_PARAMETER;

        ffxQueryDescGetVersions sdkQuery = *desc;
        uint64_t sdkCount = 0;
        sdkQuery.outputCount = &sdkCount;
        sdkQuery.versionIds = nullptr;
        sdkQuery.versionNames = nullptr;

        std::vector<uint64_t> ids;
        std::vector<const char*> names;

        // Driver provided FSR 4 works without the FFX SDK
        auto sdkResult = VULKAN_QuerySdk(nullptr, &sdkQuery.header);

        if (sdkResult == FFX_API_RETURN_OK && sdkCount > 0)
        {
            ids.resize(sdkCount);
            names.resize(sdkCount);
            sdkQuery.versionIds = ids.data();
            sdkQuery.versionNames = names.data();

            sdkResult = VULKAN_QuerySdk(nullptr, &sdkQuery.header);

            ids.resize(sdkResult == FFX_API_RETURN_OK ? sdkCount : 0);
            names.resize(ids.size());
        }

        FSR4DriverProviderVk::AddToVersionList(ids, names, driverListing);

        if (ids.empty() && sdkResult != FFX_API_RETURN_OK)
            return sdkResult;

        // Same contract as FFX, a non-zero count is the capacity of the arrays
        auto count = ids.size();
        if (*desc->outputCount != 0)
        {
            count = std::min<size_t>(count, *desc->outputCount);

            for (size_t i = 0; i < count; i++)
            {
                if (desc->versionIds != nullptr)
                    desc->versionIds[i] = ids[i];

                if (desc->versionNames != nullptr)
                    desc->versionNames[i] = names[i];
            }
        }

        *desc->outputCount = count;
        return FFX_API_RETURN_OK;
    }

    // Versions of the FFX SDK frame generation followed by the frame generation of the driver
    static ffxReturnCode_t VULKAN_QueryFgVersions(std::vector<uint64_t>& ids, std::vector<const char*>& names)
    {
        ffxQueryDescGetVersions versionQuery {};
        versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
        uint64_t versionCount = 0;
        versionQuery.outputCount = &versionCount;

        ids.clear();
        names.clear();

        // Driver provided ML FG works without the FFX SDK
        auto result = VULKAN_QuerySdk(nullptr, &versionQuery.header);

        if (result == FFX_API_RETURN_OK && versionCount > 0)
        {
            ids.resize(versionCount);
            names.resize(versionCount);
            versionQuery.versionIds = ids.data();
            versionQuery.versionNames = names.data();

            result = VULKAN_QuerySdk(nullptr, &versionQuery.header);

            ids.resize(result == FFX_API_RETURN_OK ? versionCount : 0);
            names.resize(ids.size());
        }

        FSR4DriverProviderVk::AddFgToVersionList(ids, names);

        return ids.empty() ? result : FFX_API_RETURN_OK;
    }

    static ffxReturnCode_t VULKAN_Dispatch(ffxContext* context, const ffxDispatchDescHeader* desc)
    {
        if (auto driver = VkDriverContextModule(context))
            return driver->Dispatch(context, desc);

        auto type = GetType(desc->type);
        auto isFg = type == FFXStructType::FG || type == FFXStructType::SwapchainVulkan;

        if (auto fg = VkEffectModule(fg_vk); isFg && fg != nullptr)
            return fg->Dispatch(context, desc);
        else if (auto upscaling = VkEffectModule(upscaling_vk);
                 type == FFXStructType::Upscaling && upscaling != nullptr)
            return upscaling->Dispatch(context, desc);

        const auto skipFG = isFg && fg_vk.skipDispatchCalls;
        const auto skipUpscaling = type == FFXStructType::Upscaling && upscaling_vk.skipDispatchCalls;

        if (main_vk.dll != nullptr && !skipFG && !skipUpscaling)
        {
            if (isFg)
                fg_vk.skipDispatchCalls = true;
            else if (type == FFXStructType::Upscaling)
                upscaling_vk.skipDispatchCalls = true;

            auto result = main_vk.Dispatch(context, desc);

            if (isFg)
                fg_vk.skipDispatchCalls = false;
            else if (type == FFXStructType::Upscaling)
                upscaling_vk.skipDispatchCalls = false;

            return result;
        }

        return FFX_API_RETURN_NO_PROVIDER;
    }

    // Backend description type that the module takes, FFX SDK 2.x doesn't create a backend with the older type
    static uint64_t VkBackendType(const FfxModule* module)
    {
        return module->vkBackendV2 ? FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK_V2
                                   : FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    }

  private:
    // Loads Opti's copy of a Vulkan FFX dll, falls back to the one of the game and hooks the game's one for inputs
    static bool InitFfxVkModule(FfxModule& module, FfxModule& hooked, HMODULE gameModule,
                                const std::vector<std::wstring>& dllNames, const std::wstring& overridePath)
    {
        // if dll already loaded
        if (module.dll != nullptr && module.CreateContext != nullptr)
            return true;

        spdlog::info("");

        // This is module loaded by game, need to hook it to use inputs
        if (gameModule != nullptr && hooked.dll == nullptr && !IsCustomModule(gameModule))
            hooked.dll = gameModule;

        // Try loading Opti's dlls first
        if (module.dll == nullptr)
        {
            auto optiPath = Config::Instance()->MainDllPath.value();

            for (const auto& dllName : dllNames)
            {
                LOG_DEBUG("Trying to load {}", wstring_to_string(dllName));

                if (hooked.dll == nullptr)
                {
                    LoadSdkLibrary(dllName, optiPath, overridePath, &hooked.dll, &module.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    LoadSdkLibrary(dllName, optiPath, overridePath, &memModule, &module.dll);
                }

                if (module.dll != nullptr)
                    break;
            }
        }

        // Can't find Opti dlls, use just module loaded by game
        if (module.dll == nullptr && hooked.dll != nullptr)
            module = hooked;

        if (module.dll != nullptr)
        {
            auto modulePath = Util::ModulePath(module.dll);
            module.filePath = modulePath.wstring();
            module.isLoader = IsLoader(module.filePath);

            // Only the legacy FFX SDK 1.x dll keeps the old backend type
            module.vkBackendV2 = _wcsicmp(modulePath.filename().c_str(), L"amd_fidelityfx_vk.dll") != 0;

            LOG_INFO("Loaded from {}", wstring_to_string(module.filePath));
        }

        if (module.dll != nullptr && module.CreateContext == nullptr)
            GetMethods(module);

        // Game calls Opti's dll when it has none of its own, LoadLibrary hooks hand it Opti's
        if (hooked.dll != nullptr && hooked.CreateContext == nullptr)
            GetMethods(hooked);
        else if (hooked.CreateContext == nullptr)
            hooked = module;

        if (Config::Instance()->EnableFfxInputs.value_or_default() && hooked.dll != nullptr &&
            hooked.CreateContext != nullptr && !hooked.hooked)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (hooked.Configure != nullptr)
                DetourAttach(&(PVOID&) hooked.Configure, ffxConfigure_Vk);

            if (hooked.CreateContext != nullptr)
                DetourAttach(&(PVOID&) hooked.CreateContext, ffxCreateContext_Vk);

            if (hooked.DestroyContext != nullptr)
                DetourAttach(&(PVOID&) hooked.DestroyContext, ffxDestroyContext_Vk);

            if (hooked.Dispatch != nullptr)
                DetourAttach(&(PVOID&) hooked.Dispatch, ffxDispatch_Vk);

            if (hooked.Query != nullptr)
                DetourAttach(&(PVOID&) hooked.Query, ffxQuery_Vk);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("DetourTransactionCommit result: {:X}", detourResult);
                hooked.Configure = nullptr;
                hooked.CreateContext = nullptr;
                hooked.DestroyContext = nullptr;
                hooked.Dispatch = nullptr;
                hooked.Query = nullptr;
            }
            else
            {
                // If we are hooking Opti's dll then we need to use original method ptrs
                if (module.dll == hooked.dll)
                {
                    module.Configure = hooked.Configure;
                    module.CreateContext = hooked.CreateContext;
                    module.DestroyContext = hooked.DestroyContext;
                    module.Dispatch = hooked.Dispatch;
                    module.Query = hooked.Query;
                }

                hooked.hooked = true;
                State::Instance().fsrHooks = true;

                // Pin the dll so the hooks stay valid if the game unloads it
                HMODULE pinned = nullptr;
                GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                   (LPCWSTR) hooked.dll, &pinned);
            }
        }

        bool loadResult = module.CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (loadResult)
        {
            // Pin the dll so the method pointers stay valid if the game unloads it
            HMODULE pinned = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                               (LPCWSTR) module.dll, &pinned);
        }
        else
        {
            module.dll = nullptr;
        }

        return loadResult;
    }

    // Module of an effect, amd_fidelityfx_vk.dll stands in for its SDK 2.x dll when the main module is the loader,
    // which can only forward to the SDK 2.x dlls
    static FfxModule* VkEffectModule(FfxModule& module)
    {
        if (module.dll != nullptr)
            return &module;

        if (main_vk.isLoader && legacy_vk.dll != nullptr)
            return &legacy_vk;

        return nullptr;
    }

    static std::string VkModuleName(const FfxModule* module)
    {
        if (module == &upscaling_vk)
            return "upscaling_vk";

        if (module == &fg_vk)
            return "fg_vk";

        if (module == &legacy_vk)
            return "legacy_vk";

        return "main_vk";
    }

    // Only used by Opti, the game's amd_fidelityfx_vk.dll gets the loader through the LoadLibrary hooks
    static void InitFfxVkLegacy()
    {
        if (legacy_vk.dll != nullptr)
            return;

        LOG_DEBUG("Trying to load amd_fidelityfx_vk.dll for the missing FFX SDK 2.x dlls");

        HMODULE memModule = nullptr;
        LoadSdkLibrary(L"amd_fidelityfx_vk.dll", Config::Instance()->MainDllPath.value(),
                       Config::Instance()->FfxVkPath.value_or(L""), &memModule, &legacy_vk.dll);

        if (legacy_vk.dll == nullptr)
            return;

        auto modulePath = Util::ModulePath(legacy_vk.dll);
        legacy_vk.filePath = modulePath.wstring();
        legacy_vk.isLoader = IsLoader(legacy_vk.filePath);
        legacy_vk.vkBackendV2 = _wcsicmp(modulePath.filename().c_str(), L"amd_fidelityfx_vk.dll") != 0;
        GetMethods(legacy_vk);

        if (legacy_vk.isLoader || legacy_vk.CreateContext == nullptr)
        {
            LOG_WARN("{} can't stand in for the FFX SDK 2.x dlls", wstring_to_string(legacy_vk.filePath));
            legacy_vk = {};
            return;
        }

        LOG_INFO("Loaded from {}", wstring_to_string(legacy_vk.filePath));

        // Pin the dll so the method pointers stay valid if the game unloads it
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           (LPCWSTR) legacy_vk.dll, &pinned);
    }

    static CustomFfxModule* LoadCustomUpscaler(std::map<std::string, CustomFfxModule>& customs, const std::string& name,
                                               const std::filesystem::path& path)
    {
        std::scoped_lock lock(customMutex);

        auto [it, inserted] = customs.try_emplace(name);
        auto& module = it->second.module;

        if (inserted)
        {
            it->second.name = name;
            module.dll = CustomUpscalers::LoadDll(name, path);

            if (module.dll == nullptr)
                return nullptr;

            module.filePath = path.wstring();
            module.vkBackendV2 = !CustomUpscalers::IsLegacyFfxVkDll(path);
            FSR4Presets::Hook(module.dll);
            FSR4Models::Hook(module.dll);
            GetMethods(module);
        }

        return module.CreateContext != nullptr && module.Query != nullptr ? &it->second : nullptr;
    }

    static CustomFfxModule* FindCustomUpscaler(std::map<std::string, CustomFfxModule>& customs, const std::string& name)
    {
        std::scoped_lock lock(customMutex);

        auto it = customs.find(name);
        return it != customs.end() && it->second.module.CreateContext != nullptr ? &it->second : nullptr;
    }

    static void GetMethods(FfxModule& module)
    {
        module.Configure = (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(module.dll, "ffxConfigure");
        module.CreateContext = (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(module.dll, "ffxCreateContext");
        module.DestroyContext =
            (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(module.dll, "ffxDestroyContext");
        module.Dispatch = (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(module.dll, "ffxDispatch");
        module.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(module.dll, "ffxQuery");
    }

    static feature_version QueryVersionVk(FfxModule& module, uint64_t createDescType, const char* label)
    {
        if (module.version.major != 0 || module.Query == nullptr)
            return module.version;

        ffxQueryDescGetVersions versionQuery {};
        versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versionQuery.createDescType = createDescType;
        uint64_t versionCount = 0;
        versionQuery.outputCount = &versionCount;

        auto queryResult = module.Query(nullptr, &versionQuery.header);

        // get number of versions for allocation
        if (versionCount == 0 || queryResult != FFX_API_RETURN_OK)
        {
            LOG_WARN("Vulkan {}Query result: {}", label, (UINT) queryResult);
            return module.version;
        }

        std::vector<uint64_t> versionIds(versionCount);
        std::vector<const char*> versionNames(versionCount);
        versionQuery.versionIds = versionIds.data();
        versionQuery.versionNames = versionNames.data();

        queryResult = module.Query(nullptr, &versionQuery.header);

        if (queryResult == FFX_API_RETURN_OK)
        {
            module.version.parse_version(versionNames[0]);
            LOG_INFO("FfxApi Vulkan {}version: {}.{}.{}", label, module.version.major, module.version.minor,
                     module.version.patch);
        }
        else
        {
            LOG_WARN("Vulkan {}Query 2 result: {}", label, (UINT) queryResult);
        }

        return module.version;
    }

    static FfxModule* VkDriverContextModule(ffxContext* context)
    {
        if (context == nullptr)
            return nullptr;

        std::scoped_lock lock(vkDriverMutex);
        auto it = vkDriverContexts.find(*context);
        return it != vkDriverContexts.end() ? it->second : nullptr;
    }

    // Create descs of the game carry the backend type of the SDK it was built with, the layouts of both are the same
    static ffxReturnCode_t CreateContextVk(FfxModule* module, ffxContext* context, ffxCreateContextDescHeader* desc,
                                           const ffxAllocationCallbacks* memCb)
    {
        ffxApiHeader* backend = nullptr;
        ffxStructType_t originalType = 0;

        for (ffxApiHeader* header = desc; header != nullptr; header = header->pNext)
        {
            if (header->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK ||
                header->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK_V2)
            {
                backend = header;
                originalType = header->type;
                header->type = VkBackendType(module);
                break;
            }
        }

        auto result = module->CreateContext(context, desc, memCb);

        if (backend != nullptr)
            backend->type = originalType;

        return result;
    }

    static ffxReturnCode_t VULKAN_QuerySdk(ffxContext* context, ffxQueryDescHeader* desc)
    {
        auto type = GetIndirectType(desc);
        auto isFg = type == FFXStructType::FG || type == FFXStructType::SwapchainVulkan;

        if (auto fg = VkEffectModule(fg_vk); isFg && fg != nullptr)
            return fg->Query(context, desc);
        else if (auto upscaling = VkEffectModule(upscaling_vk);
                 type == FFXStructType::Upscaling && upscaling != nullptr)
            return upscaling->Query(context, desc);

        const auto skipFG = isFg && fg_vk.skipQueryCalls;
        const auto skipUpscaling = type == FFXStructType::Upscaling && upscaling_vk.skipQueryCalls;

        if (main_vk.dll != nullptr && !skipFG && !skipUpscaling)
        {
            if (isFg)
                fg_vk.skipQueryCalls = true;
            else if (type == FFXStructType::Upscaling)
                upscaling_vk.skipQueryCalls = true;

            auto result = main_vk.Query(context, desc);

            if (isFg)
                fg_vk.skipQueryCalls = false;
            else if (type == FFXStructType::Upscaling)
                upscaling_vk.skipQueryCalls = false;

            return result;
        }

        return FFX_API_RETURN_NO_PROVIDER;
    }

  public:
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

        case 0x01000003u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_BACKEND_VK_V2 ({:X})", type);

        case 0x01000004u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_BACKEND_VK_ALLOCATION_CALLBACKS ({:X})", type);

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

        case 0x0001000bu:
            return std::format("CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION ({:X})", type);

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

        case 0x0002000eu:
            return std::format("CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION ({:X})", type);

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

        case 0x0004000au:
            return std::format("CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_VERSION_VK ({:X})", type);

        case 0x0004000bu:
            return std::format("QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_GPU_MEMORY_USAGE_VK_V2 ({:X})", type);

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
