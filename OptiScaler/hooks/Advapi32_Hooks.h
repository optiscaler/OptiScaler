#pragma once
#include "SysUtils.h"
#include "Config.h"
#include "detours/detours.h"

#include "Hook_Utils.h"

const HKEY signatureMark = (HKEY) 0xFFFFFFFF13372137;

typedef decltype(&RegOpenKeyExW) PFN_RegOpenKeyExW;
typedef decltype(&RegEnumValueW) PFN_RegEnumValueW;
typedef decltype(&RegCloseKey) PFN_RegCloseKey;
typedef decltype(&RegQueryValueExW) PFN_RegQueryValueExW;
typedef decltype(&RegQueryValueExA) PFN_RegQueryValueExA;

static PFN_RegOpenKeyExW o_RegOpenKeyExW = nullptr;
static PFN_RegEnumValueW o_RegEnumValueW = nullptr;
static PFN_RegCloseKey o_RegCloseKey = nullptr;
static PFN_RegQueryValueExW o_RegQueryValueExW = nullptr;
static PFN_RegQueryValueExA o_RegQueryValueExA = nullptr;

VALIDATE_HOOK(hkRegOpenKeyExW, PFN_RegOpenKeyExW)
static LSTATUS hkRegOpenKeyExW(HKEY hKey, LPCWSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult)
{
    if (lpSubKey != nullptr && phkResult != nullptr &&
        (_wcsicmp(L"SOFTWARE\\NVIDIA Corporation\\Global", lpSubKey) == 0 ||
         _wcsicmp(L"SYSTEM\\ControlSet001\\Services\\nvlddmkm", lpSubKey) == 0))
    {
        *phkResult = signatureMark;
        return 0;
    }

    return o_RegOpenKeyExW(hKey, lpSubKey, ulOptions, samDesired, phkResult);
}

VALIDATE_HOOK(hkRegEnumValueW, PFN_RegEnumValueW)
static LSTATUS hkRegEnumValueW(HKEY hKey, DWORD dwIndex, LPWSTR lpValueName, LPDWORD lpcchValueName, LPDWORD lpReserved,
                               LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
{
    if (hKey != signatureMark)
        return o_RegEnumValueW(hKey, dwIndex, lpValueName, lpcchValueName, lpReserved, lpType, lpData, lpcbData);

    if (dwIndex == 0)
    {
        if (lpValueName && lpcchValueName && lpData && lpcbData)
        {
            auto key = L"{41FCC608-8496-4DEF-B43E-7D9BD675A6FF}";
            auto value = 0x01;

            auto keyLength = (DWORD) wcslen(key);

            if (*lpcchValueName <= keyLength)
                return ERROR_MORE_DATA;

            wcsncpy(lpValueName, key, *lpcchValueName);
            lpValueName[*lpcchValueName - 1] = L'\0';
            *lpcchValueName = keyLength;

            if (lpType)
                *lpType = REG_BINARY;

            if (*lpcbData > 0)
            {
                lpData[0] = value;
                *lpcbData = 1;
            }
            else
            {
                return ERROR_MORE_DATA;
            }

            return ERROR_SUCCESS;
        }

        return ERROR_INVALID_PARAMETER;
    }
    else
    {
        return ERROR_NO_MORE_ITEMS;
    }
}

VALIDATE_HOOK(hkRegCloseKey, PFN_RegCloseKey)
static LSTATUS hkRegCloseKey(HKEY hKey)
{
    if (hKey == signatureMark)
        return ERROR_SUCCESS;

    return o_RegCloseKey(hKey);
}

static std::wstring GetSpoofedProviderNameW()
{
    auto vendorId = Config::Instance()->SpoofedVendorId.value_or_default();
    if (vendorId == VendorId::Nvidia)
        return L"NVIDIA";
    else if (vendorId == VendorId::AMD)
        return L"Advanced Micro Devices, Inc.";
    else if (vendorId == VendorId::Intel)
        return L"Intel Corporation";

    return L"NVIDIA";
}

static std::string GetSpoofedProviderNameA()
{
    auto vendorId = Config::Instance()->SpoofedVendorId.value_or_default();
    if (vendorId == VendorId::Nvidia)
        return "NVIDIA";
    else if (vendorId == VendorId::AMD)
        return "Advanced Micro Devices, Inc.";
    else if (vendorId == VendorId::Intel)
        return "Intel Corporation";

    return "NVIDIA";
}

static LSTATUS SpoofRegSzW(LPBYTE lpData, LPDWORD lpcbData, LPDWORD lpType, const std::wstring& spoofedValue,
                           const char* logKey)
{
    size_t spoofedValueSize = (spoofedValue.size() + 1) * sizeof(wchar_t);

    if (lpcbData == nullptr)
        return ERROR_SUCCESS;

    if (lpData != nullptr && *lpcbData >= spoofedValueSize)
    {
        std::memcpy(lpData, spoofedValue.c_str(), spoofedValueSize);
        *lpcbData = static_cast<DWORD>(spoofedValueSize);

        if (lpType)
            *lpType = REG_SZ;

        LOG_INFO("New {}: {}", logKey, wstring_to_string(spoofedValue));
        return ERROR_SUCCESS;
    }
    else
    {
        *lpcbData = std::max(static_cast<DWORD>(spoofedValueSize), *lpcbData);
    }

    if (lpData != nullptr)
        return ERROR_MORE_DATA;
    else
        return ERROR_SUCCESS;
}

static LSTATUS SpoofRegSzA(LPBYTE lpData, LPDWORD lpcbData, LPDWORD lpType, const std::string& spoofedValue,
                           const char* logKey)
{
    size_t spoofedValueSize = (spoofedValue.size() + 1) * sizeof(char);

    if (lpcbData == nullptr)
        return ERROR_SUCCESS;

    if (lpData != nullptr && *lpcbData >= spoofedValueSize)
    {
        std::memcpy(lpData, spoofedValue.c_str(), spoofedValueSize);
        *lpcbData = static_cast<DWORD>(spoofedValueSize);

        if (lpType)
            *lpType = REG_SZ;

        LOG_INFO("New {}: {}", logKey, spoofedValue);
        return ERROR_SUCCESS;
    }
    else
    {
        *lpcbData = std::max(static_cast<DWORD>(spoofedValueSize), *lpcbData);
    }

    if (lpData != nullptr)
        return ERROR_MORE_DATA;
    else
        return ERROR_SUCCESS;
}

// Replace vendor/device tokens in a single wide string segment.
// Handles VEN_XXXX, DEV_XXXX and SUBSYS_XXXXyyyy (vendor portion of subsystem).
static std::wstring ReplaceVendorDeviceTokensW(const std::wstring& input, const std::wstring& spoofedVendorId,
                                               const std::wstring& spoofedDeviceId)
{
    std::wstring result = input;
    size_t pos = 0;

    // Replace VEN_1002
    pos = 0;
    while ((pos = result.find(L"VEN_1002", pos)) != std::wstring::npos)
    {
        result.replace(pos, 8, spoofedVendorId);
        pos += spoofedVendorId.size();
    }

    // Replace VEN_8086
    pos = 0;
    while ((pos = result.find(L"VEN_8086", pos)) != std::wstring::npos)
    {
        result.replace(pos, 8, spoofedVendorId);
        pos += spoofedVendorId.size();
    }

    // Replace DEV_XXXX (4 hex digits = 8 chars total)
    pos = 0;
    while ((pos = result.find(L"DEV_", pos)) != std::wstring::npos)
    {
        if (pos + 8 <= result.size())
        {
            result.replace(pos, 8, spoofedDeviceId);
            pos += spoofedDeviceId.size();
        }
        else
        {
            break;
        }
    }

    return result;
}

// Replace vendor/device tokens in a single narrow string segment.
static std::string ReplaceVendorDeviceTokensA(const std::string& input, const std::string& spoofedVendorId,
                                              const std::string& spoofedDeviceId)
{
    std::string result = input;
    size_t pos = 0;

    pos = 0;
    while ((pos = result.find("VEN_1002", pos)) != std::string::npos)
    {
        result.replace(pos, 8, spoofedVendorId);
        pos += spoofedVendorId.size();
    }

    pos = 0;
    while ((pos = result.find("VEN_8086", pos)) != std::string::npos)
    {
        result.replace(pos, 8, spoofedVendorId);
        pos += spoofedVendorId.size();
    }

    pos = 0;
    while ((pos = result.find("DEV_", pos)) != std::string::npos)
    {
        if (pos + 8 <= result.size())
        {
            result.replace(pos, 8, spoofedDeviceId);
            pos += spoofedDeviceId.size();
        }
        else
        {
            break;
        }
    }

    return result;
}

// Spoof a REG_MULTI_SZ buffer in-place by walking each null-terminated wide string
// segment and replacing vendor/device tokens. Writes back to lpData, the data size doesn't change.
static void SpoofMultiSzW(LPBYTE lpData, LPDWORD lpcbData, const std::wstring& spoofedVendorId,
                          const std::wstring& spoofedDeviceId, const char* logKey)
{
    if (lpData == nullptr || lpcbData == nullptr || *lpcbData < sizeof(wchar_t))
        return;

    const DWORD bufferChars = *lpcbData / sizeof(wchar_t);
    const wchar_t* src = reinterpret_cast<const wchar_t*>(lpData);

    // Collect all segments
    std::vector<std::wstring> segments;
    DWORD offset = 0;
    while (offset < bufferChars)
    {
        if (src[offset] == L'\0')
        {
            offset++;
            break; // double-null terminator
        }

        std::wstring segment(src + offset, wcsnlen(src + offset, bufferChars - offset));
        auto replaced = ReplaceVendorDeviceTokensW(segment, spoofedVendorId, spoofedDeviceId);
        segments.push_back(replaced.size() == segment.size() ? replaced : segment);
        offset += static_cast<DWORD>(segment.size()) + 1;
    }

    if (segments.empty())
        return;

    // Tokens keep their length, entries are rewritten in place and keep their terminators
    wchar_t* dst = reinterpret_cast<wchar_t*>(lpData);
    DWORD written = 0;
    for (const auto& seg : segments)
    {
        if (written + seg.size() > bufferChars)
            break;

        std::wmemcpy(dst + written, seg.c_str(), seg.size());
        written += static_cast<DWORD>(seg.size()) + 1;
    }

    LOG_INFO("New {}: {}", logKey, wstring_to_string(segments[0]));
}

// Spoof a REG_MULTI_SZ buffer in-place for the narrow (A) variant.
static void SpoofMultiSzA(LPBYTE lpData, LPDWORD lpcbData, const std::string& spoofedVendorId,
                          const std::string& spoofedDeviceId, const char* logKey)
{
    if (lpData == nullptr || lpcbData == nullptr || *lpcbData == 0)
        return;

    const DWORD bufferBytes = *lpcbData;
    const char* src = reinterpret_cast<const char*>(lpData);

    std::vector<std::string> segments;
    DWORD offset = 0;
    while (offset < bufferBytes)
    {
        if (src[offset] == '\0')
        {
            offset++;
            break;
        }

        std::string segment(src + offset, strnlen(src + offset, bufferBytes - offset));
        auto replaced = ReplaceVendorDeviceTokensA(segment, spoofedVendorId, spoofedDeviceId);
        segments.push_back(replaced.size() == segment.size() ? replaced : segment);
        offset += static_cast<DWORD>(segment.size()) + 1;
    }

    if (segments.empty())
        return;

    char* dst = reinterpret_cast<char*>(lpData);
    DWORD written = 0;
    for (const auto& seg : segments)
    {
        if (written + seg.size() > bufferBytes)
            break;

        std::memcpy(dst + written, seg.c_str(), seg.size());
        written += static_cast<DWORD>(seg.size()) + 1;
    }

    LOG_INFO("New {}: {}", logKey, segments[0]);
}

// Original implementation:
// https://github.com/artur-graniszewski/dlss-enabler-main/blob/1f8b24722f1b526ffb896ae62b6aa3ca766b0728/Utils/RegistryProxy.cpp#L137
VALIDATE_HOOK(hkRegQueryValueExW, PFN_RegQueryValueExW)
static LONG hkRegQueryValueExW(HKEY hKey, LPCWSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData,
                               LPDWORD lpcbData)
{
    static std::wstring vendorId = std::format(L"VEN_{:04X}", Config::Instance()->SpoofedVendorId.value_or_default());
    static std::wstring deviceId = std::format(L"DEV_{:04X}", Config::Instance()->SpoofedDeviceId.value_or_default());
    std::wstring valueName = L"";

    if (lpValueName != NULL)
        valueName = std::wstring(lpValueName);

    if (Config::Instance()->SpoofHAGS.value_or_default() && valueName == L"HwSchMode")
    {
        if (lpcbData != nullptr)
        {
            if (lpData == nullptr)
            {
                *lpcbData = sizeof(DWORD);
                if (lpType)
                    *lpType = REG_DWORD;
                return ERROR_SUCCESS;
            }

            if (*lpcbData >= sizeof(DWORD))
            {
                *(DWORD*) lpData = 2;
                if (lpType)
                    *lpType = REG_DWORD;
                *lpcbData = sizeof(DWORD);
                return ERROR_SUCCESS;
            }
            else
            {
                *lpcbData = sizeof(DWORD);
                return ERROR_MORE_DATA;
            }
        }

        return ERROR_INVALID_PARAMETER;
    }

    // Store the buffer size that the game is providing and restore it in spoofs
    // because the real query will change the size reported to the unspoofed buffer's size
    DWORD oldCbData {};
    if (lpData && lpcbData)
        oldCbData = *lpcbData;

    DWORD localType = REG_NONE;
    if (lpType == nullptr)
        lpType = &localType;

    auto result = o_RegQueryValueExW(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);

    if (result == ERROR_SUCCESS && Config::Instance()->SpoofRegistry.value_or_default())
    {
        if (valueName == L"DriverVersion")
        {
            if (lpData && lpcbData)
                *lpcbData = oldCbData;

            const std::wstring spoofedValue = Config::Instance()->SpoofedDriver.value_or_default();
            auto spoofResult = SpoofRegSzW(lpData, lpcbData, lpType, spoofedValue, "DriverVersion");
            if (spoofResult != ERROR_SUCCESS)
                result = spoofResult;
        }

        if (valueName == L"DriverDesc")
        {
            if (lpData && lpcbData)
                *lpcbData = oldCbData;

            const std::wstring spoofedValue = Config::Instance()->SpoofedGPUName.value_or_default();
            auto spoofResult = SpoofRegSzW(lpData, lpcbData, lpType, spoofedValue, "DriverDesc");
            if (spoofResult != ERROR_SUCCESS)
                result = spoofResult;
        }

        if (valueName == L"ProviderName")
        {
            if (lpData && lpcbData)
                *lpcbData = oldCbData;

            const std::wstring spoofedValue = GetSpoofedProviderNameW();
            auto spoofResult = SpoofRegSzW(lpData, lpcbData, lpType, spoofedValue, "ProviderName");
            if (spoofResult != ERROR_SUCCESS)
                result = spoofResult;
        }

        if (valueName == L"HardwareInformation.AdapterString")
        {
            if (lpData && lpcbData)
                *lpcbData = oldCbData;

            const std::wstring spoofedValue = Config::Instance()->SpoofedGPUName.value_or_default();
            auto spoofResult = SpoofRegSzW(lpData, lpcbData, lpType, spoofedValue, "HardwareInformation.AdapterString");
            if (spoofResult != ERROR_SUCCESS)
                result = spoofResult;
        }

        if ((valueName == L"HardwareID" || valueName == L"MatchingDeviceId") && lpData != nullptr &&
            lpcbData != nullptr)
        {
            DWORD regType = *lpType;

            if (regType == REG_MULTI_SZ)
            {
                SpoofMultiSzW(lpData, lpcbData, vendorId, deviceId,
                              valueName == L"HardwareID" ? "HardwareID" : "MatchingDeviceId");
            }
            else
            {
                // REG_SZ fallback — single string, replace in-place
                auto str = reinterpret_cast<wchar_t*>(lpData);
                std::wstring data(str, wcsnlen(str, *lpcbData / sizeof(wchar_t)));
                std::wstring newData = ReplaceVendorDeviceTokensW(data, vendorId, deviceId);

                if (newData != data)
                {
                    if (newData.size() == data.size())
                    {
                        std::memcpy(lpData, newData.c_str(), newData.size() * sizeof(wchar_t));
                        LOG_INFO("New {}: {}", valueName == L"HardwareID" ? "HardwareID" : "MatchingDeviceId",
                                 wstring_to_string(newData));
                    }
                }
            }
        }

        // Intercept \Device\VideoN values from HARDWARE\DEVICEMAP\VIDEO
        // These return the full PCI registry path (e.g. \REGISTRY\Machine\SYSTEM\...\VEN_1002&DEV_...)
        // which games use to directly open the adapter's Enum\PCI key.
        if (lpData != nullptr && lpcbData != nullptr && *lpcbData >= sizeof(wchar_t) && valueName.size() >= 13 &&
            _wcsnicmp(valueName.c_str(), L"\\Device\\Video", 13) == 0)
        {
            DWORD regType = *lpType;
            if (regType == REG_SZ || regType == REG_EXPAND_SZ)
            {
                auto str = reinterpret_cast<wchar_t*>(lpData);
                std::wstring path(str, wcsnlen(str, *lpcbData / sizeof(wchar_t)));
                std::wstring newPath = ReplaceVendorDeviceTokensW(path, vendorId, deviceId);
                if (newPath != path)
                {
                    if (newPath.size() == path.size())
                    {
                        std::memcpy(lpData, newPath.c_str(), newPath.size() * sizeof(wchar_t));
                        LOG_INFO("New VideoDeviceMap path: {}", wstring_to_string(newPath));
                    }
                }
            }
        }
    }

    return result;
}

VALIDATE_HOOK(hkRegQueryValueExA, PFN_RegQueryValueExA)
LONG WINAPI hkRegQueryValueExA(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData,
                               LPDWORD lpcbData)
{
    static std::string vendorId = std::format("VEN_{:04X}", Config::Instance()->SpoofedVendorId.value_or_default());
    static std::string deviceId = std::format("DEV_{:04X}", Config::Instance()->SpoofedDeviceId.value_or_default());
    std::string valueName = "";

    if (lpValueName != NULL)
        valueName = std::string(lpValueName);

    if (Config::Instance()->SpoofHAGS.value_or_default() && valueName == "HwSchMode")
    {
        if (lpcbData != nullptr)
        {
            if (lpData == nullptr)
            {
                *lpcbData = sizeof(DWORD);
                if (lpType)
                    *lpType = REG_DWORD;
                return ERROR_SUCCESS;
            }

            if (*lpcbData >= sizeof(DWORD))
            {
                *(DWORD*) lpData = 2;
                if (lpType)
                    *lpType = REG_DWORD;
                *lpcbData = sizeof(DWORD);
                return ERROR_SUCCESS;
            }
            else
            {
                *lpcbData = sizeof(DWORD);
                return ERROR_MORE_DATA;
            }
        }

        return ERROR_INVALID_PARAMETER;
    }

    // Store the buffer size that the game is providing and restore it in spoofs
    // because the real query will change the size reported to the unspoofed buffer's size
    DWORD oldCbData {};
    if (lpData && lpcbData)
        oldCbData = *lpcbData;

    DWORD localType = REG_NONE;
    if (lpType == nullptr)
        lpType = &localType;

    auto result = o_RegQueryValueExA(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);

    if (result == ERROR_SUCCESS && Config::Instance()->SpoofRegistry.value_or_default())
    {
        if (valueName == "DriverVersion")
        {
            if (lpData && lpcbData)
                *lpcbData = oldCbData;

            const std::string spoofedValue = wstring_to_string(Config::Instance()->SpoofedDriver.value_or_default());
            auto spoofResult = SpoofRegSzA(lpData, lpcbData, lpType, spoofedValue, "DriverVersion");
            if (spoofResult != ERROR_SUCCESS)
                result = spoofResult;
        }

        if (valueName == "DriverDesc")
        {
            if (lpData && lpcbData)
                *lpcbData = oldCbData;

            const std::string spoofedValue = wstring_to_string(Config::Instance()->SpoofedGPUName.value_or_default());
            auto spoofResult = SpoofRegSzA(lpData, lpcbData, lpType, spoofedValue, "DriverDesc");
            if (spoofResult != ERROR_SUCCESS)
                result = spoofResult;
        }

        if (valueName == "ProviderName")
        {
            if (lpData && lpcbData)
                *lpcbData = oldCbData;

            const std::string spoofedValue = GetSpoofedProviderNameA();
            auto spoofResult = SpoofRegSzA(lpData, lpcbData, lpType, spoofedValue, "ProviderName");
            if (spoofResult != ERROR_SUCCESS)
                result = spoofResult;
        }

        if (valueName == "HardwareInformation.AdapterString")
        {
            if (lpData && lpcbData)
                *lpcbData = oldCbData;

            const std::string spoofedValue = wstring_to_string(Config::Instance()->SpoofedGPUName.value_or_default());
            auto spoofResult = SpoofRegSzA(lpData, lpcbData, lpType, spoofedValue, "HardwareInformation.AdapterString");
            if (spoofResult != ERROR_SUCCESS)
                result = spoofResult;
        }

        if ((valueName == "HardwareID" || valueName == "MatchingDeviceId") && lpData != nullptr && lpcbData != nullptr)
        {
            DWORD regType = *lpType;

            if (regType == REG_MULTI_SZ)
            {
                SpoofMultiSzA(lpData, lpcbData, vendorId, deviceId,
                              valueName == "HardwareID" ? "HardwareID" : "MatchingDeviceId");
            }
            else
            {
                // REG_SZ fallback
                auto str = reinterpret_cast<char*>(lpData);
                std::string data(str, strnlen(str, *lpcbData));
                std::string newData = ReplaceVendorDeviceTokensA(data, vendorId, deviceId);

                if (newData != data)
                {
                    if (newData.size() == data.size())
                    {
                        std::memcpy(lpData, newData.c_str(), newData.size());
                        LOG_INFO("New {}: {}", valueName == "HardwareID" ? "HardwareID" : "MatchingDeviceId", newData);
                    }
                }
            }
        }

        // Intercept \Device\VideoN values from HARDWARE\DEVICEMAP\VIDEO
        // These return the full PCI registry path (e.g. \REGISTRY\Machine\SYSTEM\...\VEN_1002&DEV_...)
        // which games use to directly open the adapter's Enum\PCI key.
        if (lpData != nullptr && lpcbData != nullptr && *lpcbData >= sizeof(char) && valueName.size() >= 13 &&
            _strnicmp(valueName.c_str(), "\\Device\\Video", 13) == 0)
        {
            DWORD regType = *lpType;
            if (regType == REG_SZ || regType == REG_EXPAND_SZ)
            {
                auto str = reinterpret_cast<char*>(lpData);
                std::string path(str, strnlen(str, *lpcbData));
                std::string newPath = ReplaceVendorDeviceTokensA(path, vendorId, deviceId);
                if (newPath != path)
                {
                    if (newPath.size() == path.size())
                    {
                        std::memcpy(lpData, newPath.c_str(), newPath.size());
                        LOG_INFO("New VideoDeviceMap path: {}", newPath);
                    }
                }
            }
        }
    }

    return result;
}

static void hookAdvapi32()
{
    LOG_FUNC();

    o_RegOpenKeyExW = reinterpret_cast<PFN_RegOpenKeyExW>(DetourFindFunction("Advapi32.dll", "RegOpenKeyExW"));
    o_RegEnumValueW = reinterpret_cast<PFN_RegEnumValueW>(DetourFindFunction("Advapi32.dll", "RegEnumValueW"));
    o_RegCloseKey = reinterpret_cast<PFN_RegCloseKey>(DetourFindFunction("Advapi32.dll", "RegCloseKey"));

    if (Config::Instance()->SpoofHAGS.value_or_default() || Config::Instance()->SpoofRegistry.value_or_default())
    {
        o_RegQueryValueExW =
            reinterpret_cast<PFN_RegQueryValueExW>(DetourFindFunction("Advapi32.dll", "RegQueryValueExW"));
        o_RegQueryValueExA =
            reinterpret_cast<PFN_RegQueryValueExA>(DetourFindFunction("Advapi32.dll", "RegQueryValueExA"));
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_RegOpenKeyExW)
        DetourAttach(&(PVOID&) o_RegOpenKeyExW, hkRegOpenKeyExW);

    if (o_RegEnumValueW)
        DetourAttach(&(PVOID&) o_RegEnumValueW, hkRegEnumValueW);

    if (o_RegCloseKey)
        DetourAttach(&(PVOID&) o_RegCloseKey, hkRegCloseKey);

    if (o_RegQueryValueExW)
        DetourAttach(&(PVOID&) o_RegQueryValueExW, hkRegQueryValueExW);

    if (o_RegQueryValueExA)
        DetourAttach(&(PVOID&) o_RegQueryValueExA, hkRegQueryValueExA);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("DetourTransactionCommit error: {:X}", detourResult);
        o_RegOpenKeyExW = nullptr;
        o_RegEnumValueW = nullptr;
        o_RegCloseKey = nullptr;
        o_RegQueryValueExW = nullptr;
        o_RegQueryValueExA = nullptr;
    }
}

static void unhookAdvapi32()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_RegOpenKeyExW)
        DetourDetach(&(PVOID&) o_RegOpenKeyExW, hkRegOpenKeyExW);

    if (o_RegEnumValueW)
        DetourDetach(&(PVOID&) o_RegEnumValueW, hkRegEnumValueW);

    if (o_RegCloseKey)
        DetourDetach(&(PVOID&) o_RegCloseKey, hkRegCloseKey);

    if (o_RegQueryValueExW)
        DetourDetach(&(PVOID&) o_RegQueryValueExW, hkRegQueryValueExW);

    if (o_RegQueryValueExA)
        DetourDetach(&(PVOID&) o_RegQueryValueExA, hkRegQueryValueExA);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("DetourTransactionCommit error: {:X}", detourResult);
    }
    else
    {
        o_RegCloseKey = nullptr;
        o_RegEnumValueW = nullptr;
        o_RegOpenKeyExW = nullptr;
        o_RegQueryValueExA = nullptr;
        o_RegQueryValueExW = nullptr;
    }
}
