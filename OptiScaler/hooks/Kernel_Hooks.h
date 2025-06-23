#pragma once

#include <pch.h>

#include <Util.h>
#include <State.h>
#include <Config.h>
#include <DllNames.h>

#include <proxies/Kernel32_Proxy.h>
#include <proxies/KernelBase_Proxy.h>
#include <proxies/NVNGX_Proxy.h>
#include <proxies/XeSS_Proxy.h>
#include <proxies/FfxApi_Proxy.h>
#include <proxies/Dxgi_Proxy.h>
#include <proxies/D3D12_Proxy.h>

#include <inputs/FSR2_Dx12.h>
#include <inputs/FSR3_Dx12.h>
#include <inputs/FfxApiExe_Dx12.h>

#include <spoofing/Dxgi_Spoofing.h>
#include <spoofing/Vulkan_Spoofing.h>

#include <hooks/HooksDx.h>
#include <hooks/HooksVk.h>
#include <hooks/Gdi32_Hooks.h>
#include <hooks/Streamline_Hooks.h>

#include <cwctype>

#pragma intrinsic(_ReturnAddress)

// Enables hooking of GetModuleHandle
// which might create issues, not tested very well
// #define HOOK_GET_MODULE

#ifdef HOOK_GET_MODULE
// Handle nvngx.dll calls on GetModule handle
// #define GET_MODULE_NVNGX

// Handle Opti dll calls on GetModule handle
#define GET_MODULE_DLL
#endif

class KernelHooks
{
  private:
    inline static Kernel32Proxy::PFN_FreeLibrary o_K32_FreeLibrary = nullptr;
    inline static Kernel32Proxy::PFN_LoadLibraryA o_K32_LoadLibraryA = nullptr;
    inline static Kernel32Proxy::PFN_LoadLibraryW o_K32_LoadLibraryW = nullptr;
    inline static Kernel32Proxy::PFN_LoadLibraryExA o_K32_LoadLibraryExA = nullptr;
    inline static Kernel32Proxy::PFN_LoadLibraryExW o_K32_LoadLibraryExW = nullptr;
    inline static Kernel32Proxy::PFN_GetProcAddress o_K32_GetProcAddress = nullptr;
    inline static Kernel32Proxy::PFN_GetModuleHandleA o_K32_GetModuleHandleA = nullptr;
    inline static Kernel32Proxy::PFN_GetFileAttributesW o_K32_GetFileAttributesW = nullptr;
    inline static Kernel32Proxy::PFN_CreateFileW o_K32_CreateFileW = nullptr;

    inline static KernelBaseProxy::PFN_FreeLibrary o_KB_FreeLibrary = nullptr;
    inline static KernelBaseProxy::PFN_LoadLibraryA o_KB_LoadLibraryA = nullptr;
    inline static KernelBaseProxy::PFN_LoadLibraryW o_KB_LoadLibraryW = nullptr;
    inline static KernelBaseProxy::PFN_LoadLibraryExA o_KB_LoadLibraryExA = nullptr;
    inline static KernelBaseProxy::PFN_LoadLibraryExW o_KB_LoadLibraryExW = nullptr;
    inline static KernelBaseProxy::PFN_GetProcAddress o_KB_GetProcAddress = nullptr;

    inline static bool _overlayMethodsCalled = false;

    inline static HMODULE LoadLibraryCheck(std::string lcaseLibName, LPCSTR lpLibFullPath)
    {
        LOG_TRACE("{}", lcaseLibName);

        // If Opti is not loading as nvngx.dll
        if (!State::Instance().enablerAvailable && !State::Instance().isWorkingAsNvngx)
        {
            // exe path
            auto exePath = Util::ExePath().parent_path().wstring();

            for (size_t i = 0; i < exePath.size(); i++)
                exePath[i] = std::tolower(exePath[i]);

            auto pos = lcaseLibName.rfind(wstring_to_string(exePath));

            if (Config::Instance()->EnableDlssInputs.value_or_default() && CheckDllName(&lcaseLibName, &nvngxNames) &&
                (!Config::Instance()->HookOriginalNvngxOnly.value_or_default() || pos == std::string::npos))
            {
                LOG_INFO("nvngx call: {0}, returning this dll!", lcaseLibName);
                // loadCount++;

                return dllModule;
            }
        }

        if (!State::Instance().isWorkingAsNvngx &&
            (!State::Instance().isDxgiMode || !State::Instance().skipDxgiLoadChecks) &&
            CheckDllName(&lcaseLibName, &dllNames))
        {
            if (!State::Instance().ServeOriginal())
            {
                LOG_INFO("{} call, returning this dll!", lcaseLibName);
                return dllModule;
            }
            else
            {
                LOG_INFO("{} call, ServeOriginal active returning original dll!", lcaseLibName);
                return originalModule;
            }
        }

        // NvApi64.dll
        if (CheckDllName(&lcaseLibName, &nvapiNames))
        {
            if (!State::Instance().enablerAvailable && Config::Instance()->OverrideNvapiDll.value_or_default())
            {
                LOG_INFO("{0} call!", lcaseLibName);

                auto nvapi = LoadNvApi();

                // Nvapihooks intentionally won't load nvapi so have to make sure it's loaded
                if (nvapi != nullptr)
                {
                    NvApiHooks::Hook(nvapi);
                    return nvapi;
                }
            }
            else
            {
                auto nvapi = GetModuleHandleA(lcaseLibName.c_str());

                // Try to load nvapi only from system32, like the original call would
                if (nvapi == nullptr)
                {
                    nvapi =
                        KernelBaseProxy::LoadLibraryExA_()(lcaseLibName.c_str(), NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
                }

                if (nvapi != nullptr)
                    NvApiHooks::Hook(nvapi);

                // AMD without nvapi override should fall through
            }
        }

        // sl.interposer.dll
        if (CheckDllName(&lcaseLibName, &slInterposerNames))
        {
            auto streamlineModule = KernelBaseProxy::LoadLibraryExA_()(lpLibFullPath, NULL, 0);

            if (streamlineModule != nullptr)
            {
                StreamlineHooks::hookInterposer(streamlineModule);
            }
            else
            {
                LOG_ERROR("Trying to load dll: {}", lcaseLibName);
            }

            return streamlineModule;
        }

        // sl.dlss.dll
        if (CheckDllName(&lcaseLibName, &slDlssNames))
        {
            auto dlssModule = KernelBaseProxy::LoadLibraryExA_()(lpLibFullPath, NULL, 0);

            if (dlssModule != nullptr)
            {
                StreamlineHooks::hookDlss(dlssModule);
            }
            else
            {
                LOG_ERROR("Trying to load dll: {}", lcaseLibName);
            }

            return dlssModule;
        }

        // sl.dlss_g.dll
        if (CheckDllName(&lcaseLibName, &slDlssgNames))
        {
            auto dlssgModule = KernelBaseProxy::LoadLibraryExA_()(lpLibFullPath, NULL, 0);

            if (dlssgModule != nullptr)
            {
                StreamlineHooks::hookDlssg(dlssgModule);
            }
            else
            {
                LOG_ERROR("Trying to load dll: {}", lcaseLibName);
            }

            return dlssgModule;
        }

        // sl.reflex.dll
        if (CheckDllName(&lcaseLibName, &slReflexNames))
        {
            auto reflexModule = KernelBaseProxy::LoadLibraryExA_()(lpLibFullPath, NULL, 0);

            if (reflexModule != nullptr)
            {
                StreamlineHooks::hookReflex(reflexModule);
            }
            else
            {
                LOG_ERROR("Trying to load dll: {}", lcaseLibName);
            }

            return reflexModule;
        }

        // sl.common.dll
        if (CheckDllName(&lcaseLibName, &slCommonNames))
        {
            auto commonModule = KernelBaseProxy::LoadLibraryExA_()(lpLibFullPath, NULL, 0);

            if (commonModule != nullptr)
            {
                StreamlineHooks::hookCommon(commonModule);
            }
            else
            {
                LOG_ERROR("Trying to load dll: {}", lcaseLibName);
            }

            return commonModule;
        }

        // nvngx_dlss
        if (Config::Instance()->DLSSEnabled.value_or_default() && Config::Instance()->NVNGX_DLSS_Library.has_value() &&
            CheckDllName(&lcaseLibName, &nvngxDlssNames))
        {
            auto nvngxDlss = LoadNvngxDlss(string_to_wstring(lcaseLibName));

            if (nvngxDlss == nullptr)
                LOG_ERROR("Trying to load dll: {}", lcaseLibName);

            return nvngxDlss;
        }

        // NGX OTA
        // Try to catch something like this:
        // c:\programdata/nvidia/ngx/models//dlss/versions/20316673/files/160_e658700.bin
        if (lcaseLibName.ends_with(".bin"))
        {
            auto loadedBin = KernelBaseProxy::LoadLibraryExA_()(lpLibFullPath, NULL, 0);

            if (loadedBin && lcaseLibName.contains("/versions/"))
            {
                if (lcaseLibName.contains("/dlss/"))
                {
                    State::Instance().NGX_OTA_Dlss = lpLibFullPath;
                }

                if (lcaseLibName.contains("/dlssd/"))
                {
                    State::Instance().NGX_OTA_Dlssd = lpLibFullPath;
                }
            }
            return loadedBin;
        }

        // Overlay
        if (Config::Instance()->DisableOverlays.value_or_default() && CheckDllName(&lcaseLibName, &blockOverlayNames))
        {
            LOG_DEBUG("Blocking overlay dll: {}", lcaseLibName);
            return (HMODULE) 1;
        }
        else if (CheckDllName(&lcaseLibName, &overlayNames))
        {
            LOG_DEBUG("Overlay dll: {}", lcaseLibName);

            auto module = KernelBaseProxy::LoadLibraryExA_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
            {
                if (/*!_overlayMethodsCalled &&*/ DxgiProxy::Module() != nullptr)
                {
                    LOG_INFO("Calling CreateDxgiFactory methods for overlay!");
                    IDXGIFactory* factory = nullptr;
                    IDXGIFactory1* factory1 = nullptr;
                    IDXGIFactory2* factory2 = nullptr;

                    if (DxgiProxy::CreateDxgiFactory_()(__uuidof(factory), &factory) == S_OK && factory != nullptr)
                    {
                        LOG_DEBUG("CreateDxgiFactory ok");
                        factory->Release();
                    }

                    if (DxgiProxy::CreateDxgiFactory1_()(__uuidof(factory1), &factory1) == S_OK && factory1 != nullptr)
                    {
                        LOG_DEBUG("CreateDxgiFactory1 ok");
                        factory1->Release();
                    }

                    if (DxgiProxy::CreateDxgiFactory2_()(0, __uuidof(factory2), &factory2) == S_OK &&
                        factory2 != nullptr)
                    {
                        LOG_DEBUG("CreateDxgiFactory2 ok");
                        factory2->Release();
                    }

                    _overlayMethodsCalled = true;
                }

                return module;
            }
        }

        // Hooks
        if (CheckDllName(&lcaseLibName, &dx11Names) && Config::Instance()->OverlayMenu.value_or_default())
        {
            auto module = KernelBaseProxy::LoadLibraryExA_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                HooksDx::HookDx11(module);
            else
                LOG_ERROR("Trying to load dll: {}", lcaseLibName);

            return module;
        }

        if (CheckDllName(&lcaseLibName, &dx12Names) && Config::Instance()->OverlayMenu.value_or_default())
        {
            auto module = KernelBaseProxy::LoadLibraryExA_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
            {
                D3d12Proxy::Init(module);
                HooksDx::HookDx12();
            }
            else
            {
                LOG_ERROR("Trying to load dll: {}", lcaseLibName);
            }

            return module;
        }

        if (CheckDllName(&lcaseLibName, &vkNames))
        {
            auto module = KernelBaseProxy::LoadLibraryExA_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
            {
                if (!State::Instance().enablerAvailable)
                {
                    HookForVulkanSpoofing(module);
                    HookForVulkanExtensionSpoofing(module);
                    HookForVulkanVRAMSpoofing(module);
                }

                HooksVk::HookVk(module);
            }
            else
            {
                LOG_ERROR("Trying to load dll: {}", lcaseLibName);
            }

            return module;
        }

        if (!State::Instance().skipDxgiLoadChecks && CheckDllName(&lcaseLibName, &dxgiNames))
        {
            auto module = KernelBaseProxy::LoadLibraryExA_()(lcaseLibName.c_str(), NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

            if (module != nullptr)
            {
                DxgiProxy::Init(module);

                if (!State::Instance().enablerAvailable && Config::Instance()->DxgiSpoofing.value_or_default())
                    HookDxgiForSpoofing();

                if (Config::Instance()->OverlayMenu.value_or_default())
                    HooksDx::HookDxgi();
            }
            else
            {
                LOG_ERROR("Trying to load dll: {}", lcaseLibName);
            }

            return module;
        }

        if (Config::Instance()->EnableFsr2Inputs.value_or_default() && CheckDllName(&lcaseLibName, &fsr2Names))
        {
            auto module = KernelBaseProxy::LoadLibraryExA_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                HookFSR2Inputs(module);
            else
                LOG_ERROR("Trying to load dll: {}", lcaseLibName);

            return module;
        }

        if (Config::Instance()->EnableFsr2Inputs.value_or_default() && CheckDllName(&lcaseLibName, &fsr2BENames))
        {
            auto module = KernelBaseProxy::LoadLibraryExA_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                HookFSR2Dx12Inputs(module);
            else
                LOG_ERROR("Trying to load dll: {}", lcaseLibName);

            return module;
        }

        if (Config::Instance()->EnableFsr3Inputs.value_or_default() && CheckDllName(&lcaseLibName, &fsr3Names))
        {
            auto module = KernelBaseProxy::LoadLibraryExA_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                HookFSR3Inputs(module);

            return module;
        }

        if (Config::Instance()->EnableFsr3Inputs.value_or_default() && CheckDllName(&lcaseLibName, &fsr3BENames))
        {
            auto module = KernelBaseProxy::LoadLibraryExA_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                HookFSR3Dx12Inputs(module);
            else
                LOG_ERROR("Trying to load dll: {}", lcaseLibName);

            return module;
        }

        if (CheckDllName(&lcaseLibName, &xessNames))
        {
            auto module = LoadLibxess(string_to_wstring(lcaseLibName));

            if (module != nullptr)
                XeSSProxy::HookXeSS(module);
            else
                LOG_ERROR("Trying to load dll: {}", lcaseLibName);

            return module;
        }

        if (CheckDllName(&lcaseLibName, &xessDx11Names))
        {
            auto module = LoadLibxessDx11(
                string_to_wstring(lcaseLibName)); // KernelBaseProxy::LoadLibraryExA_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                XeSSProxy::HookXeSSDx11(module);
            else
                LOG_ERROR("Trying to load dll: {}", lcaseLibName);

            return module;
        }

        if (CheckDllName(&lcaseLibName, &ffxDx12Names))
        {
            auto module = LoadFfxapiDx12(
                string_to_wstring(lcaseLibName)); // KernelBaseProxy::LoadLibraryExA_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                FfxApiProxy::InitFfxDx12(module);
            else
                LOG_ERROR("Trying to load dll: {}", lcaseLibName);

            return module;
        }

        if (CheckDllName(&lcaseLibName, &ffxVkNames))
        {
            auto module = LoadFfxapiVk(
                string_to_wstring(lcaseLibName)); // KernelBaseProxy::LoadLibraryExA_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                FfxApiProxy::InitFfxVk(module);
            else
                LOG_ERROR("Trying to load dll: {}", lcaseLibName);

            return module;
        }

        return nullptr;
    }

    inline static HMODULE LoadLibraryCheckW(std::wstring lcaseLibName, LPCWSTR lpLibFullPath)
    {
        auto lcaseLibNameA = wstring_to_string(lcaseLibName);
        LOG_TRACE("{}", lcaseLibNameA);

        // If Opti is not loading as nvngx.dll
        if (!State::Instance().enablerAvailable && !State::Instance().isWorkingAsNvngx)
        {
            // exe path
            auto exePath = Util::ExePath().parent_path().wstring();

            for (size_t i = 0; i < exePath.size(); i++)
                exePath[i] = std::tolower(exePath[i]);

            auto pos = lcaseLibName.rfind(exePath);

            if (Config::Instance()->EnableDlssInputs.value_or_default() && CheckDllNameW(&lcaseLibName, &nvngxNamesW) &&
                (!Config::Instance()->HookOriginalNvngxOnly.value_or_default() || pos == std::string::npos))
            {
                LOG_INFO("nvngx call: {0}, returning this dll!", lcaseLibNameA);

                // if (!dontCount)
                // loadCount++;

                return dllModule;
            }
        }

        if (!State::Instance().isWorkingAsNvngx &&
            (!State::Instance().isDxgiMode || !State::Instance().skipDxgiLoadChecks) &&
            CheckDllNameW(&lcaseLibName, &dllNamesW))
        {
            if (!State::Instance().ServeOriginal())
            {
                LOG_INFO("{} call, returning this dll!", lcaseLibNameA);
                return dllModule;
            }
            else
            {
                LOG_INFO("{} call, ServeOriginal active returning original dll!", lcaseLibNameA);
                return originalModule;
            }
        }

        // nvngx_dlss
        if (Config::Instance()->DLSSEnabled.value_or_default() && Config::Instance()->NVNGX_DLSS_Library.has_value() &&
            CheckDllNameW(&lcaseLibName, &nvngxDlssNamesW))
        {
            auto nvngxDlss = LoadNvngxDlss(lcaseLibName);

            if (nvngxDlss != nullptr)
                return nvngxDlss;
            else
                LOG_ERROR("Trying to load dll: {}", lcaseLibNameA);
        }

        // NGX OTA
        // Try to catch something like this:
        // c:\programdata/nvidia/ngx/models//dlss/versions/20316673/files/160_e658700.bin
        if (lcaseLibName.ends_with(L".bin"))
        {
            auto loadedBin = KernelBaseProxy::LoadLibraryExW_()(lpLibFullPath, NULL, 0);

            if (loadedBin && lcaseLibName.contains(L"/versions/"))
            {
                if (lcaseLibName.contains(L"/dlss/"))
                {
                    State::Instance().NGX_OTA_Dlss = wstring_to_string(lpLibFullPath);
                }

                if (lcaseLibName.contains(L"/dlssd/"))
                {
                    State::Instance().NGX_OTA_Dlssd = wstring_to_string(lpLibFullPath);
                }
            }
            return loadedBin;
        }

        // NvApi64.dll
        if (CheckDllNameW(&lcaseLibName, &nvapiNamesW))
        {
            if (!State::Instance().enablerAvailable && Config::Instance()->OverrideNvapiDll.value_or_default())
            {
                LOG_INFO("{0} call!", lcaseLibNameA);

                auto nvapi = LoadNvApi();

                // Nvapihooks intentionally won't load nvapi so have to make sure it's loaded
                if (nvapi != nullptr)
                {
                    NvApiHooks::Hook(nvapi);
                    return nvapi;
                }
            }
            else
            {
                auto nvapi = GetModuleHandleW(lcaseLibName.c_str());

                // Try to load nvapi only from system32, like the original call would
                if (nvapi == nullptr)
                {
                    nvapi =
                        KernelBaseProxy::LoadLibraryExW_()(lcaseLibName.c_str(), NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
                }

                if (nvapi != nullptr)
                    NvApiHooks::Hook(nvapi);

                // AMD without nvapi override should fall through
            }
        }

        // sl.interposer.dll
        if (CheckDllNameW(&lcaseLibName, &slInterposerNamesW))
        {
            auto streamlineModule = KernelBaseProxy::LoadLibraryExW_()(lpLibFullPath, NULL, 0);

            if (streamlineModule != nullptr)
            {
                StreamlineHooks::hookInterposer(streamlineModule);
            }
            else
            {
                LOG_ERROR("Trying to load dll: {}", lcaseLibNameA);
            }

            return streamlineModule;
        }

        // sl.dlss.dll
        if (CheckDllNameW(&lcaseLibName, &slDlssNamesW))
        {
            auto dlssModule = KernelBaseProxy::LoadLibraryExW_()(lpLibFullPath, NULL, 0);

            if (dlssModule != nullptr)
            {
                StreamlineHooks::hookDlss(dlssModule);
            }
            else
            {
                LOG_ERROR("Trying to load dll: {}", lcaseLibNameA);
            }

            return dlssModule;
        }

        // sl.dlss_g.dll
        if (CheckDllNameW(&lcaseLibName, &slDlssgNamesW))
        {
            auto dlssgModule = KernelBaseProxy::LoadLibraryExW_()(lpLibFullPath, NULL, 0);

            if (dlssgModule != nullptr)
            {
                StreamlineHooks::hookDlssg(dlssgModule);
            }
            else
            {
                LOG_ERROR("Trying to load dll: {}", lcaseLibNameA);
            }

            return dlssgModule;
        }

        // sl.reflex.dll
        if (CheckDllNameW(&lcaseLibName, &slReflexNamesW))
        {
            auto reflexModule = KernelBaseProxy::LoadLibraryExW_()(lpLibFullPath, NULL, 0);

            if (reflexModule != nullptr)
            {
                StreamlineHooks::hookReflex(reflexModule);
            }
            else
            {
                LOG_ERROR("Trying to load dll: {}", lcaseLibNameA);
            }

            return reflexModule;
        }

        // sl.common.dll
        if (CheckDllNameW(&lcaseLibName, &slCommonNamesW))
        {
            auto commonModule = KernelBaseProxy::LoadLibraryExW_()(lpLibFullPath, NULL, 0);

            if (commonModule != nullptr)
            {
                StreamlineHooks::hookCommon(commonModule);
            }
            else
            {
                LOG_ERROR("Trying to load dll: {}", lcaseLibNameA);
            }

            return commonModule;
        }

        if (Config::Instance()->DisableOverlays.value_or_default() && CheckDllNameW(&lcaseLibName, &blockOverlayNamesW))
        {
            LOG_DEBUG("Blocking overlay dll: {}", wstring_to_string(lcaseLibName));
            return (HMODULE) 1;
        }
        else if (CheckDllNameW(&lcaseLibName, &overlayNamesW))
        {
            LOG_DEBUG("Overlay dll: {}", wstring_to_string(lcaseLibName));

            // If we hook CreateSwapChainForHwnd & CreateSwapChainForCoreWindow here
            // Order of CreateSwapChain calls become
            // Game -> Overlay -> Opti
            // and Overlays really does not like Opti's wrapped swapchain
            // If we skip hooking here first Steam hook CreateSwapChainForHwnd & CreateSwapChainForCoreWindow
            // Then hopefully Opti hook and call order become
            // Game -> Opti -> Overlay
            // And Opti menu works with Overlay without issues

            auto module = KernelBaseProxy::LoadLibraryExW_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
            {
                if (/*!_overlayMethodsCalled && */ DxgiProxy::Module() != nullptr)
                {
                    LOG_INFO("Calling CreateDxgiFactory methods for overlay!");
                    IDXGIFactory* factory = nullptr;
                    IDXGIFactory1* factory1 = nullptr;
                    IDXGIFactory2* factory2 = nullptr;

                    if (DxgiProxy::CreateDxgiFactory_()(__uuidof(factory), &factory) == S_OK && factory != nullptr)
                    {
                        LOG_DEBUG("CreateDxgiFactory ok");
                        factory->Release();
                    }

                    if (DxgiProxy::CreateDxgiFactory1_()(__uuidof(factory1), &factory1) == S_OK && factory1 != nullptr)
                    {
                        LOG_DEBUG("CreateDxgiFactory1 ok");
                        factory1->Release();
                    }

                    if (DxgiProxy::CreateDxgiFactory2_()(0, __uuidof(factory2), &factory2) == S_OK &&
                        factory2 != nullptr)
                    {
                        LOG_DEBUG("CreateDxgiFactory2 ok");
                        factory2->Release();
                    }

                    _overlayMethodsCalled = true;
                }

                return module;
            }
        }

        // Hooks
        if (CheckDllNameW(&lcaseLibName, &dx11NamesW) && Config::Instance()->OverlayMenu.value_or_default())
        {
            auto module = KernelBaseProxy::LoadLibraryExW_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                HooksDx::HookDx11(module);

            return module;
        }

        if (CheckDllNameW(&lcaseLibName, &dx12NamesW) && Config::Instance()->OverlayMenu.value_or_default())
        {
            auto module = KernelBaseProxy::LoadLibraryExW_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
            {
                D3d12Proxy::Init(module);
                HooksDx::HookDx12();
            }

            return module;
        }

        if (CheckDllNameW(&lcaseLibName, &vkNamesW))
        {
            auto module = KernelBaseProxy::LoadLibraryExW_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
            {
                if (!State::Instance().enablerAvailable)
                {
                    HookForVulkanSpoofing(module);
                    HookForVulkanExtensionSpoofing(module);
                    HookForVulkanVRAMSpoofing(module);
                }

                HooksVk::HookVk(module);
            }

            return module;
        }

        if (!State::Instance().skipDxgiLoadChecks && CheckDllNameW(&lcaseLibName, &dxgiNamesW))
        {
            auto module = KernelBaseProxy::LoadLibraryExW_()(lcaseLibName.c_str(), NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

            if (module != nullptr)
            {
                DxgiProxy::Init(module);

                if (!State::Instance().enablerAvailable && Config::Instance()->DxgiSpoofing.value_or_default())
                    HookDxgiForSpoofing();

                if (Config::Instance()->OverlayMenu.value_or_default())
                    HooksDx::HookDxgi();
            }
        }

        if (CheckDllNameW(&lcaseLibName, &fsr2NamesW))
        {
            auto module = KernelBaseProxy::LoadLibraryExW_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                HookFSR2Inputs(module);

            return module;
        }

        if (CheckDllNameW(&lcaseLibName, &fsr2BENamesW))
        {
            auto module = KernelBaseProxy::LoadLibraryExW_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                HookFSR2Dx12Inputs(module);

            return module;
        }

        if (CheckDllNameW(&lcaseLibName, &fsr3NamesW))
        {
            auto module = KernelBaseProxy::LoadLibraryExW_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                HookFSR3Inputs(module);

            return module;
        }

        if (CheckDllNameW(&lcaseLibName, &fsr3BENamesW))
        {
            auto module = KernelBaseProxy::LoadLibraryExW_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                HookFSR3Dx12Inputs(module);

            return module;
        }

        if (CheckDllNameW(&lcaseLibName, &xessNamesW))
        {
            auto module =
                LoadLibxess(lcaseLibName); // KernelBaseProxy::LoadLibraryExW_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                XeSSProxy::HookXeSS(module);

            return module;
        }

        if (CheckDllNameW(&lcaseLibName, &xessDx11NamesW))
        {
            auto module =
                LoadLibxessDx11(lcaseLibName); // KernelBaseProxy::LoadLibraryExA_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                XeSSProxy::HookXeSSDx11(module);
            else
                LOG_ERROR("Trying to load dll: {}", wstring_to_string(lcaseLibName));

            return module;
        }

        if (CheckDllNameW(&lcaseLibName, &ffxDx12NamesW))
        {
            auto module =
                LoadFfxapiDx12(lcaseLibName); // KernelBaseProxy::LoadLibraryExW_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                FfxApiProxy::InitFfxDx12(module);

            return module;
        }

        if (CheckDllNameW(&lcaseLibName, &ffxVkNamesW))
        {
            auto module =
                LoadFfxapiVk(lcaseLibName); // KernelBaseProxy::LoadLibraryExW_()(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                FfxApiProxy::InitFfxVk(module);

            return module;
        }

        return nullptr;
    }

    static HMODULE LoadNvApi()
    {
        HMODULE nvapi = nullptr;

        if (Config::Instance()->NvapiDllPath.has_value())
        {
            nvapi = KernelBaseProxy::LoadLibraryExW_()(Config::Instance()->NvapiDllPath->c_str(), NULL, 0);

            if (nvapi != nullptr)
            {
                LOG_INFO("nvapi64.dll loaded from {0}", wstring_to_string(Config::Instance()->NvapiDllPath.value()));
                return nvapi;
            }
        }

        if (nvapi == nullptr)
        {
            auto localPath = Util::DllPath().parent_path() / L"nvapi64.dll";
            nvapi = KernelBaseProxy::LoadLibraryExW_()(localPath.wstring().c_str(), NULL, 0);

            if (nvapi != nullptr)
            {
                LOG_INFO("nvapi64.dll loaded from {0}", wstring_to_string(localPath.wstring()));
                return nvapi;
            }
        }

        if (nvapi == nullptr)
        {
            nvapi = KernelBaseProxy::LoadLibraryExW_()(L"nvapi64.dll", NULL, 0);

            if (nvapi != nullptr)
            {
                LOG_WARN("nvapi64.dll loaded from system!");
                return nvapi;
            }
        }

        return nullptr;
    }

    static HMODULE LoadNvngxDlss(std::wstring originalPath)
    {
        HMODULE nvngxDlss = nullptr;

        if (Config::Instance()->NVNGX_DLSS_Library.has_value())
        {
            nvngxDlss =
                KernelBaseProxy::LoadLibraryExW_()(Config::Instance()->NVNGX_DLSS_Library.value().c_str(), NULL, 0);

            if (nvngxDlss != nullptr)
            {
                LOG_INFO("nvngx_dlss.dll loaded from {0}",
                         wstring_to_string(Config::Instance()->NVNGX_DLSS_Library.value()));
                return nvngxDlss;
            }
            else
            {
                LOG_WARN("nvngx_dlss.dll can't found at {0}",
                         wstring_to_string(Config::Instance()->NVNGX_DLSS_Library.value()));
            }
        }

        if (nvngxDlss == nullptr)
        {
            nvngxDlss = KernelBaseProxy::LoadLibraryExW_()(originalPath.c_str(), NULL, 0);

            if (nvngxDlss != nullptr)
            {
                LOG_INFO("nvngx_dlss.dll loaded from {0}", wstring_to_string(originalPath));
                return nvngxDlss;
            }
        }

        return nullptr;
    }

    static HMODULE LoadLibxess(std::wstring originalPath)
    {
        if (XeSSProxy::Module() != nullptr)
            return XeSSProxy::Module();

        HMODULE libxess = nullptr;

        if (Config::Instance()->XeSSLibrary.has_value())
        {
            std::filesystem::path libPath(Config::Instance()->XeSSLibrary.value().c_str());

            if (libPath.has_filename())
                libxess = KernelBaseProxy::LoadLibraryExW_()(libPath.c_str(), NULL, 0);
            else
                libxess = KernelBaseProxy::LoadLibraryExW_()((libPath / L"libxess.dll").c_str(), NULL, 0);

            if (libxess != nullptr)
            {
                LOG_INFO("libxess.dll loaded from {0}", wstring_to_string(Config::Instance()->XeSSLibrary.value()));
                return libxess;
            }
            else
            {
                LOG_WARN("libxess.dll can't found at {0}", wstring_to_string(Config::Instance()->XeSSLibrary.value()));
            }
        }

        if (libxess == nullptr)
        {
            libxess = KernelBaseProxy::LoadLibraryExW_()(originalPath.c_str(), NULL, 0);

            if (libxess != nullptr)
            {
                LOG_INFO("libxess.dll loaded from {0}", wstring_to_string(originalPath));
                return libxess;
            }
        }

        return nullptr;
    }

    static HMODULE LoadLibxessDx11(std::wstring originalPath)
    {
        if (XeSSProxy::ModuleDx11() != nullptr)
            return XeSSProxy::ModuleDx11();

        HMODULE libxess = nullptr;

        if (Config::Instance()->XeSSDx11Library.has_value())
        {
            std::filesystem::path libPath(Config::Instance()->XeSSDx11Library.value().c_str());

            if (libPath.has_filename())
                libxess = KernelBaseProxy::LoadLibraryExW_()(libPath.c_str(), NULL, 0);
            else
                libxess = KernelBaseProxy::LoadLibraryExW_()((libPath / L"libxess_dx11.dll").c_str(), NULL, 0);

            if (libxess != nullptr)
            {
                LOG_INFO("libxess_dx11.dll loaded from {0}",
                         wstring_to_string(Config::Instance()->XeSSDx11Library.value()));
                return libxess;
            }
            else
            {
                LOG_WARN("libxess_dx11.dll can't found at {0}",
                         wstring_to_string(Config::Instance()->XeSSDx11Library.value()));
            }
        }

        if (libxess == nullptr)
        {
            libxess = KernelBaseProxy::LoadLibraryExW_()(originalPath.c_str(), NULL, 0);

            if (libxess != nullptr)
            {
                LOG_INFO("libxess_dx11.dll loaded from {0}", wstring_to_string(originalPath));
                return libxess;
            }
        }

        return nullptr;
    }

    static HMODULE LoadFfxapiDx12(std::wstring originalPath)
    {
        if (FfxApiProxy::Dx12Module() != nullptr)
            return FfxApiProxy::Dx12Module();

        HMODULE ffxDx12 = nullptr;

        if (Config::Instance()->FfxDx12Path.has_value())
        {
            std::filesystem::path libPath(Config::Instance()->FfxDx12Path.value().c_str());

            if (libPath.has_filename())
                ffxDx12 = KernelBaseProxy::LoadLibraryExW_()(libPath.c_str(), NULL, 0);
            else
                ffxDx12 = KernelBaseProxy::LoadLibraryExW_()((libPath / L"amd_fidelityfx_dx12.dll").c_str(), NULL, 0);

            if (ffxDx12 != nullptr)
            {
                LOG_INFO("amd_fidelityfx_dx12.dll loaded from {0}",
                         wstring_to_string(Config::Instance()->FfxDx12Path.value()));
                return ffxDx12;
            }
            else
            {
                LOG_WARN("amd_fidelityfx_dx12.dll can't found at {0}",
                         wstring_to_string(Config::Instance()->FfxDx12Path.value()));
            }
        }

        if (ffxDx12 == nullptr)
        {
            ffxDx12 = KernelBaseProxy::LoadLibraryExW_()(originalPath.c_str(), NULL, 0);

            if (ffxDx12 != nullptr)
            {
                LOG_INFO("amd_fidelityfx_dx12.dll loaded from {0}", wstring_to_string(originalPath));
                return ffxDx12;
            }
        }

        return nullptr;
    }

    static HMODULE LoadFfxapiVk(std::wstring originalPath)
    {
        if (FfxApiProxy::VkModule() != nullptr)
            return FfxApiProxy::VkModule();

        HMODULE ffxVk = nullptr;

        if (Config::Instance()->FfxVkPath.has_value())
        {
            std::filesystem::path libPath(Config::Instance()->FfxVkPath.value().c_str());

            if (libPath.has_filename())
                ffxVk = KernelBaseProxy::LoadLibraryExW_()(libPath.c_str(), NULL, 0);
            else
                ffxVk = KernelBaseProxy::LoadLibraryExW_()((libPath / L"amd_fidelityfx_vk.dll").c_str(), NULL, 0);

            if (ffxVk != nullptr)
            {
                LOG_INFO("amd_fidelityfx_vk.dll loaded from {0}",
                         wstring_to_string(Config::Instance()->FfxVkPath.value()));
                return ffxVk;
            }
            else
            {
                LOG_WARN("amd_fidelityfx_vk.dll can't found at {0}",
                         wstring_to_string(Config::Instance()->FfxVkPath.value()));
            }
        }

        if (ffxVk == nullptr)
        {
            ffxVk = KernelBaseProxy::LoadLibraryExW_()(originalPath.c_str(), NULL, 0);

            if (ffxVk != nullptr)
            {
                LOG_INFO("amd_fidelityfx_vk.dll loaded from {0}", wstring_to_string(originalPath));
                return ffxVk;
            }
        }

        return nullptr;
    }

    static BOOL hk_K32_FreeLibrary(HMODULE lpLibrary)
    {
        if (lpLibrary == nullptr)
            return FALSE;

        if (!State::Instance().isShuttingDown)
        {
            if (lpLibrary == dllModule)
            {
                LOG_WARN("Call for OptiScaler, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                return TRUE;
            }
            else if (lpLibrary == FfxApiProxy::Dx12Module())
            {
                LOG_WARN("Call for FFX Dx12, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                return TRUE;
            }
            else if (lpLibrary == FfxApiProxy::VkModule())
            {
                LOG_WARN("Call for FFX Vulkan, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                return TRUE;
            }
            else if (lpLibrary == XeSSProxy::Module())
            {
                LOG_WARN("Call for XeSS, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                return TRUE;
            }
            else if (lpLibrary == DxgiProxy::Module())
            {
                LOG_WARN("Call for DXGI, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                return TRUE;
            }
            else if (lpLibrary == D3d12Proxy::Module())
            {
                LOG_WARN("Call for D3D12, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                return TRUE;
            }
            else if (lpLibrary == Kernel32Proxy::Module())
            {
                LOG_WARN("Call for Kernel32, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                return TRUE;
            }
            else if (lpLibrary == KernelBaseProxy::Module())
            {
                LOG_WARN("Call for KernelBase, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                return TRUE;
            }
            else if (lpLibrary == vulkanModule)
            {
                LOG_WARN("Call for Vulkan, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                return TRUE;
            }
            else if (lpLibrary == d3d11Module)
            {
                LOG_WARN("Call for D3D11, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                return TRUE;
            }
        }

        return o_K32_FreeLibrary(lpLibrary);
    }

    static BOOL hk_KB_FreeLibrary(HMODULE lpLibrary)
    {
        if (lpLibrary == nullptr)
            return FALSE;

        if (!State::Instance().isShuttingDown)
        {
            if (lpLibrary == dllModule)
            {
                LOG_WARN("Call for OptiScaler, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                return TRUE;
            }
            else if (lpLibrary == FfxApiProxy::Dx12Module())
            {
                LOG_WARN("Call for FFX Dx12, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                return TRUE;
            }
            else if (lpLibrary == FfxApiProxy::VkModule())
            {
                LOG_WARN("Call for FFX Vulkan, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                return TRUE;
            }
            else if (lpLibrary == XeSSProxy::Module())
            {
                LOG_WARN("Call for XeSS, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                return TRUE;
            }
            else if (lpLibrary == DxgiProxy::Module())
            {
                LOG_WARN("Call for DXGI, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                return TRUE;
            }
            else if (lpLibrary == D3d12Proxy::Module())
            {
                LOG_WARN("Call for D3D12, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                return TRUE;
            }
            else if (lpLibrary == Kernel32Proxy::Module())
            {
                LOG_WARN("Call for Kernel32, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                return TRUE;
            }
            else if (lpLibrary == KernelBaseProxy::Module())
            {
                LOG_WARN("Call for KernelBase, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                return TRUE;
            }
            else if (lpLibrary == vulkanModule)
            {
                LOG_WARN("Call for Vulkan, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                return TRUE;
            }
            else if (lpLibrary == d3d11Module)
            {
                LOG_WARN("Call for D3D11, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                return TRUE;
            }
        }

        return o_KB_FreeLibrary(lpLibrary);
    }

    static HMODULE hk_KB_LoadLibraryA(LPCSTR lpLibFileName)
    {
        if (lpLibFileName == nullptr)
            return NULL;

        std::string libName(lpLibFileName);
        std::string lcaseLibName(libName);

        for (size_t i = 0; i < lcaseLibName.size(); i++)
            lcaseLibName[i] = std::tolower(lcaseLibName[i]);

        if (State::SkipDllChecks())
        {
            if (State::SkipDllName() == "")
            {
                LOG_TRACE("Skip checks for: {}", lcaseLibName);
                return o_KB_LoadLibraryA(lpLibFileName);
            }

            auto dllName = State::SkipDllName();
            auto pos = lcaseLibName.rfind(dllName);

            // -4 for extension `.dll`
            if (pos == (lcaseLibName.length() - dllName.length()) ||
                pos == (lcaseLibName.length() - dllName.length() - 4))
            {
                LOG_TRACE("Skip checks for: {}", lcaseLibName);
                return o_KB_LoadLibraryA(lpLibFileName);
            }
        }

#if _DEBUG
        LOG_TRACE("{}, caller: {}", lcaseLibName, Util::WhoIsTheCaller(_ReturnAddress()));
#endif
        auto moduleHandle = LoadLibraryCheck(lcaseLibName, lpLibFileName);

        // skip loading of dll
        if (moduleHandle == (HMODULE) 1)
        {
            SetLastError(ERROR_ACCESS_DENIED);
            return NULL;
        }

        if (moduleHandle != nullptr)
            return moduleHandle;

        return o_KB_LoadLibraryA(lpLibFileName);
    }

    static HMODULE hk_KB_LoadLibraryW(LPCWSTR lpLibFileName)
    {
        if (lpLibFileName == nullptr)
            return NULL;

        std::wstring libName(lpLibFileName);
        std::wstring lcaseLibName(libName);

        for (size_t i = 0; i < lcaseLibName.size(); i++)
            lcaseLibName[i] = std::towlower(lcaseLibName[i]);

        if (State::SkipDllChecks())
        {
            if (State::SkipDllName() == "")
            {
                LOG_TRACE("Skip checks for: {}", wstring_to_string(lcaseLibName));
                return o_KB_LoadLibraryW(lpLibFileName);
            }

            auto dllName = State::SkipDllName();
            auto pos = wstring_to_string(lcaseLibName).rfind(dllName);

            // -4 for extension `.dll`
            if (pos == (lcaseLibName.length() - dllName.length()) ||
                pos == (lcaseLibName.length() - dllName.length() - 4))
            {
                LOG_TRACE("Skip checks for: {}", wstring_to_string(lcaseLibName));
                return o_KB_LoadLibraryW(lpLibFileName);
            }
        }

#if _DEBUG
        LOG_TRACE("{}, caller: {}", wstring_to_string(lcaseLibName), Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        auto moduleHandle = LoadLibraryCheckW(lcaseLibName, lpLibFileName);

        // skip loading of dll
        if (moduleHandle == (HMODULE) 1)
        {
            SetLastError(ERROR_ACCESS_DENIED);
            return NULL;
        }

        if (moduleHandle != nullptr)
            return moduleHandle;

        return o_KB_LoadLibraryW(lpLibFileName);
    }

    static HMODULE hk_KB_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
    {
        if (lpLibFileName == nullptr)
            return NULL;

        std::string libName(lpLibFileName);
        std::string lcaseLibName(libName);

#if _DEBUG
        LOG_TRACE("{}, caller: {}", lcaseLibName, Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        for (size_t i = 0; i < lcaseLibName.size(); i++)
            lcaseLibName[i] = std::tolower(lcaseLibName[i]);

        if (State::SkipDllChecks())
        {
            if (State::SkipDllName() == "")
            {
                LOG_TRACE("Skip checks for: {}", lcaseLibName);
                return o_KB_LoadLibraryExA(lpLibFileName, hFile, dwFlags);
            }

            auto dllName = State::SkipDllName();
            auto pos = lcaseLibName.rfind(dllName);

            // -4 for extension `.dll`
            if (pos == (lcaseLibName.length() - dllName.length()) ||
                pos == (lcaseLibName.length() - dllName.length() - 4))
            {
                LOG_TRACE("Skip checks for: {}", lcaseLibName);
                return o_KB_LoadLibraryExA(lpLibFileName, hFile, dwFlags);
            }
        }

        auto moduleHandle = LoadLibraryCheck(lcaseLibName, lpLibFileName);

        // skip loading of dll
        if (moduleHandle == (HMODULE) 1)
        {
            SetLastError(ERROR_ACCESS_DENIED);
            return NULL;
        }

        if (moduleHandle != nullptr)
            return moduleHandle;

        auto result = o_KB_LoadLibraryExA(lpLibFileName, hFile, dwFlags);
        return result;
    }

    static HMODULE hk_KB_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
    {
        if (lpLibFileName == nullptr)
            return NULL;

        std::wstring libName(lpLibFileName);
        std::wstring lcaseLibName(libName);

#if _DEBUG
        LOG_TRACE("{}, caller: {}", wstring_to_string(lcaseLibName), Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        for (size_t i = 0; i < lcaseLibName.size(); i++)
            lcaseLibName[i] = std::towlower(lcaseLibName[i]);

        if (State::SkipDllChecks())
        {
            if (State::SkipDllName() == "")
            {
                LOG_TRACE("Skip checks for: {}", wstring_to_string(lcaseLibName));
                return o_KB_LoadLibraryExW(lpLibFileName, hFile, dwFlags);
            }

            auto dllName = State::SkipDllName();
            auto pos = wstring_to_string(lcaseLibName).rfind(dllName);

            // -4 for extension `.dll`
            if (pos == (lcaseLibName.length() - dllName.length()) ||
                pos == (lcaseLibName.length() - dllName.length() - 4))
            {
                LOG_TRACE("Skip checks for: {}", wstring_to_string(lcaseLibName));
                return o_KB_LoadLibraryExW(lpLibFileName, hFile, dwFlags);
            }
        }

        auto moduleHandle = LoadLibraryCheckW(lcaseLibName, lpLibFileName);

        // skip loading of dll
        if (moduleHandle == (HMODULE) 1)
        {
            SetLastError(ERROR_ACCESS_DENIED);
            return NULL;
        }

        if (moduleHandle != nullptr)
            return moduleHandle;

        auto result = o_KB_LoadLibraryExW(lpLibFileName, hFile, dwFlags);
        return result;
    }

    static HMODULE hk_K32_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
    {
        if (lpLibFileName == nullptr)
            return NULL;

        std::string libName(lpLibFileName);
        std::string lcaseLibName(libName);

        for (size_t i = 0; i < lcaseLibName.size(); i++)
            lcaseLibName[i] = std::tolower(lcaseLibName[i]);

        if (State::SkipDllChecks())
        {
            if (State::SkipDllName() == "")
            {
                LOG_TRACE("Skip checks for: {}", lcaseLibName);
                return o_K32_LoadLibraryExA(lpLibFileName, hFile, dwFlags);
            }

            auto dllName = State::SkipDllName();
            auto pos = lcaseLibName.rfind(dllName);

            // -4 for extension `.dll`
            if (pos == (lcaseLibName.length() - dllName.length()) ||
                pos == (lcaseLibName.length() - dllName.length() - 4))
            {
                LOG_TRACE("Skip checks for: {}", lcaseLibName);
                return o_K32_LoadLibraryExA(lpLibFileName, hFile, dwFlags);
            }
        }

#if _DEBUG
        LOG_TRACE("{}, caller: {}", lcaseLibName, Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        auto moduleHandle = LoadLibraryCheck(lcaseLibName, lpLibFileName);

        // skip loading of dll
        if (moduleHandle == (HMODULE) 1)
        {
            SetLastError(ERROR_ACCESS_DENIED);
            return NULL;
        }

        if (moduleHandle != nullptr)
            return moduleHandle;

        auto result = o_K32_LoadLibraryExA(lpLibFileName, hFile, dwFlags);
        return result;
    }

    static HMODULE hk_K32_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
    {
        if (lpLibFileName == nullptr)
            return NULL;

        std::wstring libName(lpLibFileName);
        std::wstring lcaseLibName(libName);

        for (size_t i = 0; i < lcaseLibName.size(); i++)
            lcaseLibName[i] = std::tolower(lcaseLibName[i]);

        if (State::SkipDllChecks())
        {
            if (State::SkipDllName() == "")
            {
                LOG_TRACE("Skip checks for: {}", wstring_to_string(lcaseLibName));
                return o_K32_LoadLibraryExW(lpLibFileName, hFile, dwFlags);
            }

            auto dllName = State::SkipDllName();
            auto pos = wstring_to_string(lcaseLibName).rfind(dllName);

            // -4 for extension `.dll`
            if (pos == (lcaseLibName.length() - dllName.length()) ||
                pos == (lcaseLibName.length() - dllName.length() - 4))
            {
                LOG_TRACE("Skip checks for: {}", wstring_to_string(lcaseLibName));
                return o_K32_LoadLibraryExW(lpLibFileName, hFile, dwFlags);
            }
        }

#if _DEBUG
        LOG_TRACE("{}, caller: {}", wstring_to_string(lcaseLibName), Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        auto moduleHandle = LoadLibraryCheckW(lcaseLibName, lpLibFileName);

        // skip loading of dll
        if (moduleHandle == (HMODULE) 1)
        {
            SetLastError(ERROR_ACCESS_DENIED);
            return NULL;
        }

        if (moduleHandle != nullptr)
            return moduleHandle;

        auto result = o_K32_LoadLibraryExW(lpLibFileName, hFile, dwFlags);
        return result;
    }

    static HMODULE hk_K32_LoadLibraryA(LPCSTR lpLibFileName)
    {
        if (lpLibFileName == nullptr)
            return NULL;

        std::string libName(lpLibFileName);
        std::string lcaseLibName(libName);

        for (size_t i = 0; i < lcaseLibName.size(); i++)
            lcaseLibName[i] = std::tolower(lcaseLibName[i]);

        if (State::SkipDllChecks())
        {
            if (State::SkipDllName() == "")
            {
                LOG_TRACE("Skip checks for: {}", lcaseLibName);
                return o_K32_LoadLibraryA(lpLibFileName);
            }

            auto dllName = State::SkipDllName();
            auto pos = lcaseLibName.rfind(dllName);

            // -4 for extension `.dll`
            if (pos == (lcaseLibName.length() - dllName.length()) ||
                pos == (lcaseLibName.length() - dllName.length() - 4))
            {
                LOG_TRACE("Skip checks for: {}", lcaseLibName);
                return o_K32_LoadLibraryA(lpLibFileName);
            }
        }

#if _DEBUG
        LOG_TRACE("{}, caller: {}", lcaseLibName, Util::WhoIsTheCaller(_ReturnAddress()));
#endif
        auto moduleHandle = LoadLibraryCheck(lcaseLibName, lpLibFileName);

        // skip loading of dll
        if (moduleHandle == (HMODULE) 1)
        {
            SetLastError(ERROR_ACCESS_DENIED);
            return NULL;
        }

        if (moduleHandle != nullptr)
            return moduleHandle;

        return o_K32_LoadLibraryA(lpLibFileName);
    }

    static HMODULE hk_K32_LoadLibraryW(LPCWSTR lpLibFileName)
    {
        if (lpLibFileName == nullptr)
            return NULL;

        std::wstring libName(lpLibFileName);
        std::wstring lcaseLibName(libName);

        for (size_t i = 0; i < lcaseLibName.size(); i++)
            lcaseLibName[i] = std::towlower(lcaseLibName[i]);

        if (State::SkipDllChecks())
        {
            if (State::SkipDllName() == "")
            {
                LOG_TRACE("Skip checks for: {}", wstring_to_string(lcaseLibName));
                return o_K32_LoadLibraryW(lpLibFileName);
            }

            auto dllName = State::SkipDllName();
            auto pos = wstring_to_string(lcaseLibName).rfind(dllName);

            // -4 for extension `.dll`
            if (pos == (lcaseLibName.length() - dllName.length()) ||
                pos == (lcaseLibName.length() - dllName.length() - 4))
            {
                LOG_TRACE("Skip checks for: {}", wstring_to_string(lcaseLibName));
                return o_K32_LoadLibraryW(lpLibFileName);
            }
        }

#if _DEBUG
        LOG_TRACE("{}, caller: {}", wstring_to_string(lcaseLibName), Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        auto moduleHandle = LoadLibraryCheckW(lcaseLibName, lpLibFileName);

        // skip loading of dll
        if (moduleHandle == (HMODULE) 1)
        {
            SetLastError(ERROR_ACCESS_DENIED);
            return NULL;
        }

        if (moduleHandle != nullptr)
            return moduleHandle;

        return o_K32_LoadLibraryW(lpLibFileName);
    }

    static FARPROC hk_K32_GetProcAddress(HMODULE hModule, LPCSTR lpProcName)
    {
        if ((size_t) lpProcName < 0x000000000000F000)
        {
            if (hModule == dllModule)
                LOG_TRACE("Ordinal call: {:X}", (size_t) lpProcName);

            return o_K32_GetProcAddress(hModule, lpProcName);
        }

        if (hModule == dllModule && lpProcName != nullptr)
        {
            LOG_TRACE("Trying to get process address of {}, caller: {}", lpProcName,
                      Util::WhoIsTheCaller(_ReturnAddress()));
        }

        if (State::Instance().isRunningOnLinux && lpProcName != nullptr &&
            hModule == KernelBaseProxy::GetModuleHandleW_()(L"gdi32.dll") &&
            lstrcmpA(lpProcName, "D3DKMTEnumAdapters2") == 0)
            return (FARPROC) &customD3DKMTEnumAdapters2;

        return o_K32_GetProcAddress(hModule, lpProcName);
    }

    static HMODULE hk_K32_GetModuleHandleA(LPCSTR lpModuleName)
    {
        if (lpModuleName != NULL && strcmp(lpModuleName, "nvngx_dlssg.dll") == 0)
        {
            LOG_TRACE("Trying to get module handle of {}, caller: {}", lpModuleName,
                      Util::WhoIsTheCaller(_ReturnAddress()));
            return dllModule;
        }

        return o_K32_GetModuleHandleA(lpModuleName);
    }

    static FARPROC hk_KB_GetProcAddress(HMODULE hModule, LPCSTR lpProcName)
    {
        if ((size_t) lpProcName < 0x000000000000F000)
        {
            if (hModule == dllModule)
                LOG_TRACE("Ordinal call: {:X}", (size_t) lpProcName);

            return o_K32_GetProcAddress(hModule, lpProcName);
        }

        if (hModule == dllModule && lpProcName != nullptr)
        {
            LOG_TRACE("Trying to get process address of {}, caller: {}", lpProcName,
                      Util::WhoIsTheCaller(_ReturnAddress()));
        }

        if (State::Instance().isRunningOnLinux && lpProcName != nullptr &&
            hModule == KernelBaseProxy::GetModuleHandleW_()(L"gdi32.dll") &&
            lstrcmpA(lpProcName, "D3DKMTEnumAdapters2") == 0)
            return (FARPROC) &customD3DKMTEnumAdapters2;

        return o_KB_GetProcAddress(hModule, lpProcName);
    }

    static inline void NormalizePath(std::string& path)
    {
        while (!path.empty() && (path.back() == '\\' || path.back() == '/'))
            path.pop_back();
    }

    static inline bool IsInsideWindowsDirectory(const std::string& path)
    {
        char windowsDir[MAX_PATH];
        UINT len = GetWindowsDirectoryA(windowsDir, MAX_PATH);

        if (len == 0 || len >= MAX_PATH)
            return false;

        std::string pathToCheck(path);
        std::string windowsPath(windowsDir);

        NormalizePath(pathToCheck);
        NormalizePath(windowsPath);

        to_lower_in_place(pathToCheck);
        to_lower_in_place(windowsPath);

        // Check if pathToCheck starts with windowsPath, while having a slash after that
        if (pathToCheck.compare(0, windowsPath.size(), windowsPath) == 0 &&
            (pathToCheck.size() == windowsPath.size() || pathToCheck[windowsPath.size()] == '\\' ||
             pathToCheck[windowsPath.size()] == '/'))
            return true;

        return false;
    }

    static DWORD hk_K32_GetFileAttributesW(LPCWSTR lpFileName)
    {
        if (!State::Instance().nvngxExists && State::Instance().nvngxReplacement.has_value() &&
            (Config::Instance()->DxgiSpoofing.value_or_default() ||
             Config::Instance()->StreamlineSpoofing.value_or_default()))
        {
            auto path = wstring_to_string(std::wstring(lpFileName));
            to_lower_in_place(path);

            if (path.contains("nvngx.dll") && !path.contains("_nvngx.dll") &&
                !IsInsideWindowsDirectory(path)) // apply the override to just one path
            {
                LOG_DEBUG("Overriding GetFileAttributesW for nvngx");
                return FILE_ATTRIBUTE_ARCHIVE;
            }
        }

        return o_K32_GetFileAttributesW(lpFileName);
    }

    static HANDLE hk_K32_CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                     LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                                     DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
    {
        if (!State::Instance().nvngxExists && State::Instance().nvngxReplacement.has_value() &&
            (Config::Instance()->DxgiSpoofing.value_or_default() ||
             Config::Instance()->StreamlineSpoofing.value_or_default()))
        {
            auto path = wstring_to_string(std::wstring(lpFileName));
            to_lower_in_place(path);

            static auto signedDll = Util::FindFilePath(Util::DllPath().remove_filename(), "nvngx_dlss.dll");

            if (path.contains("nvngx.dll") && !path.contains("_nvngx.dll") && // apply the override to just one path
                !IsInsideWindowsDirectory(path) && signedDll.has_value())
            {
                LOG_DEBUG("Overriding CreateFileW for nvngx with a signed dll, original path: {}", path);
                return o_K32_CreateFileW(signedDll.value().c_str(), dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                                         dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
            }
        }

        return o_K32_CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition,
                                 dwFlagsAndAttributes, hTemplateFile);
    }

  public:
    static void Hook()
    {
        if (o_K32_FreeLibrary != nullptr)
            return;

        LOG_DEBUG("");

        o_K32_FreeLibrary = Kernel32Proxy::Hook_FreeLibrary(hk_K32_FreeLibrary);
        o_K32_LoadLibraryA = Kernel32Proxy::Hook_LoadLibraryA(hk_K32_LoadLibraryA);
        o_K32_LoadLibraryW = Kernel32Proxy::Hook_LoadLibraryW(hk_K32_LoadLibraryW);
        o_K32_LoadLibraryExA = Kernel32Proxy::Hook_LoadLibraryExA(hk_K32_LoadLibraryExA);
        o_K32_LoadLibraryExW = Kernel32Proxy::Hook_LoadLibraryExW(hk_K32_LoadLibraryExW);
        o_K32_GetProcAddress = Kernel32Proxy::Hook_GetProcAddress(hk_K32_GetProcAddress);
        o_K32_GetModuleHandleA = Kernel32Proxy::Hook_GetModuleHandleA(hk_K32_GetModuleHandleA);
        o_K32_GetFileAttributesW = Kernel32Proxy::Hook_GetFileAttributesW(hk_K32_GetFileAttributesW);
        o_K32_CreateFileW = Kernel32Proxy::Hook_CreateFileW(hk_K32_CreateFileW);
    }

    static void HookBase()
    {
        if (o_KB_GetProcAddress != nullptr)
            return;

        LOG_DEBUG("");

        // These hooks cause stability regressions
        // o_KB_FreeLibrary = KernelBaseProxy::Hook_FreeLibrary(hk_KB_FreeLibrary);

        if (State::Instance().gameQuirks & GameQuirk::KernelBaseHooks)
        {
            // o_KB_LoadLibraryA = KernelBaseProxy::Hook_LoadLibraryA(hk_KB_LoadLibraryA);
            // o_KB_LoadLibraryW = KernelBaseProxy::Hook_LoadLibraryW(hk_KB_LoadLibraryW);
            // o_KB_LoadLibraryExA = KernelBaseProxy::Hook_LoadLibraryExA(hk_KB_LoadLibraryExA);
            o_KB_LoadLibraryExW = KernelBaseProxy::Hook_LoadLibraryExW(hk_KB_LoadLibraryExW);
        }

        o_KB_GetProcAddress = KernelBaseProxy::Hook_GetProcAddress(hk_KB_GetProcAddress);
    }
};
