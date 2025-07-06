#pragma once
#include <vulkan/vulkan.hpp>
#include <upscalers/IFeature.h>
#include <proxies/XeSS_Proxy.h>

#include <string>
#include <variant>

inline static std::string ResultToString(xess_result_t result)
{
    switch (result)
    {
    case XESS_RESULT_WARNING_NONEXISTING_FOLDER:
        return "Warning Nonexistent Folder";
    case XESS_RESULT_WARNING_OLD_DRIVER:
        return "Warning Old Driver";
    case XESS_RESULT_SUCCESS:
        return "Success";
    case XESS_RESULT_ERROR_UNSUPPORTED_DEVICE:
        return "Unsupported Device";
    case XESS_RESULT_ERROR_UNSUPPORTED_DRIVER:
        return "Unsupported Driver";
    case XESS_RESULT_ERROR_UNINITIALIZED:
        return "Uninitialized";
    case XESS_RESULT_ERROR_INVALID_ARGUMENT:
        return "Invalid Argument";
    case XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY:
        return "Device Out of Memory";
    case XESS_RESULT_ERROR_DEVICE:
        return "Device Error";
    case XESS_RESULT_ERROR_NOT_IMPLEMENTED:
        return "Not Implemented";
    case XESS_RESULT_ERROR_INVALID_CONTEXT:
        return "Invalid Context";
    case XESS_RESULT_ERROR_OPERATION_IN_PROGRESS:
        return "Operation in Progress";
    case XESS_RESULT_ERROR_UNSUPPORTED:
        return "Unsupported";
    case XESS_RESULT_ERROR_CANT_LOAD_LIBRARY:
        return "Cannot Load Library";
    case XESS_RESULT_ERROR_UNKNOWN:
    default:
        return "Unknown";
    }
}

struct D3D11Context
{
    ID3D11DeviceContext* d3d11Context;
    ID3D11Device* d3d11Device;
};

struct D3D12Context
{
    ID3D12Device* d3d12Device;
    ID3D12GraphicsCommandList* d3d12CommandList;
};

struct VulkanContext
{
    vk::Instance* vkInstance;
    vk::PhysicalDevice* vkPhysicalDevice;
    vk::Device* vkDevice;
    vk::CommandBuffer* vkCommandBuffer;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
};

using ApiContext = std::variant<D3D11Context, D3D12Context, VulkanContext>;

using EvalContext = std::variant<ID3D11DeviceContext*, ID3D12GraphicsCommandList*, vk::CommandBuffer*>;

using XessInitParams = std::variant<xess_d3d11_init_params_t, xess_d3d12_init_params_t, xess_vk_init_params_t>;

class XeSSFeature : public virtual IFeature
{
  protected:
    xess_context_handle_t _xessContext = nullptr;

    int dumpCount = 0;

    bool InitXeSS(ID3D12Device* device, const NVSDK_NGX_Parameter* InParameters);

  public:
    feature_version Version()
    {
        return feature_version { XeSSProxy::Version().major, XeSSProxy::Version().minor, XeSSProxy::Version().patch };
    }
    std::string Name() const { return "XeSS"; }

    XeSSFeature(unsigned int handleId, NVSDK_NGX_Parameter* InParameters);

    bool Init(ApiContext* context, const NVSDK_NGX_Parameter* InParameters);

    bool Evaluate(EvalContext* context, const NVSDK_NGX_Parameter* InParameters,
                  const NVSDK_NGX_Parameter* OutParameters);

    uint32_t GetInitFlags();

    xess_quality_settings_t GetQualitySetting();

    virtual XessInitParams CreateInitParams(xess_2d_t outputResolution, xess_quality_settings_t qualitySetting, uint32_t initFlags) = 0;

    virtual bool CheckInitializationContext(ApiContext* context) = 0;

    virtual xess_result_t CreateXessContext(ApiContext* context, xess_context_handle_t* pXessContext) = 0;

    virtual xess_result_t ApiInit(ApiContext* context, XessInitParams* xessInitParams) = 0;

    virtual void InitMenuAndOutput(ApiContext* context) = 0;

    ~XeSSFeature();
};
