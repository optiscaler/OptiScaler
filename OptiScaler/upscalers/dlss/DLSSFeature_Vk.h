#pragma once
#include <upscalers/IFeature_Vk.h>
#include "DLSSFeature.h"
#include <string>
#include "nvsdk_ngx_vk.h"

class DLSSFeatureVk : public DLSSFeature, public IFeature_Vk
{
  private:
  protected:
  public:
    bool Init(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice, VkCommandBuffer InCmdList,
              PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
              NVSDK_NGX_Parameter* InParameters) override;
    bool Evaluate(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters) override;

    feature_version Version() override { return DLSSFeature::Version(); }
    Upscaler GetUpscalerType() const final { return DLSSFeature::GetUpscalerType(); }
    API Api() const override { return IFeature_Vk::Api(); }

    bool IsWithDx12() override { return false; }

    DLSSFeatureVk(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);
    ~DLSSFeatureVk();
};
