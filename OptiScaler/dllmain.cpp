#include "pch.h"
#include "dllmain.h"

#include "Util.h"
#include "Config.h"
#include "Logger.h"
#include "resource.h"
#include "DllNames.h"

#include "proxies/Dxgi_Proxy.h"
#include <proxies/XeSS_Proxy.h>
#include <proxies/NVNGX_Proxy.h>
#include "proxies/Kernel32_Proxy.h"
#include "proxies/KernelBase_Proxy.h"
#include "proxies/Ntdll_Proxy.h"
#include <proxies/IGDExt_Proxy.h>
#include <proxies/FfxApi_Proxy.h>

#include "inputs/FSR2_Dx11.h"
#include "inputs/FSR2_Dx12.h"
#include "inputs/FSR2_Vk.h"
#include "inputs/FSR3_Dx12.h"
#include "inputs/FG/FSR3_Dx12_FG.h"

#include "fsr4/FSR4Upgrade.h"
#include <fsr4/FSR4ModelSelection.h>

#include <hooks/Dxgi_Hooks.h>
#include <hooks/D3D11_Hooks.h>
#include <hooks/D3D12_Hooks.h>
#include <hooks/Vulkan_Hooks.h>
#include <hooks/Ntdll_Hooks.h>
#include <hooks/Kernel_Hooks.h>
#include <hooks/Gdi32_Hooks.h>
#include <hooks/Wintrust_Hooks.h>
#include <hooks/Crypt32_Hooks.h>
#include <hooks/Advapi32_Hooks.h>
#include <hooks/Streamline_Hooks.h>

#include <nvapi/NvApiHooks.h>

#include "spoofing/User32_Spoofing.h"

#include <cwctype>
#include <version_check.h>
#include <misc/IdentifyGpu.h>

static std::vector<HMODULE> _asiHandles;
static bool _passThruMode = false;

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

    LOG_DEBUG(L"Checking {} for *.asi", folderPath);

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
            HMODULE hMod = NtdllProxy::LoadLibraryExW_Ldr(entry.path().c_str(), NULL, 0);

            if (hMod != nullptr)
            {
                LOG_INFO(L"Loaded: {}", entry.path().wstring());
                _asiHandles.push_back(hMod);

                auto init = (PFN_InitializeASI) KernelBaseProxy::GetProcAddress_()(hMod, "InitializeASI");
                auto patchResult = (PFN_PatchResult) KernelBaseProxy::GetProcAddress_()(hMod, "PatchResult");

                if (init != nullptr)
                    init();

                if (patchResult != nullptr)
                {
                    auto pr = patchResult();

                    if (pr)
                    {
                        LOG_INFO("Game patching is successful");
                        State::Instance().isOptiPatcherSucceed = true;

                        LOG_INFO("Disabling spoofing");

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
                LOG_ERROR(L"Failed to load: {}, error {:X}", entry.path().wstring(), err);
            }
        }
    }
}

static void CheckWorkingMode()
{
    if (!_passThruMode)
        LOG_FUNC();

    bool modeFound = false;
    std::string filename = wstring_to_string(Util::DllPath().filename().wstring()); // .string() can crash
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
            if (!_passThruMode)
                LOG_INFO("OptiScaler working as native upscaler: {0}", filename);

            dllNames.push_back("OptiScaler_DontLoad.dll");
            dllNames.push_back("OptiScaler_DontLoad");
            dllNamesW.push_back(L"OptiScaler_DontLoad.dll");
            dllNamesW.push_back(L"OptiScaler_DontLoad");

            modeFound = true;
            break;
        }

        if (!_passThruMode && Config::Instance()->EarlyHooking.value_or_default())
        {
            NtdllHooks::Hook();
            KernelHooks::Hook();
            KernelHooks::HookBase();
        }

        // version.dll
        if (lCaseFilename == "version.dll")
        {
            do
            {
                auto pluginFilePath = pluginPath / L"version.dll";
                originalModule = NtdllProxy::LoadLibraryExW_Ldr(pluginFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as version.dll, original dll loaded from plugin folder");

                    break;
                }

                originalModule = NtdllProxy::LoadLibraryExW_Ldr(L"version-original.dll", NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as version.dll, version-original.dll loaded");

                    break;
                }

                auto sysFilePath = sysPath / L"version.dll";
                originalModule = NtdllProxy::LoadLibraryExW_Ldr(sysFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr && !_passThruMode)
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
                if (!_passThruMode)
                    LOG_ERROR("OptiScaler can't find original version.dll!");
            }

            break;
        }

        // winmm.dll
        if (lCaseFilename == "winmm.dll")
        {
            do
            {
                auto pluginFilePath = pluginPath / L"winmm.dll";
                originalModule = NtdllProxy::LoadLibraryExW_Ldr(pluginFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as winmm.dll, original dll loaded from plugin folder");

                    break;
                }

                originalModule = NtdllProxy::LoadLibraryExW_Ldr(L"winmm-original.dll", NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as winmm.dll, winmm-original.dll loaded");

                    break;
                }

                auto sysFilePath = sysPath / L"winmm.dll";
                originalModule = NtdllProxy::LoadLibraryExW_Ldr(sysFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr && !_passThruMode)
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
                if (!_passThruMode)
                    LOG_ERROR("OptiScaler can't find original winmm.dll!");
            }

            break;
        }

        // wininet.dll
        if (lCaseFilename == "wininet.dll")
        {
            do
            {
                auto pluginFilePath = pluginPath / L"wininet.dll";
                originalModule = NtdllProxy::LoadLibraryExW_Ldr(pluginFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as wininet.dll, original dll loaded from plugin folder");

                    break;
                }

                originalModule = NtdllProxy::LoadLibraryExW_Ldr(L"wininet-original.dll", NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as wininet.dll, wininet-original.dll loaded");

                    break;
                }

                auto sysFilePath = sysPath / L"wininet.dll";
                originalModule = NtdllProxy::LoadLibraryExW_Ldr(sysFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr && !_passThruMode)
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
                if (!_passThruMode)
                    LOG_ERROR("OptiScaler can't find original wininet.dll!");
            }

            break;
        }

        // dbghelp.dll
        if (lCaseFilename == "dbghelp.dll")
        {
            do
            {
                auto pluginFilePath = pluginPath / L"dbghelp.dll";
                originalModule = NtdllProxy::LoadLibraryExW_Ldr(pluginFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as dbghelp.dll, original dll loaded from plugin folder");

                    break;
                }

                originalModule = NtdllProxy::LoadLibraryExW_Ldr(L"dbghelp-original.dll", NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as dbghelp.dll, dbghelp-original.dll loaded");

                    break;
                }

                auto sysFilePath = sysPath / L"dbghelp.dll";
                originalModule = NtdllProxy::LoadLibraryExW_Ldr(sysFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr && !_passThruMode)
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
                if (!_passThruMode)
                    LOG_ERROR("OptiScaler can't find original dbghelp.dll!");
            }

            break;
        }

        // optiscaler.dll
        if (lCaseFilename == "optiscaler.dll")
        {
            if (!_passThruMode)
                LOG_INFO("OptiScaler working as OptiScaler.dll");

            // quick hack for testing
            originalModule = dllModule;

            dllNames.push_back("optiscaler.dll");
            dllNames.push_back("optiscaler");
            dllNamesW.push_back(L"optiscaler.dll");
            dllNamesW.push_back(L"optiscaler");

            modeFound = true;
            break;
        }

        // optiscaler.asi
        if (lCaseFilename == "optiscaler.asi")
        {
            if (!_passThruMode)
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
                originalModule = NtdllProxy::LoadLibraryExW_Ldr(pluginFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as winhttp.dll, original dll loaded from plugin folder");

                    break;
                }

                originalModule = NtdllProxy::LoadLibraryExW_Ldr(L"winhttp-original.dll", NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as winhttp.dll, winhttp-original.dll loaded");

                    break;
                }

                auto sysFilePath = sysPath / L"winhttp.dll";
                originalModule = NtdllProxy::LoadLibraryExW_Ldr(sysFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr && !_passThruMode)
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
                if (!_passThruMode)
                    LOG_ERROR("OptiScaler can't find original winhttp.dll!");
            }

            break;
        }

        // dxgi.dll
        if (lCaseFilename == "dxgi.dll")
        {
            do
            {
                auto pluginFilePath = pluginPath / L"dxgi.dll";
                originalModule = NtdllProxy::LoadLibraryExW_Ldr(pluginFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as dxgi.dll, original dll loaded from plugin folder");

                    break;
                }

                originalModule = NtdllProxy::LoadLibraryExW_Ldr(L"dxgi-original.dll", NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as dxgi.dll, dxgi-original.dll loaded");

                    break;
                }

                auto sysFilePath = sysPath / L"dxgi.dll";
                originalModule = NtdllProxy::LoadLibraryExW_Ldr(sysFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr && !_passThruMode)
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

                State::Instance().workingMode = WorkingMode::Dxgi;
                modeFound = true;
            }
            else
            {
                if (!_passThruMode)
                    LOG_ERROR("OptiScaler can't find original dxgi.dll!");
            }

            break;
        }

        // d3d12.dll
        if (lCaseFilename == "d3d12.dll")
        {
            do
            {
                // Moved here to cover agility sdk
                if (!_passThruMode)
                {
                    NtdllHooks::Hook();
                    KernelHooks::HookBase();
                }

                auto pluginFilePath = pluginPath / L"d3d12.dll";
                originalModule = NtdllProxy::LoadLibraryExW_Ldr(pluginFilePath.wstring().c_str(), NULL, 0);
                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as d3d12.dll, original dll loaded from plugin folder");

                    break;
                }

                originalModule = NtdllProxy::LoadLibraryExW_Ldr(L"d3d12-original.dll", NULL, 0);
                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as d3d12.dll, d3d12-original.dll loaded");

                    break;
                }

                auto sysFilePath = sysPath / L"d3d12.dll";
                originalModule = NtdllProxy::LoadLibraryExW_Ldr(sysFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr && !_passThruMode)
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

                State::Instance().workingMode = WorkingMode::D3d12;

                modeFound = true;
            }
            else
            {
                if (!_passThruMode)
                    LOG_ERROR("OptiScaler can't find original d3d12.dll!");
            }

            break;
        }

    } while (false);

    // Work as a dummy dll
    if (_passThruMode)
        return;

    if (!modeFound)
    {
        LOG_ERROR("Unsupported dll name: {0}", filename);
        return;
    }

    Config::Instance()->CheckUpscalerFiles();

    // Intel Extension Framework
    if (Config::Instance()->UESpoofIntelAtomics64.value_or_default())
    {
        HMODULE igdext = NtdllProxy::LoadLibraryExW_Ldr(L"igdext64.dll", NULL, 0);

        if (igdext == nullptr)
        {
            auto paths = GetDriverStore();

            for (size_t i = 0; i < paths.size(); i++)
            {
                auto dllPath = paths[i] / L"igdext64.dll";
                LOG_DEBUG("Trying to load: {}", wstring_to_string(dllPath.c_str()));
                igdext = NtdllProxy::LoadLibraryExW_Ldr(dllPath.c_str(), NULL, 0);

                if (igdext != nullptr)
                {
                    LOG_INFO(L"igdext64.dll loaded from {}", dllPath.wstring());
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

    // DXGI
    if (DxgiProxy::Module() == nullptr)
    {
        LOG_DEBUG("Check for dxgi");
        HMODULE dxgiModule = nullptr;
        dxgiModule = GetDllNameWModule(&dxgiNamesW);
        if (dxgiModule != nullptr)
        {
            LOG_DEBUG("dxgi.dll already in memory");

            DxgiProxy::Init(dxgiModule);
            DxgiHooks::Hook();
        }
    }
    else
    {
        LOG_DEBUG("dxgi.dll already in memory");
        DxgiHooks::Hook();
    }

    // DirectX 12
    if (D3d12Proxy::Module() == nullptr)
    {
        // Moved here to cover agility sdk
        KernelHooks::HookBase();
        NtdllHooks::Hook();

        LOG_DEBUG("Check for d3d12");
        HMODULE d3d12Module = nullptr;
        d3d12Module = GetDllNameWModule(&dx12NamesW);
        if (Config::Instance()->OverlayMenu.value_or_default() && d3d12Module != nullptr)
        {
            LOG_DEBUG("d3d12.dll already in memory");
            D3d12Proxy::Init(d3d12Module);
            D3D12Hooks::Hook();
        }
    }
    else
    {
        LOG_DEBUG("d3d12.dll already in memory");
        D3D12Hooks::Hook();
    }

    if (D3d12Proxy::Module() == nullptr && State::Instance().gameQuirks & GameQuirk::LoadD3D12Manually)
    {
        LOG_DEBUG("Loading d3d12.dll manually");
        D3d12Proxy::Init();
        D3D12Hooks::Hook();
    }

    d3d12AgilityModule = GetDllNameWModule(&dx12agilityNamesW);
    if (d3d12AgilityModule != nullptr)
    {
        LOG_DEBUG("D3D12Core.dll already in memory");
        D3D12Hooks::HookAgility(d3d12AgilityModule);
    }

    if (d3d12AgilityModule == nullptr && State::Instance().gameQuirks & GameQuirk::LoadD3D12Manually)
    {
        auto path = Util::ExePath().parent_path() / L"D3D12" / L"D3D12Core.dll";
        d3d12AgilityModule = NtdllProxy::LoadLibraryExW_Ldr(path.c_str(), NULL, 0);

        if (d3d12AgilityModule == nullptr && Config::Instance()->FsrAgilitySDKUpgrade.value_or_default())
        {
            path = Util::ExePath().parent_path() / L"D3D12_Optiscaler" / L"D3D12Core.dll";
            d3d12AgilityModule = NtdllProxy::LoadLibraryExW_Ldr(path.c_str(), NULL, 0);
        }

        if (d3d12AgilityModule == nullptr)
        {
            d3d12AgilityModule = NtdllProxy::LoadLibraryExW_Ldr(L"D3D12Core.dll", NULL, 0);
        }

        if (d3d12AgilityModule != nullptr)
        {
            LOG_DEBUG("D3D12Core.dll loaded");
            D3D12Hooks::HookAgility(d3d12AgilityModule);
        }
    }

    // DirectX 11
    d3d11Module = GetDllNameWModule(&dx11NamesW);
    if (Config::Instance()->OverlayMenu.value_or_default() && d3d11Module != nullptr)
    {
        LOG_DEBUG("d3d11.dll already in memory");
        D3D11Hooks::Hook(d3d11Module);
    }

    // Vulkan
    if (State::Instance().isRunningOnLinux || State::Instance().gameQuirks & GameQuirk::LoadVulkanManually)
    {
        vulkanModule = NtdllProxy::LoadLibraryExW_Ldr(L"vulkan-1.dll", NULL, 0);
        LOG_DEBUG("Loading vulkan-1.dll for Linux, result: {:X}", (size_t) vulkanModule);
    }
    else
    {
        vulkanModule = GetDllNameWModule(&vkNamesW);
    }

    if (vulkanModule != nullptr)
    {
        LOG_DEBUG("Hooking vulkan-1.dll");
        VulkanHooks::Hook(vulkanModule);
    }

    // NVAPI
    // Doesn't seem to like GetModuleHandle for some reason, so call our load to make sure
    if (GetDllNameWModule(&nvapiNamesW) != nullptr)
    {
        // This hooks nvapi as well when possible
        auto nvapi64 = LibraryLoadHooks::LoadNvApi();
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
    {
        hookAdvapi32();
    }

    // User32
    if (Config::Instance()->SpoofUser32.value_or_default())
    {
        User32Spoofing::Hook();
    }

    // hook streamline right away if it's already loaded
    HMODULE slModule = nullptr;
    slModule = GetDllNameWModule(&slInterposerNamesW);
    if (slModule != nullptr)
    {
        LOG_DEBUG("sl.interposer.dll already in memory");
        StreamlineHooks::hookInterposer(slModule);
        slInterposerModule = slModule;
    }

    HMODULE slDlss = nullptr;
    slDlss = GetDllNameWModule(&slDlssNamesW);
    if (slDlss != nullptr)
    {
        State::Instance().postCodes |= PostCode::SlPluginsAlreadyInMemory;
        LOG_WARN("sl.dlss.dll already in memory");
        StreamlineHooks::hookDlss(slDlss);
    }

    HMODULE slDlssg = nullptr;
    slDlssg = GetDllNameWModule(&slDlssgNamesW);
    if (slDlssg != nullptr)
    {
        State::Instance().postCodes |= PostCode::SlPluginsAlreadyInMemory;
        LOG_WARN("sl.dlss_g.dll already in memory");
        StreamlineHooks::hookDlssg(slDlssg);
    }

    HMODULE slReflex = nullptr;
    slReflex = GetDllNameWModule(&slReflexNamesW);
    if (slReflex != nullptr)
    {
        State::Instance().postCodes |= PostCode::SlPluginsAlreadyInMemory;
        LOG_WARN("sl.reflex.dll already in memory");
        StreamlineHooks::hookReflex(slReflex);
    }

    HMODULE slPcl = nullptr;
    slPcl = GetDllNameWModule(&slPclNamesW);
    if (slPcl != nullptr)
    {
        State::Instance().postCodes |= PostCode::SlPluginsAlreadyInMemory;
        LOG_WARN("sl.pcl.dll already in memory");
        StreamlineHooks::hookPcl(slPcl);
    }

    HMODULE slCommon = nullptr;
    slCommon = GetDllNameWModule(&slCommonNamesW);
    if (slCommon != nullptr)
    {
        State::Instance().postCodes |= PostCode::SlPluginsAlreadyInMemory;
        LOG_WARN("sl.common.dll already in memory");
        StreamlineHooks::hookCommon(slCommon);
    }

    // XeSS
    HMODULE xessModule = nullptr;
    xessModule = GetDllNameWModule(&xessNamesW);
    if (xessModule != nullptr)
    {
        LOG_DEBUG("libxess.dll already in memory");
        XeSSProxy::HookXeSS(xessModule);
    }

    HMODULE xessDx11Module = nullptr;
    xessDx11Module = GetDllNameWModule(&xessDx11NamesW);
    if (xessDx11Module != nullptr)
    {
        LOG_DEBUG("libxess_dx11.dll already in memory");
        XeSSProxy::HookXeSSDx11(xessDx11Module);
    }

    // NVNGX
    HMODULE nvngxModule = nullptr;
    nvngxModule = GetDllNameWModule(&nvngxNamesW);
    if (nvngxModule != nullptr)
    {
        LOG_DEBUG("nvngx.dll already in memory");
        NVNGXProxy::InitNVNGX(nvngxModule);
    }

    // FFX Dx12
    HMODULE ffxDx12Module = nullptr;
    ffxDx12Module = GetDllNameWModule(&ffxDx12NamesW);
    if (ffxDx12Module != nullptr)
    {
        LOG_DEBUG("amd_fidelityfx_dx12.dll already in memory");
        FfxApiProxy::InitFfxDx12(ffxDx12Module);
    }

    HMODULE ffxDx12SRModule = nullptr;
    ffxDx12SRModule = GetDllNameWModule(&ffxDx12UpscalerNamesW);
    if (ffxDx12SRModule != nullptr)
    {
        LOG_DEBUG("amd_fidelityfx_upscaler_dx12.dll already in memory");
        FSR4ModelSelection::Hook(ffxDx12SRModule, FSR4Source::SDK);
        FfxApiProxy::InitFfxDx12_SR(ffxDx12SRModule);
    }

    HMODULE ffxDx12FGModule = nullptr;
    ffxDx12FGModule = GetDllNameWModule(&ffxDx12FGNamesW);
    if (ffxDx12FGModule != nullptr)
    {
        LOG_DEBUG("amd_fidelityfx_framegeneration_dx12.dll already in memory");
        FfxApiProxy::InitFfxDx12_FG(ffxDx12FGModule);
    }

    HMODULE ffxDx12DenoiserModule = nullptr;
    ffxDx12DenoiserModule = GetDllNameWModule(&ffxDx12DenoiserNamesW);
    if (ffxDx12DenoiserModule != nullptr)
    {
        LOG_DEBUG("amd_fidelityfx_denoiser_dx12.dll already in memory");
        FfxApiProxy::InitFfxDx12_Denoiser(ffxDx12DenoiserModule);
    }

    HMODULE ffxDx12RadianceModule = nullptr;
    ffxDx12RadianceModule = GetDllNameWModule(&ffxDx12RadianceNamesW);
    if (ffxDx12RadianceModule != nullptr)
    {
        LOG_DEBUG("amd_fidelityfx_radiancecache_dx12.dll already in memory");
        FfxApiProxy::InitFfxDx12_Radiance(ffxDx12RadianceModule);
    }

    // FFX Vulkan
    HMODULE ffxVkModule = nullptr;
    ffxVkModule = GetDllNameWModule(&ffxVkNamesW);
    if (ffxVkModule != nullptr)
    {
        LOG_DEBUG("amd_fidelityfx_vk.dll already in memory");
        FfxApiProxy::InitFfxVk(ffxVkModule);
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
        RunAgilityUpgrade(GetDllNameWModule(&dx12NamesW));
    }

    // SpecialK
    if (skModule == nullptr && Config::Instance()->LoadSpecialK.value_or_default())
    {
        auto skFile = Util::ExePath().parent_path() / L"SpecialK64.dll";
        SetEnvironmentVariableW(L"RESHADE_DISABLE_GRAPHICS_HOOK", L"1");

        State::EnableServeOriginal(200);
        skModule = NtdllProxy::LoadLibraryExW_Ldr(skFile.c_str(), NULL, 0);
        State::DisableServeOriginal(200);

        LOG_INFO("Loading SpecialK64.dll, result: {0:X}", (UINT64) skModule);
    }

    // ReShade
    // Do not load Reshade here is Luma is active and we will create D3D12 device for it
    // We will load Reshade after D3D12 device creation in that case
    if (reshadeModule == nullptr && Config::Instance()->LoadReShade.value_or_default() &&
        (!(State::Instance().gameQuirks & GameQuirk::CreateD3D12DeviceForLuma) ||
         Config::Instance()->DontCreateD3D12DeviceForLuma.value_or_default()))
    {
        auto rsFile = Util::ExePath().parent_path() / L"ReShade64.dll";
        SetEnvironmentVariableW(L"RESHADE_DISABLE_LOADING_CHECK", L"1");

        if (skModule != nullptr)
            SetEnvironmentVariableW(L"RESHADE_DISABLE_GRAPHICS_HOOK", L"1");

        State::EnableServeOriginal(201);
        reshadeModule = NtdllProxy::LoadLibraryExW_Ldr(rsFile.c_str(), NULL, 0);
        State::DisableServeOriginal(201);

        LOG_INFO("Loading ReShade64.dll, result: {0:X}", (size_t) reshadeModule);
    }

    // Version check
    if (Config::Instance()->CheckForUpdate.value_or_default())
        VersionCheck::Start();

    return;
}

static void printQuirks(flag_set<GameQuirk>& quirks)
{
    auto state = &State::Instance();
    std::vector<std::string> stringQuirks;

    if (quirks & GameQuirk::CyberpunkHudlessState)
        stringQuirks.push_back("Fixing DLSSG's hudless state in Cyberpunk");

    if (quirks & GameQuirk::FSRFGHudlessMismatchFixup)
        stringQuirks.push_back("FSR FG hudless mismatch fixup");

    if (quirks & GameQuirk::SkipFsr3Method)
        stringQuirks.push_back("Skipping first FSR 3 method");

    if (quirks & GameQuirk::FastFeatureReset)
        stringQuirks.push_back("Quick upscaler reinit");

    if (quirks & GameQuirk::LoadD3D12Manually)
        stringQuirks.push_back("Load d3d12.dll");

    if (quirks & GameQuirk::KernelBaseHooks)
        stringQuirks.push_back("Enable KernelBase hooks");

    if (quirks & GameQuirk::VulkanDLSSBarrierFixup)
        stringQuirks.push_back("Fix DLSS/DLSSG barriers on Vulkan");

    if (quirks & GameQuirk::ForceUnrealEngine)
        stringQuirks.push_back("Force detected engine as Unreal Engine");

    if (quirks & GameQuirk::DisableHudfix)
        stringQuirks.push_back("Disabling Hudfix due to known issues");

    if (quirks & GameQuirk::ForceAutoExposure)
        stringQuirks.push_back("Enabling AutoExposure");

    if (quirks & GameQuirk::DisableFFXInputs)
        stringQuirks.push_back("Disable FFX Inputs");

    if (quirks & GameQuirk::DisableFSR3Inputs)
        stringQuirks.push_back("Disable FSR 3.0 Inputs");

    if (quirks & GameQuirk::DisableFSR2Inputs)
        stringQuirks.push_back("Disable FSR 2.X Inputs");

    if (quirks & GameQuirk::DisableReactiveMasks)
        stringQuirks.push_back("Disable Reactive Masks");

    if (quirks & GameQuirk::RestoreComputeSigOnNonNvidia)
        stringQuirks.push_back("Enabling restore compute signature on AMD/Intel");

    if (quirks & GameQuirk::RestoreComputeSigOnNvidia)
        stringQuirks.push_back("Enabling restore compute signature on Nvidia");

    if (quirks & GameQuirk::DisableDxgiSpoofing)
        stringQuirks.push_back("Dxgi spoofing disabled by default");

    if (quirks & GameQuirk::DisableUseFsrInputValues)
        stringQuirks.push_back("Disable Use FSR Input Values");

    if (quirks & GameQuirk::DisableOptiXessPipelineCreation)
        stringQuirks.push_back("Disable custom pipeline creation for XeSS");

    if (quirks & GameQuirk::DontUseNTShared)
        stringQuirks.push_back("Don't use NTShared enabled");

    if (quirks & GameQuirk::DontUseUnrealBarriers)
        stringQuirks.push_back("Don't use resource barrier fix for Unreal Engine games");

    if (quirks & GameQuirk::SkipFirst10Frames)
        stringQuirks.push_back("Skipping upscaling for first 10 frames");

    if (quirks & GameQuirk::NoFSRFGFirstSwapchain)
        stringQuirks.push_back("Skip turning the first swapchain created into an FSR swapchain");

    if (quirks & GameQuirk::FixSlSimulationMarkers)
        stringQuirks.push_back("Correct simulation start marker's frame id");

    if (quirks & GameQuirk::DisableVsyncOverride)
        stringQuirks.push_back("Don't use V-Sync overrides");

    if (quirks & GameQuirk::HitmanReflexHacks)
        stringQuirks.push_back("Hack for broken Hitman reflex");

    if (quirks & GameQuirk::SkipD3D11FeatureLevelElevation)
        stringQuirks.push_back("Skipping D3D11 feature level elevation, native FSR3.1 will be disabled!");

    if (quirks & GameQuirk::DontUseNtDllHooks)
        stringQuirks.push_back("Using kernel hooks instead of NTdll ones");

    if (quirks & GameQuirk::UseFSR2PatternMatching)
        stringQuirks.push_back("Use FSR2 pattern matching");

    if (quirks & GameQuirk::AlwaysCaptureFSRFGSwapchain)
        stringQuirks.push_back("Always capture FSR-FG swapchain");

    if (quirks & GameQuirk::AllowedFrameAhead2)
        stringQuirks.push_back("Allowed Frame Ahead: 2");

    if (quirks & GameQuirk::DisableXeFGChecks)
        stringQuirks.push_back("Skip pre init checks for XeFG");

    if (quirks & GameQuirk::CreateD3D12DeviceForLuma)
        stringQuirks.push_back("Create D3D12 device for Luma before loading Reshade");

    if (quirks & GameQuirk::LoadVulkanManually)
        stringQuirks.push_back("Load vulkan-1.dll");

    if (quirks & GameQuirk::UseFsr2Dx11Inputs)
        stringQuirks.push_back("Use FSR2 DX11 inputs");

    if (quirks & GameQuirk::UseFsr2VulkanInputs)
        stringQuirks.push_back("Use FSR2 Vulkan inputs");

    if (quirks & GameQuirk::ForceBorderlessWhenUsingXeFG)
        stringQuirks.push_back("Force Borderless when using XeFG");

    if (quirks & GameQuirk::OverrideVsyncWhenUsingXeFG)
        stringQuirks.push_back("Override Vsync when using XeFG");

    if (quirks & GameQuirk::ForceCreateD3D12Device)
        stringQuirks.push_back("Force create D3D12 device for w/Dx12");

    if (quirks & GameQuirk::SetDepthValidNow)
        stringQuirks.push_back("Set Depth as ValidNow");

    if (quirks & GameQuirk::SetVelocityValidNow)
        stringQuirks.push_back("Set Velocity as ValidNow");

    if (quirks & GameQuirk::SetHudlessValidNow)
        stringQuirks.push_back("Set Hudless as ValidNow");

    if (quirks & GameQuirk::DisableResizeSkip)
        stringQuirks.push_back("Disable Resize Skip");

    if (quirks & GameQuirk::SpoofRegistry)
        stringQuirks.push_back("Spoof Registry");

    if (quirks & GameQuirk::DisableFakenvapi)
        stringQuirks.push_back("Disable fakenvapi");

    if (quirks & GameQuirk::ForceDepthD32S8)
        stringQuirks.push_back("Force depth as D32S8");

    if (quirks & GameQuirk::DoNotPreserveFGSwapChain)
        stringQuirks.push_back("Don't Preserve FG Swapchain");

    if (quirks & GameQuirk::DoNotSkipResize)
        stringQuirks.push_back("Don't Skip Resize");

    if (quirks & GameQuirk::OldOverlayMenu)
        stringQuirks.push_back("Using old overlay (draws on upscaled image)");

    state->detectedQuirks.append_range(stringQuirks);
    for (auto& stringQuirk : stringQuirks)
        spdlog::info("Quirk: {}", stringQuirk);

    return;
}

static void CheckQuirks(bool isNvidia)
{
    auto exePathFilename = Util::ExePath().filename().string();

    State::Instance().GameExe = exePathFilename;
    State::Instance().GameName = wstring_to_string(Util::GetExeProductName());

    LOG_INFO("Game's Exe: {0}", exePathFilename);
    LOG_INFO("Game Name: {0}", State::Instance().GameName);

    auto quirks = getQuirksForExe(exePathFilename);

    auto state = &State::Instance();

    // Apply config-level quirks
    if (quirks & GameQuirk::DisableHudfix && Config::Instance()->FGInput.value_or_default() == FGInput::Upscaler)
        Config::Instance()->FGHUDFix.set_volatile_value(false);

    if (quirks & GameQuirk::DisableFSR3Inputs && !Config::Instance()->EnableFsr3Inputs.has_value())
        Config::Instance()->EnableFsr3Inputs.set_volatile_value(false);

    if (quirks & GameQuirk::DisableFSR2Inputs && !Config::Instance()->EnableFsr2Inputs.has_value())
        Config::Instance()->EnableFsr2Inputs.set_volatile_value(false);

    if (quirks & GameQuirk::DisableFFXInputs && !Config::Instance()->EnableFfxInputs.has_value())
        Config::Instance()->EnableFfxInputs.set_volatile_value(false);

    if (quirks & GameQuirk::DisableDxgiSpoofing && !Config::Instance()->DxgiSpoofing.has_value())
        Config::Instance()->DxgiSpoofing.set_volatile_value(false);

    if (quirks & GameQuirk::RestoreComputeSigOnNonNvidia && !isNvidia &&
        !Config::Instance()->DxgiSpoofing.value_or_default() &&
        !Config::Instance()->RestoreComputeSignature.has_value())
    {
        Config::Instance()->RestoreComputeSignature.set_volatile_value(true);
    }

    if (quirks & GameQuirk::RestoreComputeSigOnNvidia && isNvidia &&
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

    if (quirks & GameQuirk::EnableVulkanSpoofing && !isNvidia && !Config::Instance()->VulkanSpoofing.has_value())
    {
        Config::Instance()->VulkanSpoofing.set_volatile_value(true);
    }

    if (quirks & GameQuirk::EnableVulkanExtensionSpoofing && !isNvidia &&
        !Config::Instance()->VulkanExtensionSpoofing.has_value())
    {
        Config::Instance()->VulkanExtensionSpoofing.set_volatile_value(true);
    }

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

    if (quirks & GameQuirk::DisableVsyncOverride && !Config::Instance()->OverrideVsync.has_value())
        Config::Instance()->OverrideVsync.set_volatile_value(false);

    if (quirks & GameQuirk::DontUseNtDllHooks && !Config::Instance()->UseNtdllHooks.has_value())
        Config::Instance()->UseNtdllHooks.set_volatile_value(false);

    if (quirks & GameQuirk::UseFSR2PatternMatching && !Config::Instance()->Fsr2Pattern.has_value())
        Config::Instance()->Fsr2Pattern.set_volatile_value(true);

    if (quirks & GameQuirk::AlwaysCaptureFSRFGSwapchain &&
        !Config::Instance()->FGAlwaysCaptureFSRFGSwapchain.has_value())
    {
        Config::Instance()->FGAlwaysCaptureFSRFGSwapchain.set_volatile_value(true);
    }

    if (quirks & GameQuirk::AllowedFrameAhead2 && !Config::Instance()->FGAllowedFrameAhead.has_value())
        Config::Instance()->FGAllowedFrameAhead.set_volatile_value(2);

    if (quirks & GameQuirk::DisableXeFGChecks && !Config::Instance()->FGXeFGIgnoreInitChecks.has_value())
        Config::Instance()->FGXeFGIgnoreInitChecks.set_volatile_value(true);

    if (quirks & GameQuirk::UseFsr2Dx11Inputs && !Config::Instance()->UseFsr2Dx11Inputs.has_value())
        Config::Instance()->UseFsr2Dx11Inputs.set_volatile_value(true);

    if (quirks & GameQuirk::UseFsr2VulkanInputs && !Config::Instance()->UseFsr2VulkanInputs.has_value())
        Config::Instance()->UseFsr2VulkanInputs.set_volatile_value(true);

    if (quirks & GameQuirk::ForceBorderlessWhenUsingXeFG && !Config::Instance()->FGXeFGForceBorderless.has_value() &&
        State::Instance().activeFgOutput == FGOutput::XeFG && State::Instance().activeFgInput != FGInput::NoFG &&
        State::Instance().activeFgInput != FGInput::Nukems)
    {
        Config::Instance()->FGXeFGForceBorderless.set_volatile_value(true);
    }

    if (quirks & GameQuirk::OverrideVsyncWhenUsingXeFG && !Config::Instance()->OverrideVsync.has_value() &&
        State::Instance().activeFgOutput == FGOutput::XeFG && State::Instance().activeFgInput != FGInput::NoFG &&
        State::Instance().activeFgInput != FGInput::Nukems)
    {
        Config::Instance()->OverrideVsync.set_volatile_value(true);
    }

    if (quirks & GameQuirk::SetDepthValidNow && !Config::Instance()->FGDepthValidNow.has_value() &&
        State::Instance().activeFgInput == FGInput::DLSSG && State::Instance().activeFgOutput != FGOutput::NoFG &&
        State::Instance().activeFgOutput != FGOutput::Nukems)
    {
        Config::Instance()->FGDepthValidNow.set_volatile_value(true);
    }

    if (quirks & GameQuirk::SetVelocityValidNow && !Config::Instance()->FGVelocityValidNow.has_value() &&
        State::Instance().activeFgInput == FGInput::DLSSG && State::Instance().activeFgOutput != FGOutput::NoFG &&
        State::Instance().activeFgOutput != FGOutput::Nukems)
    {
        Config::Instance()->FGVelocityValidNow.set_volatile_value(true);
    }

    if (quirks & GameQuirk::SetHudlessValidNow && !Config::Instance()->FGHudlessValidNow.has_value() &&
        State::Instance().activeFgInput == FGInput::DLSSG && State::Instance().activeFgOutput != FGOutput::NoFG &&
        State::Instance().activeFgOutput != FGOutput::Nukems)
    {
        Config::Instance()->FGHudlessValidNow.set_volatile_value(true);
    }

    if (quirks & GameQuirk::DisableResizeSkip && !Config::Instance()->FGSkipResizeBuffers.has_value())
    {
        Config::Instance()->FGSkipResizeBuffers.set_volatile_value(false);
    }

    if (quirks & GameQuirk::SpoofRegistry && !Config::Instance()->SpoofRegistry.has_value())
    {
        Config::Instance()->SpoofRegistry.set_volatile_value(true);
    }

    if (quirks & GameQuirk::DisableFakenvapi && !Config::Instance()->UseFakenvapi.has_value())
    {
        Config::Instance()->UseFakenvapi.set_volatile_value(false);
    }

    if (quirks & GameQuirk::DoNotPreserveFGSwapChain && !Config::Instance()->FGPreserveSwapChain.has_value())
    {
        Config::Instance()->FGPreserveSwapChain.set_volatile_value(false);
    }

    if (quirks & GameQuirk::DoNotSkipResize && !Config::Instance()->FGSkipResizeBuffers.has_value())
    {
        Config::Instance()->FGSkipResizeBuffers.set_volatile_value(false);
    }

    if (quirks & GameQuirk::OldOverlayMenu && !Config::Instance()->OverlayMenu.has_value())
    {
        Config::Instance()->OverlayMenu.set_volatile_value(false);
    }

    // For Luma, we assume if Luma addon in game folder it's used
    const auto dir = Util::ExePath().parent_path();
    bool lumaDetected = false;

    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
            continue;

        const auto& path = entry.path();
        if (path.extension() == L".addon")
        {
            const auto fname = path.filename().wstring();
            if (fname.rfind(L"Luma-", 0) == 0) // starts with "Luma-"
            {
                lumaDetected = true;
                break;
            }
        }
    }

    if (lumaDetected)
    {
        if (!Config::Instance()->DxgiSpoofing.has_value())
        {
            LOG_INFO("Luma detected, disabling DxgiSpoofing");
            State::Instance().detectedQuirks.push_back("Luma detected, disabling DxgiSpoofing");
            Config::Instance()->DxgiSpoofing.set_volatile_value(false);
        }

        if (!Config::Instance()->DontUseNTShared.has_value())
        {
            LOG_INFO("Luma detected, enabling DontUseNTShared");
            State::Instance().detectedQuirks.push_back("Luma detected, enabling DontUseNTShared");
            Config::Instance()->DontUseNTShared.set_volatile_value(true);
        }

        if (!Config::Instance()->DontCreateD3D12DeviceForLuma.value_or_default())
        {
            quirks |= GameQuirk::LoadD3D12Manually;

            if (Config::Instance()->LoadReShade.value_or_default())
                quirks |= GameQuirk::CreateD3D12DeviceForLuma;
        }
    }

    // For Sekiro TSR
    if (std::filesystem::exists(Util::ExePath().parent_path() / L"SekiroTSRLoader.addon"))
    {
        if (!Config::Instance()->DxgiSpoofing.has_value())
        {
            LOG_INFO("Sekiro TSR detected, disabling DxgiSpoofing");
            State::Instance().detectedQuirks.push_back("Luma UE detected, disabling DxgiSpoofing");
            Config::Instance()->DxgiSpoofing.set_volatile_value(false);
        }

        if (!Config::Instance()->DontUseNTShared.has_value())
        {
            LOG_INFO("Sekiro TSR detected, enabling DontUseNTShared");
            State::Instance().detectedQuirks.push_back("Sekiro TSR detected, enabling DontUseNTShared");
            Config::Instance()->DontUseNTShared.set_volatile_value(true);
        }

        if (!Config::Instance()->DontCreateD3D12DeviceForLuma.value_or_default())
        {
            quirks |= GameQuirk::LoadD3D12Manually;

            if (Config::Instance()->LoadReShade.value_or_default())
                quirks |= GameQuirk::CreateD3D12DeviceForLuma;
        }
    }

    if (Config::Instance()->LoadReShade.value_or_default() && quirks & GameQuirk::CreateD3D12DeviceForLuma &&
        State::Instance().activeFgInput != FGInput::NoFG && State::Instance().activeFgInput != FGInput::Nukems)
    {
        Config::Instance()->DxgiFactoryWrapping.set_volatile_value(true);
        State::Instance().detectedQuirks.push_back("Factory wrapping enabled due to delayed ReShade + FG");
        LOG_INFO("Factory wrapping enabled due to delayed ReShade + FG");
    }

    if (Config::Instance()->LoadSpecialK.value_or_default() && State::Instance().activeFgInput != FGInput::NoFG &&
        State::Instance().activeFgInput != FGInput::Nukems)
    {
        Config::Instance()->LoadSpecialK.set_volatile_value(false);
        State::Instance().detectedQuirks.push_back("FG Inputs are enabled, LoadSpecialK disabled");
        LOG_INFO("FG Inputs are enabled, LoadSpecialK disabled");
    }

    State::Instance().gameQuirks = quirks;

    printQuirks(quirks);
}

void CheckForExcludedProcess()
{
    std::wstring exeLower = Util::ExePath().filename().wstring();
    std::transform(exeLower.begin(), exeLower.end(), exeLower.begin(), ::towlower);

    // If target process is set, only that process is hooked
    if (Config::Instance()->TargetProcess.has_value())
    {
        // Config reads string as lowercase already
        std::wstring targetProcess = Config::Instance()->TargetProcess.value();

        if (exeLower != targetProcess)
        {
            _passThruMode = true;
            return;
        }
    }

    // Config reads string as lowercase already
    static const std::wstring exclusionList = Config::Instance()->ProcessExclusionList.value_or_default() + L"|";

    static std::vector<std::wstring> exclusions = []()
    {
        std::vector<std::wstring> result;
        size_t start = 0, end;

        while ((end = exclusionList.find(L'|', start)) != std::wstring::npos)
        {
            result.emplace_back(exclusionList.substr(start, end - start));
            start = end + 1;
        }
        return result;
    }();

    for (auto& e : exclusions)
    {
        if (exeLower == e)
        {
            _passThruMode = true;
            return;
        }
    }

    _passThruMode = false;
}

DWORD WINAPI InitThread(LPVOID hModuleVoid)
{
    HMODULE hModule = (HMODULE) hModuleVoid;

    auto detectedGpus = IdentifyGpu::getAllGpus();
    std::string gpus = "Detected GPUs:\n";

    std::string indent(22, ' ');

    for (auto& gpu : detectedGpus)
    {
        gpus += std::format("{}{}\n", indent, gpu.name);
        gpus += std::format("{}    vendorId: {:X}, deviceId: {:X}, VRAM: {}MB\n", indent, (uint32_t) gpu.vendorId,
                            gpu.deviceId, gpu.dedicatedVramInBytes / (1024 * 1024));
        gpus += std::format("{}    dxvk: {}, vkd3d-proton: {}\n", indent, gpu.usesDxvk, gpu.usesVkd3dProton);
        gpus += std::format("{}    Upscaler support - fsr4: {}, dlss: {}\n", indent, gpu.fsr4Capable, gpu.dlssCapable);
    }

    spdlog::info(gpus);

    InitFSR4Update();

    // TODO: try to reactivate DxgiSpoofing if was auto on Nvidia cards without DLSS

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(hModule);

        HMODULE handle = nullptr;
        OSVERSIONINFOW winVer { 0 };

        dllModule = hModule;
        exeModule = GetModuleHandle(nullptr);
        processId = GetCurrentProcessId();

        CheckForExcludedProcess();

        if (_passThruMode)
        {
            NtdllProxy::Init();
            KernelBaseProxy::Init();
            Kernel32Proxy::Init();

            CheckWorkingMode();
            return true;
        }

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
        spdlog::info("");
        spdlog::info("LogLevel: {0}", Config::Instance()->LogLevel.value_or_default());

        spdlog::info("");
        if (Util::GetRealWindowsVersion(winVer))
            spdlog::info("Windows version: {} ({}.{}.{})", Util::GetWindowsName(winVer), winVer.dwMajorVersion,
                         winVer.dwMinorVersion, winVer.dwBuildNumber, winVer.dwPlatformId);
        else
            spdlog::warn("Can't read windows version");

        spdlog::info("");

        spdlog::info("Config parameters:");
        for (const std::string& l : Config::Instance()->GetConfigLog())
            spdlog::info(l);

#ifdef VER_PRE_RELEASE
        spdlog::info("Pre-release build, disabling update checks");
        Config::Instance()->CheckForUpdate.set_volatile_value(false);
#endif

        // Initial state of FG
        State::Instance().activeFgInput = Config::Instance()->FGInput.value_or_default();
        State::Instance().activeFgOutput = Config::Instance()->FGOutput.value_or_default();

        // Init Kernel proxies
        NtdllProxy::Init();
        KernelBaseProxy::Init();
        Kernel32Proxy::Init();

        // Check for Wine
        spdlog::info("");
        State::Instance().isRunningOnLinux = IsRunningOnWine();

        // Not foolproof
        // calls LoadLibraryExW inside DllMain but seems mostly fine if we only call NvAPI_GetInterfaceVersionString
        auto isNvidiaViaNvapi = [&]()
        {
            bool nvidiaDetected = false;

            // Only try to load the real nvapi which is located in system32
            auto nvapiModule = NtdllProxy::LoadLibraryExW_Ldr(L"nvapi64.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

            // No nvapi, should not be nvidia
            if (!nvapiModule)
            {
                spdlog::debug("Nvidia detected: {}", nvidiaDetected);
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
                    spdlog::debug("Using dxvk-nvapi");
                    DISPLAY_DEVICEA dd = {};
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
                else if (o_NvAPI_QueryInterface(0x21382138))
                {
                    spdlog::error("Using fakenvapi as nvapi64.dll, remove it!");
                    nvidiaDetected = false;
                }
                else
                {
                    spdlog::debug("Using Nvidia's nvapi");
                    auto init = GET_INTERFACE(NvAPI_Initialize, o_NvAPI_QueryInterface);
                    if (init && init() == NVAPI_OK)
                    {
                        nvidiaDetected = true;

                        if (auto unload = GET_INTERFACE(NvAPI_Unload, o_NvAPI_QueryInterface))
                            unload();
                    }
                }
            }

            NtdllProxy::FreeLibrary_Ldr(nvapiModule);

            spdlog::debug("Nvidia detected: {}", nvidiaDetected);

            return nvidiaDetected;
        };

        bool possibleNvidia = isNvidiaViaNvapi();

        // Not 100% accurate for Nvidia cards without DLSS
        if (Config::Instance()->DLSSEnabled.value_or_default() && possibleNvidia)
        {
            auto exePath = Util::ExePath().remove_filename();
            State::Instance().NVNGX_DLSS_Path = Util::FindFilePath(exePath, "nvngx_dlss.dll");
            State::Instance().NVNGX_DLSSD_Path = Util::FindFilePath(exePath, "nvngx_dlssd.dll");
            State::Instance().NVNGX_DLSSG_Path = Util::FindFilePath(exePath, "nvngx_dlssg.dll");

            if (State::Instance().NVNGX_DLSS_Path.has_value())
            {
                spdlog::info("Enabling DLSS");
                Config::Instance()->DLSSEnabled.set_volatile_value(true);
            }
            else
            {
                spdlog::warn("nvngx_dlss.dll not found, disabling DLSS");
                Config::Instance()->DLSSEnabled.set_volatile_value(false);
            }

            // Assumes that dxgi spoofing is only used to enable DLSS
            if (!Config::Instance()->DxgiSpoofing.has_value())
            {
                spdlog::info("Disabling DxgiSpoofing");
                Config::Instance()->DxgiSpoofing.set_volatile_value(false);
            }
        }
        else
        {
            Config::Instance()->DLSSEnabled.set_volatile_value(false);
        }

        spdlog::info("");
        CheckQuirks(possibleNvidia);

        // Check for working mode and attach hooks
        spdlog::info("");
        CheckWorkingMode();

        // OptiFG & Overlay Checks
        if ((Config::Instance()->FGInput.value_or_default() == FGInput::Upscaler) &&
            !Config::Instance()->DisableOverlays.has_value())
            Config::Instance()->DisableOverlays.set_volatile_value(true);

        if (Config::Instance()->DisableOverlays.value_or_default())
        {
            _wputenv_s(L"SteamNoOverlayUIDrawing", L"1");
            SetEnvironmentVariableW(L"SteamNoOverlayUIDrawing", L"1");
        }

        // FSR4 Watermark, overrides environment variable only if set in config
        if (Config::Instance()->Fsr4EnableWatermark.has_value())
        {
            if (Config::Instance()->Fsr4EnableWatermark.value())
            {
                _wputenv_s(L"MLSR-WATERMARK", L"1");
                SetEnvironmentVariableW(L"MLSR-WATERMARK", L"1");

                if (!Config::Instance()->FpsOverlayPos.has_value())
                    Config::Instance()->FpsOverlayPos.set_volatile_value(1); // Top right
            }
            else
            {
                _wputenv_s(L"MLSR-WATERMARK", L"0");
                SetEnvironmentVariableW(L"MLSR-WATERMARK", L"0");
            }
        }

        if (Config::Instance()->FSRFGEnableWatermark.has_value())
        {
            if (Config::Instance()->FSRFGEnableWatermark.value())
            {
                _wputenv_s(L"MLFI-WATERMARK", L"1");
                SetEnvironmentVariableW(L"MLFI-WATERMARK", L"1");

                if (!Config::Instance()->FpsOverlayPos.has_value())
                    Config::Instance()->FpsOverlayPos.set_volatile_value(1); // Top right
            }
            else
            {
                _wputenv_s(L"MLFI-WATERMARK", L"0");
                SetEnvironmentVariableW(L"MLFI-WATERMARK", L"0");
            }
        }

        // Asi plugins
        if (Config::Instance()->LoadAsiPlugins.value_or_default())
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
            spdlog::info("");

            if (Config::Instance()->UseFsr2VulkanInputs.value_or_default())
                HookFSR2VkExeInputs();
            else if (Config::Instance()->UseFsr2Dx11Inputs.value_or_default())
                HookFSR2Dx11ExeInputs();
            else
            {
                handle = GetDllNameWModule(&fsr2NamesW);
                if (handle != nullptr)
                    HookFSR2Inputs(handle);

                handle = GetDllNameWModule(&fsr2BENamesW);
                if (handle != nullptr)
                    HookFSR2Dx12Inputs(handle);

                HookFSR2ExeInputs();
            }
        }

        if (Config::Instance()->EnableFsr3Inputs.value_or_default())
        {
            handle = GetDllNameWModule(&fsr3NamesW);
            if (handle != nullptr)
                HookFSR3Inputs(handle);

            handle = GetDllNameWModule(&fsr3BENamesW);
            if (handle != nullptr)
                HookFSR3Dx12Inputs(handle);

            HookFSR3ExeInputs();
        }
        // HookFfxExeInputs();

        if (State::Instance().activeFgInput == FGInput::FSRFG30)
        {
            FSR3FG::HookFSR3FGInputs();
            FSR3FG::HookFSR3FGExeInputs();
        }

        for (size_t i = 0; i < 300; i++)
        {
            State::Instance().frameTimes.push_back(0.0f);
            State::Instance().upscaleTimes.push_back(0.0f);
        }

        spdlog::info("");
        spdlog::info("Init done");
        spdlog::info("---------------------------------------------");
        spdlog::info("");

        CreateThread(nullptr, 0, InitThread, hModule, 0, nullptr);
        break;
    }

    case DLL_PROCESS_DETACH:
        State::Instance().isShuttingDown = true;

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
            NtdllProxy::FreeLibrary_Ldr(skModule);

        if (reshadeModule != nullptr)
            NtdllProxy::FreeLibrary_Ldr(reshadeModule);

        if (_asiHandles.size() > 0)
        {
            for (size_t i = 0; i < _asiHandles.size(); i++)
                NtdllProxy::FreeLibrary_Ldr(_asiHandles[i]);
        }

        for (const PVOID& v : State::Instance().modulesToFree)
        {
            NtdllProxy::FreeLibrary_Ldr(v);
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
