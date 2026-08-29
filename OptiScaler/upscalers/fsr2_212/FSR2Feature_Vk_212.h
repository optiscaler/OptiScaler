#pragma once

#include <fsr2_212/ffx_fsr2.h>
#include <fsr2_212/vk/ffx_fsr2_vk.h>

#include "FSR2Feature_212.h"
#include <upscalers/IFeature_Vk.h>

class FSR2FeatureVk212 : public FSR2Feature212, public IFeature_Vk
{
  private:
  protected:
    bool InitFSR2(const NVSDK_NGX_Parameter* InParameters);

    // From IFeature_Vk
    bool InitInternal(VkCommandBuffer InCmdList, NVSDK_NGX_Parameter* InParameters) override;
    bool EvaluateInternal(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters) override;

  public:
    FSR2FeatureVk212(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
        : FSR2Feature212(InHandleId, InParameters), IFeature_Vk(InHandleId, InParameters),
          IFeature(InHandleId, InParameters)
    {
    }

    feature_version Version() override { return FSR2Feature212::Version(); }
    Upscaler GetUpscalerType() const final { return Upscaler::FSR21; }
    API Api() const override { return IFeature_Vk::Api(); }

    bool IsWithDx12() override { return false; }
};
