#pragma once
#include "DLSSFeature.h"
#include <upscalers/IFeature_Dx12.h>
#include <shaders/rcas/RCAS_Dx12.h>
#include <string>

class DLSSFeatureDx12 : public DLSSFeature, public IFeature_Dx12
{
  private:
    // Custom NGX runtime, nullptr for NVIDIA's
    CustomNgxModule* _custom = nullptr;

    // Functions of the NGX runtime this runs on
    PFN_D3D12_CreateFeature CreateFeature() const;
    PFN_D3D12_EvaluateFeature EvaluateFeature() const;
    PFN_D3D12_ReleaseFeature ReleaseFeature() const;

  protected:
    bool InitDLSS(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters);

  public:
    bool InitInternal(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters) override;
    bool EvaluateInternal(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters) override;

    void Shutdown(ID3D12Device* InDevice);

    feature_version Version() override { return DLSSFeature::Version(); }
    Upscaler GetUpscalerType() const final { return DLSSFeature::GetUpscalerType(); }
    API Api() const override { return IFeature_Dx12::Api(); }
    bool CallsUpscalerEndByItself() override { return IFeature_Dx12::CallsUpscalerEndByItself(); }
    std::optional<double> ReadUpscalerTime(void* commandQueue) override
    {
        return IFeature_Dx12::ReadUpscalerTime(commandQueue);
    };
    void ReadDetailedGpuTimes(void* commandQueue, std::vector<DetailedGpuTime>& detailedGpuTimes) override
    {
        return IFeature_Dx12::ReadDetailedGpuTimes(commandQueue, detailedGpuTimes);
    };

    bool IsWithDx12() override { return false; }

    DLSSFeatureDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters, const std::string& customUpscaler = "");
    ~DLSSFeatureDx12();
};
