#pragma once

#include "pch.h"

#include "Config.h"

#include "detours/detours.h"
#include <wincrypt.h>

typedef BOOL (*PFN_CryptQueryObject)(DWORD dwObjectType, const void* pvObject, DWORD dwExpectedContentTypeFlags,
                                     DWORD dwExpectedFormatTypeFlags, DWORD dwFlags, DWORD* pdwMsgAndCertEncodingType,
                                     DWORD* pdwContentType, DWORD* pdwFormatType, HCERTSTORE* phCertStore,
                                     HCRYPTMSG* phMsg, const void** ppvContext);
static PFN_CryptQueryObject o_CryptQueryObject = nullptr;

static BOOL hkCryptQueryObject(DWORD dwObjectType, const void* pvObject, DWORD dwExpectedContentTypeFlags,
                               DWORD dwExpectedFormatTypeFlags, DWORD dwFlags, DWORD* pdwMsgAndCertEncodingType,
                               DWORD* pdwContentType, DWORD* pdwFormatType, HCERTSTORE* phCertStore, HCRYPTMSG* phMsg,
                               const void** ppvContext)
{
    if (dwObjectType == CERT_QUERY_OBJECT_FILE && pvObject)
    {
        std::wstring originalPath((WCHAR*) pvObject);
        auto pathString = wstring_to_string(std::wstring(originalPath));
        to_lower_in_place(pathString);

        // It's applied even if ffx is already signed, could be improved
        if (pathString.contains("amd_fidelityfx_dx12.dll") ||
            pathString.contains("amd_fidelityfx_vk.dll") && fsr4Module)
        {
            LOG_DEBUG("Replacing FFX with a signed dll");
            WCHAR signedDll[256] {};
            GetModuleFileNameW(fsr4Module, signedDll, 256);

            return o_CryptQueryObject(dwObjectType, signedDll, dwExpectedContentTypeFlags, dwExpectedFormatTypeFlags,
                                      dwFlags, pdwMsgAndCertEncodingType, pdwContentType, pdwFormatType, phCertStore,
                                      phMsg, ppvContext);
        }

        if (pathString.contains("nvngx.dll") && !State::Instance().nvngxExists &&
            State::Instance().nvngxReplacement.has_value() &&
            (Config::Instance()->DxgiSpoofing.value_or_default() ||
             Config::Instance()->StreamlineSpoofing.value_or_default()))
        {
            LOG_DEBUG("Replacing nvngx with a signed dll");
            return o_CryptQueryObject(dwObjectType, State::Instance().nvngxReplacement.value().c_str(),
                                      dwExpectedContentTypeFlags, dwExpectedFormatTypeFlags, dwFlags,
                                      pdwMsgAndCertEncodingType, pdwContentType, pdwFormatType, phCertStore, phMsg,
                                      ppvContext);
        }
    }

    return o_CryptQueryObject(dwObjectType, pvObject, dwExpectedContentTypeFlags, dwExpectedFormatTypeFlags, dwFlags,
                              pdwMsgAndCertEncodingType, pdwContentType, pdwFormatType, phCertStore, phMsg, ppvContext);
}

static void hookCrypt32()
{
    LOG_FUNC();

    o_CryptQueryObject = reinterpret_cast<PFN_CryptQueryObject>(DetourFindFunction("crypt32.dll", "CryptQueryObject"));

    if (o_CryptQueryObject != nullptr)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_CryptQueryObject, hkCryptQueryObject);

        DetourTransactionCommit();
    }
}

static void unhookCrypt32()
{
    if (o_CryptQueryObject != nullptr)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourDetach(&(PVOID&) o_CryptQueryObject, hkCryptQueryObject);
        o_CryptQueryObject = nullptr;

        DetourTransactionCommit();
    }
}
