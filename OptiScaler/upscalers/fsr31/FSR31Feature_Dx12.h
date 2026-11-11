#pragma once
#include "FSR31Feature.h"
#include <upscalers/IFeature_Dx12.h>

#include "dx12/ffx_api_dx12.h"
#include "proxies/FfxApi_Proxy.h"
#include <shaders/auto_exposure/AE_Dx12.h>

class FSR31FeatureDx12 : public FSR31Feature, public IFeature_Dx12
{
  private:
    std::unique_ptr<AE_Dx12> _autoExposure;
    bool _customAutoExposure = false;

    NVSDK_NGX_Parameter* SetParameters(NVSDK_NGX_Parameter* InParameters);

  protected:
    bool InitFSR3(const NVSDK_NGX_Parameter* InParameters);

  public:
    FSR31FeatureDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);

    bool Init(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCommandList,
              NVSDK_NGX_Parameter* InParameters) override;
    bool Evaluate(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters) override;

    feature_version Version() override { return FSR31Feature::Version(); }
    std::string Name() const override { return FSR31Feature::Name(); }

    bool IsWithDx12() final { return false; }

    ~FSR31FeatureDx12()
    {
        if (State::Instance().isShuttingDown)
            return;

        if (_context != nullptr)
            FfxApiProxy::D3D12_DestroyContext(&_context, NULL);
    }
};
