#include "pch.h"
#include "Nvngx_Combo.h"

#include <NVNGX_Parameter.h>

#include "proxies/NVNGX_Proxy.h"
#include "proxies/Ntdll_Proxy.h"
#include <proxies/FfxApi_Proxy.h>
#include <numbers>
#include <misc/IdentifyGpu.h>

using Microsoft::WRL::ComPtr;

NVSDK_NGX_Result Nvngx_Combo::D3D12_Init(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                         ID3D12Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                         NVSDK_NGX_Version InSDKVersion)
{
    return D3D12_Init_Ext(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion, InFeatureInfo);
}

NVSDK_NGX_Result Nvngx_Combo::D3D12_Init_Ext(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                             ID3D12Device* InDevice, NVSDK_NGX_Version InSDKVersion,
                                             const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    auto resultArturs =
        artursProvider->D3D12_Init_Ext(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion, InFeatureInfo);
    auto resultFfx =
        ffxProvider->D3D12_Init_Ext(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion, InFeatureInfo);

    if (resultArturs != NVSDK_NGX_Result_Success || resultFfx != NVSDK_NGX_Result_Success)
    {
        return NVSDK_NGX_Result_Fail;
    }

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_Result Nvngx_Combo::D3D12_Shutdown()
{
    auto resultArturs = artursProvider->D3D12_Shutdown();
    auto resultFfx = ffxProvider->D3D12_Shutdown();

    if (resultArturs != NVSDK_NGX_Result_Success || resultFfx != NVSDK_NGX_Result_Success)
    {
        return NVSDK_NGX_Result_Fail;
    }

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_Result Nvngx_Combo::D3D12_Shutdown1(ID3D12Device* InDevice)
{
    auto resultArturs = artursProvider->D3D12_Shutdown1(InDevice);
    auto resultFfx = ffxProvider->D3D12_Shutdown1(InDevice);

    if (resultArturs != NVSDK_NGX_Result_Success || resultFfx != NVSDK_NGX_Result_Success)
    {
        return NVSDK_NGX_Result_Fail;
    }

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_Result Nvngx_Combo::D3D12_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId,
                                                         const NVSDK_NGX_Parameter* InParameters,
                                                         size_t* OutSizeInBytes)
{
    if (!OutSizeInBytes)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    *OutSizeInBytes = 0;

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_Result Nvngx_Combo::D3D12_CreateFeature(ID3D12GraphicsCommandList* InCmdList, NVSDK_NGX_Feature InFeatureID,
                                                  NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle)
{
    Nvngx_Combo_Handle** OutOurHandle = (Nvngx_Combo_Handle**) OutHandle;

    if (!OutOurHandle || !InParameters)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    if (InFeatureID != NVSDK_NGX_Feature_FrameGeneration)
        return NVSDK_NGX_Result_FAIL_FeatureNotSupported;

    *OutOurHandle = new Nvngx_Combo_Handle(lastIdCreated++ + NVNGX_PROVIDER_ID_OFFSET, nullptr);

    auto resultArturs =
        artursProvider->D3D12_CreateFeature(InCmdList, InFeatureID, InParameters, &(*OutOurHandle)->artursHandle);
    auto resultFfx =
        ffxProvider->D3D12_CreateFeature(InCmdList, InFeatureID, InParameters, &(*OutOurHandle)->ffxHandle);

    if (resultArturs != NVSDK_NGX_Result_Success || resultFfx != NVSDK_NGX_Result_Success)
    {
        if (resultArturs == NVSDK_NGX_Result_Success)
            artursProvider->D3D12_ReleaseFeature((*OutOurHandle)->artursHandle);

        if (resultFfx == NVSDK_NGX_Result_Success)
            ffxProvider->D3D12_ReleaseFeature((*OutOurHandle)->ffxHandle);

        delete *OutOurHandle;
        *OutOurHandle = nullptr;

        return NVSDK_NGX_Result_Fail;
    }

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_Result Nvngx_Combo::D3D12_ReleaseFeature(NVSDK_NGX_Handle* InHandle)
{
    Nvngx_Combo_Handle* InOurHandle = (Nvngx_Combo_Handle*) InHandle;

    if (!InOurHandle || InOurHandle->Id < NVNGX_PROVIDER_ID_OFFSET)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    auto resultArturs = artursProvider->D3D12_ReleaseFeature(InOurHandle->artursHandle);
    auto resultFfx = ffxProvider->D3D12_ReleaseFeature(InOurHandle->ffxHandle);

    delete InOurHandle;

    if (resultArturs != NVSDK_NGX_Result_Success || resultFfx != NVSDK_NGX_Result_Success)
    {
        return NVSDK_NGX_Result_Fail;
    }

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_Result Nvngx_Combo::D3D12_GetFeatureRequirements(IDXGIAdapter* Adapter,
                                                           const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                                           NVSDK_NGX_FeatureRequirement* OutSupported)
{
    if (!OutSupported)
        return NVSDK_NGX_Result_Fail;

    OutSupported->FeatureSupported = NVSDK_NGX_FeatureSupportResult_Supported;
    OutSupported->MinHWArchitecture = 0x0;
    strcpy_s(OutSupported->MinOSVersion, "10.0.0");

    return NVSDK_NGX_Result_Success;
}

static D3D12_RESOURCE_STATES GetD3D12State(FfxApiResourceState state)
{
    switch (state)
    {
    case FFX_API_RESOURCE_STATE_COMMON:
        return D3D12_RESOURCE_STATE_COMMON;
    case FFX_API_RESOURCE_STATE_UNORDERED_ACCESS:
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case FFX_API_RESOURCE_STATE_COMPUTE_READ:
        return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case FFX_API_RESOURCE_STATE_PIXEL_READ:
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ:
        return (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    case FFX_API_RESOURCE_STATE_COPY_SRC:
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case FFX_API_RESOURCE_STATE_COPY_DEST:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    case FFX_API_RESOURCE_STATE_GENERIC_READ:
        return D3D12_RESOURCE_STATE_GENERIC_READ;
    case FFX_API_RESOURCE_STATE_INDIRECT_ARGUMENT:
        return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    case FFX_API_RESOURCE_STATE_RENDER_TARGET:
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    default:
        return D3D12_RESOURCE_STATE_COMMON;
    }
}

static void CopyTexture(ID3D12GraphicsCommandList* CommandList, const FfxApiResource* Destination,
                        const FfxApiResource* Source)
{
    const auto cmdList12 = reinterpret_cast<ID3D12GraphicsCommandList*>(CommandList);

    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[0].Transition.pResource = static_cast<ID3D12Resource*>(Destination->resource); // Destination
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[0].Transition.StateBefore = GetD3D12State((FfxApiResourceState) Destination->state);
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;

    barriers[1] = barriers[0];
    barriers[1].Transition.pResource = static_cast<ID3D12Resource*>(Source->resource); // Source
    barriers[1].Transition.StateBefore = GetD3D12State((FfxApiResourceState) Source->state);
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;

    cmdList12->ResourceBarrier(2, barriers);
    cmdList12->CopyResource(barriers[0].Transition.pResource, barriers[1].Transition.pResource);
    std::swap(barriers[0].Transition.StateBefore, barriers[0].Transition.StateAfter);
    std::swap(barriers[1].Transition.StateBefore, barriers[1].Transition.StateAfter);
    cmdList12->ResourceBarrier(2, barriers);
}

NVSDK_NGX_Result Nvngx_Combo::D3D12_EvaluateFeature(ID3D12GraphicsCommandList* InCmdList,
                                                    const NVSDK_NGX_Handle* InFeatureHandle,
                                                    NVSDK_NGX_Parameter* InParameters,
                                                    PFN_NVSDK_NGX_ProgressCallback InCallback)
{
    // This is our handle so can cast away const
    Nvngx_Combo_Handle* InOurHandle = (Nvngx_Combo_Handle*) InFeatureHandle;

    if (!InParameters || !InOurHandle || InOurHandle->Id < NVNGX_PROVIDER_ID_OFFSET)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    // Assuming ffx can only do one fake frame
    uint32_t frameIndex = 1;
    InParameters->Get("DLSSG.MultiFrameIndex", &frameIndex);

    uint32_t frameCount = 0;
    InParameters->Get("DLSSG.MultiFrameCount", &frameCount);

    NVSDK_NGX_Result result {};

    if ((frameCount == 1 && frameIndex == 1) || (frameCount == 3 && frameIndex == 2) ||
        (frameCount == 5 && frameIndex == 3))
    {
        // Make the provider think we only want 2x to it generates the middle frame
        InParameters->Set("DLSSG.MultiFrameIndex", 1);
        InParameters->Set("DLSSG.MultiFrameCount", 1);

        result = ffxProvider->D3D12_EvaluateFeature(InCmdList, InOurHandle->ffxHandle, InParameters, InCallback);

        // Restore
        InParameters->Set("DLSSG.MultiFrameIndex", frameIndex);
        InParameters->Set("DLSSG.MultiFrameCount", frameCount);
    }
    else
    {
        result = artursProvider->D3D12_EvaluateFeature(InCmdList, InOurHandle->artursHandle, InParameters, InCallback);
    }

    ID3D12Resource* outputReal = nullptr;
    InParameters->Get("DLSSG.OutputReal", &outputReal);
    auto outputRealFfx = ffxApiGetResourceDX12(outputReal, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);

    // Copy backbuffer to output real
    if (outputReal)
    {
        ID3D12Resource* backbuffer = nullptr;
        InParameters->Get("DLSSG.Backbuffer", &backbuffer);
        auto backbufferFfx = ffxApiGetResourceDX12(backbuffer, FFX_API_RESOURCE_STATE_COMPUTE_READ);

        if (backbuffer)
            CopyTexture(InCmdList, &outputRealFfx, &backbufferFfx);
    }

    return result;
}

static NVSDK_NGX_Result GetCurrentSettingsCallback(Nvngx_Combo_Handle* InHandle, NVSDK_NGX_Parameter* InParameters)
{
    if (!InHandle || !InParameters)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    InParameters->Set("DLSSG.MustCallEval", 1);
    InParameters->Set("DLSSG.BurstCaptureRunning", 0);

    return NVSDK_NGX_Result_Success;
}

static NVSDK_NGX_Result EstimateVRAMCallback(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                             uint32_t, uint32_t, size_t* EstimatedSize)
{
    // Assume 300MB
    if (EstimatedSize)
        *EstimatedSize = 300 * 1024 * 1024;

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_Result Nvngx_Combo::D3D12_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters)
{
    if (!InParameters)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    InParameters->Set("DLSSG.GetCurrentSettingsCallback", &GetCurrentSettingsCallback);
    InParameters->Set("DLSSG.EstimateVRAMCallback", &EstimateVRAMCallback);

    // if (inited) // Query FFX
    InParameters->Set("DLSSG.MultiFrameCountMax", 1);

    return NVSDK_NGX_Result_Success;
}
