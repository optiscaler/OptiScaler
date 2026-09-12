#pragma once

#include <d3d12.h>
#include <functional>
#include <cstdint>
#include <vector>
#include <nvsdk_ngx.h>

inline ID3D12Resource* GetUpscalerResource_Dx12(NVSDK_NGX_Parameter* parameters, const char* name)
{
    ID3D12Resource* resource = nullptr;
    parameters->Get(name, &resource);
    if (resource == nullptr)
    {
        void* untyped = nullptr;
        parameters->Get(name, &untyped);
        resource = static_cast<ID3D12Resource*>(untyped);
    }
    return resource;
}

inline void SetUpscalerResource_Dx12(NVSDK_NGX_Parameter* parameters, const char* name, ID3D12Resource* resource)
{
    // A DX11 bridge can retain the driver's DX11 parameter table. That table rejects
    // DX12-typed access, including Set, but accepts the bridge's resources as void*.
    ID3D12Resource* previous = nullptr;
    if (parameters->Get(name, &previous) == NVSDK_NGX_Result_Success)
        parameters->Set(name, resource);
    else
        parameters->Set(name, static_cast<void*>(resource));
}

// The same resource routing is used before and after the upscaler, including API bridges.
struct ShaderPass_Dx12
{
    std::function<ID3D12Resource*(ID3D12Resource* nextOutput)> Setup;
    std::function<bool(ID3D12Resource* input, ID3D12Resource* output)> Dispatch;
    ID3D12Resource* inputBuffer = nullptr;
    ID3D12Resource* outputBuffer = nullptr;
};

using ShaderPipeline_Dx12 = std::vector<ShaderPass_Dx12>;

inline ID3D12Resource* SetupShaderPipeline(ShaderPipeline_Dx12& pipeline, ID3D12Resource* output)
{
    for (auto it = pipeline.rbegin(); it != pipeline.rend(); ++it)
    {
        it->inputBuffer = it->outputBuffer = nullptr;
        if (auto* input = it->Setup(output))
        {
            it->inputBuffer = input;
            it->outputBuffer = output;
            output = input;
        }
    }
    return output;
}

inline bool DispatchShaderPipeline(ShaderPipeline_Dx12& pipeline)
{
    for (auto& pass : pipeline)
        if (pass.inputBuffer && pass.outputBuffer && !pass.Dispatch(pass.inputBuffer, pass.outputBuffer))
            return false;
    return true;
}

// Parameter restoration belongs to the upscaler call, regardless of which shader stages ran.
class RestoreUpscalerResources_Dx12
{
    NVSDK_NGX_Parameter* _parameters;
    ID3D12Resource* _color = nullptr;
    ID3D12Resource* _output = nullptr;
    bool _untypedColor = false;
    bool _untypedOutput = false;

  public:
    explicit RestoreUpscalerResources_Dx12(NVSDK_NGX_Parameter* parameters) : _parameters(parameters)
    {
        parameters->Get(NVSDK_NGX_Parameter_Color, &_color);
        parameters->Get(NVSDK_NGX_Parameter_Output, &_output);
        _untypedColor = _color == nullptr;
        _untypedOutput = _output == nullptr;
        _color = GetUpscalerResource_Dx12(parameters, NVSDK_NGX_Parameter_Color);
        _output = GetUpscalerResource_Dx12(parameters, NVSDK_NGX_Parameter_Output);
    }
    ~RestoreUpscalerResources_Dx12()
    {
        if (_untypedColor)
            _parameters->Set(NVSDK_NGX_Parameter_Color, static_cast<void*>(_color));
        else
            _parameters->Set(NVSDK_NGX_Parameter_Color, _color);
        if (_untypedOutput)
            _parameters->Set(NVSDK_NGX_Parameter_Output, static_cast<void*>(_output));
        else
            _parameters->Set(NVSDK_NGX_Parameter_Output, _output);
    }
};
