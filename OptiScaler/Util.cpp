#include "pch.h"

#include "Util.h"
#include "Config.h"

#include <proxies/Ntdll_Proxy.h>
#include <proxies/KernelBase_Proxy.h>

#include <shlobj.h>
#include <cwctype>
#include <queue>

#include <hooks/Gdi32_Hooks.h>

typedef LONG(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
typedef decltype(&GetFileVersionInfoSizeW) PFN_GetFileVersionInfoSizeW;
typedef decltype(&GetFileVersionInfoW) PFN_GetFileVersionInfoW;
typedef decltype(&VerQueryValueW) PFN_VerQueryValueW;

static IID streamlineRiid {};

/// <summary>
/// Returns caller module filename
/// Don't forget to add #pragma intrinsic(_ReturnAddress)
/// </summary>
/// <param name="returnAddress">Use _ReturnAddress() for this</param>
/// <returns>Caller module filename</returns>
std::string Util::WhoIsTheCaller(void* returnAddress)
{
    char callerPath[MAX_PATH] = { 0 };

    // Get the return address from the current function call.
    // void* returnAddress = _ReturnAddress();

    // Get the base address of the module containing the return address.
    if (HMODULE hModule = GetCallerModule(returnAddress); hModule != nullptr)
    {
        // Get the full path of the calling module.
        GetModuleFileNameA(hModule, callerPath, sizeof(callerPath));
        auto path = std::filesystem::path(callerPath);

        return wstring_to_string(path.filename().wstring());
    }

    return "";
}

HMODULE Util::GetCallerModule(void* returnAddress)
{
    HMODULE hModule = NULL;

    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR) returnAddress, &hModule);

    return hModule;
}

std::wstring Util::GetWindowTitle(HWND hwnd)
{
    const int maxLength = 512;
    wchar_t buffer[maxLength] = { 0 };

    // First, check if the window is valid and visible
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd))
        return L"";

    // Try to get the text using SendMessageTimeout to avoid hanging
    LRESULT result = 0;
    if (SendMessageTimeoutW(hwnd, WM_GETTEXT, (WPARAM) maxLength, (LPARAM) buffer, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2,
                            (PDWORD_PTR) &result) != 0)
    {
        return std::wstring(buffer);
    }

    return L"";
}

bool Util::GetRealWindowsVersion(OSVERSIONINFOW& osInfo)
{
    HMODULE hMod = ::GetModuleHandleW(L"ntdll.dll");
    if (hMod)
    {
        RtlGetVersionPtr fxPtr = (RtlGetVersionPtr)::GetProcAddress(hMod, "RtlGetVersion");
        if (fxPtr != nullptr)
        {
            osInfo.dwOSVersionInfoSize = sizeof(osInfo);

            if (fxPtr(&osInfo) == 0) // STATUS_SUCCESS == 0
                return true;
        }
    }

    return false;
}

std::string Util::GetWindowsName(const OSVERSIONINFOW& os)
{
    DWORD major = os.dwMajorVersion;
    DWORD minor = os.dwMinorVersion;
    DWORD build = os.dwBuildNumber;

    if (major == 10 && build >= 22000)
        return "Windows 11";
    if (major == 10)
        return "Windows 10";
    if (major == 6 && minor == 3)
        return "Windows 8.1";
    if (major == 6 && minor == 2)
        return "Windows 8";
    if (major == 6 && minor == 1)
        return "Windows 7";
    if (major == 6 && minor == 0)
        return "Windows Vista";
    if (major == 5 && minor == 1)
        return "Windows XP";

    return "Unknown Windows Version";
}

std::wstring Util::ToLower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });

    return value;
}

static bool ContainsAny(const std::wstring& value, std::initializer_list<std::wstring_view> needles)
{
    for (auto needle : needles)
    {
        if (value.find(needle) != std::wstring::npos)
            return true;
    }

    return false;
}

static bool HasUnrealBinariesParentStructure(const std::filesystem::path& exePath)
{
    auto platformDir = exePath.parent_path();

    if (platformDir.empty())
        return false;

    auto binariesDir = platformDir.parent_path();

    if (binariesDir.empty())
        return false;

    std::wstring binariesName = Util::ToLower(binariesDir.filename().wstring());

    bool isBinariesFolder = binariesName == L"binaries";

    if (!isBinariesFolder)
        return false;

    std::wstring platformName = Util::ToLower(platformDir.filename().wstring());

    bool isWindowsPlatformFolder = platformName == L"win64" || platformName == L"win32" || platformName == L"wingdk" ||
                                   platformName == L"windows" || platformName.starts_with(L"win");

    return isBinariesFolder && isWindowsPlatformFolder;
}

void Util::GetExeInfo()
{
    // In case of working ag version.dll
    // Loading original dll from system

    auto exePath = Util::ExePath();
    auto exeDir = exePath.parent_path();
    auto exePathFilename = exePath.filename().string();
    auto exePathFilenameW = exePath.filename().wstring();
    State::Instance().gameExe = exePathFilename;

    wchar_t sysFolder[MAX_PATH];
    GetSystemDirectory(sysFolder, MAX_PATH);
    std::filesystem::path sysPath(sysFolder);

    auto dll = NtdllProxy::LoadLibraryExW_Ldr(L"version.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

    if (dll == nullptr)
        return;

    auto o_GetFileVersionInfoSizeW = (PFN_GetFileVersionInfoSizeW) GetProcAddress(dll, "GetFileVersionInfoSizeW");
    auto o_GetFileVersionInfoW = (PFN_GetFileVersionInfoW) GetProcAddress(dll, "GetFileVersionInfoW");
    auto o_VerQueryValueW = (PFN_VerQueryValueW) GetProcAddress(dll, "VerQueryValueW");

    if (o_GetFileVersionInfoSizeW == nullptr || o_GetFileVersionInfoW == nullptr || o_VerQueryValueW == nullptr)
        return;

    DWORD handle = 0;
    DWORD versionSize = o_GetFileVersionInfoSizeW(Util::ExePath().c_str(), &handle);
    if (versionSize == 0)
        return;

    std::vector<BYTE> versionData(versionSize);
    if (!o_GetFileVersionInfoW(Util::ExePath().c_str(), handle, versionSize, versionData.data()))
        return;

    struct LANGANDCODEPAGE
    {
        WORD wLanguage;
        WORD wCodePage;
    }* lpTranslate;

    UINT cbTranslate = 0;
    if (!o_VerQueryValueW(versionData.data(), L"\\VarFileInfo\\Translation", (LPVOID*) &lpTranslate, &cbTranslate))
        return;

    auto QueryVersionString = [&](const wchar_t* name) -> std::wstring
    {
        std::wstring query = L"\\StringFileInfo\\" +
                             std::format(L"{:04x}{:04x}", lpTranslate[0].wLanguage, lpTranslate[0].wCodePage) + L"\\" +
                             name;

        LPWSTR value = nullptr;
        UINT size = 0;

        if (o_VerQueryValueW(versionData.data(), query.c_str(), reinterpret_cast<LPVOID*>(&value), &size) && value &&
            size > 0)
        {
            return value;
        }

        return {};
    };

    std::wstring productName = QueryVersionString(L"ProductName");
    std::wstring productVersion = QueryVersionString(L"ProductVersion");
    std::wstring fileVersion = QueryVersionString(L"FileVersion");
    std::wstring legalCopyright = QueryVersionString(L"LegalCopyright");
    std::wstring companyName = QueryVersionString(L"CompanyName");
    std::wstring fileDescription = QueryVersionString(L"FileDescription");

    if (!productName.empty())
    {
        State::Instance().gameName = wstring_to_string(productName);
    }

    if (!productVersion.empty())
    {
        State::Instance().gameVersion = wstring_to_string(productVersion);
    }
    else if (!fileVersion.empty())
    {
        State::Instance().gameVersion = wstring_to_string(fileVersion);
    }

    std::wstring productLower = ToLower(productName);
    std::wstring copyrightLower = ToLower(legalCopyright);
    std::wstring companyLower = ToLower(companyName);
    std::wstring descriptionLower = ToLower(fileDescription);
    std::wstring exeLower = ToLower(exePathFilenameW);

    bool hasUnityFiles =
        std::filesystem::exists(exeDir / L"UnityPlayer.dll") || std::filesystem::exists(exeDir / L"GameAssembly.dll");

    bool isUnity = hasUnityFiles || ContainsAny(productLower, { L"unity" }) ||
                   ContainsAny(companyLower, { L"unity", L"unity technologies" }) ||
                   ContainsAny(copyrightLower, { L"unity", L"unity technologies" }) ||
                   ContainsAny(descriptionLower, { L"unity" });

    if (isUnity)
    {
        State::Instance().gameEngine = GameEngineType::Unity;
        return;
    }

    bool hasUnrealShippingExePattern =
        exeLower.ends_with(L"-win64-shipping.exe") || exeLower.ends_with(L"-wingdk-shipping.exe") ||
        exeLower.ends_with(L"-windows-shipping.exe") || exeLower.ends_with(L"-win64-test.exe") ||
        exeLower.ends_with(L"-win64-debug.exe") || exeLower.ends_with(L"-windows-test.exe") ||
        exeLower.ends_with(L"-windows-debug.exe");

    bool hasUnrealBinariesStructure = HasUnrealBinariesParentStructure(exePath);

    bool isUnreal = ContainsAny(productLower, { L"unreal", L"unreal engine" }) ||
                    ContainsAny(companyLower, { L"epic games", L"epic games, inc." }) ||
                    ContainsAny(copyrightLower, { L"epic games", L"unreal", L"unreal engine" }) ||
                    ContainsAny(descriptionLower, { L"unreal", L"unreal engine" }) || hasUnrealShippingExePattern ||
                    hasUnrealBinariesStructure;

    if (isUnreal)
    {
        State::Instance().gameEngine = GameEngineType::Unreal;
        return;
    }
}

std::filesystem::path Util::DllPath()
{
    static std::filesystem::path dll;

    if (dll.empty())
    {
        wchar_t dllPath[MAX_PATH];
        GetModuleFileNameW(dllModule, dllPath, MAX_PATH);
        dll = std::filesystem::path(dllPath);

        LOG_INFO("{0}", wstring_to_string(dll.wstring()));
    }

    return dll;
}

std::optional<std::filesystem::path> Util::NvngxPath()
{
    // Checking _nvngx.dll / nvngx.dll location from registry based on DLSSTweaks
    // https://github.com/emoose/DLSSTweaks/blob/7ebf418c79670daad60a079c0e7b84096c6a7037/src/ProxyNvngx.cpp#L303
    LOG_INFO("trying to load nvngx from registry path!");

    HKEY regNGXCore;
    LSTATUS ls;
    std::optional<std::filesystem::path> result;

    ls = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Services\\nvlddmkm\\NGXCore", 0, KEY_READ,
                       &regNGXCore);

    if (ls == ERROR_SUCCESS)
    {
        wchar_t regNGXCorePath[260] {};
        DWORD NGXCorePathSize = 260;

        ls = RegQueryValueExW(regNGXCore, L"NGXPath", nullptr, nullptr, (LPBYTE) regNGXCorePath, &NGXCorePathSize);

        if (ls == ERROR_SUCCESS)
        {
            auto path = std::filesystem::path(regNGXCorePath);
            LOG_INFO(L"nvngx registry path: {0}", path.wstring());
            return path;
        }
    }

    return result;
}

double Util::MillisecondsNow()
{
    static LARGE_INTEGER s_frequency;
    static BOOL s_use_qpc = QueryPerformanceFrequency(&s_frequency);
    double milliseconds = 0;

    if (s_use_qpc)
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        milliseconds = double(1000.0 * now.QuadPart) / s_frequency.QuadPart;
    }
    else
    {
        milliseconds = double(GetTickCount64());
    }

    return milliseconds;
}

std::filesystem::path Util::ExePath()
{
    static std::filesystem::path exe;

    if (exe.empty())
    {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        exe = std::filesystem::path(exePath);
    }

    return exe;
}

static BOOL CALLBACK EnumWindowsCallback(HWND handle, LPARAM lParam)
{
    const auto isMainWindow = [handle]()
    { return GetWindow(handle, GW_OWNER) == nullptr && IsWindowVisible(handle) && handle != GetConsoleWindow(); };

    DWORD pID = 0;
    GetWindowThreadProcessId(handle, &pID);

    if (pID != processId || !isMainWindow() || handle == GetConsoleWindow())
        return TRUE;

    *reinterpret_cast<HWND*>(lParam) = handle;

    return FALSE;
}

HWND Util::GetProcessWindow()
{
    HWND hwnd = nullptr;
    EnumWindows(EnumWindowsCallback, reinterpret_cast<LPARAM>(&hwnd));

    if (hwnd == nullptr)
    {
        LOG_DEBUG("EnumWindows returned null using GetForegroundWindow()");
        hwnd = GetForegroundWindow();
    }

    return hwnd;
}

static inline std::string LogLastError()
{
    DWORD errorCode = GetLastError();
    LPWSTR errorBuffer = nullptr;
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                  errorCode, 0, (LPWSTR) &errorBuffer, 0, NULL);

    std::string result;

    if (errorBuffer)
    {
        std::wstring errMsg(errorBuffer);
        result =
            std::format("{} ({})", errorCode,
                        wstring_to_string(errMsg).erase(wstring_to_string(errMsg).find_last_not_of("\t\n\v\f\r ") + 1));
        LocalFree(errorBuffer);
    }
    else
    {
        result = std::format("Unknown error ({}).", errorCode);
    }

    return result;
}

bool Util::GetFileVersion(std::wstring dllPath, version_t* fileVersionOut, version_t* productVersionOut)
{
    // Step 1: Get the size of the version information
    DWORD handle = 0;
    DWORD versionSize = GetFileVersionInfoSizeW(dllPath.c_str(), &handle);

    if (versionSize == 0)
    {
        // LOG_ERROR("Failed to get version info size: {0:X}", LogLastError());
        return false;
    }

    // Step 2: Allocate buffer and get the version information
    std::vector<BYTE> versionInfo(versionSize);
    if (!GetFileVersionInfoW(dllPath.c_str(), handle, versionSize, versionInfo.data()))
    {
        // LOG_ERROR("Failed to get version info: {0:X}", LogLastError());
        return false;
    }

    // Step 3: Extract the version information
    VS_FIXEDFILEINFO* fileInfo = nullptr;
    UINT size = 0;
    if (!VerQueryValueW(versionInfo.data(), L"\\", reinterpret_cast<LPVOID*>(&fileInfo), &size))
    {
        // LOG_ERROR("Failed to query version value: {0:X}", LogLastError());
        return false;
    }

    if (fileInfo != nullptr && fileVersionOut != nullptr)
    {
        // Extract major, minor, build, and revision numbers from version information
        DWORD fileVersionMS = fileInfo->dwFileVersionMS;
        DWORD fileVersionLS = fileInfo->dwFileVersionLS;

        fileVersionOut->major = (fileVersionMS >> 16) & 0xffff;
        fileVersionOut->minor = (fileVersionMS >> 0) & 0xffff;
        fileVersionOut->patch = (fileVersionLS >> 16) & 0xffff;
        fileVersionOut->reserved = (fileVersionLS >> 0) & 0xffff;

        if (productVersionOut != nullptr)
        {
            DWORD productVersionMS = fileInfo->dwProductVersionMS;
            DWORD productVersionLS = fileInfo->dwProductVersionLS;

            productVersionOut->major = (productVersionMS >> 16) & 0xffff;
            productVersionOut->minor = (productVersionMS >> 0) & 0xffff;
            productVersionOut->patch = (productVersionLS >> 16) & 0xffff;
            productVersionOut->reserved = (productVersionLS >> 0) & 0xffff;
        }
    }
    else
    {
        LOG_ERROR("No version information found!");
        return false;
    }
    return true;
}

bool Util::IsSubpath(const std::filesystem::path& path, const std::filesystem::path& base)
{
    auto rel = std::filesystem::relative(path, base);
    auto first = *rel.begin();
    return first != "." && first != "..";
}

std::optional<std::filesystem::path> Util::FindFilePath(const std::filesystem::path& startDir,
                                                        const std::filesystem::path& fileName)
{
    std::filesystem::path optiPath(Config::Instance()->MainDllPath.value());
    optiPath /= L"streamline";
    auto normalizedStreamlinePath = optiPath.lexically_normal();

    const bool isDlssgOutput = State::Instance().activeFgOutput == FGOutput::DLSSG;

    // 1) Direct check in startDir
    std::filesystem::path candidate = startDir / fileName;
    if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate))
    {
        LOG_INFO(L"{} found at {}", fileName.wstring(), candidate.parent_path().wstring());
        return candidate;
    }

    // Helper lambda to perform a Breadth-First Search (shallowest files first)
    auto SearchDirectoryBFS = [&](const std::filesystem::path& root) -> std::optional<std::filesystem::path>
    {
        std::queue<std::filesystem::path> directoriesToSearch;
        directoriesToSearch.push(root);

        while (!directoriesToSearch.empty())
        {
            std::filesystem::path currentDir = directoriesToSearch.front();
            directoriesToSearch.pop();

            std::error_code ec;
            auto dirIterator = std::filesystem::directory_iterator(
                currentDir, std::filesystem::directory_options::skip_permission_denied, ec);

            if (ec)
                continue;

            for (const auto& entry : dirIterator)
            {
                if (entry.is_directory())
                {
                    directoriesToSearch.push(entry.path());
                }
                else if (entry.path().filename() == fileName)
                {
                    auto normalizedPath = entry.path().lexically_normal();
                    if (isDlssgOutput || !IsSubpath(normalizedPath, normalizedStreamlinePath))
                    {
                        return entry.path();
                    }
                }
            }
        }
        return std::nullopt;
    };

    // 2) Breadth-First search under startDir
    if (auto foundPath = SearchDirectoryBFS(startDir))
    {
        LOG_INFO(L"{} found at {}", fileName.wstring(), foundPath.value().wstring());
        return foundPath;
    }

    // 3) Unreal-Engine/WinGDK fallback: check for Win64 or WinGDK in parent
    std::filesystem::path parent = startDir.parent_path().parent_path();
    uint32_t cnt = 0;
    for (const char* folder : { "Win64", "WinGDK", "Win64MasterMasterSteamPGO" })
    {
        if (std::filesystem::exists(parent / folder) && std::filesystem::is_directory(parent / folder))
        {
            // Move up two more levels from 'parent' to reach UE project root but one level for KCD2
            std::filesystem::path gameRoot;
            if (cnt < 2)
                gameRoot = parent.parent_path().parent_path();
            else
                gameRoot = parent.parent_path();

            // Run Breadth-First Search on the gameRoot fallback
            if (auto foundPath = SearchDirectoryBFS(gameRoot))
            {
                LOG_INFO(L"{} found at {}", fileName.wstring(), foundPath.value().wstring());
                return foundPath;
            }

            // If not found under this folder, break to avoid double-search
            break;
        }

        cnt++;
    }

    // Not found anywhere
    return std::nullopt;
}

int Util::GetActiveRefreshRate(HWND hwnd)
{
    // Step 1: Get monitor handle
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!hMon)
        return 0;

    // Step 2: Get monitor device name
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);

    if (!GetMonitorInfoW(hMon, &mi))
        return 0;

    // Step 3: Query active display mode
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);

    // Use ENUM_CURRENT_SETTINGS to get ACTUAL active mode
    if (!EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
        return 0;

    // Note: dmDisplayFrequency may contain 0 or 1 for "unspecified" in rare driver cases.
    return dm.dmDisplayFrequency;
}

Util::MonitorInfo Util::GetMonitorInfoForWindow(HWND hwnd)
{
    MonitorInfo out {};
    out.handle = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

    MONITORINFOEXW mi {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(out.handle, &mi))
    {
        out.x = mi.rcMonitor.left;
        out.y = mi.rcMonitor.top;
        out.width = mi.rcMonitor.right - mi.rcMonitor.left;
        out.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
        out.monitorRect = mi.rcMonitor;
        out.workRect = mi.rcWork;

        wchar_t bufName[32];
        std::wcsncpy(bufName, mi.szDevice, _countof(bufName) - 1);
        bufName[_countof(bufName) - 1] = L'\0';
        out.name = std::wstring(bufName);
    }
    return out;
}

Util::MonitorInfo Util::GetMonitorInfoForOutput(IDXGIOutput* pOutput)
{
    MonitorInfo out {};

    DXGI_OUTPUT_DESC desc {};
    pOutput->GetDesc(&desc);

    out.handle = desc.Monitor;

    MONITORINFOEXW mi {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(out.handle, &mi))
    {
        out.x = mi.rcMonitor.left;
        out.y = mi.rcMonitor.top;
        out.width = mi.rcMonitor.right - mi.rcMonitor.left;
        out.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
        out.monitorRect = mi.rcMonitor;
        out.workRect = mi.rcWork;

        wchar_t bufName[32];
        std::wcsncpy(bufName, mi.szDevice, _countof(bufName) - 1);
        bufName[_countof(bufName) - 1] = L'\0';
        out.name = std::wstring(bufName);
    }
    return out;
}

bool Util::CheckForRealObject(std::string functionName, IUnknown* pObject, IUnknown** ppRealObject)
{
    // Need to check if this is necessary

    if (streamlineRiid.Data1 == 0)
    {
        auto iidResult = IIDFromString(L"{ADEC44E2-61F0-45C3-AD9F-1B37379284FF}", &streamlineRiid);

        if (iidResult != S_OK)
            return false;
    }

    auto qResult = pObject->QueryInterface(streamlineRiid, (void**) ppRealObject);

    if (qResult == S_OK && *ppRealObject != nullptr)
    {
        LOG_INFO("{} Streamline proxy found!", functionName);
        (*ppRealObject)->Release();
        return true;
    }

    return false;
}

void Util::GetDeviceRemovedReason(ID3D11Device* pDevice)
{
    auto reason = pDevice->GetDeviceRemovedReason();

    switch (reason)
    {
    case DXGI_ERROR_DEVICE_HUNG:
        LOG_ERROR("Device removed reason: DXGI_ERROR_DEVICE_HUNG");
        break;

    case DXGI_ERROR_DEVICE_RESET:
        LOG_ERROR("Device removed reason: DXGI_ERROR_DEVICE_RESET");
        break;

    case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
        LOG_ERROR("Device removed reason: DXGI_ERROR_DRIVER_INTERNAL_ERROR");
        break;

    case DXGI_ERROR_INVALID_CALL:
        LOG_ERROR("Device removed reason: DXGI_ERROR_INVALID_CALL");
        break;

    case E_OUTOFMEMORY:
        LOG_ERROR("Device removed reason: E_OUTOFMEMORY");
        break;

    case E_FAIL:
        LOG_ERROR("Device removed reason: E_FAIL");
        break;

    default:
        LOG_ERROR("Device removed reason: Unknown ({:X})", reason);
    }
}

void Util::GetDeviceRemovedReason(ID3D12Device* pDevice)
{
    auto reason = pDevice->GetDeviceRemovedReason();

    switch (reason)
    {
    case DXGI_ERROR_DEVICE_HUNG:
        LOG_ERROR("Device removed reason: DXGI_ERROR_DEVICE_HUNG");
        break;

    case DXGI_ERROR_DEVICE_RESET:
        LOG_ERROR("Device removed reason: DXGI_ERROR_DEVICE_RESET");
        break;

    case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
        LOG_ERROR("Device removed reason: DXGI_ERROR_DRIVER_INTERNAL_ERROR");
        break;

    case DXGI_ERROR_INVALID_CALL:
        LOG_ERROR("Device removed reason: DXGI_ERROR_INVALID_CALL");
        break;

    case E_OUTOFMEMORY:
        LOG_ERROR("Device removed reason: E_OUTOFMEMORY");
        break;

    case E_FAIL:
        LOG_ERROR("Device removed reason: E_FAIL");
        break;

    default:
        LOG_ERROR("Device removed reason: Unknown ({:X})", (UINT) reason);
    }
}

void Util::LoadProxyLibrary(const std::wstring& name, const std::wstring& optiPath, const std::wstring& overridePath,
                            HMODULE* memoryModule, HMODULE* loadedModule)
{
    HMODULE result = nullptr;

    result = KernelBaseProxy::GetModuleHandleW_()(name.c_str());
    if (result != nullptr && result != dllModule)
    {
        LOG_INFO("{} already loaded at memory: {:X}", wstring_to_string(name), (size_t) result);
        *memoryModule = result;
    }
    result = nullptr;

    if (overridePath.size() > 0)
    {
        auto path = std::filesystem::path(overridePath);

        if (std::filesystem::exists(path))
        {
            if (std::filesystem::is_directory(path))
                path = path / name;

            result = NtdllProxy::LoadLibraryExW_Ldr(path.c_str(), NULL, NULL);

            if (result != nullptr && result != dllModule)
            {
                LOG_INFO("{} loaded from override path: {}", wstring_to_string(name), path.string());
                *loadedModule = result;
            }
        }
    }

    if (optiPath.size() > 0 && *loadedModule == nullptr)
    {
        auto path = std::filesystem::path(optiPath);

        if (std::filesystem::exists(path))
        {
            if (std::filesystem::is_directory(path))
                path = path / name;

            result = NtdllProxy::LoadLibraryExW_Ldr(path.c_str(), NULL, NULL);

            if (result != nullptr && result != dllModule)
            {
                LOG_INFO("{} loaded from Opti dll path: {}", wstring_to_string(name), path.string());
                *loadedModule = result;
            }
        }
    }

    if (*loadedModule == nullptr)
    {
        result = NtdllProxy::LoadLibraryExW_Ldr(name.c_str(), NULL, NULL);
        if (result != nullptr && result != dllModule)
        {
            LOG_INFO("{} loaded from system path", wstring_to_string(name));
            *loadedModule = result;
        }
    }

    if (*loadedModule == nullptr)
        LOG_WARN("Can't find {}, returning nullptr!", wstring_to_string(name));
}

std::map<Util::Luid, std::filesystem::path> Util::GetDriverStore()
{
    std::map<Util::Luid, std::filesystem::path> result;

    // Load D3DKMT functions dynamically
    bool libraryLoaded = false;
    HMODULE hGdi32 = KernelBaseProxy::GetModuleHandleW_()(L"Gdi32.dll");

    if (hGdi32 == nullptr)
    {
        hGdi32 = NtdllProxy::LoadLibraryExW_Ldr(L"Gdi32.dll", NULL, 0);
        libraryLoaded = hGdi32 != nullptr;
    }

    if (hGdi32 == nullptr)
    {
        LOG_ERROR("Failed to load Gdi32.dll");
        return result;
    }

    do
    {
        auto o_D3DKMTEnumAdapters =
            (PFN_D3DKMTEnumAdapters) KernelBaseProxy::GetProcAddress_()(hGdi32, "D3DKMTEnumAdapters");
        auto o_D3DKMTQueryAdapterInfo =
            (PFN_D3DKMTQueryAdapterInfo) KernelBaseProxy::GetProcAddress_()(hGdi32, "D3DKMTQueryAdapterInfo");
        auto o_D3DKMTCloseAdapter =
            (PFN_D3DKMTCloseAdapter) KernelBaseProxy::GetProcAddress_()(hGdi32, "D3DKMTCloseAdapter");

        if (o_D3DKMTEnumAdapters == nullptr || o_D3DKMTQueryAdapterInfo == nullptr || o_D3DKMTCloseAdapter == nullptr)
        {
            LOG_ERROR("Failed to resolve D3DKMT functions");
            break;
        }

        D3DKMT_UMDFILENAMEINFO umdFileInfo = {};
        D3DKMT_QUERYADAPTERINFO queryAdapterInfo = {};

        queryAdapterInfo.Type = KMTQAITYPE_UMDRIVERNAME;
        queryAdapterInfo.pPrivateDriverData = &umdFileInfo;
        queryAdapterInfo.PrivateDriverDataSize = sizeof(umdFileInfo);

        D3DKMT_ENUMADAPTERS enumAdapters = {};

        // Query the number of adapters first
        if (o_D3DKMTEnumAdapters(&enumAdapters) != 0)
        {
            LOG_ERROR("Failed to enumerate adapters.");
            break;
        }

        // If there are any adapters, the first one should be in the list
        if (enumAdapters.NumAdapters > 0)
        {
            for (size_t i = 0; i < enumAdapters.NumAdapters; i++)
            {
                D3DKMT_ADAPTERINFO adapter = enumAdapters.Adapters[i];
                queryAdapterInfo.hAdapter = adapter.hAdapter;

                auto hr = o_D3DKMTQueryAdapterInfo(&queryAdapterInfo);

                if (hr != 0)
                    LOG_WARN("Failed to query adapter info {:X}", hr);
                else
                {
                    Util::Luid luid(adapter.AdapterLuid.LowPart, adapter.AdapterLuid.HighPart);
                    result.emplace(std::move(luid), std::filesystem::path(umdFileInfo.UmdFileName).parent_path());
                }

                D3DKMT_CLOSEADAPTER closeAdapter = {};
                closeAdapter.hAdapter = adapter.hAdapter;
                auto closeResult = o_D3DKMTCloseAdapter(&closeAdapter);
                if (closeResult != 0)
                    LOG_ERROR("D3DKMTCloseAdapter error: {:X}", closeResult);
            }
        }
        else
        {
            LOG_ERROR("No adapters found.");
            break;
        }

    } while (false);

    if (libraryLoaded)
        NtdllProxy::FreeLibrary_Ldr(hGdi32);

    return result;
}
