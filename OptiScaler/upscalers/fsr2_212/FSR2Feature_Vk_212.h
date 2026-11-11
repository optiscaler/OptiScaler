#pragma once

#include <fsr2_212/ffx_fsr2.h>
#include <fsr2_212/vk/ffx_fsr2_vk.h>

#include "FSR2Feature_212.h"
#include <upscalers/IFeature_Vk.h>
#include <shaders/velocity_convert/VC_Vk.h>

class FSR2FeatureVk212 : public FSR2Feature212, public IFeature_Vk
{
  private:
    std::unique_ptr<VelocityConvert_Vk> _velocityConvert = nullptr;

  protected:
    bool InitFSR2(const NVSDK_NGX_Parameter* InParameters);

  public:
    FSR2FeatureVk212(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
        : FSR2Feature212(InHandleId, InParameters), IFeature_Vk(InHandleId, InParameters),
          IFeature(InHandleId, InParameters)
    {
    }

    bool Init(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice, VkCommandBuffer InCmdList,
              PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
              NVSDK_NGX_Parameter* InParameters) override;
    bool Evaluate(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters) override;

    feature_version Version() override { return FSR2Feature212::Version(); }
    std::string Name() const override { return FSR2Feature212::Name(); }

    bool IsWithDx12() override { return false; }
};
