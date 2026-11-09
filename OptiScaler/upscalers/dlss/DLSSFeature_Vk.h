#pragma once
#include <upscalers/IFeature_Vk.h>
#include "DLSSFeature.h"
#include <string>
#include "nvsdk_ngx_vk.h"

class DLSSFeatureVk : public DLSSFeature, public IFeature_Vk
{
  private:
    // Custom NGX runtime, nullptr for NVIDIA's
    CustomNgxModule* _custom = nullptr;

    // Functions of the NGX runtime this runs on
    PFN_VULKAN_CreateFeature CreateFeature() const;
    PFN_VULKAN_EvaluateFeature EvaluateFeature() const;
    PFN_VULKAN_ReleaseFeature ReleaseFeature() const;

  protected:
    // From IFeature_Vk
    bool InitInternal(VkCommandBuffer InCmdList, NVSDK_NGX_Parameter* InParameters) override;
    bool EvaluateInternal(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters) override;

  public:
    feature_version Version() override { return DLSSFeature::Version(); }
    Upscaler GetUpscalerType() const final { return DLSSFeature::GetUpscalerType(); }
    API Api() const override { return IFeature_Vk::Api(); }
    std::optional<double> ReadUpscalerTime(void* unused) override { return IFeature_Vk::ReadUpscalerTime(unused); };
    void ReadDetailedGpuTimes(void* unused, std::vector<DetailedGpuTime>& detailedGpuTimes) override
    {
        return IFeature_Vk::ReadDetailedGpuTimes(unused, detailedGpuTimes);
    };

    bool IsWithDx12() override { return false; }

    DLSSFeatureVk(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters, const std::string& customUpscaler = "");
    ~DLSSFeatureVk();
};
