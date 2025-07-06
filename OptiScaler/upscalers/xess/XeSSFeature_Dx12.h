#pragma once
#include "XeSSFeature.h"

#include <upscalers/IFeature_Dx12.h>

class XeSSFeatureDx12 : public XeSSFeature, public IFeature_Dx12
{
  private:
    ID3D12PipelineLibrary* _localPipeline = nullptr;
    ID3D12Heap* _localBufferHeap = nullptr;
    ID3D12Heap* _localTextureHeap = nullptr;

  protected:
  public:
    std::string Name() const { return "XeSS"; }

    XeSSFeatureDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
        : IFeature(InHandleId, InParameters), IFeature_Dx12(InHandleId, InParameters),
          XeSSFeature(InHandleId, InParameters)
    {
        if (XeSSProxy::Module() == nullptr && XeSSProxy::InitXeSS())
            XeSSProxy::HookXeSS();

        _moduleLoaded = XeSSProxy::Module() != nullptr && XeSSProxy::D3D12CreateContext() != nullptr;
    }

    bool Evaluate(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters) override;

    ~XeSSFeatureDx12();

    bool CheckInitializationContext(ApiContext* context) override;
    xess_result_t CreateXessContext(ApiContext* context, xess_context_handle_t* pXessContext) override;
    xess_result_t ApiInit(ApiContext* context, XessInitParams* xessInitParams) override;
    XessInitParams CreateInitParams(xess_2d_t outputResolution, xess_quality_settings_t qualitySetting,
                                    uint32_t initFlags) override;
    void InitMenuAndOutput(ApiContext* context) override;

    //Hotfix until using the real init method works directly
    bool Init(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCommandList,
              NVSDK_NGX_Parameter* InParameters) override;
};
