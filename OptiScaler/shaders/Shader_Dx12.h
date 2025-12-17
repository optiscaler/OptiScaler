#pragma once

#include <pch.h>
#include <d3d12.h>

class Shader_Dx12
{
  protected:
    std::string _name = "";
    bool _init = false;
    int _counter = 0;

    ID3D12RootSignature* _rootSignature = nullptr;
    ID3D12PipelineState* _pipelineState = nullptr;

    ID3D12Device* _device = nullptr;
    ID3D12Resource* _constantBuffer = nullptr;

    static DXGI_FORMAT TranslateTypelessFormats(DXGI_FORMAT format);
    static bool CreateComputeShader(ID3D12Device* device, ID3D12RootSignature* rootSignature,
                                    ID3D12PipelineState** pipelineState, ID3DBlob* shaderBlob);
    static bool CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InResource, D3D12_RESOURCE_STATES InState,
                                     ID3D12Resource** OutResource, D3D12_RESOURCE_FLAGS ResourceFlags,
                                     uint64_t InWidth = 0, uint32_t InHeight = 0,
                                     DXGI_FORMAT InFormat = DXGI_FORMAT_UNKNOWN);
    static void SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState,
                               ID3D12Resource* Buffer, D3D12_RESOURCE_STATES* BufferState);

  public:
    bool IsInit() const { return _init; }

    Shader_Dx12(std::string InName, ID3D12Device* InDevice);
};