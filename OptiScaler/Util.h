#pragma once
#include "SysUtils.h"

#include <filesystem>

#include <dxgi1_6.h>
#include <d3d11.h>
#include <d3d12.h>
#include <xess.h>

namespace Util
{
typedef struct _version_t
{
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    uint16_t reserved;
} version_t;

struct MonitorInfo
{
    HMONITOR handle;
    int x;
    int y;
    int width;
    int height;
    RECT monitorRect;  // full monitor bounds
    RECT workRect;     // work area (taskbar excluded)
    std::wstring name; // e.g., \\.\DISPLAY1
};

std::filesystem::path ExePath();
std::filesystem::path DllPath();
std::optional<std::filesystem::path> NvngxPath();

double MillisecondsNow();
std::wstring ToLower(std::wstring value);

HWND GetProcessWindow();
bool GetDLLVersion(std::wstring dllPath, version_t* versionOut);
bool GetDLLVersion(std::wstring dllPath, xess_version_t* versionOut);
bool GetRealWindowsVersion(OSVERSIONINFOW& osInfo);
std::string GetWindowsName(const OSVERSIONINFOW& os);
void GetExeInfo();
std::wstring GetWindowTitle(HWND hwnd);
std::optional<std::filesystem::path> FindFilePath(const std::filesystem::path& startDir,
                                                  const std::filesystem::path fileName);
std::string WhoIsTheCaller(void* returnAddress);
HMODULE GetCallerModule(void* returnAddress);
MonitorInfo GetMonitorInfoForWindow(HWND hwnd);
MonitorInfo GetMonitorInfoForOutput(IDXGIOutput* pOutput);
int GetActiveRefreshRate(HWND hwnd);
bool CheckForRealObject(std::string functionName, IUnknown* pObject, IUnknown** ppRealObject);
void GetDeviceRemovedReason(ID3D11Device* pDevice);
void GetDeviceRemovedReason(ID3D12Device* pDevice);
void LoadProxyLibrary(const std::wstring& name, const std::wstring& optiPath, const std::wstring& overridePath,
                      HMODULE* memoryModule, HMODULE* loadedModule);
void** FindVTableByRtti(HMODULE module, const char* mangledName);

}; // namespace Util

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        // Set a breakpoint on this line to catch DirectX API errors
        throw std::exception();
    }
}
