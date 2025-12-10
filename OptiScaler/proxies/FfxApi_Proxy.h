#pragma once

#include "pch.h"
#include "Util.h"
#include "Config.h"
#include "Logger.h"

#include <proxies/Ntdll_Proxy.h>
#include <proxies/KernelBase_Proxy.h>

#include <inputs/FfxApi_Dx12.h>
#include <inputs/FfxApi_Vk.h>

#include "ffx_api.h"
#include <detours/detours.h>
#include <ffx_framegeneration.h>
#include <ffx_upscale.h>

struct FfxModule
{
    HMODULE dll = nullptr;
    feature_version version { 0, 0, 0 };

    bool skipCreateCalls = false;
    bool skipConfigureCalls = false;
    bool skipQueryCalls = false;
    bool skipDispatchCalls = false;

    bool isLoader = false;

    PfnFfxCreateContext CreateContext = nullptr;
    PfnFfxDestroyContext DestroyContext = nullptr;
    PfnFfxConfigure Configure = nullptr;
    PfnFfxQuery Query = nullptr;
    PfnFfxDispatch Dispatch = nullptr;
};

class FfxApiProxy
{
  private:
    inline static FfxModule main_dx12;
    inline static FfxModule upscaling_dx12;
    inline static FfxModule fg_dx12;

    inline static FfxModule main_vk;

    enum class FFXStructType
    {
        General,
        Upscaling,
        Swapchain,
        FG,
        VulkanSwapchain,
        Unknown
    };

    inline static ankerl::unordered_dense::map<ffxContext*, FFXStructType> contextToType;

    inline static bool _skipDestroyCalls = false;

    static inline void parse_version(const char* version_str, feature_version* _version)
    {
        const char* p = version_str;

        // Skip non-digits at front
        while (*p)
        {
            if (isdigit((unsigned char) p[0]))
            {
                if (sscanf(p, "%u.%u.%u", &_version->major, &_version->minor, &_version->patch) == 3)
                    return;
            }
            ++p;
        }

        LOG_WARN("can't parse {0}", version_str);
    }

    static FFXStructType GetType(ffxStructType_t type)
    {
        switch (type & FFX_API_EFFECT_MASK) // type without the specific effect
        {
        case FFX_API_EFFECT_ID_GENERAL:
            return FFXStructType::General;

        case FFX_API_EFFECT_ID_UPSCALE:
            return FFXStructType::Upscaling;

        case FFX_API_EFFECT_ID_FRAMEGENERATION:
            return FFXStructType::FG;

        case 0x00030000u: // don't want to include ffx_api_dx12.h because of enum name pollution
            return FFXStructType::Swapchain;

        case 0x00040000u: // don't want to include ffx_api_vk.h because of enum name pollution
            return FFXStructType::VulkanSwapchain;

        default:
            return FFXStructType::Unknown;
        }
    }

    // Can't directly check for type when query is used
    // might apply to FFX_API_DESC_TYPE_OVERRIDE_VERSION as well
    static FFXStructType GetType(ffxQueryDescHeader* header)
    {
        ffxStructType_t type = header->type;

        if (header->type == FFX_API_QUERY_DESC_TYPE_GET_VERSIONS ||
            header->type == FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION)
        {
            type = header[1].type;
        }

        return GetType(type);
    }

    static bool IsLoader(const std::wstring& filePath)
    {
        auto size = std::filesystem::file_size(filePath);

        // < 1 MB
        return size < 1048576;
    }

  public:
    static HMODULE Dx12Module() { return main_dx12.dll; }
    static HMODULE Dx12Module_SR() { return upscaling_dx12.dll; }
    static HMODULE Dx12Module_FG() { return fg_dx12.dll; }

    static bool IsFGReady() { return (main_dx12.dll && !main_dx12.isLoader) || fg_dx12.dll != nullptr; }
    static bool IsSRReady() { return (main_dx12.dll && !main_dx12.isLoader) || upscaling_dx12.dll != nullptr; }

    static bool InitFfxDx12(HMODULE module = nullptr)
    {
        // if dll already loaded
        if (main_dx12.dll != nullptr && main_dx12.CreateContext != nullptr)
            return true;

        spdlog::info("");

        if (module != nullptr)
        {
            main_dx12.dll = module;

            wchar_t path[MAX_PATH];
            DWORD len = GetModuleFileNameW(module, path, MAX_PATH);
            std::wstring fileName(path);
            main_dx12.isLoader = IsLoader(fileName);
        }

        if (main_dx12.dll == nullptr)
        {
            // Try new api first
            std::vector<std::wstring> dllNames = { L"amd_fidelityfx_loader_dx12.dll", L"amd_fidelityfx_dx12.dll" };

            for (size_t i = 0; i < dllNames.size(); i++)
            {
                LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

                if (main_dx12.dll == nullptr && Config::Instance()->FfxDx12Path.has_value())
                {
                    std::filesystem::path libPath(Config::Instance()->FfxDx12Path.value().c_str());
                    std::wstring fileName;

                    if (libPath.has_filename())
                        fileName = libPath.c_str();
                    else
                        fileName = (libPath / dllNames[i]).c_str();

                    main_dx12.dll = NtdllProxy::LoadLibraryExW_Ldr(fileName.c_str(), NULL, 0);

                    if (main_dx12.dll != nullptr)
                    {
                        LOG_INFO("{} loaded from {}", wstring_to_string(dllNames[i]),
                                 wstring_to_string(Config::Instance()->FfxDx12Path.value()));

                        // hacky but works for now
                        main_dx12.isLoader = IsLoader(fileName);
                        break;
                    }
                }

                if (main_dx12.dll == nullptr)
                {
                    auto filePath = (Util::DllPath().parent_path() / dllNames[i]);
                    main_dx12.dll = NtdllProxy::LoadLibraryExW_Ldr(filePath.c_str(), NULL, 0);

                    if (main_dx12.dll != nullptr)
                    {
                        LOG_INFO("{} loaded from exe folder", wstring_to_string(dllNames[i]));

                        // hacky but works for now
                        main_dx12.isLoader = IsLoader(filePath.c_str());
                        break;
                    }
                }
            }
        }

        if (main_dx12.dll != nullptr && main_dx12.Configure == nullptr)
        {
            main_dx12.Configure = (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(main_dx12.dll, "ffxConfigure");
            main_dx12.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(main_dx12.dll, "ffxCreateContext");
            main_dx12.DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(main_dx12.dll, "ffxDestroyContext");
            main_dx12.Dispatch = (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(main_dx12.dll, "ffxDispatch");
            main_dx12.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(main_dx12.dll, "ffxQuery");

            if (Config::Instance()->EnableFfxInputs.value_or_default() && main_dx12.CreateContext != nullptr)
            {
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                if (main_dx12.Configure != nullptr)
                    DetourAttach(&(PVOID&) main_dx12.Configure, ffxConfigure_Dx12);

                if (main_dx12.CreateContext != nullptr)
                    DetourAttach(&(PVOID&) main_dx12.CreateContext, ffxCreateContext_Dx12);

                if (main_dx12.DestroyContext != nullptr)
                    DetourAttach(&(PVOID&) main_dx12.DestroyContext, ffxDestroyContext_Dx12);

                if (main_dx12.Dispatch != nullptr)
                    DetourAttach(&(PVOID&) main_dx12.Dispatch, ffxDispatch_Dx12);

                if (main_dx12.Query != nullptr)
                    DetourAttach(&(PVOID&) main_dx12.Query, ffxQuery_Dx12);

                State::Instance().fsrHooks = true;

                DetourTransactionCommit();
            }
        }

        InitFfxDx12_SR();
        InitFfxDx12_FG();

        bool loadResult = main_dx12.CreateContext != nullptr || upscaling_dx12.CreateContext != nullptr ||
                          fg_dx12.CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (loadResult)
            VersionDx12();
        else
            main_dx12.dll = nullptr;

        return loadResult;
    }

    static bool InitFfxDx12_SR(HMODULE module = nullptr)
    {
        // if dll already loaded
        if (upscaling_dx12.dll != nullptr && upscaling_dx12.CreateContext != nullptr)
            return true;

        spdlog::info("");

        if (module != nullptr)
            upscaling_dx12.dll = module;

        if (upscaling_dx12.dll == nullptr)
        {
            // Try new api first
            std::vector<std::wstring> dllNames = { L"amd_fidelityfx_upscaler_dx12.dll" };

            for (size_t i = 0; i < dllNames.size(); i++)
            {
                LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

                // if (upscaling_dx12.dll == nullptr && Config::Instance()->FfxDx12Path.has_value())
                //{
                //     std::filesystem::path libPath(Config::Instance()->FfxDx12Path.value().c_str());

                //    if (libPath.has_filename())
                //        upscaling_dx12.dll = NtdllProxy::LoadLibraryExW_Ldr(libPath.c_str(), NULL, 0);
                //    else
                //        upscaling_dx12.dll = NtdllProxy::LoadLibraryExW_Ldr((libPath / dllNames[i]).c_str(), NULL, 0);

                //    if (upscaling_dx12.dll != nullptr)
                //    {
                //        LOG_INFO("{} loaded from {}", wstring_to_string(dllNames[i]),
                //                 wstring_to_string(Config::Instance()->FfxDx12Path.value()));
                //        break;
                //    }
                //}

                if (upscaling_dx12.dll == nullptr)
                {
                    upscaling_dx12.dll = NtdllProxy::LoadLibraryExW_Ldr(dllNames[i].c_str(), NULL, 0);

                    if (upscaling_dx12.dll != nullptr)
                    {
                        LOG_INFO("{} loaded from exe folder", wstring_to_string(dllNames[i]));
                        break;
                    }
                }
            }
        }

        if (upscaling_dx12.dll != nullptr && upscaling_dx12.Configure == nullptr)
        {
            upscaling_dx12.Configure =
                (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(upscaling_dx12.dll, "ffxConfigure");
            upscaling_dx12.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(upscaling_dx12.dll, "ffxCreateContext");
            upscaling_dx12.DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(upscaling_dx12.dll, "ffxDestroyContext");
            upscaling_dx12.Dispatch =
                (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(upscaling_dx12.dll, "ffxDispatch");
            upscaling_dx12.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(upscaling_dx12.dll, "ffxQuery");

            if (Config::Instance()->EnableFfxInputs.value_or_default() && upscaling_dx12.CreateContext != nullptr)
            {
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                if (upscaling_dx12.Configure != nullptr)
                    DetourAttach(&(PVOID&) upscaling_dx12.Configure, ffxConfigure_Dx12);

                if (upscaling_dx12.CreateContext != nullptr)
                    DetourAttach(&(PVOID&) upscaling_dx12.CreateContext, ffxCreateContext_Dx12);

                if (upscaling_dx12.DestroyContext != nullptr)
                    DetourAttach(&(PVOID&) upscaling_dx12.DestroyContext, ffxDestroyContext_Dx12);

                if (upscaling_dx12.Dispatch != nullptr)
                    DetourAttach(&(PVOID&) upscaling_dx12.Dispatch, ffxDispatch_Dx12);

                if (upscaling_dx12.Query != nullptr)
                    DetourAttach(&(PVOID&) upscaling_dx12.Query, ffxQuery_Dx12);

                State::Instance().fsrHooks = true;

                DetourTransactionCommit();
            }
        }

        bool loadResult = upscaling_dx12.CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (loadResult)
            VersionDx12_SR();
        else
            upscaling_dx12.dll = nullptr;

        return loadResult;
    }

    static bool InitFfxDx12_FG(HMODULE module = nullptr)
    {
        // if dll already loaded
        if (fg_dx12.dll != nullptr && fg_dx12.CreateContext != nullptr)
            return true;

        spdlog::info("");

        if (module != nullptr)
            fg_dx12.dll = module;

        if (fg_dx12.dll == nullptr)
        {
            // Try new api first
            std::vector<std::wstring> dllNames = { L"amd_fidelityfx_framegeneration_dx12.dll" };

            for (size_t i = 0; i < dllNames.size(); i++)
            {
                LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

                // if (fg_dx12.dll == nullptr && Config::Instance()->FfxDx12Path.has_value())
                //{
                //     std::filesystem::path libPath(Config::Instance()->FfxDx12Path.value().c_str());

                //    if (libPath.has_filename())
                //        fg_dx12.dll = NtdllProxy::LoadLibraryExW_Ldr(libPath.c_str(), NULL, 0);
                //    else
                //        fg_dx12.dll = NtdllProxy::LoadLibraryExW_Ldr((libPath / dllNames[i]).c_str(), NULL, 0);

                //    if (fg_dx12.dll != nullptr)
                //    {
                //        LOG_INFO("{} loaded from {}", wstring_to_string(dllNames[i]),
                //                 wstring_to_string(Config::Instance()->FfxDx12Path.value()));
                //        break;
                //    }
                //}

                if (fg_dx12.dll == nullptr)
                {
                    fg_dx12.dll = NtdllProxy::LoadLibraryExW_Ldr(dllNames[i].c_str(), NULL, 0);

                    if (fg_dx12.dll != nullptr)
                    {
                        LOG_INFO("{} loaded from exe folder", wstring_to_string(dllNames[i]));
                        break;
                    }
                }
            }
        }

        if (fg_dx12.dll != nullptr && fg_dx12.Configure == nullptr)
        {
            fg_dx12.Configure = (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(fg_dx12.dll, "ffxConfigure");
            fg_dx12.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(fg_dx12.dll, "ffxCreateContext");
            fg_dx12.DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(fg_dx12.dll, "ffxDestroyContext");
            fg_dx12.Dispatch = (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(fg_dx12.dll, "ffxDispatch");
            fg_dx12.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(fg_dx12.dll, "ffxQuery");

            if (Config::Instance()->EnableFfxInputs.value_or_default() && fg_dx12.CreateContext != nullptr)
            {
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                if (fg_dx12.Configure != nullptr)
                    DetourAttach(&(PVOID&) fg_dx12.Configure, ffxConfigure_Dx12);

                if (fg_dx12.CreateContext != nullptr)
                    DetourAttach(&(PVOID&) fg_dx12.CreateContext, ffxCreateContext_Dx12);

                if (fg_dx12.DestroyContext != nullptr)
                    DetourAttach(&(PVOID&) fg_dx12.DestroyContext, ffxDestroyContext_Dx12);

                if (fg_dx12.Dispatch != nullptr)
                    DetourAttach(&(PVOID&) fg_dx12.Dispatch, ffxDispatch_Dx12);

                if (fg_dx12.Query != nullptr)
                    DetourAttach(&(PVOID&) fg_dx12.Query, ffxQuery_Dx12);

                State::Instance().fsrHooks = true;

                DetourTransactionCommit();
            }
        }

        bool loadResult = fg_dx12.CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (loadResult)
            VersionDx12_FG();
        else
            fg_dx12.dll = nullptr;

        return loadResult;
    }

    static feature_version VersionDx12()
    {
        if (main_dx12.version.major == 0 && main_dx12.Query != nullptr /* && device != nullptr*/)
        {
            ffxQueryDescGetVersions versionQuery {};
            versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            uint64_t versionCount = 0;
            versionQuery.outputCount = &versionCount;

            auto queryResult = main_dx12.Query(nullptr, &versionQuery.header);

            // get number of versions for allocation
            if (versionCount > 0 && queryResult == FFX_API_RETURN_OK)
            {

                std::vector<uint64_t> versionIds;
                std::vector<const char*> versionNames;
                versionIds.resize(versionCount);
                versionNames.resize(versionCount);
                versionQuery.versionIds = versionIds.data();
                versionQuery.versionNames = versionNames.data();

                // fill version ids and names arrays.
                queryResult = main_dx12.Query(nullptr, &versionQuery.header);

                if (queryResult == FFX_API_RETURN_OK)
                {
                    parse_version(versionNames[0], &main_dx12.version);
                    LOG_INFO("FfxApi Dx12 version: {}.{}.{}", main_dx12.version.major, main_dx12.version.minor,
                             main_dx12.version.patch);
                }
                else
                {
                    LOG_WARN("main_dx12.Query 2 result: {}", (UINT) queryResult);
                }
            }
            else
            {
                LOG_WARN("main_dx12.Query result: {}", (UINT) queryResult);
            }
        }

        if (main_dx12.version.major == 0 && upscaling_dx12.Query != nullptr)
            main_dx12.version = VersionDx12_SR();

        if (main_dx12.version.major == 0 && fg_dx12.Query != nullptr)
            main_dx12.version = VersionDx12_FG();

        return main_dx12.version;
    }

    static feature_version VersionDx12_SR()
    {
        if (upscaling_dx12.Query == nullptr)
            return VersionDx12();

        if (upscaling_dx12.version.major == 0)
        {
            ffxQueryDescGetVersions versionQuery {};
            versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            uint64_t versionCount = 0;
            versionQuery.outputCount = &versionCount;

            auto queryResult = upscaling_dx12.Query(nullptr, &versionQuery.header);

            // get number of versions for allocation
            if (versionCount > 0 && queryResult == FFX_API_RETURN_OK)
            {

                std::vector<uint64_t> versionIds;
                std::vector<const char*> versionNames;
                versionIds.resize(versionCount);
                versionNames.resize(versionCount);
                versionQuery.versionIds = versionIds.data();
                versionQuery.versionNames = versionNames.data();

                // fill version ids and names arrays.
                queryResult = upscaling_dx12.Query(nullptr, &versionQuery.header);

                if (queryResult == FFX_API_RETURN_OK)
                {
                    parse_version(versionNames[0], &upscaling_dx12.version);
                    LOG_INFO("FfxApi Dx12 SR version: {}.{}.{}", upscaling_dx12.version.major,
                             upscaling_dx12.version.minor, upscaling_dx12.version.patch);
                }
                else
                {
                    LOG_WARN("main_dx12.Query 2 result: {}", (UINT) queryResult);
                }
            }
            else
            {
                LOG_WARN("main_dx12.Query result: {}", (UINT) queryResult);
            }
        }

        return upscaling_dx12.version;
    }

    static feature_version VersionDx12_FG()
    {
        if (fg_dx12.Query == nullptr)
            return VersionDx12();

        if (fg_dx12.version.major == 0)
        {
            ffxQueryDescGetVersions versionQuery {};
            versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
            uint64_t versionCount = 0;
            versionQuery.outputCount = &versionCount;

            auto queryResult = fg_dx12.Query(nullptr, &versionQuery.header);

            // get number of versions for allocation
            if (versionCount > 0 && queryResult == FFX_API_RETURN_OK)
            {

                std::vector<uint64_t> versionIds;
                std::vector<const char*> versionNames;
                versionIds.resize(versionCount);
                versionNames.resize(versionCount);
                versionQuery.versionIds = versionIds.data();
                versionQuery.versionNames = versionNames.data();

                // fill version ids and names arrays.
                queryResult = fg_dx12.Query(nullptr, &versionQuery.header);

                if (queryResult == FFX_API_RETURN_OK)
                {
                    parse_version(versionNames[0], &fg_dx12.version);
                    LOG_INFO("FfxApi Dx12 FG version: {}.{}.{}", fg_dx12.version.major, fg_dx12.version.minor,
                             fg_dx12.version.patch);
                }
                else
                {
                    LOG_WARN("main_dx12.Query 2 result: {}", (UINT) queryResult);
                }
            }
            else
            {
                LOG_WARN("main_dx12.Query result: {}", (UINT) queryResult);
            }
        }

        return fg_dx12.version;
    }

    static ffxReturnCode_t D3D12_CreateContext(ffxContext* context, ffxCreateContextDescHeader* desc,
                                               const ffxAllocationCallbacks* memCb)
    {
        auto type = GetType(desc->type);
        auto isFg = type == FFXStructType::FG || type == FFXStructType::Swapchain;

        contextToType[context] = type;

        if (isFg && fg_dx12.dll != nullptr)
            return fg_dx12.CreateContext(context, desc, memCb);
        else if (!isFg && upscaling_dx12.dll != nullptr)
            return upscaling_dx12.CreateContext(context, desc, memCb);

        if (main_dx12.dll != nullptr && !(isFg && fg_dx12.skipCreateCalls) &&
            !(!isFg && upscaling_dx12.skipCreateCalls))
        {
            if (isFg)
                fg_dx12.skipCreateCalls = true;
            else
                upscaling_dx12.skipCreateCalls = true;

            auto result = main_dx12.CreateContext(context, desc, memCb);

            if (isFg)
                fg_dx12.skipCreateCalls = false;
            else
                upscaling_dx12.skipCreateCalls = false;

            return result;
        }

        return FFX_API_RETURN_NO_PROVIDER;
    }

    static ffxReturnCode_t D3D12_DestroyContext(ffxContext* context, const ffxAllocationCallbacks* memCb)
    {
        ffxReturnCode_t result = FFX_API_RETURN_ERROR;
        auto type = FFXStructType::Unknown;

        if (contextToType.contains(context))
        {
            type = contextToType[context];
            contextToType.erase(context);
        }

        switch (type)
        {
        case FFXStructType::General:
            if (main_dx12.dll != nullptr)
                return main_dx12.DestroyContext(context, memCb);
            break;

        case FFXStructType::Upscaling:
            if (upscaling_dx12.dll != nullptr)
                return upscaling_dx12.DestroyContext(context, memCb);
            break;

        case FFXStructType::FG:
        case FFXStructType::Swapchain:
            if (fg_dx12.dll != nullptr)
                return fg_dx12.DestroyContext(context, memCb);
            break;

        default:
            break;
        }

        if (main_dx12.dll != nullptr && !_skipDestroyCalls)
        {
            _skipDestroyCalls = true;
            result = main_dx12.DestroyContext(context, memCb);
            _skipDestroyCalls = false;
        }

        if (result == FFX_API_RETURN_OK)
            return result;

        if (upscaling_dx12.dll != nullptr)
            result = upscaling_dx12.DestroyContext(context, memCb);

        if (result == FFX_API_RETURN_OK)
            return result;

        if (fg_dx12.dll != nullptr)
            result = fg_dx12.DestroyContext(context, memCb);

        if (result == FFX_API_RETURN_OK)
            return result;

        return FFX_API_RETURN_NO_PROVIDER;
    }

    static ffxReturnCode_t D3D12_Configure(ffxContext* context, const ffxConfigureDescHeader* desc)
    {
        auto type = GetType(desc->type);
        auto isFg = type == FFXStructType::FG || type == FFXStructType::Swapchain;

        if (isFg && fg_dx12.dll != nullptr)
            return fg_dx12.Configure(context, desc);
        else if (!isFg && upscaling_dx12.dll != nullptr)
            return upscaling_dx12.Configure(context, desc);

        if (main_dx12.dll != nullptr && !(isFg && fg_dx12.skipConfigureCalls) &&
            !(!isFg && upscaling_dx12.skipConfigureCalls))
        {
            if (isFg)
                fg_dx12.skipConfigureCalls = true;
            else
                upscaling_dx12.skipConfigureCalls = true;

            auto result = main_dx12.Configure(context, desc);

            if (isFg)
                fg_dx12.skipConfigureCalls = false;
            else
                upscaling_dx12.skipConfigureCalls = false;

            return result;
        }

        return FFX_API_RETURN_NO_PROVIDER;
    }

    static ffxReturnCode_t D3D12_Query(ffxContext* context, ffxQueryDescHeader* desc)
    {
        auto type = GetType(desc);
        auto isFg = type == FFXStructType::FG || type == FFXStructType::Swapchain;

        if (isFg && fg_dx12.dll != nullptr)
            return fg_dx12.Query(context, desc);
        else if (!isFg && upscaling_dx12.dll != nullptr)
            return upscaling_dx12.Query(context, desc);

        if (main_dx12.dll != nullptr && !(isFg && fg_dx12.skipQueryCalls) && !(!isFg && upscaling_dx12.skipQueryCalls))
        {
            if (isFg)
                fg_dx12.skipQueryCalls = true;
            else
                upscaling_dx12.skipQueryCalls = true;

            auto result = main_dx12.Query(context, desc);

            if (isFg)
                fg_dx12.skipQueryCalls = false;
            else
                upscaling_dx12.skipQueryCalls = false;

            return result;
        }

        return FFX_API_RETURN_NO_PROVIDER;
    }

    static ffxReturnCode_t D3D12_Dispatch(ffxContext* context, const ffxDispatchDescHeader* desc)
    {
        auto type = GetType(desc->type);
        auto isFg = type == FFXStructType::FG || type == FFXStructType::Swapchain;

        if (isFg && fg_dx12.dll != nullptr)
            return fg_dx12.Dispatch(context, desc);
        else if (!isFg && upscaling_dx12.dll != nullptr)
            return upscaling_dx12.Dispatch(context, desc);

        if (main_dx12.dll != nullptr && !(isFg && fg_dx12.skipDispatchCalls) &&
            !(!isFg && upscaling_dx12.skipDispatchCalls))
        {
            if (isFg)
                fg_dx12.skipDispatchCalls = true;
            else
                upscaling_dx12.skipDispatchCalls = true;

            auto result = main_dx12.Dispatch(context, desc);

            if (isFg)
                fg_dx12.skipDispatchCalls = false;
            else
                upscaling_dx12.skipDispatchCalls = false;

            return result;
        }

        return FFX_API_RETURN_NO_PROVIDER;
    }

    static HMODULE VkModule() { return main_vk.dll; }

    static bool InitFfxVk(HMODULE module = nullptr)
    {
        // if dll already loaded
        if (main_vk.dll != nullptr && main_vk.CreateContext != nullptr)
            return true;

        spdlog::info("");

        LOG_DEBUG("Loading amd_fidelityfx_vk.dll methods");

        if (module != nullptr)
            main_vk.dll = module;

        if (main_vk.dll == nullptr && Config::Instance()->FfxVkPath.has_value())
        {
            std::filesystem::path libPath(Config::Instance()->FfxVkPath.value().c_str());

            if (libPath.has_filename())
                main_vk.dll = NtdllProxy::LoadLibraryExW_Ldr(libPath.c_str(), NULL, 0);
            else
                main_vk.dll = NtdllProxy::LoadLibraryExW_Ldr((libPath / L"amd_fidelityfx_vk.dll").c_str(), NULL, 0);

            if (main_vk.dll != nullptr)
            {
                LOG_INFO("amd_fidelityfx_vk.dll loaded from {0}",
                         wstring_to_string(Config::Instance()->FfxVkPath.value()));
            }
        }

        if (main_vk.dll == nullptr)
        {
            main_vk.dll = NtdllProxy::LoadLibraryExW_Ldr(L"amd_fidelityfx_vk.dll", NULL, 0);

            if (main_vk.dll != nullptr)
                LOG_INFO("amd_fidelityfx_vk.dll loaded from exe folder");
        }

        if (main_vk.dll != nullptr && main_vk.CreateContext == nullptr)
        {
            main_vk.Configure = (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(main_vk.dll, "ffxConfigure");
            main_vk.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(main_vk.dll, "ffxCreateContext");
            main_vk.DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(main_vk.dll, "ffxDestroyContext");
            main_vk.Dispatch = (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(main_vk.dll, "ffxDispatch");
            main_vk.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(main_vk.dll, "ffxQuery");

            if (Config::Instance()->EnableFfxInputs.value_or_default() && main_vk.CreateContext != nullptr)
            {
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                if (main_vk.Configure != nullptr)
                    DetourAttach(&(PVOID&) main_vk.Configure, ffxConfigure_Vk);

                if (main_vk.CreateContext != nullptr)
                    DetourAttach(&(PVOID&) main_vk.CreateContext, ffxCreateContext_Vk);

                if (main_vk.DestroyContext != nullptr)
                    DetourAttach(&(PVOID&) main_vk.DestroyContext, ffxDestroyContext_Vk);

                if (main_vk.Dispatch != nullptr)
                    DetourAttach(&(PVOID&) main_vk.Dispatch, ffxDispatch_Vk);

                if (main_vk.Query != nullptr)
                    DetourAttach(&(PVOID&) main_vk.Query, ffxQuery_Vk);

                State::Instance().fsrHooks = true;

                DetourTransactionCommit();
            }
        }

        bool loadResult = main_vk.CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (loadResult)
            VersionVk();
        else
            main_vk.dll = nullptr;

        return loadResult;
    }

    static feature_version VersionVk()
    {
        if (main_vk.version.major == 0 && main_vk.Query != nullptr)
        {
            ffxQueryDescGetVersions versionQuery {};
            versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            uint64_t versionCount = 0;
            versionQuery.outputCount = &versionCount;

            auto queryResult = main_vk.Query(nullptr, &versionQuery.header);

            // get number of versions for allocation
            if (versionCount > 0 && queryResult == FFX_API_RETURN_OK)
            {

                std::vector<uint64_t> versionIds;
                std::vector<const char*> versionNames;
                versionIds.resize(versionCount);
                versionNames.resize(versionCount);
                versionQuery.versionIds = versionIds.data();
                versionQuery.versionNames = versionNames.data();

                queryResult = main_vk.Query(nullptr, &versionQuery.header);

                if (queryResult == FFX_API_RETURN_OK)
                {
                    parse_version(versionNames[0], &main_vk.version);
                    LOG_INFO("FfxApi Vulkan version: {}.{}.{}", main_vk.version.major, main_vk.version.minor,
                             main_vk.version.patch);
                }
                else
                {
                    LOG_WARN("main_vk.Query 2 result: {}", (UINT) queryResult);
                }
            }
            else
            {
                LOG_WARN("main_vk.Query result: {}", (UINT) queryResult);
            }
        }

        return main_vk.version;
    }

    static PfnFfxCreateContext VULKAN_CreateContext() { return main_vk.CreateContext; }
    static PfnFfxDestroyContext VULKAN_DestroyContext() { return main_vk.DestroyContext; }
    static PfnFfxConfigure VULKAN_Configure() { return main_vk.Configure; }
    static PfnFfxQuery VULKAN_Query() { return main_vk.Query; }
    static PfnFfxDispatch VULKAN_Dispatch() { return main_vk.Dispatch; }

    static std::string ReturnCodeToString(ffxReturnCode_t result)
    {
        switch (result)
        {
        case FFX_API_RETURN_OK:
            return "The oparation was successful.";
        case FFX_API_RETURN_ERROR:
            return "An error occurred that is not further specified.";
        case FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE:
            return "The structure type given was not recognized for the function or context with which it was used. "
                   "This is likely a programming error.";
        case FFX_API_RETURN_ERROR_RUNTIME_ERROR:
            return "The underlying runtime (e.g. D3D12, Vulkan) or effect returned an error code.";
        case FFX_API_RETURN_NO_PROVIDER:
            return "No provider was found for the given structure type. This is likely a programming error.";
        case FFX_API_RETURN_ERROR_MEMORY:
            return "A memory allocation failed.";
        case FFX_API_RETURN_ERROR_PARAMETER:
            return "A parameter was invalid, e.g. a null pointer, empty resource or out-of-bounds enum value.";
        default:
            return "Unknown";
        }
    }
};
