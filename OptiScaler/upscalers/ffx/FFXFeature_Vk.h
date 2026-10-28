#pragma once
#include "FFXFeature.h"
#include <upscalers/IFeature_Vk.h>

#include "vk/ffx_api_vk.h"
#include <proxies/FfxApi_Proxy.h>
#include <shaders/velocity_convert/VC_Vk.h>

class FFXFeatureVk : public FFXFeature, public IFeature_Vk
{
  private:
    // FFX SDK dll, the driver provided upscaler or the custom upscaler's dll
    FfxModule* _ffx = nullptr;

    // Custom upscaler dll, nullptr for the regular FFX ones
    CustomFfxModule* _customUpscaler = nullptr;

    std::unique_ptr<VelocityConvert_Vk> _velocityConvert = nullptr;

    // Last logged inputs of the driver provided FSR 4
    std::string _lastFsr4Inputs;

  protected:
    bool InitFFX(const NVSDK_NGX_Parameter* InParameters);

    // From IFeature_Vk
    bool InitInternal(VkCommandBuffer InCmdList, NVSDK_NGX_Parameter* InParameters) override;
    bool EvaluateInternal(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters) override;

  public:
    FFXFeatureVk(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters, const std::string& customUpscaler = "");

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
