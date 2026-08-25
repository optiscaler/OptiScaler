#pragma once
#include "IFGNvngx.h"
#include <ffx_framegeneration.h>
#include <d3d12.h>
#include <proxies/FfxApi_Proxy.h>

struct ffxContext_wrap
{
    ffxContext ctx = nullptr;

    ~ffxContext_wrap()
    {
        if (!ctx)
            return;

        auto retCode = FfxApiProxy::D3D12_DestroyContext(&ctx, nullptr);

        if (retCode != FFX_API_RETURN_OK)
            LOG_WARN("Could destroy FFX context");
    }
};

struct Nvngx_FFX_Handle
{
    unsigned int Id;
    std::unique_ptr<ffxContext_wrap> fgContext {};
    ID3D12Device* device = nullptr;

    uint32_t swapchainWidth = 0;
    uint32_t swapchainHeight = 0;
    uint64_t lastFrameId = 0;

    // for HDR
    bool hdrRangeSet = false;
    float hdrMinLuminance = 0.0001f;
    float hdrMaxLuminance = 1000.0f;
};

class Nvngx_FFX : public IFGNvngx
{
  private:
    static inline std::atomic_uint32_t lastIdCreated = 0;
    static inline bool inited = false;

    static inline ID3D12Device* initDevice = nullptr;

    static bool Init();

  public:
    bool isDx12Available() override final { return Init(); };
    bool isVulkanAvailable() override final { return false; };

    // DX12
    NVSDK_NGX_Result D3D12_Init(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                ID3D12Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                NVSDK_NGX_Version InSDKVersion) override;

    NVSDK_NGX_Result D3D12_Init_Ext(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                    ID3D12Device* InDevice, NVSDK_NGX_Version InSDKVersion,
                                    const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo) override;

    NVSDK_NGX_Result D3D12_Shutdown() override;

    NVSDK_NGX_Result D3D12_Shutdown1(ID3D12Device* InDevice) override;

    NVSDK_NGX_Result D3D12_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId, const NVSDK_NGX_Parameter* InParameters,
                                                size_t* OutSizeInBytes) override;

    NVSDK_NGX_Result D3D12_CreateFeature(ID3D12GraphicsCommandList* InCmdList, NVSDK_NGX_Feature InFeatureID,
                                         NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle) override;

    NVSDK_NGX_Result D3D12_ReleaseFeature(NVSDK_NGX_Handle* InHandle) override;

    NVSDK_NGX_Result D3D12_GetFeatureRequirements(IDXGIAdapter* Adapter,
                                                  const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                                  NVSDK_NGX_FeatureRequirement* OutSupported) override;

    NVSDK_NGX_Result D3D12_EvaluateFeature(ID3D12GraphicsCommandList* InCmdList,
                                           const NVSDK_NGX_Handle* InFeatureHandle, NVSDK_NGX_Parameter* InParameters,
                                           PFN_NVSDK_NGX_ProgressCallback InCallback) override;

    NVSDK_NGX_Result D3D12_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters) override;

    int getMaxFakeFramesCount() override { return 1; }
    FGNvngxReplacement getType() override { return FGNvngxReplacement::FFX; }
};
