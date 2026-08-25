#pragma once

#include <fsr2_212/ffx_fsr2.h>
#include <fsr2_212/dx12/ffx_fsr2_dx12.h>

#include "FSR2Feature_212.h"
#include <upscalers/IFeature_VkwDx12.h>

class FSR2FeatureVkOnDx12_212 : public FSR2Feature212, public IFeature_VkwDx12
{
  private:
    bool _baseInit = false;
    NVSDK_NGX_Parameter* SetParameters(NVSDK_NGX_Parameter* InParameters);

  protected:
    bool InitFSR2(const NVSDK_NGX_Parameter* InParameters);

  public:
    FSR2FeatureVkOnDx12_212(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
        : FSR2Feature212(InHandleId, InParameters), IFeature_VkwDx12(InHandleId, InParameters),
          IFeature_Vk(InHandleId, InParameters), IFeature(InHandleId, InParameters)
    {
    }

    bool Init(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice, VkCommandBuffer InCmdList,
              PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
              NVSDK_NGX_Parameter* InParameters) override;
    bool Evaluate(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters) override;

    feature_version Version() override { return FSR2Feature212::Version(); }
    Upscaler GetUpscalerType() const final { return Upscaler::FSR21_on12; }
    API Api() const override { return IFeature_VkwDx12::Api(); }
    bool IsWithDx12() override { return IFeature_VkwDx12::IsWithDx12(); }
};
