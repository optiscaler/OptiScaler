#pragma once
#include "SysUtils.h"
#include "Config.h"

#include "detours/detours.h"
#include <WinTrust.h>
#include <Softpub.h>

#include "Hook_Utils.h"

typedef decltype(&WinVerifyTrust) PFN_WinVerifyTrust;
static PFN_WinVerifyTrust o_WinVerifyTrust = nullptr;

VALIDATE_HOOK(hkWinVerifyTrust, PFN_WinVerifyTrust)
static LONG hkWinVerifyTrust(HWND hwnd, GUID* pgActionID, LPVOID pWVTData)
{
    if (!pWVTData || !IsEqualGUID(*pgActionID, WINTRUST_ACTION_GENERIC_VERIFY_V2))
        return o_WinVerifyTrust(hwnd, pgActionID, pWVTData);

    const auto data = reinterpret_cast<WINTRUST_DATA*>(pWVTData);

    if (!data->pFile || !data->pFile->pcwszFilePath)
        return o_WinVerifyTrust(hwnd, pgActionID, pWVTData);

    auto path = wstring_to_string(std::wstring(data->pFile->pcwszFilePath));
    to_lower_in_place(path);

    if (path.contains("amd_fidelityfx_dx12.dll") || path.contains("amd_fidelityfx_upscaler_vk.dll") ||
        path.contains("amd_fidelityfx_vk.dll"))
    {
        return ERROR_SUCCESS;
    }

    // This generally isn't needed but for some reason, when using SpecialK, our hooked CreateFileW doesn't get called
    // and WinVerifyTrust fails as nvngx.dll doesn't exist
    if (path.contains("nvngx.dll") && State::Instance().nvngxReplacement.has_value())
    {
        WINTRUST_DATA newData = *data;
        WINTRUST_FILE_INFO_ newFile = *newData.pFile;
        newData.pFile = &newFile;
        newData.pFile->pcwszFilePath = State::Instance().nvngxReplacement.value().c_str();

        auto result = o_WinVerifyTrust(hwnd, pgActionID, &newData);

        data->hWVTStateData = newData.hWVTStateData;

        return result;
    }

    return o_WinVerifyTrust(hwnd, pgActionID, pWVTData);
}

static void hookWintrust()
{
    LOG_FUNC();

    o_WinVerifyTrust = reinterpret_cast<PFN_WinVerifyTrust>(DetourFindFunction("Wintrust.dll", "WinVerifyTrust"));

    if (o_WinVerifyTrust != nullptr)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_WinVerifyTrust, hkWinVerifyTrust);

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("DetourTransactionCommit error: {:X}", detourResult);
            o_WinVerifyTrust = nullptr;
        }
    }
}

static void unhookWintrust()
{
    if (o_WinVerifyTrust != nullptr)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourDetach(&(PVOID&) o_WinVerifyTrust, hkWinVerifyTrust);

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("DetourTransactionCommit error: {:X}", detourResult);
        }
        else
        {
            o_WinVerifyTrust = nullptr;
        }
    }
}
