#pragma once
#include "FFXFeature.h"
#include <upscalers/IFeature_VkwDx12.h>

#include "dx12/ffx_api_dx12.h"
#include "proxies/FfxApi_Proxy.h"

class FFXFeatureVkOn12 : public FFXFeature, public IFeature_VkwDx12
{
  private:
    bool _baseInit = false;
    NVSDK_NGX_Parameter* SetParameters(NVSDK_NGX_Parameter* InParameters);

  protected:
    bool InitFFX(const NVSDK_NGX_Parameter* InParameters);

  public:
    feature_version Version() override { return FFXFeature::Version(); }
    Upscaler GetUpscalerType() const final { return Upscaler::FFX_on12; }
    API Api() const override { return IFeature_VkwDx12::Api(); }
    bool IsWithDx12() override { return IFeature_VkwDx12::IsWithDx12(); }

    FFXFeatureVkOn12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);

    bool Init(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice, VkCommandBuffer InCmdList,
              PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
              NVSDK_NGX_Parameter* InParameters) override;

    bool Evaluate(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters) override;

    ~FFXFeatureVkOn12()
    {
        if (State::Instance().isShuttingDown)
            return;

        if (VulkanDevice)
            vkDeviceWaitIdle(VulkanDevice);

        if (_context != nullptr)
            FfxApiProxy::D3D12_DestroyContext(&_context, NULL);
    }
};
