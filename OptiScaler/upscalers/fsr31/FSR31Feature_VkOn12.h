#pragma once
#include "FSR31Feature.h"
#include <upscalers/IFeature_VkwDx12.h>

#include "dx12/ffx_api_dx12.h"
#include "proxies/FfxApi_Proxy.h"

class FSR31FeatureVkOn12 : public FSR31Feature, public IFeature_VkwDx12
{
  private:
    bool _baseInit = false;
    NVSDK_NGX_Parameter* SetParameters(NVSDK_NGX_Parameter* InParameters);

  protected:
    bool InitFSR3(const NVSDK_NGX_Parameter* InParameters);

  public:
    std::string Name() const { return "FSR3 w/Dx12"; }
    feature_version Version() override { return FSR31Feature::Version(); }

    FSR31FeatureVkOn12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);

    bool Init(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice, VkCommandBuffer InCmdList,
              PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
              NVSDK_NGX_Parameter* InParameters) override;

    bool Evaluate(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters) override;

    bool IsWithDx12() final { return true; }

    ~FSR31FeatureVkOn12()
    {
        if (State::Instance().isShuttingDown)
            return;

        if (VulkanDevice)
            vkDeviceWaitIdle(VulkanDevice);

        FinishPendingWork();

        if (_context != nullptr)
            FfxApiProxy::D3D12_DestroyContext(&_context, NULL);
    }
};
