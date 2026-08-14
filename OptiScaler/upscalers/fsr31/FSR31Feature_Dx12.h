#pragma once
#include "FSR31Feature.h"
#include <upscalers/IFeature_Dx12.h>

#include "dx12/ffx_api_dx12.h"
#include "proxies/FfxApi_Proxy.h"

class FSR31FeatureDx12 : public FSR31Feature, public IFeature_Dx12
{
  private:
    ID3D12Resource* smallerColor[2];

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

        if (smallerColor[0])
        {
            smallerColor[0]->Release();
            smallerColor[0] = nullptr;
        }
        if (smallerColor[1])
        {
            smallerColor[1]->Release();
            smallerColor[1] = nullptr;
        }
    }
};
