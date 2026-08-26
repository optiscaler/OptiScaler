#pragma once
#include <upscalers/IFeature_Dx11.h>
#include "FSR2Feature.h"

#include <dxgi1_4.h>

#include <fsr2/ffx_fsr2.h>
#include <fsr2/dx11/ffx_fsr2_dx11.h>

class FSR2FeatureDx11 : public FSR2Feature, public IFeature_Dx11
{
  private:
    using D3D11_TEXTURE2D_DESC_C = struct D3D11_TEXTURE2D_DESC_C
    {
        UINT Width;
        UINT Height;
        DXGI_FORMAT Format;
        UINT BindFlags;
    };

    using D3D11_TEXTURE2D_RESOURCE_C = struct D3D11_TEXTURE2D_RESOURCE_C
    {
        D3D11_TEXTURE2D_DESC_C Desc = {};
        ID3D11Texture2D* Texture = nullptr;
        bool usingOriginal = false;
    };

    D3D11_TEXTURE2D_RESOURCE_C bufferColor = {};
    D3D11_TEXTURE2D_RESOURCE_C bufferDepth = {};
    D3D11_TEXTURE2D_RESOURCE_C bufferVelocity = {};

    bool CopyTexture(ID3D11Resource* InResource, D3D11_TEXTURE2D_RESOURCE_C* OutTextureDesc, UINT bindFlags,
                     bool InCopy);
    void ReleaseResources();

  protected:
    bool InitFSR2(const NVSDK_NGX_Parameter* InParameters) override;

  public:
    FSR2FeatureDx11(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);

    // Inherited via IFeature_Dx11
    bool InitInternal(ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters) override;
    bool EvaluateInternal(ID3D11DeviceContext* DeviceContext, NVSDK_NGX_Parameter* InParameters) override;

    feature_version Version() override { return FSR2Feature::Version(); }
    Upscaler GetUpscalerType() const final { return Upscaler::FSR22; }
    API Api() const override { return IFeature_Dx11::Api(); }
    bool CallsUpscalerEndByItself() override { return IFeature_Dx11::CallsUpscalerEndByItself(); }

    bool IsWithDx12() override { return false; }

    ~FSR2FeatureDx11();
};
