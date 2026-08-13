#pragma once
#include "IFGNvngx.h"
#include "Nvngx_FFX.h"
#include "Nvngx_Arturs.h"
#include <d3d12.h>

struct Nvngx_Combo_Handle
{
    unsigned int Id;

    NVSDK_NGX_Handle* ffxHandle = nullptr;
    NVSDK_NGX_Handle* artursHandle = nullptr;
};

class Nvngx_Combo : public IFGNvngx
{
  private:
    std::atomic_uint32_t lastIdCreated = 0;

    std::unique_ptr<Nvngx_Arturs> artursProvider = nullptr;
    std::unique_ptr<Nvngx_FFX> ffxProvider = nullptr;

  public:
    Nvngx_Combo()
    {
        artursProvider = std::make_unique<Nvngx_Arturs>();
        ffxProvider = std::make_unique<Nvngx_FFX>();
    }

    bool isDx12Available() override final
    {
        return artursProvider->isDx12Available() && ffxProvider->isDx12Available();
    };
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

    int getMaxFakeFramesCount() override { return 5; }
    FGNvngxReplacement getType() override { return FGNvngxReplacement::Combo; }
    feature_version version() override;
    feature_version extraVersion() override;
};
