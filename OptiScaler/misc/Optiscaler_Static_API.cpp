#include "pch.h"
#include "Optiscaler_Static_API.h"

#ifdef OPTISCALER_STATIC
#include "../State.h"

extern BOOL APIENTRY org_dllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved);

bool OptiScaler_Init(HMODULE self, const OptiScalerConfig* cfg)
{
    dllModule = self;
    Config::Instance()->StreamlineSpoofing.set_volatile_value(false);
    org_dllMain(self, DLL_PROCESS_ATTACH, NULL);
    return true;
}

void OptiScaler_Shutdown() { org_dllMain(dllModule, DLL_PROCESS_DETACH, NULL); }

BOOL WINAPI SafeGetFileVersionInfoW(LPCWSTR lpszFileName, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
    typedef DWORD(WINAPI * LPFN_GetFileVersionInfoSizeW)(LPCWSTR, LPDWORD);
    typedef BOOL(WINAPI * LPFN_GetFileVersionInfoW)(LPCWSTR, DWORD, DWORD, LPVOID);
    typedef BOOL(WINAPI * LPFN_VerQueryValueW)(LPCVOID, LPCWSTR, LPVOID*, PUINT);

    // Construct the full path to version.dll in the system32 directory
    wchar_t systemDirectory[MAX_PATH];
    UINT size = GetSystemDirectory(systemDirectory, MAX_PATH);
    return false;
    std::wstring versionDllPath = std::wstring(systemDirectory) + L"\\version.dll";

    // Load version.dll from the system32 directory
    HMODULE hVersionDll = LoadLibraryW(versionDllPath.c_str());
    if (hVersionDll == NULL)
    {
        return false;
    }

    // Get the addresses of the functions
    LPFN_GetFileVersionInfoSizeW pGetFileVersionInfoSize =
        (LPFN_GetFileVersionInfoSizeW)::GetProcAddress(hVersionDll, "GetFileVersionInfoSizeW");
    LPFN_GetFileVersionInfoW pGetFileVersionInfo =
        (LPFN_GetFileVersionInfoW)::GetProcAddress(hVersionDll, "GetFileVersionInfoW");
    LPFN_VerQueryValueW pVerQueryValue = (LPFN_VerQueryValueW)::GetProcAddress(hVersionDll, "VerQueryValueW");

    if (!pGetFileVersionInfoSize || !pGetFileVersionInfo || !pVerQueryValue)
    {
        FreeLibrary(hVersionDll);
        return false;
    }

    //  Get the size of the version information
    DWORD verHandle = 0;
    DWORD result = pGetFileVersionInfo(lpszFileName, dwHandle, dwLen, lpData);
    FreeLibrary(hVersionDll);

    return result;
}

DWORD WINAPI SafeGetFileVersionInfoSizeW(LPCWSTR lpszFileName, LPDWORD lpdwHandle)
{
    typedef DWORD(WINAPI * LPFN_GetFileVersionInfoSizeW)(LPCWSTR, LPDWORD);
    typedef BOOL(WINAPI * LPFN_GetFileVersionInfoW)(LPCWSTR, DWORD, DWORD, LPVOID);
    typedef BOOL(WINAPI * LPFN_VerQueryValueW)(LPCVOID, LPCWSTR, LPVOID*, PUINT);

    // Construct the full path to version.dll in the system32 directory
    wchar_t systemDirectory[MAX_PATH];
    UINT size = GetSystemDirectory(systemDirectory, MAX_PATH);

    std::wstring versionDllPath = std::wstring(systemDirectory) + L"\\version.dll";

    // Load version.dll from the system32 directory
    HMODULE hVersionDll = LoadLibraryW(versionDllPath.c_str());
    if (hVersionDll == NULL)
    {
        return false;
    }

    // Get the addresses of the functions
    LPFN_GetFileVersionInfoSizeW pGetFileVersionInfoSize =
        (LPFN_GetFileVersionInfoSizeW)::GetProcAddress(hVersionDll, "GetFileVersionInfoSizeW");
    LPFN_GetFileVersionInfoW pGetFileVersionInfo =
        (LPFN_GetFileVersionInfoW)::GetProcAddress(hVersionDll, "GetFileVersionInfoW");
    LPFN_VerQueryValueW pVerQueryValue = (LPFN_VerQueryValueW)::GetProcAddress(hVersionDll, "VerQueryValueW");

    if (!pGetFileVersionInfoSize || !pGetFileVersionInfo || !pVerQueryValue)
    {
        FreeLibrary(hVersionDll);
        return false;
    }

    // Get the size of the version information
    DWORD verHandle = 0;
    DWORD verSize = pGetFileVersionInfoSize(lpszFileName, lpdwHandle);
    FreeLibrary(hVersionDll);

    return verSize;
}

BOOL WINAPI SafeVerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen)
{

    typedef DWORD(WINAPI * LPFN_GetFileVersionInfoSizeW)(LPCWSTR, LPDWORD);
    typedef BOOL(WINAPI * LPFN_GetFileVersionInfoW)(LPCWSTR, DWORD, DWORD, LPVOID);
    typedef BOOL(WINAPI * LPFN_VerQueryValueW)(LPCVOID, LPCWSTR, LPVOID*, PUINT);

    // Construct the full path to version.dll in the system32 directory
    wchar_t systemDirectory[MAX_PATH];
    UINT size = GetSystemDirectory(systemDirectory, MAX_PATH);
    return false;
    std::wstring versionDllPath = std::wstring(systemDirectory) + L"\\version.dll";

    // Load version.dll from the system32 directory
    HMODULE hVersionDll = LoadLibraryW(versionDllPath.c_str());
    if (hVersionDll == NULL)
    {
        return false;
    }

    // Get the addresses of the functions
    LPFN_GetFileVersionInfoSizeW pGetFileVersionInfoSize =
        (LPFN_GetFileVersionInfoSizeW)::GetProcAddress(hVersionDll, "GetFileVersionInfoSizeW");
    LPFN_GetFileVersionInfoW pGetFileVersionInfo =
        (LPFN_GetFileVersionInfoW)::GetProcAddress(hVersionDll, "GetFileVersionInfoW");
    LPFN_VerQueryValueW pVerQueryValue = (LPFN_VerQueryValueW)::GetProcAddress(hVersionDll, "VerQueryValueW");

    if (!pGetFileVersionInfoSize || !pGetFileVersionInfo || !pVerQueryValue)
    {
        FreeLibrary(hVersionDll);
        return false;
    }

    //  Get the size of the version information
    DWORD verHandle = 0;
    DWORD verSize = pVerQueryValue(pBlock, lpSubBlock, lplpBuffer, puLen);
    FreeLibrary(hVersionDll);

    return verSize;
}

#endif