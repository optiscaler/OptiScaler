#include <pch.h>
#include "User32_Spoofing.h"

#include <Config.h>

#include <detours/detours.h>

typedef BOOL(WINAPI* PFN_EnumDisplayDevicesA)(PSTR lpDevice, uint32_t iDevNum, DISPLAY_DEVICEA* lpDisplayDevice,
                                              uint32_t dwFlags);
typedef BOOL(WINAPI* PFN_EnumDisplayDevicesW)(PWSTR lpDevice, uint32_t iDevNum, DISPLAY_DEVICEW* lpDisplayDevice,
                                              uint32_t dwFlags);

static PFN_EnumDisplayDevicesA o_EnumDisplayDevicesA = nullptr;
static PFN_EnumDisplayDevicesW o_EnumDisplayDevicesW = nullptr;

inline static BOOL WINAPI hkEnumDisplayDevicesA(PSTR lpDevice, uint32_t iDevNum, DISPLAY_DEVICEA* lpDisplayDevice,
                                                uint32_t dwFlags)
{
    auto result = o_EnumDisplayDevicesA(lpDevice, iDevNum, lpDisplayDevice, dwFlags);

    // Monitors are enumerated with a device name
    if (!result || lpDisplayDevice == nullptr || lpDevice != nullptr || lpDisplayDevice->cb < sizeof(DISPLAY_DEVICEA))
        return result;

    static std::string vendorId = std::format("VEN_{:04X}", Config::Instance()->SpoofedVendorId.value_or_default());
    static std::string deviceId = std::format("DEV_{:04X}", Config::Instance()->SpoofedDeviceId.value_or_default());

    // Spoof VEN_/DEV_ tokens in DeviceID (e.g. "PCI\VEN_1002&DEV_687F&...")
    std::string deviceIdStr(lpDisplayDevice->DeviceID);
    bool found = false;

    static const char* srcVendors[] = { "VEN_1002", "VEN_8086" };
    for (auto srcVendor : srcVendors)
    {
        size_t pos = 0;
        while ((pos = deviceIdStr.find(srcVendor, pos)) != std::string::npos)
        {
            deviceIdStr.replace(pos, 8, vendorId);
            pos += vendorId.size();
            found = true;
        }
        if (found)
            break;
    }

    if (found)
    {
        size_t pos = 0;
        while ((pos = deviceIdStr.find("DEV_", pos)) != std::string::npos)
        {
            deviceIdStr.replace(pos, 8, deviceId);
            pos += deviceId.size();
        }
        strncpy_s(lpDisplayDevice->DeviceID, deviceIdStr.c_str(), sizeof(lpDisplayDevice->DeviceID) - 1);
        LOG_INFO("Spoofed DeviceID: {}", deviceIdStr);

        auto gpuName = wstring_to_string(Config::Instance()->SpoofedGPUName.value_or_default());
        strncpy_s(lpDisplayDevice->DeviceString, gpuName.c_str(), sizeof(lpDisplayDevice->DeviceString) - 1);
    }

    return result;
}

inline static BOOL WINAPI hkEnumDisplayDevicesW(PWSTR lpDevice, uint32_t iDevNum, DISPLAY_DEVICEW* lpDisplayDevice,
                                                uint32_t dwFlags)
{
    auto result = o_EnumDisplayDevicesW(lpDevice, iDevNum, lpDisplayDevice, dwFlags);

    // Monitors are enumerated with a device name
    if (!result || lpDisplayDevice == nullptr || lpDevice != nullptr || lpDisplayDevice->cb < sizeof(DISPLAY_DEVICEW))
        return result;

    static std::wstring vendorId = std::format(L"VEN_{:04X}", Config::Instance()->SpoofedVendorId.value_or_default());
    static std::wstring deviceId = std::format(L"DEV_{:04X}", Config::Instance()->SpoofedDeviceId.value_or_default());

    // Spoof VEN_/DEV_ tokens in DeviceID (e.g. L"PCI\VEN_1002&DEV_687F&...")
    std::wstring deviceIdStr(lpDisplayDevice->DeviceID);
    bool found = false;

    static const wchar_t* srcVendors[] = { L"VEN_1002", L"VEN_8086" };
    for (auto srcVendor : srcVendors)
    {
        size_t pos = 0;
        while ((pos = deviceIdStr.find(srcVendor, pos)) != std::wstring::npos)
        {
            deviceIdStr.replace(pos, 8, vendorId);
            pos += vendorId.size();
            found = true;
        }
        if (found)
            break;
    }

    if (found)
    {
        size_t pos = 0;
        while ((pos = deviceIdStr.find(L"DEV_", pos)) != std::wstring::npos)
        {
            deviceIdStr.replace(pos, 8, deviceId);
            pos += deviceId.size();
        }
        wcsncpy_s(lpDisplayDevice->DeviceID, deviceIdStr.c_str(),
                  sizeof(lpDisplayDevice->DeviceID) / sizeof(wchar_t) - 1);
        LOG_INFO("Spoofed DeviceID: {}", wstring_to_string(deviceIdStr));

        auto gpuName = Config::Instance()->SpoofedGPUName.value_or_default();
        wcsncpy_s(lpDisplayDevice->DeviceString, gpuName.c_str(),
                  sizeof(lpDisplayDevice->DeviceString) / sizeof(wchar_t) - 1);
    }

    return result;
}

void User32Spoofing::Hook()
{
    LOG_FUNC();

    o_EnumDisplayDevicesA =
        reinterpret_cast<PFN_EnumDisplayDevicesA>(DetourFindFunction("User32.dll", "EnumDisplayDevicesA"));
    o_EnumDisplayDevicesW =
        reinterpret_cast<PFN_EnumDisplayDevicesW>(DetourFindFunction("User32.dll", "EnumDisplayDevicesW"));

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_EnumDisplayDevicesA)
        DetourAttach(&(PVOID&) o_EnumDisplayDevicesA, hkEnumDisplayDevicesA);

    if (o_EnumDisplayDevicesW)
        DetourAttach(&(PVOID&) o_EnumDisplayDevicesW, hkEnumDisplayDevicesW);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to install User32 spoofing hooks: {}", detourResult);
        o_EnumDisplayDevicesA = nullptr;
        o_EnumDisplayDevicesW = nullptr;
    }
}

void User32Spoofing::Unhook()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_EnumDisplayDevicesA)
        DetourDetach(&(PVOID&) o_EnumDisplayDevicesA, hkEnumDisplayDevicesA);

    if (o_EnumDisplayDevicesW)
        DetourDetach(&(PVOID&) o_EnumDisplayDevicesW, hkEnumDisplayDevicesW);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to uninstall User32 spoofing hooks: {}", detourResult);
    }
    else
    {
        o_EnumDisplayDevicesA = nullptr;
        o_EnumDisplayDevicesW = nullptr;
    }
}
