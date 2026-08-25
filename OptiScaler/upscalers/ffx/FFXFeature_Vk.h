#pragma once
#include "FFXFeature.h"
#include <upscalers/IFeature_Vk.h>

#include "vk/ffx_api_vk.h"
#include <proxies/FfxApi_Proxy.h>

class FFXFeatureVk : public FFXFeature, public IFeature_Vk
{
  private:
  protected:
    bool InitFFX(const NVSDK_NGX_Parameter* InParameters);

  public:
    FFXFeatureVk(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);

    bool Init(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice, VkCommandBuffer InCmdList,
              PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
              NVSDK_NGX_Parameter* InParameters) override;
    bool Evaluate(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters) override;

    feature_version Version() override { return FFXFeature::Version(); }
    Upscaler GetUpscalerType() const final { return Upscaler::FFX; }
    API Api() const override { return IFeature_Vk::Api(); }

    bool IsWithDx12() final { return false; }

    ~FFXFeatureVk()
    {
        if (State::Instance().isShuttingDown)
            return;

        if (_context != nullptr)
            FfxApiProxy::VULKAN_DestroyContext()(&_context, NULL);
    }
};
