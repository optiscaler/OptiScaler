#include "pch.h"
#include "LibraryLoad_Hooks.h"

#include <Config.h>
#include <DllNames.h>

#include <proxies/Ntdll_Proxy.h>
#include <proxies/Kernel32_Proxy.h>
#include <proxies/NVNGX_Proxy.h>
#include <proxies/XeSS_Proxy.h>
#include <proxies/FfxApi_Proxy.h>
#include <proxies/Dxgi_Proxy.h>
#include <proxies/D3D12_Proxy.h>

#include <inputs/FSR2_Dx12.h>
#include <inputs/FSR3_Dx12.h>
#include <inputs/FfxApiExe_Dx12.h>

#include <spoofing/Dxgi_Spoofing.h>

#include <hooks/Dxgi_Hooks.h>
#include <hooks/D3D11_Hooks.h>
#include <hooks/D3D12_Hooks.h>
#include <hooks/Vulkan_Hooks.h>
#include <hooks/Gdi32_Hooks.h>
#include <hooks/Streamline_Hooks.h>

#include <fsr4/FSR4ModelSelection.h>

// #define LOG_LIB_OPERATIONS

HMODULE LibraryLoadHooks::LoadLibraryCheckA(std::string libName, LPCSTR lpLibFullPath)
{
    auto fullPath = std::string(lpLibFullPath);

    return LoadLibraryCheckW(string_to_wstring(libName), string_to_wstring(fullPath).c_str());
}

HMODULE LibraryLoadHooks::LoadLibraryCheckW(std::wstring libName, LPCWSTR lpLibFullPath)
{
    auto libNameA = wstring_to_string(libName);

#ifdef LOG_LIB_OPERATIONS
    LOG_TRACE("{}", libNameA);
#endif

    // C:\\Path\\like\\this.dll
    auto normalizedPath = std::filesystem::path(libName).lexically_normal().wstring();

    // If Opti is not loading as nvngx.dll
    if (!State::Instance().isWorkingAsNvngx)
    {
        // exe path
        auto exePath = Util::ExePath().parent_path().wstring();

        for (size_t i = 0; i < exePath.size(); i++)
            exePath[i] = std::tolower(exePath[i]);

        auto pos = libName.rfind(exePath);

        if (Config::Instance()->EnableDlssInputs.value_or_default() && CheckDllNameW(&libName, &nvngxNamesW) &&
            (!Config::Instance()->HookOriginalNvngxOnly.value_or_default() || pos == std::string::npos))
        {
            LOG_INFO("nvngx call: {0}, returning this dll!", libNameA);

            // if (!dontCount)
            // loadCount++;

            return dllModule;
        }
    }

    if (!State::Instance().isWorkingAsNvngx &&
        (!State::Instance().isDxgiMode || !State::Instance().skipDxgiLoadChecks) && CheckDllNameW(&libName, &dllNamesW))
    {
        if (!State::Instance().ServeOriginal())
        {
            LOG_INFO("{} call, returning this dll!", libNameA);
            return dllModule;
        }
        else
        {
            LOG_INFO("{} call, ServeOriginal active returning original dll!", libNameA);
            return originalModule;
        }
    }

    // nvngx_dlss
    if (Config::Instance()->DLSSEnabled.value_or_default() && Config::Instance()->NVNGX_DLSS_Library.has_value() &&
        CheckDllNameW(&libName, &nvngxDlssNamesW))
    {
        auto nvngxDlss = LoadNvngxDlss(libName);

        if (nvngxDlss != nullptr)
            return nvngxDlss;
        else
            LOG_ERROR("Trying to load dll: {}", libNameA);
    }

    // NGX OTA
    // Try to catch something like this:
    // c:\programdata/nvidia/ngx/models//dlss/versions/20316673/files/160_e658700.bin
    if (libName.ends_with(L".bin"))
    {
        auto loadedBin = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

        if (loadedBin && normalizedPath.contains(L"\\versions\\"))
        {
            if (normalizedPath.contains(L"\\dlss\\"))
            {
                State::Instance().NGX_OTA_Dlss = wstring_to_string(lpLibFullPath);
            }

            if (normalizedPath.contains(L"\\dlssd\\"))
            {
                State::Instance().NGX_OTA_Dlssd = wstring_to_string(lpLibFullPath);
            }
        }
        return loadedBin;
    }

    // NvApi64.dll
    if (CheckDllNameW(&libName, &nvapiNamesW))
    {
        if (Config::Instance()->OverrideNvapiDll.value_or_default())
        {
            LOG_INFO("Overrided {} call!", libNameA);

            auto nvapi = LoadNvApi();

            // Nvapihooks intentionally won't load nvapi so have to make sure it's loaded
            if (nvapi != nullptr)
            {
                NvApiHooks::Hook(nvapi);
                return nvapi;
            }

            LOG_DEBUG("Not loaded");
        }
        else
        {
            LOG_INFO("{} call!", libNameA);

            auto nvapi = GetModuleHandleW(libName.c_str());

            // Try to load nvapi only from system32, like the original call would
            if (nvapi == nullptr)
            {
                nvapi = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
            }

            if (nvapi != nullptr)
                NvApiHooks::Hook(nvapi);

            // AMD without nvapi override should fall through
        }
    }

    // sl.interposer.dll
    if (CheckDllNameW(&libName, &slInterposerNamesW))
    {
        auto streamlineModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

        if (streamlineModule != nullptr)
        {
            // Skip SL hooking when DLSSG is the FG output - we drive SL ourselves
            if (State::Instance().activeFgOutput != FGOutput::DLSSG)
            {
                StreamlineHooks::hookInterposer(streamlineModule);
            }
            else
            {
                LOG_DEBUG("Skipping StreamlineHooks::hookInterposer - DLSSG output active");
            }
            slInterposerModule = streamlineModule;
        }
        else
        {
            LOG_ERROR("Trying to load dll: {}", libNameA);
        }

        return streamlineModule;
    }

    // sl.dlss.dll
    // Try to catch something like this:
    // C:\ProgramData/NVIDIA/NGX/models/sl_dlss_0/versions/133120/files/190_E658703.dll
    if (CheckDllNameW(&libName, &slDlssNamesW) ||
        (normalizedPath.contains(L"\\versions\\") && normalizedPath.contains(L"\\sl_dlss_0")))
    {
        auto dlssModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

        if (dlssModule != nullptr)
        {
            if (State::Instance().activeFgOutput != FGOutput::DLSSG)
                StreamlineHooks::hookDlss(dlssModule);
            else
                LOG_DEBUG("Skipping StreamlineHooks::hookDlss - DLSSG output active");
        }
        else
        {
            LOG_ERROR("Trying to load dll as sl.dlss: {}", libNameA);
        }

        return dlssModule;
    }

    // sl.dlss_g.dll
    if (CheckDllNameW(&libName, &slDlssgNamesW) ||
        (normalizedPath.contains(L"\\versions\\") && normalizedPath.contains(L"\\sl_dlss_g_")))
    {
        auto dlssgModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

        if (dlssgModule != nullptr)
        {
            // Always hook sl.dlss_g even for DLSSG output - needed for JSON patching
            // (hws.required=false, vsync.supported=true) and systemCaps spoofing.
            // The hook's slGetPluginFunction intercept is made output-aware in Streamline_Hooks.cpp.
            StreamlineHooks::hookDlssg(dlssgModule);
        }
        else
        {
            LOG_ERROR("Trying to load dll as sl.dlss_g: {}", libNameA);
        }

        return dlssgModule;
    }

    // sl.reflex.dll
    if (CheckDllNameW(&libName, &slReflexNamesW) ||
        (normalizedPath.contains(L"\\versions\\") && normalizedPath.contains(L"\\sl_reflex_")))
    {
        auto reflexModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

        if (reflexModule != nullptr)
        {
            if (State::Instance().activeFgOutput != FGOutput::DLSSG)
                StreamlineHooks::hookReflex(reflexModule);
            else
                LOG_DEBUG("Skipping StreamlineHooks::hookReflex - DLSSG output active");
        }
        else
        {
            LOG_ERROR("Trying to load dll as sl.reflex: {}", libNameA);
        }

        return reflexModule;
    }

    // sl.pcl.dll
    if (CheckDllNameW(&libName, &slPclNamesW) ||
        (normalizedPath.contains(L"\\versions\\") && normalizedPath.contains(L"\\sl_pcl_")))
    {
        auto pclModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

        if (pclModule != nullptr)
        {
            if (State::Instance().activeFgOutput != FGOutput::DLSSG)
                StreamlineHooks::hookPcl(pclModule);
            else
                LOG_DEBUG("Skipping StreamlineHooks::hookPcl - DLSSG output active");
        }
        else
        {
            LOG_ERROR("Trying to load dll as sl.pcl: {}", libNameA);
        }

        return pclModule;
    }

    // sl.common.dll
    if (CheckDllNameW(&libName, &slCommonNamesW) ||
        (normalizedPath.contains(L"\\versions\\") && normalizedPath.contains(L"\\sl_common_")))
    {
        auto commonModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

        if (commonModule != nullptr)
        {
            // Always hook sl.common even for DLSSG output - needed for systemCaps
            // hwsSupported spoofing and architecture spoofing in setArch().
            StreamlineHooks::hookCommon(commonModule);
        }
        else
        {
            LOG_ERROR("Trying to load dll as sl.common: {}", libNameA);
        }

        return commonModule;
    }

    if (Config::Instance()->DisableOverlays.value_or_default() && CheckDllNameW(&libName, &blockOverlayNamesW))
    {
        LOG_DEBUG("Blocking overlay dll: {}", wstring_to_string(libName));
        return (HMODULE) 1337;
    }
    else if (CheckDllNameW(&libName, &overlayNamesW))
    {
        LOG_DEBUG("Overlay dll: {}", wstring_to_string(libName));

        // If we hook CreateSwapChainForHwnd & CreateSwapChainForCoreWindow here
        // Order of CreateSwapChain calls become
        // Game -> Overlay -> Opti
        // and Overlays really does not like Opti's wrapped swapchain
        // If we skip hooking here first Steam hook CreateSwapChainForHwnd & CreateSwapChainForCoreWindow
        // Then hopefully Opti hook and call order become
        // Game -> Opti -> Overlay
        // And Opti menu works with Overlay without issues

        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, 0);

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

                if (DxgiProxy::CreateDxgiFactory2_()(0, __uuidof(factory2), &factory2) == S_OK && factory2 != nullptr)
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
    if (CheckDllNameW(&libName, &dx11NamesW))
    {
        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, 0);

        if (module != nullptr)
            D3D11Hooks::Hook(module);

        return module;
    }

    if (CheckDllNameW(&libName, &dx12NamesW))
    {
        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, 0);

        if (module != nullptr)
        {
            D3d12Proxy::Init(module);
            D3D12Hooks::Hook();
        }

        return module;
    }

    if (CheckDllNameW(&libName, &dx12agilityNamesW))
    {
        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, 0);

        if (module != nullptr)
            D3D12Hooks::HookAgility(module);

        return module;
    }

    if (CheckDllNameW(&libName, &vkNamesW))
    {
        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, 0);

        if (module != nullptr)
        {
            VulkanHooks::Hook(module);
        }

        return module;
    }

    if (!State::Instance().skipDxgiLoadChecks && CheckDllNameW(&libName, &dxgiNamesW))
    {
        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

        if (module != nullptr)
        {
            DxgiProxy::Init(module);
            DxgiHooks::Hook();
        }
    }

    if (CheckDllNameW(&libName, &fsr2NamesW))
    {
        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, 0);

        if (module != nullptr)
            HookFSR2Inputs(module);

        return module;
    }

    if (CheckDllNameW(&libName, &fsr2BENamesW))
    {
        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, 0);

        if (module != nullptr)
            HookFSR2Dx12Inputs(module);

        return module;
    }

    if (CheckDllNameW(&libName, &fsr3NamesW))
    {
        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, 0);

        if (module != nullptr)
            HookFSR3Inputs(module);

        return module;
    }

    if (CheckDllNameW(&libName, &fsr3BENamesW))
    {
        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, 0);

        if (module != nullptr)
            HookFSR3Dx12Inputs(module);

        return module;
    }

    if (CheckDllNameW(&libName, &xessNamesW))
    {
        auto module = LoadLibxess(libName);

        LOG_DEBUG("Libxess: {:X}", (size_t) module);

        if (module != nullptr)
            XeSSProxy::HookXeSS(module);

        return module;
    }

    if (CheckDllNameW(&libName, &xessDx11NamesW))
    {
        auto module = LoadLibxessDx11(libName);

        if (module != nullptr)
            XeSSProxy::HookXeSSDx11(module);
        else
            LOG_ERROR("Trying to load dll: {}", wstring_to_string(libName));

        return module;
    }

    if (CheckDllNameW(&libName, &ffxDx12NamesW))
    {
        auto module = LoadFfxapiDx12(libName);

        if (module != nullptr)
            FfxApiProxy::InitFfxDx12(module);

        return module;
    }

    if (CheckDllNameW(&libName, &ffxDx12UpscalerNamesW))
    {
        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, 0);

        FSR4ModelSelection::Hook(module, true);

        if (module != nullptr)
            FfxApiProxy::InitFfxDx12_SR(module);

        return module;
    }

    if (CheckDllNameW(&libName, &ffxDx12FGNamesW))
    {
        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, 0);

        if (module != nullptr)
            FfxApiProxy::InitFfxDx12_FG(module);

        return module;
    }

    if (CheckDllNameW(&libName, &ffxVkNamesW))
    {
        auto module = LoadFfxapiVk(libName);

        if (module != nullptr)
            FfxApiProxy::InitFfxVk(module);

        return module;
    }

    CheckModulesInMemory();

    return nullptr;
}

std::optional<NTSTATUS> LibraryLoadHooks::FreeLibrary(PVOID lpLibrary)
{
    std::optional<NTSTATUS> result;

    if (lpLibrary == dllModule)
    {
#ifdef LOG_LIB_OPERATIONS
        LOG_WARN("Call for OptiScaler, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
#endif
        State::Instance().modulesToFree.insert(lpLibrary);
        result = TRUE;
    }
    else if (lpLibrary == FfxApiProxy::Dx12Module())
    {
#ifdef LOG_LIB_OPERATIONS
        LOG_WARN("Call for FFX Dx12, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
#endif
        State::Instance().modulesToFree.insert(lpLibrary);
        result = TRUE;
    }
    else if (lpLibrary == FfxApiProxy::VkModule())
    {
#ifdef LOG_LIB_OPERATIONS
        LOG_WARN("Call for FFX Vulkan, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
#endif
        State::Instance().modulesToFree.insert(lpLibrary);
        result = TRUE;
    }
    else if (lpLibrary == XeSSProxy::Module())
    {
#ifdef LOG_LIB_OPERATIONS
        LOG_WARN("Call for XeSS, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
#endif
        State::Instance().modulesToFree.insert(lpLibrary);
        result = TRUE;
    }
    else if (lpLibrary == DxgiProxy::Module())
    {
#ifdef LOG_LIB_OPERATIONS
        LOG_WARN("Call for DXGI, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
#endif
        State::Instance().modulesToFree.insert(lpLibrary);
        result = TRUE;
    }
    else if (lpLibrary == D3d12Proxy::Module())
    {
#ifdef LOG_LIB_OPERATIONS
        LOG_WARN("Call for D3D12, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
#endif
        State::Instance().modulesToFree.insert(lpLibrary);
        result = TRUE;
    }
    else if (lpLibrary == Kernel32Proxy::Module())
    {
#ifdef LOG_LIB_OPERATIONS
        LOG_WARN("Call for Kernel32, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
#endif
        State::Instance().modulesToFree.insert(lpLibrary);
        result = TRUE;
    }
    else if (lpLibrary == KernelBaseProxy::Module())
    {
#ifdef LOG_LIB_OPERATIONS
        LOG_WARN("Call for KernelBase, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
#endif
        State::Instance().modulesToFree.insert(lpLibrary);
        result = TRUE;
    }
    else if (lpLibrary == NtdllProxy::Module())
    {
#ifdef LOG_LIB_OPERATIONS
        LOG_WARN("Call for ntdll, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
#endif
        State::Instance().modulesToFree.insert(lpLibrary);
        result = TRUE;
    }
    else if (lpLibrary == vulkanModule)
    {
#ifdef LOG_LIB_OPERATIONS
        LOG_WARN("Call for Vulkan, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
#endif
        State::Instance().modulesToFree.insert(lpLibrary);
        result = TRUE;
    }
    else if (lpLibrary == d3d11Module)
    {
#ifdef LOG_LIB_OPERATIONS
        LOG_WARN("Call for D3D11, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
#endif
        State::Instance().modulesToFree.insert(lpLibrary);
        result = TRUE;
    }
    return result;
}

HMODULE LibraryLoadHooks::LoadNvApi()
{
    LOG_FUNC();

    HMODULE nvapi = nullptr;

    if (Config::Instance()->NvapiDllPath.has_value())
    {
        LOG_DEBUG("Load NvapiDllPath");

        nvapi = NtdllProxy::LoadLibraryExW_Ldr(Config::Instance()->NvapiDllPath->c_str(), NULL, 0);

        if (nvapi != nullptr)
        {
            LOG_INFO("nvapi64.dll loaded from {0}", wstring_to_string(Config::Instance()->NvapiDllPath.value()));
            return nvapi;
        }
    }

    if (nvapi == nullptr)
    {
        LOG_DEBUG("Load nvapi64.dll");

        auto localPath = Util::DllPath().parent_path() / L"nvapi64.dll";
        nvapi = NtdllProxy::LoadLibraryExW_Ldr(localPath.wstring().c_str(), NULL, 0);

        if (nvapi != nullptr)
        {
            LOG_INFO("nvapi64.dll loaded from {0}", wstring_to_string(localPath.wstring()));
            return nvapi;
        }
    }

    if (nvapi == nullptr)
    {
        LOG_DEBUG("Load nvapi64.dll 2");

        nvapi = NtdllProxy::LoadLibraryExW_Ldr(L"nvapi64.dll", NULL, 0);

        if (nvapi != nullptr)
        {
            LOG_WARN("nvapi64.dll loaded from system!");
            return nvapi;
        }
    }

    return nullptr;
}

HMODULE LibraryLoadHooks::LoadNvngxDlss(std::wstring originalPath)
{
    HMODULE nvngxDlss = nullptr;

    if (Config::Instance()->NVNGX_DLSS_Library.has_value())
    {
        nvngxDlss = NtdllProxy::LoadLibraryExW_Ldr(Config::Instance()->NVNGX_DLSS_Library.value().c_str(), NULL, 0);

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
        nvngxDlss = NtdllProxy::LoadLibraryExW_Ldr(originalPath.c_str(), NULL, 0);

        if (nvngxDlss != nullptr)
        {
            LOG_INFO("nvngx_dlss.dll loaded from {0}", wstring_to_string(originalPath));
            return nvngxDlss;
        }
    }

    return nullptr;
}

HMODULE LibraryLoadHooks::LoadLibxess(std::wstring originalPath)
{
    if (XeSSProxy::Module() != nullptr)
        return XeSSProxy::Module();

    HMODULE libxess = nullptr;

    if (Config::Instance()->XeSSLibrary.has_value())
    {
        std::filesystem::path libPath(Config::Instance()->XeSSLibrary.value().c_str());

        if (libPath.has_filename())
            libxess = NtdllProxy::LoadLibraryExW_Ldr(libPath.c_str(), NULL, 0);
        else
            libxess = NtdllProxy::LoadLibraryExW_Ldr((libPath / L"libxess.dll").c_str(), NULL, 0);

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
        libxess = NtdllProxy::LoadLibraryExW_Ldr(originalPath.c_str(), NULL, 0);

        if (libxess != nullptr)
        {
            LOG_INFO("libxess.dll loaded from {0}", wstring_to_string(originalPath));
            return libxess;
        }
    }

    return nullptr;
}

HMODULE LibraryLoadHooks::LoadLibxessDx11(std::wstring originalPath)
{
    if (XeSSProxy::ModuleDx11() != nullptr)
        return XeSSProxy::ModuleDx11();

    HMODULE libxess = nullptr;

    if (Config::Instance()->XeSSDx11Library.has_value())
    {
        std::filesystem::path libPath(Config::Instance()->XeSSDx11Library.value().c_str());

        if (libPath.has_filename())
            libxess = NtdllProxy::LoadLibraryExW_Ldr(libPath.c_str(), NULL, 0);
        else
            libxess = NtdllProxy::LoadLibraryExW_Ldr((libPath / L"libxess_dx11.dll").c_str(), NULL, 0);

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
        libxess = NtdllProxy::LoadLibraryExW_Ldr(originalPath.c_str(), NULL, 0);

        if (libxess != nullptr)
        {
            LOG_INFO("libxess_dx11.dll loaded from {0}", wstring_to_string(originalPath));
            return libxess;
        }
    }

    return nullptr;
}

HMODULE LibraryLoadHooks::LoadFfxapiDx12(std::wstring originalPath)
{
    if (FfxApiProxy::Dx12Module() != nullptr)
        return FfxApiProxy::Dx12Module();

    HMODULE ffxDx12 = nullptr;

    std::vector<std::wstring> dllNames = { L"amd_fidelityfx_loader_dx12.dll", L"amd_fidelityfx_dx12.dll" };

    for (size_t i = 0; i < dllNames.size(); i++)
    {
        if (Config::Instance()->FfxDx12Path.has_value())
        {
            std::filesystem::path libPath(Config::Instance()->FfxDx12Path.value().c_str());

            if (libPath.has_filename())
                ffxDx12 = NtdllProxy::LoadLibraryExW_Ldr(libPath.c_str(), NULL, 0);
            else
                ffxDx12 = NtdllProxy::LoadLibraryExW_Ldr((libPath / dllNames[i]).c_str(), NULL, 0);

            if (ffxDx12 != nullptr)
            {
                LOG_INFO("{0} loaded from {1}", wstring_to_string(dllNames[i]),
                         wstring_to_string(Config::Instance()->FfxDx12Path.value()));
                return ffxDx12;
            }
            else
            {
                LOG_WARN("{0} can't found at {1}", wstring_to_string(dllNames[i]),
                         wstring_to_string(Config::Instance()->FfxDx12Path.value()));
            }
        }

        if (ffxDx12 == nullptr)
        {
            ffxDx12 = NtdllProxy::LoadLibraryExW_Ldr(originalPath.c_str(), NULL, 0);

            if (ffxDx12 != nullptr)
            {
                LOG_INFO("{0} loaded from {1}", wstring_to_string(dllNames[i]), wstring_to_string(originalPath));
                return ffxDx12;
            }
        }
    }

    return nullptr;
}

HMODULE LibraryLoadHooks::LoadFfxapiVk(std::wstring originalPath)
{
    if (FfxApiProxy::VkModule() != nullptr)
        return FfxApiProxy::VkModule();

    HMODULE ffxVk = nullptr;

    if (Config::Instance()->FfxVkPath.has_value())
    {
        std::filesystem::path libPath(Config::Instance()->FfxVkPath.value().c_str());

        if (libPath.has_filename())
            ffxVk = NtdllProxy::LoadLibraryExW_Ldr(libPath.c_str(), NULL, 0);
        else
            ffxVk = NtdllProxy::LoadLibraryExW_Ldr((libPath / L"amd_fidelityfx_vk.dll").c_str(), NULL, 0);

        if (ffxVk != nullptr)
        {
            LOG_INFO("amd_fidelityfx_vk.dll loaded from {0}", wstring_to_string(Config::Instance()->FfxVkPath.value()));
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
        ffxVk = NtdllProxy::LoadLibraryExW_Ldr(originalPath.c_str(), NULL, 0);

        if (ffxVk != nullptr)
        {
            LOG_INFO("amd_fidelityfx_vk.dll loaded from {0}", wstring_to_string(originalPath));
            return ffxVk;
        }
    }

    return nullptr;
}

void LibraryLoadHooks::CheckModulesInMemory()
{
    // When DLSSG is FG output: skip interposer/dlss/reflex/pcl hooks, but ALLOW dlssg+common hooks
    // (needed for JSON patching hws.required=false and systemCaps hwsSupported spoofing)
    bool skipSLHooks = (State::Instance().activeFgOutput == FGOutput::DLSSG);

    if (!skipSLHooks && !StreamlineHooks::isInterposerHooked())
    {
        // hook streamline right away if it's already loaded
        HMODULE slModule = nullptr;
        slModule = GetDllNameWModule(&slInterposerNamesW);
        if (slModule != nullptr)
        {
            LOG_DEBUG("sl.interposer.dll already in memory");
            StreamlineHooks::hookInterposer(slModule);
            slInterposerModule = slModule;
        }
    }

    if (!skipSLHooks && !StreamlineHooks::isDlssHooked())
    {
        HMODULE slDlss = nullptr;
        slDlss = GetDllNameWModule(&slDlssNamesW);
        if (slDlss != nullptr)
        {
            LOG_DEBUG("sl.dlss.dll already in memory");
            StreamlineHooks::hookDlss(slDlss);
        }
    }

    // Always hook sl.dlss_g - needed for JSON config patching even when DLSSG is output
    if (!StreamlineHooks::isDlssgHooked())
    {
        HMODULE slDlssg = nullptr;
        slDlssg = GetDllNameWModule(&slDlssgNamesW);
        if (slDlssg != nullptr)
        {
            LOG_DEBUG("sl.dlss_g.dll already in memory");
            StreamlineHooks::hookDlssg(slDlssg);
        }
    }

    if (!skipSLHooks && !StreamlineHooks::isReflexHooked())
    {
        HMODULE slReflex = nullptr;
        slReflex = GetDllNameWModule(&slReflexNamesW);
        if (slReflex != nullptr)
        {
            LOG_DEBUG("sl.reflex.dll already in memory");
            StreamlineHooks::hookReflex(slReflex);
        }
    }

    if (!skipSLHooks && !StreamlineHooks::isPclHooked())
    {
        HMODULE slPcl = nullptr;
        slPcl = GetDllNameWModule(&slPclNamesW);
        if (slPcl != nullptr)
        {
            LOG_DEBUG("sl.pcl.dll already in memory");
            StreamlineHooks::hookPcl(slPcl);
        }
    }

    // Always hook sl.common - needed for systemCaps hwsSupported spoofing even when DLSSG is output
    if (!StreamlineHooks::isCommonHooked())
    {
        HMODULE slCommon = nullptr;
        slCommon = GetDllNameWModule(&slCommonNamesW);
        if (slCommon != nullptr)
        {
            LOG_DEBUG("sl.common.dll already in memory");
            StreamlineHooks::hookCommon(slCommon);
        }
    }

    // XeSS
    if (XeSSProxy::Module() == nullptr)
    {
        HMODULE xessModule = nullptr;
        xessModule = GetDllNameWModule(&xessNamesW);
        if (xessModule != nullptr)
        {
            LOG_DEBUG("libxess.dll already in memory");
            XeSSProxy::HookXeSS(xessModule);
        }
    }

    if (XeSSProxy::ModuleDx11() == nullptr)
    {
        HMODULE xessDx11Module = nullptr;
        xessDx11Module = GetDllNameWModule(&xessDx11NamesW);
        if (xessDx11Module != nullptr)
        {
            LOG_DEBUG("libxess_dx11.dll already in memory");
            XeSSProxy::HookXeSSDx11(xessDx11Module);
        }
    }

    // FFX Dx12
    if (FfxApiProxy::Dx12Module() == nullptr)
    {
        HMODULE ffxDx12Module = nullptr;
        ffxDx12Module = GetDllNameWModule(&ffxDx12NamesW);
        if (ffxDx12Module != nullptr)
        {
            LOG_DEBUG("amd_fidelityfx_dx12.dll already in memory");
            FfxApiProxy::InitFfxDx12(ffxDx12Module);
        }
    }

    // FFX Vulkan
    if (FfxApiProxy::VkModule() == nullptr)
    {
        HMODULE ffxVkModule = nullptr;
        ffxVkModule = GetDllNameWModule(&ffxVkNamesW);
        if (ffxVkModule != nullptr)
        {
            LOG_DEBUG("amd_fidelityfx_vk.dll already in memory");
            FfxApiProxy::InitFfxVk(ffxVkModule);
        }
    }
}

bool LibraryLoadHooks::EndsWithInsensitive(std::wstring_view text, std::wstring_view suffix)
{
    if (suffix.size() > text.size())
        return false;

    if (suffix.empty())
        return true;

    const wchar_t* tail = text.data() + (text.size() - suffix.size());
    const int res =
        CompareStringOrdinal(tail, static_cast<int>(suffix.size()), suffix.data(), static_cast<int>(suffix.size()),
                             TRUE); // case-insensitive
    return res == CSTR_EQUAL;
}

bool LibraryLoadHooks::EndsWithInsensitive(const UNICODE_STRING& text, std::wstring_view suffix)
{
    return EndsWithInsensitive(std::wstring_view { text.Buffer, static_cast<size_t>(text.Length) / sizeof(wchar_t) },
                               suffix);
}

bool LibraryLoadHooks::StartsWithInsensitive(std::wstring_view str, std::wstring_view prefix)
{
    if (str.size() < prefix.size())
        return false;

    for (size_t i = 0; i < prefix.size(); ++i)
    {
        if (std::towlower(str[i]) != std::towlower(prefix[i]))
            return false;
    }

    return true;
}

bool LibraryLoadHooks::IsApiSetName(const std::wstring_view& n)
{
    return StartsWithInsensitive(n, L"api-ms-win-") || StartsWithInsensitive(n, L"ext-ms-") ||
           StartsWithInsensitive(n, L"api-ms-onecore-");
}
