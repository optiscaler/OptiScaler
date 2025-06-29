#pragma once
#include <pch.h>
#include "UtilCom.h"

#include <filesystem>

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

std::filesystem::path ExePath();
std::filesystem::path DllPath();
std::optional<std::filesystem::path> NvngxPath();
double MillisecondsNow();

HWND GetProcessWindow();
bool GetDLLVersion(std::wstring dllPath, version_t* versionOut);
bool GetDLLVersion(std::wstring dllPath, xess_version_t* versionOut);
bool GetRealWindowsVersion(OSVERSIONINFOW& osInfo);
std::string GetWindowsName(const OSVERSIONINFOW& os);
std::wstring GetExeProductName();
std::wstring GetWindowTitle(HWND hwnd);
std::optional<std::filesystem::path> FindFilePath(const std::filesystem::path& startDir,
                                                  const std::filesystem::path fileName);
std::string WhoIsTheCaller(void* returnAddress);
}; // namespace Util

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        // Set a breakpoint on this line to catch DirectX API errors
        throw std::exception();
    }
}
