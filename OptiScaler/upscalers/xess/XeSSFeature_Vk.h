#pragma once

#include "SysUtils.h"

#include <proxies/XeSS_Proxy.h>
#include <upscalers/IFeature_Vk.h>

#include <xess_vk.h>

class XeSSFeature_Vk : public virtual IFeature_Vk
{
  private:
    std::string _version = "1.3.0";

  protected:
    xess_context_handle_t _xessContext = nullptr;

    uint32_t _xessInitFlags = 0;
    int dumpCount = 0;

    // From IFeature_Vk
    bool InitInternal(VkCommandBuffer InCmdList, NVSDK_NGX_Parameter* InParameters) override;
    bool EvaluateInternal(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters) override;

  public:
    // version is above 1.3 if we can use vulkan
    feature_version Version() final
    {
        return feature_version { XeSSProxy::Version().major, XeSSProxy::Version().minor, XeSSProxy::Version().patch };
    }
    Upscaler GetUpscalerType() const final { return Upscaler::XeSS; }

    bool IsWithDx12() final { return false; }

    XeSSFeature_Vk(unsigned int handleId, NVSDK_NGX_Parameter* InParameters);
    ~XeSSFeature_Vk();
};
