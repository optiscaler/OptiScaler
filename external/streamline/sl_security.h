/*
* Copyright (c) 2022-2023 NVIDIA CORPORATION. All rights reserved
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

#pragma once

#define _UNICODE 1
#define UNICODE 1

#include <tchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <Softpub.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <inttypes.h>

#define GetProc(hModule, procName, proc) (((NULL == proc) && (NULL == (*((FARPROC*)&proc) = GetProcAddress(hModule, procName)))) ? FALSE : TRUE)

typedef BOOL(WINAPI* PfnCryptMsgClose)(IN HCRYPTMSG hCryptMsg);
static PfnCryptMsgClose pfnCryptMsgClose = NULL;

typedef BOOL(WINAPI* PfnCertCloseStore)(IN HCERTSTORE hCertStore, DWORD dwFlags);
static PfnCertCloseStore pfnCertCloseStore = NULL;

typedef HCERTSTORE (WINAPI* PfnCertOpenStore)(
    _In_ LPCSTR lpszStoreProvider,
    _In_ DWORD dwEncodingType,
    _In_opt_ HCRYPTPROV_LEGACY hCryptProv,
    _In_ DWORD dwFlags,
    _In_opt_ const void* pvPara
);
static PfnCertOpenStore pfnCertOpenStore = NULL;

typedef BOOL(WINAPI* PfnCertFreeCertificateContext)(IN PCCERT_CONTEXT pCertContext);
static PfnCertFreeCertificateContext pfnCertFreeCertificateContext = NULL;

typedef PCCERT_CONTEXT(WINAPI* PfnCertFindCertificateInStore)(
    IN HCERTSTORE hCertStore,
    IN DWORD dwCertEncodingType,
    IN DWORD dwFindFlags,
    IN DWORD dwFindType,
    IN const void* pvFindPara,
    IN PCCERT_CONTEXT pPrevCertContext
    );
static PfnCertFindCertificateInStore pfnCertFindCertificateInStore = NULL;

typedef BOOL(WINAPI* PfnCryptMsgGetParam)(
    IN HCRYPTMSG hCryptMsg,
    IN DWORD dwParamType,
    IN DWORD dwIndex,
    OUT void* pvData,
    IN OUT DWORD* pcbData
    );
static PfnCryptMsgGetParam pfnCryptMsgGetParam = NULL;

typedef HCRYPTMSG (WINAPI* PfnCryptMsgOpenToDecode)(
    _In_ DWORD dwMsgEncodingType,
    _In_ DWORD dwFlags,
    _In_ DWORD dwMsgType,
    _In_opt_ HCRYPTPROV_LEGACY hCryptProv,
    _Reserved_ PCERT_INFO pRecipientInfo,
    _In_opt_ PCMSG_STREAM_INFO pStreamInfo
);
PfnCryptMsgOpenToDecode pfnCryptMsgOpenToDecode = {};

typedef BOOL (WINAPI* PfnCryptMsgUpdate)(
    _In_ HCRYPTMSG hCryptMsg,
    _In_reads_bytes_opt_(cbData) const BYTE* pbData,
    _In_ DWORD cbData,
    _In_ BOOL fFinal
);
PfnCryptMsgUpdate pfnCryptMsgUpdate = {};

typedef BOOL(WINAPI* PfnCryptQueryObject)(
    DWORD            dwObjectType,
    const void* pvObject,
    DWORD            dwExpectedContentTypeFlags,
    DWORD            dwExpectedFormatTypeFlags,
    DWORD            dwFlags,
    DWORD* pdwMsgAndCertEncodingType,
    DWORD* pdwContentType,
    DWORD* pdwFormatType,
    HCERTSTORE* phCertStore,
    HCRYPTMSG* phMsg,
    const void** ppvContext
    );
static PfnCryptQueryObject pfnCryptQueryObject = NULL;

typedef BOOL(WINAPI* PfnCryptDecodeObjectEx)(
    IN DWORD              dwCertEncodingType,
    IN LPCSTR             lpszStructType,
    IN const BYTE* pbEncoded,
    IN DWORD              cbEncoded,
    IN DWORD              dwFlags,
    IN PCRYPT_DECODE_PARA pDecodePara,
    OUT void* pvStructInfo,
    IN OUT DWORD* pcbStructInfo
    );
static PfnCryptDecodeObjectEx pfnCryptDecodeObjectEx = NULL;

typedef LONG(WINAPI* PfnWinVerifyTrust)(
    IN HWND   hwnd,
    IN GUID* pgActionID,
    IN LPVOID pWVTData
    );
static PfnWinVerifyTrust pfnWinVerifyTrust = NULL;

namespace sl
{

namespace security
{

bool isSignedByNVIDIA(const wchar_t* pathToFile)
{
    bool valid = false;

    // Now let's make sure this is actually signed by NVIDIA

    DWORD dwEncoding, dwContentType, dwFormatType;
    HCERTSTORE hStore = NULL;
    HCRYPTMSG hMsg = NULL;
    PCMSG_SIGNER_INFO pSignerInfo = NULL;
    DWORD dwSignerInfo;
    
    if (!pfnCertOpenStore)
    {
        // We only support Win10+ so we can search for module in system32 directly
        auto hModCrypt32 = LoadLibraryExW(L"crypt32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!hModCrypt32 ||
            !GetProc(hModCrypt32, "CryptMsgClose", pfnCryptMsgClose) ||
            !GetProc(hModCrypt32, "CertOpenStore", pfnCertOpenStore) ||
            !GetProc(hModCrypt32, "CertCloseStore", pfnCertCloseStore) ||
            !GetProc(hModCrypt32, "CertFreeCertificateContext", pfnCertFreeCertificateContext) ||
            !GetProc(hModCrypt32, "CertFindCertificateInStore", pfnCertFindCertificateInStore) ||
            !GetProc(hModCrypt32, "CryptMsgGetParam", pfnCryptMsgGetParam) ||
            !GetProc(hModCrypt32, "CryptMsgUpdate", pfnCryptMsgUpdate) ||
            !GetProc(hModCrypt32, "CryptMsgOpenToDecode", pfnCryptMsgOpenToDecode) ||
            !GetProc(hModCrypt32, "CryptQueryObject", pfnCryptQueryObject) ||
            !GetProc(hModCrypt32, "CryptDecodeObjectEx", pfnCryptDecodeObjectEx))
        {
            return false;
        }
    }

    // Get message handle and store handle from the signed file.
    auto bResult = pfnCryptQueryObject(CERT_QUERY_OBJECT_FILE,
        pathToFile,
        CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
        CERT_QUERY_FORMAT_FLAG_BINARY,
        0,
        &dwEncoding,
        &dwContentType,
        &dwFormatType,
        &hStore,
        &hMsg,
        NULL);
    if (!bResult)
    {
        return false;
    }

    // Get signer information size.
    bResult = pfnCryptMsgGetParam(hMsg,
        CMSG_SIGNER_INFO_PARAM,
        0,
        NULL,
        &dwSignerInfo);
    if (!bResult)
    {
        return false;
    }

    // Allocate memory for signer information.
    pSignerInfo = (PCMSG_SIGNER_INFO)LocalAlloc(LPTR, dwSignerInfo);
    if (!pSignerInfo)
    {
        return false;
    }

    // Get Signer Information.
    bResult = pfnCryptMsgGetParam(hMsg,
        CMSG_SIGNER_INFO_PARAM,
        0,
        (PVOID)pSignerInfo,
        &dwSignerInfo);
    if (!bResult)
    {
        LocalFree(pSignerInfo);
        return false;
    }

    // Look for nested signature
    constexpr const char* kOID_NESTED_SIGNATURE = "1.3.6.1.4.1.311.2.4.1";
    for (DWORD i = 0; i < pSignerInfo->UnauthAttrs.cAttr; i++)
    {
        if (strcmp(kOID_NESTED_SIGNATURE, pSignerInfo->UnauthAttrs.rgAttr[i].pszObjId) == 0)
        {
            HCRYPTMSG hMsg2 = pfnCryptMsgOpenToDecode(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, NULL, NULL, NULL);
            if (hMsg2)
            {
                if (pfnCryptMsgUpdate(hMsg2,pSignerInfo->UnauthAttrs.rgAttr[i].rgValue->pbData,pSignerInfo->UnauthAttrs.rgAttr[i].rgValue->cbData,TRUE))
                {
                    dwSignerInfo = 0;
                    pfnCryptMsgGetParam(hMsg2, CMSG_SIGNER_INFO_PARAM, 0, NULL, &dwSignerInfo);
                    if (dwSignerInfo != 0)
                    {
                        PCMSG_SIGNER_INFO pSignerInfo2 = (PCMSG_SIGNER_INFO)LocalAlloc(LPTR, dwSignerInfo);
                        if (pSignerInfo2)
                        {
                            if (pfnCryptMsgGetParam(hMsg2, CMSG_SIGNER_INFO_PARAM, 0, (PVOID)pSignerInfo2, &dwSignerInfo))
                            {
                                CRYPT_DATA_BLOB c7Data;
                                c7Data.pbData = pSignerInfo->UnauthAttrs.rgAttr[i].rgValue->pbData;
                                c7Data.cbData = pSignerInfo->UnauthAttrs.rgAttr[i].rgValue->cbData;

                                auto hStore2 = pfnCertOpenStore(CERT_STORE_PROV_PKCS7, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, NULL, 0, &c7Data);
                                if (!hStore2)
                                {
                                    LocalFree(pSignerInfo2);
                                    return false;
                                }

                                CERT_INFO CertInfo{};
                                PCCERT_CONTEXT pCertContext = NULL;

                                // Search for the signer certificate in the temporary certificate store.
                                CertInfo.Issuer = pSignerInfo2->Issuer;
                                CertInfo.SerialNumber = pSignerInfo2->SerialNumber;
                                pCertContext = pfnCertFindCertificateInStore(hStore2,
                                    (X509_ASN_ENCODING | PKCS_7_ASN_ENCODING),
                                    0,
                                    CERT_FIND_SUBJECT_CERT,
                                    (PVOID)&CertInfo,
                                    NULL);
                                if (!pCertContext)
                                {
                                    LocalFree(pSignerInfo2);
                                    pfnCertCloseStore(hStore2, CERT_CLOSE_STORE_FORCE_FLAG);
                                    return false;
                                }

                                void* decodedPublicKey{};
                                DWORD decodedPublicLength{};
                                if (pfnCryptDecodeObjectEx((PKCS_7_ASN_ENCODING | X509_ASN_ENCODING),
                                    CNG_RSA_PUBLIC_KEY_BLOB,
                                    pCertContext->pCertInfo->SubjectPublicKeyInfo.PublicKey.pbData,
                                    pCertContext->pCertInfo->SubjectPublicKeyInfo.PublicKey.cbData,
                                    CRYPT_ENCODE_ALLOC_FLAG,
                                    NULL,
                                    &decodedPublicKey,
                                    &decodedPublicLength))
                                {
                                    static uint8_t s_rsaStreamlinePublicKey[] =
                                    {
                                        0x52, 0x53, 0x41, 0x31, 0x00, 0x0c, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00,
                                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0xc1, 0x8e, 0x40, 0xc3, 0xf5,
                                        0xa7, 0x01, 0x9a, 0x37, 0x6b, 0x47, 0xa8, 0x58, 0xe8, 0xbe, 0xe3, 0x55, 0x0a, 0xee, 0x0f, 0x0d,
                                        0x32, 0xaa, 0x12, 0xf9, 0x56, 0x7f, 0x5d, 0xfd, 0x82, 0x09, 0x33, 0x21, 0x42, 0xf2, 0xe8, 0x74,
                                        0x98, 0x51, 0xb3, 0x88, 0x74, 0xcd, 0x00, 0x6e, 0xb1, 0x08, 0x10, 0x4b, 0xf1, 0xda, 0xd6, 0x97,
                                        0x87, 0xd4, 0x9c, 0xb1, 0x13, 0xa8, 0xa2, 0x86, 0x15, 0x0e, 0xc1, 0xa5, 0x9c, 0xe5, 0x90, 0x9b,
                                        0xbe, 0x69, 0xdc, 0x6a, 0x82, 0xbe, 0xb4, 0x4b, 0x4b, 0xfa, 0x95, 0x8e, 0xc1, 0xfc, 0x2b, 0x61,
                                        0x95, 0xd1, 0x91, 0xed, 0xeb, 0x87, 0xe7, 0x09, 0x84, 0x05, 0x41, 0x03, 0xb0, 0x2d, 0xd4, 0x39,
                                        0x7f, 0x62, 0x06, 0x56, 0x33, 0x93, 0x7e, 0x77, 0x54, 0x06, 0x77, 0x2b, 0x75, 0x05, 0xbc, 0xeb,
                                        0x98, 0xea, 0xc0, 0xa2, 0xca, 0x98, 0x86, 0x0f, 0x10, 0x65, 0xde, 0x19, 0x2c, 0xa6, 0x1e, 0x93,
                                        0xb0, 0x92, 0x5d, 0x5f, 0x5b, 0x6f, 0x79, 0x6d, 0x2c, 0x76, 0xa6, 0x67, 0x50, 0xaa, 0x8f, 0xc2,
                                        0x4c, 0xf1, 0x08, 0xf7, 0xc0, 0x27, 0x29, 0xf0, 0x68, 0xf4, 0x64, 0x00, 0x1c, 0xb6, 0x28, 0x1e,
                                        0x25, 0xb8, 0xf3, 0x8a, 0xd1, 0x6e, 0x65, 0xa3, 0x61, 0x9d, 0xf8, 0xca, 0x4a, 0x41, 0x60, 0x80,
                                        0x62, 0xdf, 0x41, 0xa4, 0x8b, 0xdc, 0x97, 0xee, 0xeb, 0x64, 0x6f, 0xe4, 0x8f, 0x4b, 0xdf, 0x24,
                                        0x01, 0x80, 0xd9, 0xb4, 0x0a, 0xec, 0x0d, 0x3e, 0xb7, 0x76, 0xba, 0xe9, 0xe7, 0xde, 0x07, 0xdd,
                                        0x30, 0xc8, 0x4a, 0x14, 0x79, 0xec, 0x15, 0xed, 0x5c, 0xc6, 0xcc, 0xd4, 0xe6, 0x06, 0x3c, 0x42,
                                        0x92, 0x10, 0xf7, 0x7c, 0x80, 0x1e, 0x78, 0xd3, 0xb4, 0x9f, 0xc2, 0x3b, 0xa8, 0x7b, 0xa0, 0xe3,
                                        0x0c, 0xd9, 0xad, 0x2e, 0x09, 0x72, 0xe2, 0x8f, 0x54, 0x28, 0x87, 0x3c, 0xba, 0x7c, 0x97, 0x80,
                                        0xdc, 0x09, 0xb5, 0x12, 0x34, 0x78, 0x9a, 0x26, 0xd0, 0xa3, 0xa7, 0xa7, 0x1b, 0x25, 0x19, 0xe5,
                                        0x6e, 0xbe, 0xd7, 0x5a, 0x91, 0x32, 0xc4, 0xa9, 0x2f, 0xcc, 0xd5, 0x82, 0x4b, 0x5b, 0x9f, 0xad,
                                        0xf3, 0x2f, 0xed, 0x4f, 0x33, 0xe1, 0x50, 0x33, 0xd6, 0x90, 0x79, 0x22, 0xe5, 0x1c, 0xc7, 0x35,
                                        0xe7, 0x58, 0xe6, 0xb4, 0x8b, 0xc4, 0x28, 0x20, 0xec, 0xca, 0x70, 0xbb, 0x02, 0x1b, 0x48, 0xd8,
                                        0x84, 0x51, 0x24, 0x33, 0x2a, 0x08, 0xb1, 0x15, 0x4e, 0xbc, 0x88, 0xa5, 0xe1, 0x37, 0x76, 0x70,
                                        0xe6, 0xdf, 0x3f, 0x73, 0xfd, 0x0d, 0x8a, 0xd9, 0x0d, 0xa5, 0x35, 0xb2, 0xb4, 0x01, 0x42, 0x96,
                                        0xc4, 0xaa, 0x1c, 0xeb, 0x68, 0x62, 0x36, 0xbf, 0xef, 0x5e, 0x2a, 0x3d, 0x18, 0x91, 0x8b, 0x92,
                                        0x0a, 0x1e, 0xce, 0x98, 0x5b, 0x7b, 0x64, 0x42, 0x09, 0xb0, 0x1d
                                    };

                                    valid = decodedPublicLength == sizeof(s_rsaStreamlinePublicKey) && memcmp(s_rsaStreamlinePublicKey, decodedPublicKey, decodedPublicLength) == 0;
                                    LocalFree(decodedPublicKey);
                                }

                                pfnCertFreeCertificateContext(pCertContext);
                                pfnCertCloseStore(hStore2, CERT_CLOSE_STORE_FORCE_FLAG);
                            }
                            LocalFree(pSignerInfo2);
                        }
                    }
                }
                pfnCryptMsgClose(hMsg2);
            }
            break;
        }
    }

    LocalFree(pSignerInfo);
    pfnCryptMsgClose(hMsg);
    pfnCertCloseStore(hStore, CERT_CLOSE_STORE_FORCE_FLAG);

    return valid;
}

//! See https://docs.microsoft.com/en-us/windows/win32/seccrypto/example-c-program--verifying-the-signature-of-a-pe-file
//! 
//! IMPORTANT: Always pass in the FULL PATH to the file, relative paths are NOT allowed!
bool verifyEmbeddedSignature(const wchar_t* pathToFile)
{
    bool valid = true;

    LONG lStatus = {};

    // Initialize the WINTRUST_FILE_INFO structure.

    WINTRUST_FILE_INFO FileData;
    memset(&FileData, 0, sizeof(FileData));
    FileData.cbStruct = sizeof(WINTRUST_FILE_INFO);
    FileData.pcwszFilePath = pathToFile;
    FileData.hFile = NULL;
    FileData.pgKnownSubject = NULL;

    if (!pfnWinVerifyTrust)
    {
        // We only support Win10+ so we can search for module in system32 directly
        auto hModWintrust = LoadLibraryExW(L"wintrust.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!hModWintrust || !GetProc(hModWintrust, "WinVerifyTrust", pfnWinVerifyTrust))
        {
            return false;
        }
    }

    /*
    WVTPolicyGUID specifies the policy to apply on the file
    WINTRUST_ACTION_GENERIC_VERIFY_V2 policy checks:

    1) The certificate used to sign the file chains up to a root
    certificate located in the trusted root certificate store. This
    implies that the identity of the publisher has been verified by
    a certification authority.

    2) In cases where user interface is displayed (which this example
    does not do), WinVerifyTrust will check for whether the
    end entity certificate is stored in the trusted publisher store,
    implying that the user trusts content from this publisher.

    3) The end entity certificate has sufficient permission to sign
    code, as indicated by the presence of a code signing EKU or no
    EKU.
    */

    GUID WVTPolicyGUID = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA WinTrustData;

    // Initialize the WinVerifyTrust input data structure.

    // Default all fields to 0.
    memset(&WinTrustData, 0, sizeof(WinTrustData));

    WinTrustData.cbStruct = sizeof(WinTrustData);
    // Use default code signing EKU.
    WinTrustData.pPolicyCallbackData = NULL;
    // No data to pass to SIP.
    WinTrustData.pSIPClientData = NULL;
    // Disable WVT UI.
    WinTrustData.dwUIChoice = WTD_UI_NONE;
    // No revocation checking.
    WinTrustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    // Verify an embedded signature on a file.
    WinTrustData.dwUnionChoice = WTD_CHOICE_FILE;
    // Verify action.
    WinTrustData.dwStateAction = WTD_STATEACTION_VERIFY;
    // Verification sets this value.
    WinTrustData.hWVTStateData = NULL;
    // Not used.
    WinTrustData.pwszURLReference = NULL;
    // This is not applicable if there is no UI because it changes 
    // the UI to accommodate running applications instead of 
    // installing applications.
    WinTrustData.dwUIContext = 0;
    // Set pFile.
    WinTrustData.pFile = &FileData;

    // First verify the primary signature (index 0) to determine how many secondary signatures
    // are present. We use WSS_VERIFY_SPECIFIC and dwIndex to do this, also setting 
    // WSS_GET_SECONDARY_SIG_COUNT to have the number of secondary signatures returned.
    WINTRUST_SIGNATURE_SETTINGS SignatureSettings = {};
    CERT_STRONG_SIGN_PARA StrongSigPolicy = {};
    SignatureSettings.cbStruct = sizeof(WINTRUST_SIGNATURE_SETTINGS);
    SignatureSettings.dwFlags = WSS_GET_SECONDARY_SIG_COUNT | WSS_VERIFY_SPECIFIC;
    SignatureSettings.dwIndex = 0;
    WinTrustData.pSignatureSettings = &SignatureSettings;

    StrongSigPolicy.cbSize = sizeof(CERT_STRONG_SIGN_PARA);
    StrongSigPolicy.dwInfoChoice = CERT_STRONG_SIGN_OID_INFO_CHOICE;
    StrongSigPolicy.pszOID = (LPSTR)szOID_CERT_STRONG_SIGN_OS_CURRENT;
    WinTrustData.pSignatureSettings->pCryptoPolicy = &StrongSigPolicy;

    // WinVerifyTrust verifies signatures as specified by the GUID  and Wintrust_Data.
    lStatus = pfnWinVerifyTrust(NULL, &WVTPolicyGUID, &WinTrustData);
    
    // First signature must be validated by the OS
    valid = lStatus == ERROR_SUCCESS;
    if (!valid)
    {
        printf("File '%S' is NOT correctly signed - Streamline will not load unsecured modules\n", pathToFile);
    }
    else
    {
        // Now there has to be a secondary one
        valid &= WinTrustData.pSignatureSettings->cSecondarySigs == 1;
        if (!valid)
        {
            printf("File '%S' does not have the secondary NVIDIA signature - Streamline will not load unsecured modules\n", pathToFile);
        }
        else
        {
            // The secondary signature must be from NVIDIA
            valid &= isSignedByNVIDIA(pathToFile);
            if (valid)
            {
                printf("File '%S' is signed by NVIDIA and the signature was verified.\n", pathToFile);
            }
            else
            {
                printf("File '%S' is NOT correctly signed - Streamline will not load unsecured modules\n", pathToFile);
            }
        }
    }

    // Any hWVTStateData must be released by a call with close.
    WinTrustData.dwStateAction = WTD_STATEACTION_CLOSE;
    lStatus = pfnWinVerifyTrust(NULL, &WVTPolicyGUID, &WinTrustData);

    return valid;
}

}
}

