#pragma once
#include "FSR31Feature.h"
#include <upscalers/IFeature_Vk.h>

#include "vk/ffx_api_vk.h"
#include <proxies/FfxApi_Proxy.h>

class FSR31FeatureVk : public FSR31Feature, public IFeature_Vk
{
  private:
  protected:
    bool InitFSR3(const NVSDK_NGX_Parameter* InParameters);

  public:
    FSR31FeatureVk(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);

    bool Init(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice, VkCommandBuffer InCmdList,
              PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
              NVSDK_NGX_Parameter* InParameters) override;
    bool Evaluate(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters) override;

    feature_version Version() override { return FSR31Feature::Version(); }
    std::string Name() const override { return FSR31Feature::Name(); }

    bool IsWithDx12() final { return false; }

    ~FSR31FeatureVk()
    {
        if (State::Instance().isShuttingDown)
            return;

        if (_context != nullptr)
            FfxApiProxy::VULKAN_DestroyContext(&_context, NULL);
    }
};
