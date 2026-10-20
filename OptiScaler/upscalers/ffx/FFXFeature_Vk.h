#pragma once
#include "FFXFeature.h"
#include <upscalers/IFeature_Vk.h>

#include "vk/ffx_api_vk.h"
#include <proxies/FfxApi_Proxy.h>

class FFXFeatureVk : public FFXFeature, public IFeature_Vk
{
  private:
    // amd_fidelityfx_vk.dll or the driver provided upscaler
    FfxModule* _ffx = nullptr;

    // Last logged inputs of the driver provided FSR 4
    std::string _lastFsr4Inputs;

  protected:
    bool InitFFX(const NVSDK_NGX_Parameter* InParameters);

    // From IFeature_Vk
    bool InitInternal(VkCommandBuffer InCmdList, NVSDK_NGX_Parameter* InParameters) override;
    bool EvaluateInternal(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters) override;

  public:
    FFXFeatureVk(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);

    feature_version Version() override { return FFXFeature::Version(); }
    Upscaler GetUpscalerType() const final { return Upscaler::FFX; }
    API Api() const override { return IFeature_Vk::Api(); }
    std::optional<double> ReadUpscalerTime(void* unused) override { return IFeature_Vk::ReadUpscalerTime(unused); };
    void ReadDetailedGpuTimes(void* unused, std::vector<DetailedGpuTime>& detailedGpuTimes) override
    {
        return IFeature_Vk::ReadDetailedGpuTimes(unused, detailedGpuTimes);
    };

    bool IsWithDx12() final { return false; }

    ~FFXFeatureVk()
    {
        if (State::Instance().isShuttingDown)
            return;

        if (_context != nullptr && _ffx != nullptr)
            _ffx->DestroyContext(&_context, NULL);
    }
};
