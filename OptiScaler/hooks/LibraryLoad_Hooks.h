#pragma once
#include "SysUtils.h"

class LibraryLoadHooks
{
  private:
    static void CheckModulesInMemory();
    static bool StartsWithInsensitive(std::wstring_view str, std::wstring_view prefix);

  public:
    static HMODULE LoadLibraryCheckA(std::string libName, LPCSTR lpLibFullPath);
    static HMODULE LoadLibraryCheckW(std::wstring libName, LPCWSTR lpLibFullPath);
    static std::optional<NTSTATUS> FreeLibrary(PVOID library);

    // Call after the original FreeLibrary / LdrUnloadDll returned
    static void AfterFreeLibrary(PVOID library);

    static HMODULE LoadNvApi();
    // static HMODULE LoadFfxapiVk(std::wstring originalPath);
    // static HMODULE LoadFfxapiDx12(std::wstring originalPath);
    // static HMODULE LoadLibxessDx11(std::wstring originalPath);
    // static HMODULE LoadLibxess(std::wstring originalPath);
    static HMODULE LoadNvngxDlss(std::wstring originalPath);

    static bool IsApiSetName(const std::wstring_view& n);
    static bool EndsWithInsensitive(std::wstring_view text, std::wstring_view suffix);
    static bool EndsWithInsensitive(const UNICODE_STRING& text, std::wstring_view suffix);
};
