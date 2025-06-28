#pragma once

#include "pch.h"

#include "Config.h"

#include "detours/detours.h"

const HKEY signatureMark = (HKEY) 0xFFFFFFFF13372137;

typedef LSTATUS (*PFN_RegOpenKeyExW)(HKEY hKey, LPCWSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult);
typedef LSTATUS (*PFN_RegEnumValueW)(HKEY hKey, DWORD dwIndex, LPWSTR lpValueName, LPDWORD lpcchValueName,
                                     LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
typedef LSTATUS (*PFN_RegCloseKey)(HKEY hKey);

static PFN_RegOpenKeyExW o_RegOpenKeyExW = nullptr;
static PFN_RegEnumValueW o_RegEnumValueW = nullptr;
static PFN_RegCloseKey o_RegCloseKey = nullptr;

static LSTATUS hkRegOpenKeyExW(HKEY hKey, LPCWSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult)
{
    LSTATUS result = 0;

    if (lpSubKey != nullptr && wcscmp(L"SOFTWARE\\NVIDIA Corporation\\Global", lpSubKey) == 0)
    {
        *phkResult = signatureMark;
        return 0;
    }

    return o_RegOpenKeyExW(hKey, lpSubKey, ulOptions, samDesired, phkResult);
}

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

static LSTATUS hkRegCloseKey(HKEY hKey)
{
    if (hKey == signatureMark)
        return ERROR_SUCCESS;

    return o_RegCloseKey(hKey);
}

// Replaces all values returned for SOFTWARE\\NVIDIA Corporation\\Global so could potentially cause issues on Nvidia
static void hookAdvapi32()
{
    LOG_FUNC();

    o_RegOpenKeyExW = reinterpret_cast<PFN_RegOpenKeyExW>(DetourFindFunction("Advapi32.dll", "RegOpenKeyExW"));
    o_RegEnumValueW = reinterpret_cast<PFN_RegEnumValueW>(DetourFindFunction("Advapi32.dll", "RegEnumValueW"));
    o_RegCloseKey = reinterpret_cast<PFN_RegCloseKey>(DetourFindFunction("Advapi32.dll", "RegCloseKey"));

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_RegOpenKeyExW)
        DetourAttach(&(PVOID&) o_RegOpenKeyExW, hkRegOpenKeyExW);

    if (o_RegEnumValueW)
        DetourAttach(&(PVOID&) o_RegEnumValueW, hkRegEnumValueW);

    if (o_RegCloseKey)
        DetourAttach(&(PVOID&) o_RegCloseKey, hkRegCloseKey);

    DetourTransactionCommit();
}

static void unhookAdvapi32()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_RegOpenKeyExW)
    {
        DetourDetach(&(PVOID&) o_RegOpenKeyExW, hkRegOpenKeyExW);
        o_RegOpenKeyExW = nullptr;
    }

    if (o_RegEnumValueW)
    {
        DetourDetach(&(PVOID&) o_RegEnumValueW, hkRegEnumValueW);
        o_RegEnumValueW = nullptr;
    }

    if (o_RegCloseKey)
    {
        DetourDetach(&(PVOID&) o_RegCloseKey, hkRegCloseKey);
        o_RegCloseKey = nullptr;
    }

    DetourTransactionCommit();
}
