#pragma once
#include "FFXFeature.h"
#include <upscalers/IFeature_Dx12.h>

#include "dx12/ffx_api_dx12.h"
#include "proxies/FfxApi_Proxy.h"
#include <shaders/auto_exposure/AE_Dx12.h>
#include <shaders/color_clamp/CC_Dx12.h>

class FFXFeatureDx12 : public FFXFeature, public IFeature_Dx12
{
  private:
    std::unique_ptr<AE_Dx12> _autoExposure;
    bool _customAutoExposure = false;

    std::unique_ptr<ColorClamp_Dx12> _colorClamp;

    // Custom upscaler dll, nullptr for the regular FFX ones
    CustomFfxModule* _customUpscaler = nullptr;

    // Module running the context, to see which FSR 4 model it picked
    HMODULE _upscalerModule = nullptr;

    NVSDK_NGX_Parameter* SetParameters(NVSDK_NGX_Parameter* InParameters);

    // Clamped copy of the color for FSR 4 models that can't handle non finite input, the color itself otherwise
    ID3D12Resource* ClampColorInput(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InColor);

  protected:
    bool InitFFX(const NVSDK_NGX_Parameter* InParameters);

  public:
    FFXFeatureDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters, const std::string& customUpscaler = "");

    bool InitInternal(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters) override;
    bool EvaluateInternal(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters) override;
    bool QueryProviders(ID3D12Device* device);

    feature_version Version() override { return FFXFeature::Version(); }
    Upscaler GetUpscalerType() const final { return Upscaler::FFX; }
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

    bool IsWithDx12() final { return false; }

    ~FFXFeatureDx12()
    {
        if (State::Instance().isShuttingDown)
            return;

        if (_context != nullptr)
            FfxApiProxy::D3D12_DestroyContext(&_context, NULL);
    }
};
