#pragma once
#include <vulkan/vulkan.hpp>
#include "IFeature.h"

#include <shaders/rcas/RCAS_Vk.h>
#include <shaders/output_scaling/OS_Vk.h>
#include <shaders/magnifier/Magnifier_Vk.h>

class IFeature_Vk : public virtual IFeature
{
  private:
    struct ShaderPass
    {
        // Requests the target image it needs to write to. Returns the image the PREVIOUS stage must write to.
        std::function<VkImageInfo(const VkImageInfo& nextOutput)> Setup;

        // Runs the shader
        std::function<bool(const VkImageInfo& input, const VkImageInfo& output)> Dispatch;

        // Internal state tracked by the pipeline setup loop
        VkImageInfo inputBuffer {};
        VkImageInfo outputBuffer {};
    };

  protected:
    VkInstance Instance = nullptr;
    VkPhysicalDevice PhysicalDevice = nullptr;
    VkDevice Device = nullptr;
    PFN_vkGetInstanceProcAddr GIPA = nullptr;
    PFN_vkGetDeviceProcAddr GDPA = nullptr;

    std::unique_ptr<OS_Vk> OutputScaler = nullptr;
    std::unique_ptr<RCAS_Vk> RCAS = nullptr;
    std::unique_ptr<Magnifier_Vk> Magnifier = nullptr;

    virtual bool InitInternal(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters) = 0;
    virtual bool EvaluateInternal(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters) = 0;

  public:
    virtual bool Init(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice, VkCommandBuffer InCmdBuffer,
                      PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                      NVSDK_NGX_Parameter* InParameters);
    virtual bool Evaluate(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters);

    IFeature_Vk(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters) : IFeature(InHandleId, InParameters) {}

    bool IsWithDx12() override { return false; }
    API Api() const override { return API::Vulkan; }

    virtual ~IFeature_Vk() {}
};
