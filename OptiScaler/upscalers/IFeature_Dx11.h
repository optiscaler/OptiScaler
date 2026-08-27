#pragma once
#include "IFeature.h"

#include <menu/menu_dx11.h>
#include <shaders/rcas/RCAS_Dx11.h>
#include <shaders/output_scaling/OS_Dx11.h>
#include <shaders/bias/Bias_Dx11.h>
#include <shaders/magnifier/Magnifier_Dx11.h>
#include <gpu_time/GpuTime_Dx11.h>

class IFeature_Dx11 : public virtual IFeature
{
  private:
    struct ShaderPass
    {
        // Requests the target buffer it needs to write to. Returns the buffer the PREVIOUS stage must write to
        std::function<ID3D11Resource*(ID3D11Resource* nextOutput)> Setup;

        // Runs the shader
        std::function<bool(ID3D11Resource* input, ID3D11Resource* output)> Dispatch;

        // Internal state tracked by the pipeline setup loop
        ID3D11Resource* inputBuffer = nullptr;
        ID3D11Resource* outputBuffer = nullptr;
    };

  protected:
    ID3D11Device* Device = nullptr;
    ID3D11DeviceContext* DeviceContext = nullptr;
    inline static std::unique_ptr<Menu_Dx11> Imgui = nullptr;
    std::unique_ptr<OS_Dx11> OutputScaler = nullptr;
    std::unique_ptr<RCAS_Dx11> RCAS = nullptr;
    std::unique_ptr<Bias_Dx11> Bias = nullptr;
    std::unique_ptr<Magnifier_Dx11> Magnifier = nullptr;

    std::unique_ptr<GpuTime_Dx11> UpscalerTime = nullptr;

    virtual bool InitInternal(ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters) = 0;
    virtual bool EvaluateInternal(ID3D11DeviceContext* DeviceContext, NVSDK_NGX_Parameter* InParameters) = 0;

  public:
    // Only DX11 w/DX12 should be overriding those
    virtual bool Init(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters);
    virtual bool Evaluate(ID3D11DeviceContext* DeviceContext, NVSDK_NGX_Parameter* InParameters);

    API Api() const override { return API::DX11; }
    std::optional<double> ReadUpscalerTime(void* deviceContextVoid) override;
    void ReadDetailedGpuTimes(void* deviceContextVoid, std::vector<DetailedGpuTime>& detailedGpuTimes) override;

    IFeature_Dx11(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters) : IFeature(InHandleId, InParameters) {}

    ~IFeature_Dx11();
};
