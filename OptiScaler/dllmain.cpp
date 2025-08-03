#include "dllmain.h"

#include "Util.h"
#include "Config.h"
#include "Logger.h"
#include "resource.h"
#include "DllNames.h"
#include "FSR4Upgrade.h"

#include "proxies/Dxgi_Proxy.h"
#include <proxies/XeSS_Proxy.h>
#include <proxies/NVNGX_Proxy.h>
#include <hooks/Gdi32_Hooks.h>
#include <hooks/Wintrust_Hooks.h>
#include <hooks/Crypt32_Hooks.h>
#include <hooks/Advapi32_Hooks.h>
#include <hooks/Streamline_Hooks.h>
#include "proxies/Kernel32_Proxy.h"
#include "proxies/KernelBase_Proxy.h"
#include <proxies/IGDExt_Proxy.h>

#include "inputs/FSR2_Dx12.h"
#include "inputs/FSR3_Dx12.h"
#include "inputs/FfxApi_Dx12.h"
#include "inputs/FfxApi_Vk.h"
#include "inputs/FfxApiExe_Dx12.h"

#include "spoofing/Vulkan_Spoofing.h"

#include <hooks/HooksDx.h>
#include <hooks/HooksVk.h>
#include <hooks/Ntdll_Hooks.h>
#include <hooks/Kernel_Hooks.h>

#include <nvapi/NvApiHooks.h>

#include <cwctype>

static std::vector<HMODULE> _asiHandles;

#pragma warning(disable : 4996)

typedef const char*(CDECL* PFN_wine_get_version)(void);
typedef void (*PFN_InitializeASI)(void);
typedef bool (*PFN_PatchResult)(void);

static inline void* ManualGetProcAddress(HMODULE hModule, const char* functionName)
{
    if (!hModule)
        return nullptr;

    // Verify the alignment
    auto dosHeader = (IMAGE_DOS_HEADER*) hModule;
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        return nullptr;

    auto ntHeaders = (IMAGE_NT_HEADERS*) ((BYTE*) hModule + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
        return nullptr;

    // Look at the export directory
    IMAGE_DATA_DIRECTORY exportData = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!exportData.VirtualAddress)
        return nullptr;

    auto exportDir = (IMAGE_EXPORT_DIRECTORY*) ((BYTE*) hModule + exportData.VirtualAddress);

    DWORD* nameRvas = (DWORD*) ((BYTE*) hModule + exportDir->AddressOfNames);
    WORD* ordinalTable = (WORD*) ((BYTE*) hModule + exportDir->AddressOfNameOrdinals);
    DWORD* functionTable = (DWORD*) ((BYTE*) hModule + exportDir->AddressOfFunctions);

    // Iterate over exported names
    for (DWORD i = 0; i < exportDir->NumberOfNames; ++i)
    {
        const char* name = (const char*) hModule + nameRvas[i];
        if (_stricmp(name, functionName) == 0)
        {
            WORD ordinal = ordinalTable[i];
            DWORD funcRva = functionTable[ordinal];
            return (BYTE*) hModule + funcRva;
        }
    }

    return nullptr; // Not found
}

static bool IsRunningOnWine()
{
    LOG_FUNC();

    HMODULE ntdll = GetModuleHandle(L"ntdll.dll");

    if (!ntdll)
    {
        LOG_WARN("Not running on NT!?!");
        return true;
    }

    auto pWineGetVersion = (PFN_wine_get_version) KernelBaseProxy::GetProcAddress_()(ntdll, "wine_get_version");

    // Workaround for the ntdll-Hide_Wine_Exports patch
    if (!pWineGetVersion && KernelBaseProxy::GetProcAddress_()(ntdll, "wine_server_call") != nullptr)
        pWineGetVersion = (PFN_wine_get_version) ManualGetProcAddress(ntdll, "wine_get_version");

    if (pWineGetVersion)
    {
        LOG_INFO("Running on Wine {0}!", pWineGetVersion());
        return true;
    }

    LOG_WARN("Wine not detected");
    return false;
}

UINT customD3D12SDKVersion = 615;

const char8_t* customD3D12SDKPath = u8".\\D3D12_Optiscaler\\"; // Hardcoded for now

static void RunAgilityUpgrade(HMODULE dx12Module)
{
    typedef HRESULT (*PFN_IsDeveloperModeEnabled)(BOOL* isEnabled);
    PFN_IsDeveloperModeEnabled o_IsDeveloperModeEnabled =
        (PFN_IsDeveloperModeEnabled) GetProcAddress(GetModuleHandle(L"kernelbase.dll"), "IsDeveloperModeEnabled");

    if (o_IsDeveloperModeEnabled == nullptr)
    {
        LOG_ERROR("Failed to get IsDeveloperModeEnabled function address");
        return;
    }

    auto hk_IsDeveloperModeEnabled = [](BOOL* isEnabled) -> HRESULT
    {
        *isEnabled = TRUE;
        return S_OK;
    };

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&) o_IsDeveloperModeEnabled, static_cast<HRESULT (*)(BOOL*)>(hk_IsDeveloperModeEnabled));
    DetourTransactionCommit();

    if (Config::Instance()->FsrAgilitySDKUpgrade.value_or_default())
    {
        Microsoft::WRL::ComPtr<ID3D12SDKConfiguration> sdkConfig;
        auto hr = D3D12GetInterface(CLSID_D3D12SDKConfiguration, IID_PPV_ARGS(&sdkConfig));

        if (SUCCEEDED(hr))
        {
            hr = sdkConfig->SetSDKVersion(customD3D12SDKVersion, reinterpret_cast<LPCSTR>(customD3D12SDKPath));
            if (FAILED(hr))
            {
                LOG_ERROR("Failed to upgrade Agility SDK: {0}", hr);
            }
            else
            {
                LOG_INFO("Agility SDK upgraded successfully");
            }
            // sdkConfig->Release();
        }
        else
        {
            LOG_ERROR("Failed to get D3D12 SDK Configuration interface: {0}", hr);
        }
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID&) o_IsDeveloperModeEnabled, static_cast<HRESULT (*)(BOOL*)>(hk_IsDeveloperModeEnabled));
    DetourTransactionCommit();
}

void LoadAsiPlugins()
{
    std::filesystem::path pluginPath(Config::Instance()->PluginPath.value_or_default());
    auto folderPath = pluginPath.wstring();

    LOG_DEBUG("Checking {} for *.asi", pluginPath.string());

    if (!std::filesystem::exists(pluginPath))
        return;

    for (const auto& entry : std::filesystem::directory_iterator(folderPath))
    {
        if (!entry.is_regular_file())
            continue;

        std::wstring ext = entry.path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return std::towlower(c); });

        if (ext == L".asi")
        {
            HMODULE hMod = KernelBaseProxy::LoadLibraryW_()(entry.path().c_str());

            if (hMod != nullptr)
            {
                LOG_INFO("Loaded: {}", entry.path().string());
                _asiHandles.push_back(hMod);

                auto init = (PFN_InitializeASI) KernelBaseProxy::GetProcAddress_()(hMod, "InitializeASI");
                auto patchResult = (PFN_PatchResult) KernelBaseProxy::GetProcAddress_()(hMod, "PatchResult");

                if (init != nullptr)
                    init();

                if (!State::Instance().isRunningOnNvidia && patchResult != nullptr)
                {
                    auto pr = patchResult();

                    if (pr)
                    {
                        LOG_INFO("Game patching is successful, disabling spoofing");

                        if (!Config::Instance()->DxgiSpoofing.has_value())
                            Config::Instance()->DxgiSpoofing.set_volatile_value(false);

                        if (!Config::Instance()->VulkanSpoofing.has_value())
                            Config::Instance()->VulkanSpoofing.set_volatile_value(false);

                        if (!Config::Instance()->VulkanExtensionSpoofing.has_value())
                            Config::Instance()->VulkanExtensionSpoofing.set_volatile_value(false);
                    }
                }
            }
            else
            {
                DWORD err = GetLastError();
                LOG_ERROR("Failed to load: {}, error {:X}", entry.path().string(), err);
            }
        }
    }
}

static void CheckWorkingMode()
{
    LOG_FUNC();

    if (Config::Instance()->EarlyHooking.value_or_default())
    {

        NtdllHooks::Hook();
        KernelHooks::Hook();
        KernelHooks::HookBase();
    }

    bool modeFound = false;
    std::string filename = Util::DllPath().filename().string();
    std::string lCaseFilename(filename);
    wchar_t sysFolder[MAX_PATH];
    GetSystemDirectory(sysFolder, MAX_PATH);
    std::filesystem::path sysPath(sysFolder);
    std::filesystem::path pluginPath(Config::Instance()->PluginPath.value_or_default());

    for (size_t i = 0; i < lCaseFilename.size(); i++)
        lCaseFilename[i] = std::tolower(lCaseFilename[i]);

    do
    {
        if (lCaseFilename == "nvngx.dll" || lCaseFilename == "_nvngx.dll" ||
            lCaseFilename == "dlss-enabler-upscaler.dll")
        {
            LOG_INFO("OptiScaler working as native upscaler: {0}", filename);

            dllNames.push_back("OptiScaler_DontLoad.dll");
            dllNames.push_back("OptiScaler_DontLoad");
            dllNamesW.push_back(L"OptiScaler_DontLoad.dll");
            dllNamesW.push_back(L"OptiScaler_DontLoad");

            State::Instance().enablerAvailable = lCaseFilename == "dlss-enabler-upscaler.dll";
            if (State::Instance().enablerAvailable)
                Config::Instance()->LogToNGX.set_volatile_value(true);

            State::Instance().isWorkingAsNvngx = !State::Instance().enablerAvailable;

            modeFound = true;
            break;
        }

        // version.dll
        if (lCaseFilename == "version.dll")
        {
            do
            {
                auto pluginFilePath = pluginPath / L"version.dll";
                originalModule = KernelBaseProxy::LoadLibraryExW_()(pluginFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                {
                    LOG_INFO("OptiScaler working as version.dll, original dll loaded from plugin folder");
                    break;
                }

                originalModule = KernelBaseProxy::LoadLibraryExW_()(L"version-original.dll", NULL, 0);

                if (originalModule != nullptr)
                {
                    LOG_INFO("OptiScaler working as version.dll, version-original.dll loaded");
                    break;
                }

                auto sysFilePath = sysPath / L"version.dll";
                originalModule = KernelBaseProxy::LoadLibraryExW_()(sysFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                    LOG_INFO("OptiScaler working as version.dll, system dll loaded");

            } while (false);

            if (originalModule != nullptr)
            {
                dllNames.push_back("version.dll");
                dllNames.push_back("version");
                dllNamesW.push_back(L"version.dll");
                dllNamesW.push_back(L"version");

                shared.LoadOriginalLibrary(originalModule);
                version.LoadOriginalLibrary(originalModule);

                modeFound = true;
            }
            else
            {
                spdlog::error("OptiScaler can't find original version.dll!");
            }

            break;
        }

        // winmm.dll
        if (lCaseFilename == "winmm.dll")
        {
            do
            {
                auto pluginFilePath = pluginPath / L"winmm.dll";
                originalModule = KernelBaseProxy::LoadLibraryExW_()(pluginFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                {
                    LOG_INFO("OptiScaler working as winmm.dll, original dll loaded from plugin folder");
                    break;
                }

                originalModule = KernelBaseProxy::LoadLibraryExW_()(L"winmm-original.dll", NULL, 0);

                if (originalModule != nullptr)
                {
                    LOG_INFO("OptiScaler working as winmm.dll, winmm-original.dll loaded");
                    break;
                }

                auto sysFilePath = sysPath / L"winmm.dll";
                originalModule = KernelBaseProxy::LoadLibraryExW_()(sysFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                    LOG_INFO("OptiScaler working as winmm.dll, system dll loaded");

            } while (false);

            if (originalModule != nullptr)
            {
                dllNames.push_back("winmm.dll");
                dllNames.push_back("winmm");
                dllNamesW.push_back(L"winmm.dll");
                dllNamesW.push_back(L"winmm");

                shared.LoadOriginalLibrary(originalModule);
                winmm.LoadOriginalLibrary(originalModule);
                modeFound = true;
            }
            else
            {
                spdlog::error("OptiScaler can't find original winmm.dll!");
            }

            break;
        }

        // wininet.dll
        if (lCaseFilename == "wininet.dll")
        {
            do
            {
                auto pluginFilePath = pluginPath / L"wininet.dll";
                originalModule = KernelBaseProxy::LoadLibraryExW_()(pluginFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                {
                    LOG_INFO("OptiScaler working as wininet.dll, original dll loaded from plugin folder");
                    break;
                }

                originalModule = KernelBaseProxy::LoadLibraryExW_()(L"wininet-original.dll", NULL, 0);

                if (originalModule != nullptr)
                {
                    LOG_INFO("OptiScaler working as wininet.dll, wininet-original.dll loaded");
                    break;
                }

                auto sysFilePath = sysPath / L"wininet.dll";
                originalModule = KernelBaseProxy::LoadLibraryExW_()(sysFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                    LOG_INFO("OptiScaler working as wininet.dll, system dll loaded");

            } while (false);

            if (originalModule != nullptr)
            {
                dllNames.push_back("wininet.dll");
                dllNames.push_back("wininet");
                dllNamesW.push_back(L"wininet.dll");
                dllNamesW.push_back(L"wininet");

                shared.LoadOriginalLibrary(originalModule);
                wininet.LoadOriginalLibrary(originalModule);
                modeFound = true;
            }
            else
            {
                spdlog::error("OptiScaler can't find original wininet.dll!");
            }

            break;
        }

        // dbghelp.dll
        if (lCaseFilename == "dbghelp.dll")
        {
            do
            {
                auto pluginFilePath = pluginPath / L"dbghelp.dll";
                originalModule = KernelBaseProxy::LoadLibraryExW_()(pluginFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                {
                    LOG_INFO("OptiScaler working as dbghelp.dll, original dll loaded from plugin folder");
                    break;
                }

                originalModule = KernelBaseProxy::LoadLibraryExW_()(L"dbghelp-original.dll", NULL, 0);

                if (originalModule != nullptr)
                {
                    LOG_INFO("OptiScaler working as dbghelp.dll, dbghelp-original.dll loaded");
                    break;
                }

                auto sysFilePath = sysPath / L"dbghelp.dll";
                originalModule = KernelBaseProxy::LoadLibraryExW_()(sysFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                    LOG_INFO("OptiScaler working as dbghelp.dll, system dll loaded");

            } while (false);

            if (originalModule != nullptr)
            {
                dllNames.push_back("dbghelp.dll");
                dllNames.push_back("dbghelp");
                dllNamesW.push_back(L"dbghelp.dll");
                dllNamesW.push_back(L"dbghelp");

                shared.LoadOriginalLibrary(originalModule);
                dbghelp.LoadOriginalLibrary(originalModule);
                modeFound = true;
            }
            else
            {
                spdlog::error("OptiScaler can't find original dbghelp.dll!");
            }

            break;
        }

        // optiscaler.dll
        if (lCaseFilename == "optiscaler.asi")
        {
            LOG_INFO("OptiScaler working as OptiScaler.asi");

            // quick hack for testing
            originalModule = dllModule;

            dllNames.push_back("optiscaler.asi");
            dllNames.push_back("optiscaler");
            dllNamesW.push_back(L"optiscaler.asi");
            dllNamesW.push_back(L"optiscaler");

            modeFound = true;
            break;
        }

        // winhttp.dll
        if (lCaseFilename == "winhttp.dll")
        {
            do
            {
                auto pluginFilePath = pluginPath / L"winhttp.dll";
                originalModule = KernelBaseProxy::LoadLibraryExW_()(pluginFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                {
                    LOG_INFO("OptiScaler working as winhttp.dll, original dll loaded from plugin folder");
                    break;
                }

                originalModule = KernelBaseProxy::LoadLibraryExW_()(L"winhttp-original.dll", NULL, 0);

                if (originalModule != nullptr)
                {
                    LOG_INFO("OptiScaler working as winhttp.dll, winhttp-original.dll loaded");
                    break;
                }

                auto sysFilePath = sysPath / L"winhttp.dll";
                originalModule = KernelBaseProxy::LoadLibraryExW_()(sysFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                    LOG_INFO("OptiScaler working as winhttp.dll, system dll loaded");

            } while (false);

            if (originalModule != nullptr)
            {
                dllNames.push_back("winhttp.dll");
                dllNames.push_back("winhttp");
                dllNamesW.push_back(L"winhttp.dll");
                dllNamesW.push_back(L"winhttp");

                shared.LoadOriginalLibrary(originalModule);
                winhttp.LoadOriginalLibrary(originalModule);
                modeFound = true;
            }
            else
            {
                spdlog::error("OptiScaler can't find original winhttp.dll!");
            }

            break;
        }

        // dxgi.dll
        if (lCaseFilename == "dxgi.dll")
        {
            do
            {
                auto pluginFilePath = pluginPath / L"dxgi.dll";
                originalModule = KernelBaseProxy::LoadLibraryExW_()(pluginFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                {
                    LOG_INFO("OptiScaler working as dxgi.dll, original dll loaded from plugin folder");
                    break;
                }

                originalModule = KernelBaseProxy::LoadLibraryExW_()(L"dxgi-original.dll", NULL, 0);

                if (originalModule != nullptr)
                {
                    LOG_INFO("OptiScaler working as dxgi.dll, dxgi-original.dll loaded");
                    break;
                }

                auto sysFilePath = sysPath / L"dxgi.dll";
                originalModule = KernelBaseProxy::LoadLibraryExW_()(sysFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                    LOG_INFO("OptiScaler working as dxgi.dll, system dll loaded");

            } while (false);

            if (originalModule != nullptr)
            {
                dllNames.push_back("dxgi.dll");
                dllNames.push_back("dxgi");
                dllNamesW.push_back(L"dxgi.dll");
                dllNamesW.push_back(L"dxgi");

                DxgiProxy::Init(originalModule);
                dxgi.LoadOriginalLibrary(originalModule);

                State::Instance().isDxgiMode = true;
                modeFound = true;
            }
            else
            {
                spdlog::error("OptiScaler can't find original dxgi.dll!");
            }

            break;
        }

        // d3d12.dll
        if (lCaseFilename == "d3d12.dll")
        {
            do
            {
                // Moved here to cover agility sdk
                KernelHooks::HookBase();

                auto pluginFilePath = pluginPath / L"d3d12.dll";
                originalModule = KernelBaseProxy::LoadLibraryExW_()(pluginFilePath.wstring().c_str(), NULL, 0);
                if (originalModule != nullptr)
                {
                    LOG_INFO("OptiScaler working as d3d12.dll, original dll loaded from plugin folder");
                    break;
                }

                originalModule = KernelBaseProxy::LoadLibraryExW_()(L"d3d12-original.dll", NULL, 0);
                if (originalModule != nullptr)
                {
                    LOG_INFO("OptiScaler working as d3d12.dll, d3d12-original.dll loaded");
                    break;
                }

                auto sysFilePath = sysPath / L"d3d12.dll";
                originalModule = KernelBaseProxy::LoadLibraryExW_()(sysFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                    LOG_INFO("OptiScaler working as d3d12.dll, system dll loaded");

            } while (false);

            if (originalModule != nullptr)
            {
                dllNames.push_back("d3d12.dll");
                dllNames.push_back("d3d12");
                dllNamesW.push_back(L"d3d12.dll");
                dllNamesW.push_back(L"d3d12");

                D3d12Proxy::Init(originalModule);
                d3d12.LoadOriginalLibrary(originalModule);

                State::Instance().isD3D12Mode = true;

                modeFound = true;
            }
            else
            {
                spdlog::error("OptiScaler can't find original d3d12.dll!");
            }

            break;
        }

    } while (false);

    if (modeFound)
    {
        Config::Instance()->CheckUpscalerFiles();

        if (!State::Instance().isWorkingAsNvngx || State::Instance().enablerAvailable)
        {
            Config::Instance()->OverlayMenu.set_volatile_value(
                (!State::Instance().isWorkingAsNvngx || State::Instance().enablerAvailable) &&
                Config::Instance()->OverlayMenu.value_or_default());

            // DXGI
            if (DxgiProxy::Module() == nullptr)
            {
                LOG_DEBUG("Check for dxgi");
                HMODULE dxgiModule = nullptr;
                dxgiModule = KernelBaseProxy::GetModuleHandleW_()(L"dxgi.dll");
                if (dxgiModule != nullptr)
                {
                    LOG_DEBUG("dxgi.dll already in memory");

                    DxgiProxy::Init(dxgiModule);

                    if (!State::Instance().enablerAvailable && Config::Instance()->DxgiSpoofing.value_or_default())
                        HookDxgiForSpoofing();

                    if (Config::Instance()->OverlayMenu.value())
                        HooksDx::HookDxgi();
                }
            }
            else
            {
                LOG_DEBUG("dxgi.dll already in memory");
                if (!State::Instance().enablerAvailable && Config::Instance()->DxgiSpoofing.value_or_default())
                    HookDxgiForSpoofing();

                if (Config::Instance()->OverlayMenu.value())
                    HooksDx::HookDxgi();
            }

            // DirectX 12
            if (D3d12Proxy::Module() == nullptr)
            {
                // Moved here to cover agility sdk
                KernelHooks::HookBase();

                LOG_DEBUG("Check for d3d12");
                HMODULE d3d12Module = nullptr;
                d3d12Module = KernelBaseProxy::GetModuleHandleW_()(L"d3d12.dll");
                if (Config::Instance()->OverlayMenu.value() && d3d12Module != nullptr)
                {
                    LOG_DEBUG("d3d12.dll already in memory");
                    D3d12Proxy::Init(d3d12Module);
                    HooksDx::HookDx12();
                }
            }
            else
            {
                LOG_DEBUG("d3d12.dll already in memory");
                HooksDx::HookDx12();
            }

            if (D3d12Proxy::Module() == nullptr && State::Instance().gameQuirks & GameQuirk::LoadD3D12Manually)
            {
                LOG_DEBUG("Loading d3d12.dll manually");
                D3d12Proxy::Init();
            }

            // DirectX 11
            d3d11Module = KernelBaseProxy::GetModuleHandleW_()(L"d3d11.dll");
            if (Config::Instance()->OverlayMenu.value() && d3d11Module != nullptr)
            {
                LOG_DEBUG("d3d11.dll already in memory");
                HooksDx::HookDx11(d3d11Module);
            }

            // Vulkan
            vulkanModule = KernelBaseProxy::GetModuleHandleW_()(L"vulkan-1.dll");
            if ((State::Instance().isRunningOnDXVK || State::Instance().isRunningOnLinux) && vulkanModule == nullptr)
                vulkanModule = KernelBaseProxy::LoadLibraryExW_()(L"vulkan-1.dll", NULL, 0);

            if (vulkanModule != nullptr)
            {
                LOG_DEBUG("vulkan-1.dll already in memory");

                if (!State::Instance().enablerAvailable)
                {
                    HookForVulkanSpoofing(vulkanModule);
                    HookForVulkanExtensionSpoofing(vulkanModule);
                    HookForVulkanVRAMSpoofing(vulkanModule);
                }

                HooksVk::HookVk(vulkanModule);
            }

            // NVAPI
            HMODULE nvapi64 = nullptr;
            nvapi64 = KernelBaseProxy::GetModuleHandleW_()(L"nvapi64.dll");
            if (nvapi64 != nullptr)
            {
                LOG_DEBUG("nvapi64.dll already in memory");

                // if (!isWorkingWithEnabler)
                NvApiHooks::Hook(nvapi64);
            }

            // GDI32
            hookGdi32();

            // Wintrust
            hookWintrust();

            // Crypt32
            hookCrypt32();

            // Advapi32
            if (Config::Instance()->DxgiSpoofing.value_or_default() ||
                Config::Instance()->StreamlineSpoofing.value_or_default())
                hookAdvapi32();

            // hook streamline right away if it's already loaded
            HMODULE slModule = nullptr;
            slModule = KernelBaseProxy::GetModuleHandleW_()(L"sl.interposer.dll");
            if (slModule != nullptr)
            {
                LOG_DEBUG("sl.interposer.dll already in memory");
                StreamlineHooks::hookInterposer(slModule);
            }

            HMODULE slDlss = nullptr;
            slDlss = KernelBaseProxy::GetModuleHandleW_()(L"sl.dlss.dll");
            if (slDlss != nullptr)
            {
                LOG_DEBUG("sl.dlss.dll already in memory");
                StreamlineHooks::hookDlss(slDlss);
            }

            HMODULE slDlssg = nullptr;
            slDlssg = KernelBaseProxy::GetModuleHandleW_()(L"sl.dlss_g.dll");
            if (slDlssg != nullptr)
            {
                LOG_DEBUG("sl.dlss_g.dll already in memory");
                StreamlineHooks::hookDlssg(slDlssg);
            }

            HMODULE slReflex = nullptr;
            slReflex = KernelBaseProxy::GetModuleHandleW_()(L"sl.reflex.dll");
            if (slReflex != nullptr)
            {
                LOG_DEBUG("sl.reflex.dll already in memory");
                StreamlineHooks::hookReflex(slReflex);
            }

            HMODULE slCommon = nullptr;
            slCommon = KernelBaseProxy::GetModuleHandleW_()(L"sl.common.dll");
            if (slCommon != nullptr)
            {
                LOG_DEBUG("sl.common.dll already in memory");
                StreamlineHooks::hookCommon(slCommon);
            }

            // XeSS
            HMODULE xessModule = nullptr;
            xessModule = KernelBaseProxy::GetModuleHandleW_()(L"libxess.dll");
            if (xessModule != nullptr)
            {
                LOG_DEBUG("libxess.dll already in memory");
                XeSSProxy::HookXeSS(xessModule);
            }

            HMODULE xessDx11Module = nullptr;
            xessDx11Module = KernelBaseProxy::GetModuleHandleW_()(L"libxess_dx11.dll");
            if (xessDx11Module != nullptr)
            {
                LOG_DEBUG("libxess_dx11.dll already in memory");
                XeSSProxy::HookXeSSDx11(xessDx11Module);
            }

            // NVNGX
            HMODULE nvngxModule = nullptr;
            nvngxModule = KernelBaseProxy::GetModuleHandleW_()(L"_nvngx.dll");
            if (nvngxModule == nullptr)
                nvngxModule = KernelBaseProxy::GetModuleHandleW_()(L"nvngx.dll");

            if (nvngxModule != nullptr)
            {
                LOG_DEBUG("nvngx.dll already in memory");
                NVNGXProxy::InitNVNGX(nvngxModule);
            }

            // FFX Dx12
            HMODULE ffxDx12Module = nullptr;
            ffxDx12Module = KernelBaseProxy::GetModuleHandleW_()(L"amd_fidelityfx_dx12.dll");
            if (ffxDx12Module != nullptr)
            {
                LOG_DEBUG("amd_fidelityfx_dx12.dll already in memory");
                FfxApiProxy::InitFfxDx12(ffxDx12Module);
            }

            // FFX Vulkan
            HMODULE ffxVkModule = nullptr;
            ffxVkModule = KernelBaseProxy::GetModuleHandleW_()(L"amd_fidelityfx_vk.dll");
            if (ffxVkModule != nullptr)
            {
                LOG_DEBUG("amd_fidelityfx_vk.dll already in memory");
                FfxApiProxy::InitFfxVk(ffxVkModule);
            }

            // SpecialK
            if (!State::Instance().enablerAvailable && !Config::Instance()->OverlayMenu.value_or_default() &&
                skModule == nullptr && Config::Instance()->LoadSpecialK.value_or_default())
            {
                auto skFile = Util::DllPath().parent_path() / L"SpecialK64.dll";
                SetEnvironmentVariableW(L"RESHADE_DISABLE_GRAPHICS_HOOK", L"1");

                State::EnableServeOriginal(200);
                skModule = LoadLibraryW(skFile.c_str());
                State::DisableServeOriginal(200);

                LOG_INFO("Loading SpecialK64.dll, result: {0:X}", (UINT64) skModule);
            }

            // ReShade
            if (!State::Instance().enablerAvailable && reshadeModule == nullptr &&
                Config::Instance()->LoadReShade.value_or_default())
            {
                auto rsFile = Util::DllPath().parent_path() / L"ReShade64.dll";
                SetEnvironmentVariableW(L"RESHADE_DISABLE_LOADING_CHECK", L"1");

                if (skModule != nullptr)
                    SetEnvironmentVariableW(L"RESHADE_DISABLE_GRAPHICS_HOOK", L"1");

                State::EnableServeOriginal(201);
                reshadeModule = LoadLibraryW(rsFile.c_str());
                State::DisableServeOriginal(201);

                LOG_INFO("Loading ReShade64.dll, result: {0:X}", (size_t) reshadeModule);
            }

            // Hook kernel32 methods
            if (!Config::Instance()->EarlyHooking.value_or_default())
            {
                NtdllHooks::Hook();
                KernelHooks::Hook();
            }

            // For Agility SDK Upgrade
            if (Config::Instance()->FsrAgilitySDKUpgrade.value_or_default())
            {
                RunAgilityUpgrade(KernelBaseProxy::GetModuleHandleW_()(L"d3d12.dll"));
            }

            // Intel Extension Framework
            if (Config::Instance()->UESpoofIntelAtomics64.value_or_default())
            {
                HMODULE igdext = KernelBaseProxy::LoadLibraryW_()(L"igdext64.dll");

                if (igdext == nullptr)
                {
                    auto paths = GetDriverStore();

                    for (size_t i = 0; i < paths.size(); i++)
                    {
                        auto dllPath = paths[i] / L"igdext64.dll";
                        LOG_DEBUG("Trying to load: {}", wstring_to_string(dllPath.c_str()));
                        igdext = KernelBaseProxy::LoadLibraryExW_()(dllPath.c_str(), NULL, 0);

                        if (igdext != nullptr)
                        {
                            LOG_INFO("igdext64.dll loaded from {}", dllPath.string());
                            break;
                        }
                    }
                }
                else
                {
                    LOG_INFO("igdext64.dll loaded from game folder");
                }

                if (igdext != nullptr)
                    IGDExtProxy::Init(igdext);
                else
                    LOG_ERROR("Failed to load igdext64.dll");
            }
        }

        return;
    }

    LOG_ERROR("Unsupported dll name: {0}", filename);
}

static void CheckQuirks()
{
    auto exePathFilename = Util::ExePath().filename().string();

    State::Instance().GameExe = exePathFilename;
    State::Instance().GameName = wstring_to_string(Util::GetExeProductName());

    LOG_INFO("Game's Exe: {0}", exePathFilename);
    LOG_INFO("Game Name: {0}", State::Instance().GameName);

    auto quirks = getQuirksForExe(exePathFilename);
    printQuirks(quirks);

    // Apply config-level quirks
    if (quirks & GameQuirk::ForceNoUpscalerFGInputs &&
        Config::Instance()->FGInput.value_or_default() == FGInput::Upscaler)
    {
        Config::Instance()->FGInput.set_volatile_value(FGInput::NoFG);
        Config::Instance()->FGPreset.set_volatile_value(FGPreset::NoFG);
    }

    if (quirks & GameQuirk::DisableFSR3Inputs && !Config::Instance()->EnableFsr3Inputs.has_value())
        Config::Instance()->UseFsr3Inputs.set_volatile_value(false);

    if (quirks & GameQuirk::DisableFSR2Inputs && !Config::Instance()->EnableFsr2Inputs.has_value())
        Config::Instance()->UseFsr2Inputs.set_volatile_value(false);

    if (quirks & GameQuirk::DisableFFXInputs && !Config::Instance()->EnableFfxInputs.has_value())
        Config::Instance()->EnableFfxInputs.set_volatile_value(false);

    if (quirks & GameQuirk::RestoreComputeSigOnNonNvidia && !State::Instance().isRunningOnNvidia &&
        !Config::Instance()->DxgiSpoofing.value_or_default() &&
        !Config::Instance()->RestoreComputeSignature.has_value())
    {
        Config::Instance()->RestoreComputeSignature.set_volatile_value(true);
    }

    if (quirks & GameQuirk::DisableReactiveMasks)
        Config::Instance()->DisableReactiveMask.set_volatile_value(true);

    if (quirks & GameQuirk::ForceAutoExposure)
        Config::Instance()->AutoExposure.set_volatile_value(true);

    if (quirks & GameQuirk::DisableUseFsrInputValues)
        Config::Instance()->FsrUseFsrInputValues.set_volatile_value(false);

    if (quirks & GameQuirk::DisableDxgiSpoofing && !Config::Instance()->DxgiSpoofing.has_value())
        Config::Instance()->DxgiSpoofing.set_volatile_value(false);

    if (quirks & GameQuirk::EnableVulkanSpoofing && !State::Instance().isRunningOnNvidia &&
        !Config::Instance()->VulkanSpoofing.has_value())
        Config::Instance()->VulkanSpoofing.set_volatile_value(true);

    if (quirks & GameQuirk::EnableVulkanExtensionSpoofing && !State::Instance().isRunningOnNvidia &&
        !Config::Instance()->VulkanExtensionSpoofing.has_value())
        Config::Instance()->VulkanExtensionSpoofing.set_volatile_value(true);

    if (quirks & GameQuirk::DisableOptiXessPipelineCreation && !Config::Instance()->CreateHeaps.has_value() &&
        !Config::Instance()->BuildPipelines.has_value())
    {
        Config::Instance()->CreateHeaps.set_volatile_value(false);
        Config::Instance()->BuildPipelines.set_volatile_value(false);
    }

    if (quirks & GameQuirk::DontUseNTShared && !Config::Instance()->DontUseNTShared.has_value())
        Config::Instance()->DontUseNTShared.set_volatile_value(true);

    if (quirks & GameQuirk::DontUseUnrealBarriers && !Config::Instance()->ColorResourceBarrier.has_value())
        Config::Instance()->ColorResourceBarrier.set_volatile_value(128);

    if (quirks & GameQuirk::DontUseUnrealBarriers && !Config::Instance()->MVResourceBarrier.has_value())
        Config::Instance()->MVResourceBarrier.set_volatile_value(128);

    if (quirks & GameQuirk::SkipFirst10Frames && !Config::Instance()->SkipFirstFrames.has_value())
        Config::Instance()->SkipFirstFrames.set_volatile_value(10);

    State::Instance().gameQuirks = quirks;
}

bool isNvidia()
{
    bool nvidiaDetected = false;
    bool loadedHere = false;
    auto nvapiModule = KernelBaseProxy::GetModuleHandleW_()(L"nvapi64.dll");

    if (!nvapiModule)
    {
        nvapiModule = KernelBaseProxy::LoadLibraryExW_()(L"nvapi64.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        loadedHere = true;
    }

    // No nvapi, should not be nvidia
    if (!nvapiModule)
    {
        LOG_DEBUG("Detected: {}", nvidiaDetected);
        return nvidiaDetected;
    }

    if (auto o_NvAPI_QueryInterface =
            (PFN_NvApi_QueryInterface) KernelBaseProxy::GetProcAddress_()(nvapiModule, "nvapi_QueryInterface"))
    {
        // dxvk-nvapi calls CreateDxgiFactory which we can't do because we are inside DLL_PROCESS_ATTACH
        NvAPI_ShortString desc;
        auto* getVersion = GET_INTERFACE(NvAPI_GetInterfaceVersionString, o_NvAPI_QueryInterface);
        if (getVersion && getVersion(desc) == NVAPI_OK &&
            (std::string_view(desc) == std::string_view("NVAPI Open Source Interface (DXVK-NVAPI)") ||
             std::string_view(desc) == std::string_view("DXVK_NVAPI")))
        {
            LOG_DEBUG("Using dxvk-nvapi");
            DISPLAY_DEVICEA dd;
            dd.cb = sizeof(dd);
            int deviceIndex = 0;

            while (EnumDisplayDevicesA(nullptr, deviceIndex, &dd, 0))
            {
                if (dd.StateFlags & DISPLAY_DEVICE_ACTIVE && std::string_view(dd.DeviceID).contains("VEN_10DE"))
                {
                    // Having any Nvidia GPU active will take precedence
                    nvidiaDetected = true;
                }
                deviceIndex++;
            }
        }
        else if (o_NvAPI_QueryInterface(GET_ID(Fake_InformFGState)))
        {
            // Check for fakenvapi in system32, assume it's not nvidia if found
            LOG_DEBUG("Using fakenvapi");
            nvidiaDetected = false;
        }
        else
        {
            LOG_DEBUG("Using Nvidia's nvapi");
            auto init = GET_INTERFACE(NvAPI_Initialize, o_NvAPI_QueryInterface);
            if (init && init() == NVAPI_OK)
            {
                nvidiaDetected = true;

                if (auto unload = GET_INTERFACE(NvAPI_Unload, o_NvAPI_QueryInterface))
                    unload();
            }
        }
    }

    if (loadedHere)
        KernelBaseProxy::FreeLibrary_()(nvapiModule);

    LOG_DEBUG("Detected: {}", nvidiaDetected);

    return nvidiaDetected;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    HMODULE handle = nullptr;
    OSVERSIONINFOW winVer { 0 };

    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);

        dllModule = hModule;
        exeModule = GetModuleHandle(nullptr);
        processId = GetCurrentProcessId();

#ifdef _DEBUG // VER_PRE_RELEASE
        // Enable file logging for pre builds
        Config::Instance()->LogToFile.set_volatile_value(true);

        // Set log level to debug
        if (Config::Instance()->LogLevel.value_or_default() > 1)
            Config::Instance()->LogLevel.set_volatile_value(1);
#endif

        PrepareLogger();

        spdlog::warn("{0} loaded", VER_PRODUCT_NAME);
        spdlog::warn("---------------------------------");
        spdlog::warn("OptiScaler is freely downloadable from");
        spdlog::warn("GitHub : https://github.com/optiscaler/OptiScaler/releases");
        spdlog::warn("Nexus  : https://www.nexusmods.com/site/mods/986");
        spdlog::warn("If you paid for these files, you've been scammed!");
        spdlog::warn("DO NOT USE IN MULTIPLAYER GAMES");
        spdlog::warn("");
        spdlog::warn("LogLevel: {0}", Config::Instance()->LogLevel.value_or_default());

        spdlog::info("");
        if (Util::GetRealWindowsVersion(winVer))
            spdlog::info("Windows version: {} ({}.{}.{})", Util::GetWindowsName(winVer), winVer.dwMajorVersion,
                         winVer.dwMinorVersion, winVer.dwBuildNumber, winVer.dwPlatformId);
        else
            spdlog::warn("Can't read windows version");

        spdlog::info("");
        CheckQuirks();

        // OptiFG & Overlay Checks
        // TODO: Either FGInput == FGInput::Upscaler or FGOutput == FGOutput::FSRFG
        if ((Config::Instance()->FGInput.value_or_default() == FGInput::Upscaler) &&
            !Config::Instance()->DisableOverlays.has_value())
            Config::Instance()->DisableOverlays.set_volatile_value(true);

        if (Config::Instance()->DisableOverlays.value_or_default())
            SetEnvironmentVariable(L"SteamNoOverlayUIDrawing", L"1");

        // Initial state of FG
        State::Instance().currentFgPreset = Config::Instance()->FGPreset.value_or_default();
        State::Instance().activeFgInput = Config::Instance()->FGInput.value_or_default();
        State::Instance().activeFgOutput = Config::Instance()->FGOutput.value_or_default();

        // Init Kernel proxies
        KernelBaseProxy::Init();
        Kernel32Proxy::Init();

        // Hook FSR4 stuff as early as possible
        spdlog::info("");
        InitFSR4Update();

        // Check for Wine
        spdlog::info("");
        State::Instance().isRunningOnLinux = IsRunningOnWine();
        State::Instance().isRunningOnDXVK = State::Instance().isRunningOnLinux;

        // Disable splash for Linux
        if (!Config::Instance()->DisableSplash.has_value())
            Config::Instance()->DisableSplash.set_volatile_value(State::Instance().isRunningOnLinux);

        // Check if real DLSS available
        if (Config::Instance()->DLSSEnabled.value_or_default())
        {
            spdlog::info("");
            State::Instance().isRunningOnNvidia = isNvidia();

            if (State::Instance().isRunningOnNvidia)
            {
                spdlog::info(
                    "Running on Nvidia, setting DLSS as default upscaler and disabling spoofing options set to auto");

                Config::Instance()->DLSSEnabled.set_volatile_value(true);

                if (!Config::Instance()->DxgiSpoofing.has_value())
                    Config::Instance()->DxgiSpoofing.set_volatile_value(false);

                if (!Config::Instance()->StreamlineSpoofing.has_value())
                    Config::Instance()->StreamlineSpoofing.set_volatile_value(false);
            }
            else
            {
                spdlog::info("Not running on Nvidia, disabling DLSS");
                Config::Instance()->DLSSEnabled.set_volatile_value(false);
            }
        }

        if (!Config::Instance()->OverrideNvapiDll.has_value())
        {
            spdlog::info("OverrideNvapiDll not set, setting it to: {}",
                         !State::Instance().isRunningOnNvidia ? "true" : "false");
            Config::Instance()->OverrideNvapiDll.set_volatile_value(!State::Instance().isRunningOnNvidia);
        }

        // Check for working mode and attach hooks
        spdlog::info("");
        CheckWorkingMode();

        // Asi plugins
        if (!State::Instance().isWorkingAsNvngx && Config::Instance()->LoadAsiPlugins.value_or_default())
        {
            spdlog::info("");
            LoadAsiPlugins();
        }

        if (!Config::Instance()->DxgiSpoofing.has_value() && !State::Instance().nvngxReplacement.has_value())
        {
            LOG_WARN("Nvngx replacement not found!");

            if (!State::Instance().nvngxExists)
            {
                LOG_WARN("nvngx.dll not found! - disabling spoofing");
                Config::Instance()->DxgiSpoofing.set_volatile_value(false);
            }
        }

        if (Config::Instance()->EnableFsr2Inputs.value_or_default())
        {

            handle = KernelBaseProxy::GetModuleHandleW_()(fsr2NamesW[0].c_str());
            if (handle != nullptr)
                HookFSR2Inputs(handle);

            handle = KernelBaseProxy::GetModuleHandleW_()(fsr2BENamesW[0].c_str());
            if (handle != nullptr)
                HookFSR2Dx12Inputs(handle);

            spdlog::info("");

            HookFSR2ExeInputs();
        }

        if (Config::Instance()->EnableFsr3Inputs.value_or_default())
        {
            handle = KernelBaseProxy::GetModuleHandleW_()(fsr3NamesW[0].c_str());
            if (handle != nullptr)
                HookFSR3Inputs(handle);

            handle = KernelBaseProxy::GetModuleHandleW_()(fsr3BENamesW[0].c_str());
            if (handle != nullptr)
                HookFSR3Dx12Inputs(handle);

            HookFSR3ExeInputs();
        }
        // HookFfxExeInputs();

        for (size_t i = 0; i < 300; i++)
        {
            State::Instance().frameTimes.push_back(0.0f);
            State::Instance().upscaleTimes.push_back(0.0f);
        }

        spdlog::info("");
        spdlog::info("Init done");
        spdlog::info("---------------------------------------------");
        spdlog::info("");

        break;

    case DLL_PROCESS_DETACH:
        // Unhooking and cleaning stuff causing issues during shutdown.
        // Disabled for now to check if it cause any issues
        // UnhookApis();
        // unhookStreamline();
        // unhookGdi32();
        // unhookWintrust();
        // unhookCrypt32();
        // unhookAdvapi32();
        // DetachHooks();

        if (skModule != nullptr)
            KernelBaseProxy::FreeLibrary_()(skModule);

        if (reshadeModule != nullptr)
            KernelBaseProxy::FreeLibrary_()(reshadeModule);

        if (_asiHandles.size() > 0)
        {
            for (size_t i = 0; i < _asiHandles.size(); i++)
                KernelBaseProxy::FreeLibrary_()(_asiHandles[i]);
        }

        spdlog::info("");
        spdlog::info("DLL_PROCESS_DETACH");
        spdlog::info("Unloading OptiScaler");
        CloseLogger();

        break;

    case DLL_THREAD_ATTACH:
        // LOG_DEBUG_ONLY("DLL_THREAD_ATTACH from module: {0:X}, count: {1}", (UINT64)hModule, loadCount);
        break;

    case DLL_THREAD_DETACH:
        // LOG_DEBUG_ONLY("DLL_THREAD_DETACH from module: {0:X}, count: {1}", (UINT64)hModule, loadCount);
        break;

    default:
        LOG_WARN("Call reason: {0:X}", ul_reason_for_call);
        break;
    }

    return TRUE;
}
