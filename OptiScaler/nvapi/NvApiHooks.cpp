#include "pch.h"
#include "NvApiHooks.h"
#include <NvApiDriverSettings.h>

#include "State.h"
#include <Config.h>

#include <proxies/KernelBase_Proxy.h>

#include <detours/detours.h>
#include <misc/IdentifyGpu.h>
#include <low_latency/input/input_reflex.h>

// #define LOG_ALL_DRS_GET_CALLS

#ifdef LOG_ALL_DRS_GET_CALLS
#include <magic_enum.hpp>
#endif

NvAPI_Status __stdcall NvApiHooks::hkNvAPI_GPU_GetArchInfo(NvPhysicalGpuHandle hPhysicalGpu,
                                                           NV_GPU_ARCH_INFO* pGpuArchInfo)
{
    if (!o_NvAPI_GPU_GetArchInfo)
    {
        LOG_DEBUG("nullptr");
        return NVAPI_ERROR;
    }

    const auto status = o_NvAPI_GPU_GetArchInfo(hPhysicalGpu, pGpuArchInfo);

    if (status == NVAPI_OK && pGpuArchInfo)
    {
        if (pGpuArchInfo->architecture_id <= NV_GPU_ARCHITECTURE_GP100)
        {
            // Check if values were volatile, override them if so
            // if (!Config::Instance()->StreamlineSpoofing.value_for_config().has_value())
            //    Config::Instance()->StreamlineSpoofing.set_volatile_value(true);

            if (!Config::Instance()->DisableFlipMetering.value_for_config().has_value())
                Config::Instance()->DisableFlipMetering.set_volatile_value(true);
        }

        LOG_DEBUG("Original arch: {0:X} impl: {1:X} rev: {2:X}!", pGpuArchInfo->architecture,
                  pGpuArchInfo->implementation, pGpuArchInfo->revision);

        // for DLSS on 16xx cards
        // Can't spoof ada for DLSSG here as that breaks DLSS/DLSSD
        if (pGpuArchInfo->architecture == NV_GPU_ARCHITECTURE_TU100 &&
            pGpuArchInfo->implementation > NV_GPU_ARCH_IMPLEMENTATION_TU106)
        {
            pGpuArchInfo->implementation = NV_GPU_ARCH_IMPLEMENTATION_TU106;
            LOG_INFO("Spoofed arch: {0:X} impl: {1:X} rev: {2:X}!", pGpuArchInfo->architecture,
                     pGpuArchInfo->implementation, pGpuArchInfo->revision);
        }
    }

    return status;
}

NvAPI_Status __stdcall NvApiHooks::hkNvAPI_DRS_GetSetting(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile,
                                                          NvU32 settingId, NVDRS_SETTING* pSetting)
{
    if (!o_NvAPI_DRS_GetSetting)
        return NVAPI_ERROR;

    auto result = o_NvAPI_DRS_GetSetting(hSession, hProfile, settingId, pSetting);
    if (pSetting && result == NVAPI_OK)
    {
#ifdef LOG_ALL_DRS_GET_CALLS
        LOG_TRACE("settingId: {:X}, settingLocation: {}, isCurrentPredefined: {}", settingId,
                  magic_enum::enum_name(pSetting->settingLocation), pSetting->isCurrentPredefined,
                  pSetting->isPredefinedValid);

        switch (pSetting->settingType)
        {
        case NVDRS_DWORD_TYPE:
            LOG_TRACE("    u32CurrentValue: {}, u32PredefinedValue: {}", pSetting->u32CurrentValue,
                      pSetting->u32PredefinedValue);
            break;
        case NVDRS_BINARY_TYPE:
            LOG_TRACE("    binary data");
            break;
        case NVDRS_STRING_TYPE:
            LOG_TRACE("    NVDRS_STRING_TYPE");
            break;
        case NVDRS_WSTRING_TYPE:
        {
            std::wstring wstrCurrentValue(reinterpret_cast<const wchar_t*>(pSetting->wszCurrentValue));
            std::wstring wstrPredefinedValue(reinterpret_cast<const wchar_t*>(pSetting->wszPredefinedValue));

            LOG_TRACE(L"    wszCurrentValue: {}, wszPredefinedValue: {}", wstrCurrentValue, wstrPredefinedValue);
            break;
        }
        }
#endif

        // TODO: maybe check those values and inform if they are being overridden externally

        // const auto dmfgFpsTarget = Config::Instance()->FGDLSSGFramerateTargetDMFG.value_or_default();
        // if (settingId == NGX_DLSSG_MODE_ID && dmfgFpsTarget != 0)
        //{
        //     pSetting->settingId = settingId;
        //     // constexpr auto name = L"NGX_DLSSG_MODE_ID";
        //     // memcpy_s(pSetting->settingName, sizeof(pSetting->settingName), name, sizeof(*name) * wcslen(name));
        //     pSetting->settingType = NVDRS_DWORD_TYPE;
        //     pSetting->isCurrentPredefined = 0;
        //     pSetting->u32CurrentValue = NGX_DLSSG_MODE_DEFAULT;

        //    LOG_DEBUG("Set NGX_DLSSG_MODE_ID to {}", pSetting->u32CurrentValue);
        //}

        // if (settingId == NGX_DLSSG_DYNAMIC_TARGET_FRAME_RATE_ID && dmfgFpsTarget != 0)
        //{
        //     pSetting->settingId = settingId;
        //     // constexpr auto name = L"NGX_DLSSG_DYNAMIC_TARGET_FRAME_RATE_ID";
        //     // memcpy_s(pSetting->settingName, sizeof(pSetting->settingName), name, sizeof(*name) * wcslen(name));
        //     pSetting->settingType = NVDRS_DWORD_TYPE;
        //     pSetting->isCurrentPredefined = 0;
        //     pSetting->u32CurrentValue = dmfgFpsTarget;

        //    LOG_DEBUG("Set NGX_DLSSG_DYNAMIC_TARGET_FRAME_RATE_ID to {}", pSetting->u32CurrentValue);
        //}

        // if (settingId == NGX_DLSSG_DYNAMIC_MULTI_FRAME_COUNT_MAX_ID && dmfgFpsTarget != 0)
        //{
        //     pSetting->settingId = settingId;
        //     // constexpr auto name = L"NGX_DLSSG_DYNAMIC_MULTI_FRAME_COUNT_MAX_ID";
        //     // memcpy_s(pSetting->settingName, sizeof(pSetting->settingName), name, sizeof(*name) * wcslen(name));
        //     pSetting->settingType = NVDRS_DWORD_TYPE;
        //     pSetting->isCurrentPredefined = 0;
        //     pSetting->u32CurrentValue = NGX_DLSSG_DYNAMIC_MULTI_FRAME_COUNT_MAX_DEFAULT;

        //    LOG_DEBUG("Set NGX_DLSSG_DYNAMIC_MULTI_FRAME_COUNT_MAX_ID to {}", pSetting->u32CurrentValue);
        //}

        // Making sure DLSSG is not set to force off
        if (settingId == NGX_DLSSG_MODE_ID)
        {
            if (State::Instance().activeFgOutput == FGOutput::DLSSG)
            {
                pSetting->u32CurrentValue = NGX_DLSSG_MODE_DISABLED;
            }
        }

        if (settingId == NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_ID)
        {
            State::Instance().dlssRenderPresetExternal = pSetting->u32CurrentValue;

            State::Instance().dlssPresetsOverriddenExternally =
                pSetting->u32CurrentValue != NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_OFF;

            // Report no override, we will handle presets from now on
            pSetting->u32CurrentValue = NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_OFF;

            LOG_DEBUG("DLSS External override: {}", State::Instance().dlssRenderPresetExternal);
        }

        if (settingId == NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION_ID)
        {
            State::Instance().dlssdRenderPresetExternal = pSetting->u32CurrentValue;

            State::Instance().dlssdPresetsOverriddenExternally =
                pSetting->u32CurrentValue != NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION_OFF;

            // Report no override, we will handle presets from now on
            pSetting->u32CurrentValue = NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION_OFF;

            LOG_DEBUG("DLSSD External override: {}", State::Instance().dlssdRenderPresetExternal);
        }

        if (settingId == NGX_DLSS_RR_OVERRIDE_SCALING_RATIO_ID || settingId == NGX_DLSS_SR_OVERRIDE_SCALING_RATIO_ID)
        {
            if (Config::Instance()->UpscaleRatioOverrideEnabled.value_or_default())
            {
                auto ratio = Config::Instance()->UpscaleRatioOverrideValue.value_or_default();
                auto ratioPercentage = (uint32_t) std::round(100.f / ratio);

                // Uses the clamp from SR for RR but it should be fine
                ratioPercentage = std::clamp(ratioPercentage, (uint32_t) NGX_DLSS_SR_OVERRIDE_SCALING_RATIO_MIN,
                                             (uint32_t) NGX_DLSS_SR_OVERRIDE_SCALING_RATIO_MAX);

                pSetting->u32CurrentValue = ratioPercentage;
            }
        }
    }

    return result;
}

void* __stdcall NvApiHooks::hkNvAPI_QueryInterface(unsigned int InterfaceId)
{
    if (!o_NvAPI_QueryInterface)
        if (Config::Instance()->UseFakenvapi.value_or_default())
            o_NvAPI_QueryInterface = (PFN_NvApi_QueryInterface) fakenvapi::queryInterface;
        else
            return nullptr;

    auto primaryGpu = IdentifyGpu::getPrimaryGpu();

    // Disable flip metering
    if (InterfaceId == GET_ID(NvAPI_D3D12_SetFlipConfig) &&
        Config::Instance()->DisableFlipMetering.value_or(primaryGpu.vendorId != VendorId::Nvidia))
    {
        LOG_INFO("FlipMetering is disabled!");
        return nullptr;
    }

    if (InterfaceId == GET_ID(NvAPI_D3D_SetSleepMode) || InterfaceId == GET_ID(NvAPI_D3D_Sleep) ||
        InterfaceId == GET_ID(NvAPI_D3D_GetLatency) || InterfaceId == GET_ID(NvAPI_D3D_SetLatencyMarker) ||
        InterfaceId == GET_ID(NvAPI_D3D12_SetAsyncFrameMarker) || InterfaceId == GET_ID(NvAPI_Vulkan_GetLatency) ||
        InterfaceId == GET_ID(NvAPI_Vulkan_SetLatencyMarker) || InterfaceId == GET_ID(NvAPI_Vulkan_SetSleepMode)
#ifdef LOW_LATENCY_INPUTS
        || InterfaceId == GET_ID(NvAPI_D3D_GetSleepStatus)
#endif
    )
    {
#ifdef LOW_LATENCY_INPUTS
        if (InterfaceId == GET_ID(NvAPI_D3D_SetSleepMode))
            return InputReflex::D3D_SetSleepMode;
        if (InterfaceId == GET_ID(NvAPI_D3D_GetSleepStatus))
            return InputReflex::D3D_GetSleepStatus;
        else if (InterfaceId == GET_ID(NvAPI_D3D_Sleep))
            return InputReflex::D3D_Sleep;
        else if (InterfaceId == GET_ID(NvAPI_D3D_GetLatency))
            return InputReflex::D3D_GetLatency;
        else if (InterfaceId == GET_ID(NvAPI_D3D_SetLatencyMarker))
            return InputReflex::D3D_SetLatencyMarker;
        else if (InterfaceId == GET_ID(NvAPI_D3D12_SetAsyncFrameMarker))
            return InputReflex::D3D12_SetAsyncFrameMarker;
#endif

        // LOG_DEBUG("counter: {}, hookReflex()", qiCounter);
        ReflexHooks::hookReflex(o_NvAPI_QueryInterface);
        return ReflexHooks::getHookedReflex(InterfaceId);
    }

    ReflexHooks::hookReflex(o_NvAPI_QueryInterface);

    const auto functionPointer = o_NvAPI_QueryInterface(InterfaceId);

    if (functionPointer)
    {
        if (InterfaceId == GET_ID(NvAPI_GPU_GetArchInfo))
        {
            o_NvAPI_GPU_GetArchInfo = reinterpret_cast<decltype(&NvAPI_GPU_GetArchInfo)>(functionPointer);
            return &hkNvAPI_GPU_GetArchInfo;
        }
        if (InterfaceId == GET_ID(NvAPI_DRS_GetSetting))
        {
            o_NvAPI_DRS_GetSetting = reinterpret_cast<decltype(&NvAPI_DRS_GetSetting)>(functionPointer);
            return &hkNvAPI_DRS_GetSetting;
        }
    }

    // LOG_DEBUG("counter: {} functionPointer: {:X}", qiCounter, (size_t)functionPointer);

    return functionPointer;
}

// Requires HMODULE to make sure nvapi is loaded before calling this function
void NvApiHooks::Hook(HMODULE nvapiModule)
{
    if (o_NvAPI_QueryInterface != nullptr)
        return;

    if (nvapiModule == nullptr)
    {
        LOG_ERROR("Hook called with a nullptr nvapi module");
        return;
    }

    LOG_DEBUG("Trying to hook NvApi");

    o_NvAPI_QueryInterface =
        (PFN_NvApi_QueryInterface) KernelBaseProxy::GetProcAddress_()(nvapiModule, "nvapi_QueryInterface");

    LOG_DEBUG("OriginalNvAPI_QueryInterface = {0:X}", (unsigned long long) o_NvAPI_QueryInterface);

    if (o_NvAPI_QueryInterface != nullptr)
    {
        LOG_INFO("NvAPI_QueryInterface found, hooking!");

        constexpr bool leanMode = true;
        if (fakenvapi::isUsingAsMainNvapi())
            fakenvapi::init(!leanMode);
        else if (State::Instance().activeFgOutput == FGOutput::XeFG)
            fakenvapi::init(leanMode);

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) o_NvAPI_QueryInterface, hkNvAPI_QueryInterface);
        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook NvAPI_QueryInterface: {:X}", detourResult);
            o_NvAPI_QueryInterface = nullptr;
        }
    }
}

void NvApiHooks::Unhook()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_NvAPI_QueryInterface != nullptr)
        DetourDetach(&(PVOID&) o_NvAPI_QueryInterface, hkNvAPI_QueryInterface);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to unhook NvAPI_QueryInterface: {:X}", detourResult);
    }
    else
    {
        o_NvAPI_QueryInterface = nullptr;
    }
}
