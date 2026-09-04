#include "pch.h"
#include "LibraryLoad_Hooks.h"

#include <Config.h>
#include <DllNames.h>

#include <framegen/dlssg/MfgUnlock.h>

#include <proxies/Ntdll_Proxy.h>
#include <proxies/Kernel32_Proxy.h>
#include <proxies/Dxgi_Proxy.h>
#include <proxies/D3D12_Proxy.h>

#include <proxies/XeSS_Proxy.h>
#include <proxies/XeFG_Proxy.h>
#include <proxies/XeLL_Proxy.h>
#include <proxies/NVNGX_Proxy.h>
#include <proxies/FfxApi_Proxy.h>

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
#include <fsr4/FSR4Upgrade.h>
#include <misc/IdentifyGpu.h>
#include <low_latency/input/input_uell.h>

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
    auto path = std::filesystem::path(libName).lexically_normal();
    auto normalizedPath = path.wstring();
    to_lower_in_place(normalizedPath);

    std::filesystem::path localSlPath(Config::Instance()->MainDllPath.value());
    localSlPath = localSlPath / L"streamline"; // Hardcoded streamline folder
    auto normalizedLocalSlPath = localSlPath.lexically_normal();

    const bool pathInsideLocalSlPath = Util::IsSubpath(path, normalizedLocalSlPath);

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

    if ((State::Instance().workingMode != WorkingMode::Dxgi || !State::Instance().skipDxgiLoadChecks) &&
        CheckDllNameW(&libName, &dllNamesW))
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

    // nvngx_dlssg
    //
    // The module publishes DLSSG.MultiFrameCountMax while it initialises and slDLSSGGetState returns
    // the published value rather than recomputing it, so the patch has to be in before anything in
    // here runs. Patching at the first GetState is one call too late.
    if (libName.contains(L"nvngx_dlssg"))
    {
        auto dlssgSnippet = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

        if (dlssgSnippet != nullptr)
            MfgUnlock::TryApply();
        else
            LOG_ERROR("Trying to load dll as nvngx_dlssg: {}", libNameA);

        return dlssgSnippet;
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
        LOG_INFO("{} call!", libNameA);

        return LibraryLoadHooks::LoadNvApi();
    }

    // Hook SL from local path if using Nvngx FG (and probably upgrading SL for it)
    const bool shouldHookSl = !pathInsideLocalSlPath || State::Instance().activeFgInput == FGInput::NvngxFG;

    // sl.interposer.dll
    if (CheckDllNameW(&libName, &slInterposerNamesW) && shouldHookSl)
    {
        auto streamlineModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

        if (streamlineModule != nullptr && streamlineModule != State::Instance().optiSlInterposer)
        {
            StreamlineHooks::hookInterposer(streamlineModule);
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
    if (shouldHookSl && (CheckDllNameW(&libName, &slDlssNamesW) ||
                         (normalizedPath.contains(L"\\versions\\") && normalizedPath.contains(L"\\sl_dlss_0"))))
    {
        auto dlssModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

        if (dlssModule != nullptr)
        {
            StreamlineHooks::hookDlss(dlssModule);
        }
        else
        {
            LOG_ERROR("Trying to load dll as sl.dlss: {}", libNameA);
        }

        return dlssModule;
    }

    // sl.dlss_g.dll
    if ((CheckDllNameW(&libName, &slDlssgNamesW) ||
         (normalizedPath.contains(L"\\versions\\") && normalizedPath.contains(L"\\sl_dlss_g_"))))
    {
        auto dlssgModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

        if (dlssgModule != nullptr)
        {
            const bool localDlssg = pathInsideLocalSlPath && State::Instance().activeFgOutput == FGOutput::DLSSG;

            if (!localDlssg && dlssgModule != State::Instance().optiSlDLSSG)
                StreamlineHooks::hookDlssg(dlssgModule);
            else
                StreamlineHooks::hookLocalDlssg(dlssgModule);
        }
        else
        {
            LOG_ERROR("Trying to load dll as sl.dlss_g: {}", libNameA);
        }

        return dlssgModule;
    }

    // sl.reflex.dll
    if (shouldHookSl && (CheckDllNameW(&libName, &slReflexNamesW) ||
                         (normalizedPath.contains(L"\\versions\\") && normalizedPath.contains(L"\\sl_reflex_"))))
    {
        auto reflexModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

        if (reflexModule != nullptr && reflexModule != State::Instance().optiSlReflex)
        {
            StreamlineHooks::hookReflex(reflexModule);
        }
        else
        {
            LOG_ERROR("Trying to load dll as sl.reflex: {}", libNameA);
        }

        return reflexModule;
    }

    // sl.pcl.dll
    if (shouldHookSl && (CheckDllNameW(&libName, &slPclNamesW) ||
                         (normalizedPath.contains(L"\\versions\\") && normalizedPath.contains(L"\\sl_pcl_"))))
    {
        auto pclModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

        if (pclModule != nullptr && pclModule != State::Instance().optiSlPCL)
        {
            StreamlineHooks::hookPcl(pclModule);
        }
        else
        {
            LOG_ERROR("Trying to load dll as sl.pcl: {}", libNameA);
        }

        return pclModule;
    }

    // sl.common.dll
    if (shouldHookSl && (CheckDllNameW(&libName, &slCommonNamesW) ||
                         (normalizedPath.contains(L"\\versions\\") && normalizedPath.contains(L"\\sl_common_"))))
    {
        auto commonModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

        if (commonModule != nullptr && commonModule != State::Instance().optiSlCommon)
        {
            StreamlineHooks::hookCommon(commonModule);
        }
        else
        {
            LOG_ERROR("Trying to load dll as sl.common: {}", libNameA);
        }

        return commonModule;
    }

    // Make EOS block separate as it's the only one having issues with non-OptiFG FGs
    if (!Config::Instance()->DisableOverlays.has_value() && State::Instance().activeFgOutput != FGOutput::NoFG &&
        CheckDllNameW(&libName, &eosOverlayNamesW))
    {
        LOG_DEBUG("Blocking overlay dll: {}", wstring_to_string(libName));
        return (HMODULE) 1337;
    }

    if (CheckDllNameW(&libName, &blockedDllNamesW))
    {
        LOG_DEBUG("Blocking dll: {}", wstring_to_string(libName));
        return (HMODULE) 1337;
    }
    else if (Config::Instance()->DisableOverlays.value_or_default() && CheckDllNameW(&libName, &blockOverlayNamesW))
    {
        LOG_DEBUG("Blocking overlay dll: {}", wstring_to_string(libName));
        return (HMODULE) 1337;
    }
    else if (CheckDllNameW(&libName, &overlayNamesW))
    {
        // If we hook CreateSwapChainForHwnd & CreateSwapChainForCoreWindow here
        // Order of CreateSwapChain calls become
        // Game -> Overlay -> Opti
        // and Overlays really does not like Opti's wrapped swapchain
        // If we skip hooking here first Steam hook CreateSwapChainForHwnd & CreateSwapChainForCoreWindow
        // Then hopefully Opti hook and call order become
        // Game -> Opti -> Overlay
        // And Opti menu works with Overlay without issues

        std::filesystem::path path(libName);
        auto dllName = path.filename().string();

        thread_local bool insideThisCode = false;
        if (insideThisCode)
            return nullptr;

        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, 0);

        LOG_DEBUG("Overlay dll: {}, module: {:X}", wstring_to_string(libName), (size_t) module);

        if (module != nullptr)
        {
            if (DxgiProxy::Module() != nullptr)
            {
                LOG_INFO("Calling CreateDxgiFactory methods for overlay!");
                IDXGIFactory* factory = nullptr;
                IDXGIFactory1* factory1 = nullptr;
                IDXGIFactory2* factory2 = nullptr;

                auto ocResult = DxgiProxy::CreateDxgiFactory_()(__uuidof(factory), &factory);
                LOG_DEBUG("CreateDxgiFactory result: {:X}", ocResult);

                if (ocResult == S_OK && factory != nullptr)
                {
                    insideThisCode = true;
                    LOG_DEBUG("CreateDxgiFactory ok");
                    factory->Release();
                }

                ocResult = DxgiProxy::CreateDxgiFactory1_()(__uuidof(factory1), &factory1);
                LOG_DEBUG("CreateDxgiFactory1 result: {:X}", ocResult);

                if (ocResult == S_OK && factory1 != nullptr)
                {
                    LOG_DEBUG("CreateDxgiFactory1 ok");
                    factory1->Release();
                }

                ocResult = DxgiProxy::CreateDxgiFactory2_()(0, __uuidof(factory2), &factory2);
                LOG_DEBUG("CreateDxgiFactory2 result: {:X}", ocResult);

                if (ocResult == S_OK && factory2 != nullptr)
                {
                    LOG_DEBUG("CreateDxgiFactory2 ok");
                    factory2->Release();
                }
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
            // Prevent vulkan-1 from unloading so that our hooks are valid
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, L"vulkan-1", &module);
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
        if (XeSSProxy::Module() != nullptr)
        {
            auto module = NtdllProxy::LoadLibraryExW_Ldr(XeSSProxy::Module_Path().c_str(), NULL, NULL);
            return module;
        }

        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, NULL);

        if (module != nullptr)
            XeSSProxy::InitXeSS(module);

        return module;
    }

    if (CheckDllNameW(&libName, &xessDx11NamesW))
    {
        if (XeSSProxy::ModuleDx11() != nullptr)
        {
            auto module = NtdllProxy::LoadLibraryExW_Ldr(XeSSProxy::ModuleDx11_Path().c_str(), NULL, NULL);
            return module;
        }

        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, NULL);

        if (module != nullptr)
            XeSSProxy::InitXeSSDx11(module);

        return module;
    }

    if (CheckDllNameW(&libName, &amdxc64NamesW))
    {
        HMODULE moduleAmdxc64 = nullptr;

        if (Config::Instance()->LoadCustomAmdxc64OnRdna2.value_or_default())
        {
            auto allDetectedGpus = IdentifyGpu::getAllGpus();
            GpuInformation gpuMatchingDriverStore {};

            for (auto& gpu : allDetectedGpus)
            {
                // Only grabbing the first one
                if (Util::IsSubpath(path, gpu.driverStore))
                {
                    gpuMatchingDriverStore = gpu;
                    break;
                }
            }

            // kGfx10_3 == RDNA 2
            if (gpuMatchingDriverStore.amdHwGeneration == device_info::HwGeneration::kGfx10_3)
            {
                LOG_INFO("Trying to loading custom amdxc64.dll");

                HMODULE memModule = nullptr;
                auto& optiPath = Config::Instance()->MainDllPath.value();
                Util::LoadProxyLibrary(L"amdxc64.dll", L"", optiPath, &memModule, &moduleAmdxc64);

                if (moduleAmdxc64 == nullptr && memModule != nullptr)
                    moduleAmdxc64 = memModule;
            }
        }

        if (moduleAmdxc64 == nullptr && !Config::Instance()->Fsr4DoNotLoadAmdxc64.value_or_default())
            moduleAmdxc64 = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, 0);

        if (moduleAmdxc64 != nullptr)
            Amdxc64Hooks::Init();

        return moduleAmdxc64;
    }

    if (CheckDllNameW(&libName, &ffxDx12NamesW))
    {
        if (FfxApiProxy::Dx12Module() != nullptr)
        {
            auto module = NtdllProxy::LoadLibraryExW_Ldr(FfxApiProxy::Dx12Module_Path().c_str(), NULL, NULL);
            return module;
        }

        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, NULL);

        if (module != nullptr)
            FfxApiProxy::InitFfxDx12(module);

        return module;
    }

    if (CheckDllNameW(&libName, &ffxDx12UpscalerNamesW))
    {
        if (FfxApiProxy::Dx12Module_SR() != nullptr)
        {
            auto module = NtdllProxy::LoadLibraryExW_Ldr(FfxApiProxy::Dx12Module_SR_Path().c_str(), NULL, NULL);
            return module;
        }

        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, NULL);

        if (module != nullptr)
            FfxApiProxy::InitFfxDx12_SR(module);

        return module;
    }

    if (CheckDllNameW(&libName, &ffxDx12FGNamesW))
    {
        if (FfxApiProxy::Dx12Module_FG() != nullptr)
        {
            auto module = NtdllProxy::LoadLibraryExW_Ldr(FfxApiProxy::Dx12Module_FG_Path().c_str(), NULL, NULL);
            return module;
        }

        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, NULL);

        if (module != nullptr)
            FfxApiProxy::InitFfxDx12_FG(module);

        return module;
    }

    if (CheckDllNameW(&libName, &ffxVkNamesW))
    {
        if (FfxApiProxy::VkModule() != nullptr)
        {
            auto module = NtdllProxy::LoadLibraryExW_Ldr(FfxApiProxy::VkModule_Path().c_str(), NULL, NULL);
            return module;
        }

        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, NULL);

        if (module != nullptr)
            FfxApiProxy::InitFfxVk(module);

        return module;
    }

    // UeLL
    // This will try to hook all UE4SS dll mods but only our one should be exporting those functions
    if (CheckDllNameW(&libName, &uellNamesW))
    {
        LOG_INFO("{} call!", libNameA);

        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, NULL);

        auto setTickStartCallback =
            (PFN_setTickCallback) KernelBaseProxy::GetProcAddress_()(module, "setTickStartCallback");
        auto setTickEndCallback =
            (PFN_setTickCallback) KernelBaseProxy::GetProcAddress_()(module, "setTickEndCallback");

        if (setTickStartCallback)
            setTickStartCallback(InputUeLowLatency::tickStart);

        if (setTickEndCallback)
            setTickEndCallback(InputUeLowLatency::tickEnd);

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

    // Opti exports a query function that nvapi would export
    if (Config::Instance()->UseFakenvapi.value_or_default() &&
        IdentifyGpu::getPrimaryGpu().vendorId != VendorId::Nvidia)
    {
        nvapi = dllModule;
        fakenvapi::setUsingAsMainNvapi(true);

        if (GetModuleHandleW(L"nvapi64.dll"))
            LOG_WARN("nvapi64.dll is loaded when Nvidia is not the primary GPU");
    }

    // Try to load nvapi only from system32, like the original call would
    if (nvapi == nullptr)
        nvapi = NtdllProxy::LoadLibraryExW_Ldr(L"nvapi64.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

    // Fallback for Nvidia, doubt it's helpful
    if (Config::Instance()->UseFakenvapi.value_or_default() && nvapi == nullptr)
    {
        LOG_WARN("Using fakenvapi on Nvidia as the main nvapi");
        nvapi = dllModule;
        fakenvapi::setUsingAsMainNvapi(true);
    }

    if (nvapi != nullptr)
        NvApiHooks::Hook(nvapi);

    return nvapi;
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

// HMODULE LibraryLoadHooks::LoadLibxess(std::wstring originalPath)
//{
//     if (XeSSProxy::Module() != nullptr)
//         return XeSSProxy::Module();
//
//     HMODULE libxess = nullptr;
//
//     if (Config::Instance()->XeSSLibrary.has_value())
//     {
//         std::filesystem::path libPath(Config::Instance()->XeSSLibrary.value().c_str());
//
//         if (libPath.has_filename())
//             libxess = NtdllProxy::LoadLibraryExW_Ldr(libPath.c_str(), NULL, 0);
//         else
//             libxess = NtdllProxy::LoadLibraryExW_Ldr((libPath / L"libxess.dll").c_str(), NULL, 0);
//
//         if (libxess != nullptr)
//         {
//             LOG_INFO("libxess.dll loaded from {0}", wstring_to_string(Config::Instance()->XeSSLibrary.value()));
//             return libxess;
//         }
//         else
//         {
//             LOG_WARN("libxess.dll can't found at {0}", wstring_to_string(Config::Instance()->XeSSLibrary.value()));
//         }
//     }
//
//     if (libxess == nullptr)
//     {
//         libxess = NtdllProxy::LoadLibraryExW_Ldr(originalPath.c_str(), NULL, 0);
//
//         if (libxess != nullptr)
//         {
//             LOG_INFO("libxess.dll loaded from {0}", wstring_to_string(originalPath));
//             return libxess;
//         }
//     }
//
//     return nullptr;
// }
//
// HMODULE LibraryLoadHooks::LoadLibxessDx11(std::wstring originalPath)
//{
//     if (XeSSProxy::ModuleDx11() != nullptr)
//         return XeSSProxy::ModuleDx11();
//
//     HMODULE libxess = nullptr;
//
//     if (Config::Instance()->XeSSDx11Library.has_value())
//     {
//         std::filesystem::path libPath(Config::Instance()->XeSSDx11Library.value().c_str());
//
//         if (libPath.has_filename())
//             libxess = NtdllProxy::LoadLibraryExW_Ldr(libPath.c_str(), NULL, 0);
//         else
//             libxess = NtdllProxy::LoadLibraryExW_Ldr((libPath / L"libxess_dx11.dll").c_str(), NULL, 0);
//
//         if (libxess != nullptr)
//         {
//             LOG_INFO("libxess_dx11.dll loaded from {0}",
//                      wstring_to_string(Config::Instance()->XeSSDx11Library.value()));
//             return libxess;
//         }
//         else
//         {
//             LOG_WARN("libxess_dx11.dll can't found at {0}",
//                      wstring_to_string(Config::Instance()->XeSSDx11Library.value()));
//         }
//     }
//
//     if (libxess == nullptr)
//     {
//         libxess = NtdllProxy::LoadLibraryExW_Ldr(originalPath.c_str(), NULL, 0);
//
//         if (libxess != nullptr)
//         {
//             LOG_INFO("libxess_dx11.dll loaded from {0}", wstring_to_string(originalPath));
//             return libxess;
//         }
//     }
//
//     return nullptr;
// }
//
// HMODULE LibraryLoadHooks::LoadFfxapiDx12(std::wstring originalPath)
//{
//     if (FfxApiProxy::Dx12Module() != nullptr)
//         return FfxApiProxy::Dx12Module();
//
//     HMODULE ffxDx12 = nullptr;
//
//     std::vector<std::wstring> dllNames = { L"amd_fidelityfx_loader_dx12.dll", L"amd_fidelityfx_dx12.dll" };
//
//     for (size_t i = 0; i < dllNames.size(); i++)
//     {
//         if (Config::Instance()->FfxDx12Path.has_value())
//         {
//             std::filesystem::path libPath(Config::Instance()->FfxDx12Path.value().c_str());
//
//             if (libPath.has_filename())
//                 ffxDx12 = NtdllProxy::LoadLibraryExW_Ldr(libPath.c_str(), NULL, 0);
//             else
//                 ffxDx12 = NtdllProxy::LoadLibraryExW_Ldr((libPath / dllNames[i]).c_str(), NULL, 0);
//
//             if (ffxDx12 != nullptr)
//             {
//                 LOG_INFO("{0} loaded from {1}", wstring_to_string(dllNames[i]),
//                          wstring_to_string(Config::Instance()->FfxDx12Path.value()));
//                 return ffxDx12;
//             }
//             else
//             {
//                 LOG_WARN("{0} can't found at {1}", wstring_to_string(dllNames[i]),
//                          wstring_to_string(Config::Instance()->FfxDx12Path.value()));
//             }
//         }
//
//         if (ffxDx12 == nullptr)
//         {
//             ffxDx12 = NtdllProxy::LoadLibraryExW_Ldr(originalPath.c_str(), NULL, 0);
//
//             if (ffxDx12 != nullptr)
//             {
//                 LOG_INFO("{0} loaded from {1}", wstring_to_string(dllNames[i]), wstring_to_string(originalPath));
//                 return ffxDx12;
//             }
//         }
//     }
//
//     return nullptr;
// }
//
// HMODULE LibraryLoadHooks::LoadFfxapiVk(std::wstring originalPath)
//{
//     if (FfxApiProxy::VkModule() != nullptr)
//         return FfxApiProxy::VkModule();
//
//     HMODULE ffxVk = nullptr;
//
//     if (Config::Instance()->FfxVkPath.has_value())
//     {
//         std::filesystem::path libPath(Config::Instance()->FfxVkPath.value().c_str());
//
//         if (libPath.has_filename())
//             ffxVk = NtdllProxy::LoadLibraryExW_Ldr(libPath.c_str(), NULL, 0);
//         else
//             ffxVk = NtdllProxy::LoadLibraryExW_Ldr((libPath / L"amd_fidelityfx_vk.dll").c_str(), NULL, 0);
//
//         if (ffxVk != nullptr)
//         {
//             LOG_INFO("amd_fidelityfx_vk.dll loaded from {0}",
//             wstring_to_string(Config::Instance()->FfxVkPath.value())); return ffxVk;
//         }
//         else
//         {
//             LOG_WARN("amd_fidelityfx_vk.dll can't found at {0}",
//                      wstring_to_string(Config::Instance()->FfxVkPath.value()));
//         }
//     }
//
//     if (ffxVk == nullptr)
//     {
//         ffxVk = NtdllProxy::LoadLibraryExW_Ldr(originalPath.c_str(), NULL, 0);
//
//         if (ffxVk != nullptr)
//         {
//             LOG_INFO("amd_fidelityfx_vk.dll loaded from {0}", wstring_to_string(originalPath));
//             return ffxVk;
//         }
//     }
//
//     return nullptr;
// }

void LibraryLoadHooks::CheckModulesInMemory()
{
    if (!StreamlineHooks::isInterposerHooked())
    {
        // hook streamline right away if it's already loaded
        HMODULE slModule = nullptr;
        slModule = GetDllNameWModule(&slInterposerNamesW);
        if (slModule != nullptr && slModule != State::Instance().optiSlInterposer)
        {
            LOG_DEBUG("sl.interposer.dll already in memory");
            StreamlineHooks::hookInterposer(slModule);
            slInterposerModule = slModule;
        }
    }

    if (!StreamlineHooks::isDlssHooked())
    {
        HMODULE slDlss = nullptr;
        slDlss = GetDllNameWModule(&slDlssNamesW);
        if (slDlss != nullptr)
        {
            LOG_DEBUG("sl.dlss.dll already in memory");
            StreamlineHooks::hookDlss(slDlss);
        }
    }

    const auto isLocalStreamlineModule = [](HMODULE module) -> bool
    {
        if (module == nullptr)
            return false;

        char modulePath[MAX_PATH] = {};
        if (GetModuleFileNameA(module, modulePath, sizeof(modulePath)) == 0)
            return false;

        const auto path = std::filesystem::path(modulePath).lexically_normal();
        const std::filesystem::path localSlPath =
            std::filesystem::path(Config::Instance()->MainDllPath.value()) / L"streamline";
        return Util::IsSubpath(path, localSlPath.lexically_normal());
    };

    // DLSS-G
    if (!StreamlineHooks::isDlssgHooked() || !StreamlineHooks::isLocalDlssgHooked())
    {
        HMODULE slDlssg = GetDllNameWModule(&slDlssgNamesW);

        if (slDlssg != nullptr && slDlssg != State::Instance().optiSlDLSSG)
        {
            const bool localDlssg = isLocalStreamlineModule(slDlssg);

            if (localDlssg && State::Instance().activeFgOutput == FGOutput::DLSSG)
            {
                if (!StreamlineHooks::isLocalDlssgHooked())
                {
                    LOG_DEBUG("local sl.dlss_g.dll already in memory");
                    StreamlineHooks::hookLocalDlssg(slDlssg);
                }
            }
            else if (!localDlssg && !StreamlineHooks::isDlssgHooked())
            {
                LOG_DEBUG("sl.dlss_g.dll already in memory");
                StreamlineHooks::hookDlssg(slDlssg);
            }
        }
    }

    // Reflex
    if (!StreamlineHooks::isReflexHooked())
    {
        HMODULE slReflex = GetDllNameWModule(&slReflexNamesW);

        if (slReflex != nullptr && slReflex != State::Instance().optiSlReflex)
        {
            const bool localReflex = isLocalStreamlineModule(slReflex);

            if (!localReflex || State::Instance().activeFgOutput != FGOutput::DLSSG)
            {
                if (localReflex)
                    LOG_DEBUG("local sl.reflex.dll already in memory");
                else
                    LOG_DEBUG("sl.reflex.dll already in memory");

                StreamlineHooks::hookReflex(slReflex);
            }
        }
    }

    // PCL
    if (!StreamlineHooks::isPclHooked())
    {
        HMODULE slPcl = GetDllNameWModule(&slPclNamesW);

        if (slPcl != nullptr && slPcl != State::Instance().optiSlPCL)
        {
            const bool localPcl = isLocalStreamlineModule(slPcl);

            if (!localPcl || State::Instance().activeFgOutput != FGOutput::DLSSG)
            {
                if (localPcl)
                    LOG_DEBUG("local sl.pcl.dll already in memory");
                else
                    LOG_DEBUG("sl.pcl.dll already in memory");

                StreamlineHooks::hookPcl(slPcl);
            }
        }
    }

    if (!StreamlineHooks::isCommonHooked())
    {
        HMODULE slCommon = nullptr;
        slCommon = GetDllNameWModule(&slCommonNamesW);
        if (slCommon != nullptr && slCommon != State::Instance().optiSlCommon)
        {
            LOG_DEBUG("sl.common.dll already in memory");
            StreamlineHooks::hookCommon(slCommon);
        }
    }

    //// XeSS
    // if (XeSSProxy::Module() == nullptr)
    //{
    //     HMODULE xessModule = nullptr;
    //     xessModule = GetDllNameWModule(&xessNamesW);
    //     if (xessModule != nullptr)
    //     {
    //         LOG_DEBUG("libxess.dll already in memory");
    //         XeSSProxy::HookXeSS(xessModule);
    //     }
    // }

    // if (XeSSProxy::ModuleDx11() == nullptr)
    //{
    //     HMODULE xessDx11Module = nullptr;
    //     xessDx11Module = GetDllNameWModule(&xessDx11NamesW);
    //     if (xessDx11Module != nullptr)
    //     {
    //         LOG_DEBUG("libxess_dx11.dll already in memory");
    //         XeSSProxy::HookXeSSDx11(xessDx11Module);
    //     }
    // }

    //// FFX Dx12
    // if (FfxApiProxy::Dx12Module() == nullptr)
    //{
    //     HMODULE ffxDx12Module = nullptr;
    //     ffxDx12Module = GetDllNameWModule(&ffxDx12NamesW);
    //     if (ffxDx12Module != nullptr)
    //     {
    //         LOG_DEBUG("amd_fidelityfx_dx12.dll already in memory");
    //         FfxApiProxy::InitFfxDx12(ffxDx12Module);
    //     }
    // }

    //// FFX Vulkan
    // if (FfxApiProxy::VkModule() == nullptr)
    //{
    //     HMODULE ffxVkModule = nullptr;
    //     ffxVkModule = GetDllNameWModule(&ffxVkNamesW);
    //     if (ffxVkModule != nullptr)
    //     {
    //         LOG_DEBUG("amd_fidelityfx_vk.dll already in memory");
    //         FfxApiProxy::InitFfxVk(ffxVkModule);
    //     }
    // }
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
