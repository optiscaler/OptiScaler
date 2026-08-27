#pragma once
#include <d3d12.h>
#include "Shader_Common.h"
#include <gpu_time/GpuTime_Dx12.h>

class Shader_Dx12
{
  private:
    uint32_t _srcCount = 0;
    uint32_t _uavCount = 0;
    uint32_t _cbvCount = 0;
    uint32_t _rtvCount = 0;
    uint32_t _samplerCount = 0;

  protected:
    std::string _name = "";
    bool _init = false;
    int _counter = 0;

    std::unique_ptr<GpuTime_Dx12> GpuTime = nullptr;

    ID3D12RootSignature* _rootSignature = nullptr;
    ID3D12PipelineState* _pipelineState = nullptr;

    ID3D12Device* _device = nullptr;
    ID3D12Resource* _constantBuffer = nullptr;

    std::vector<CD3DX12_DESCRIPTOR_RANGE1> _descriptorRanges;

    static DXGI_FORMAT TranslateTypelessFormats(DXGI_FORMAT format);
    static bool CreateComputeShader(ID3D12Device* device, ID3D12RootSignature* rootSignature,
                                    ID3D12PipelineState** pipelineState, ID3DBlob* shaderBlob,
                                    D3D12_SHADER_BYTECODE byteCode);
    bool CreateComputePipeline(ID3D12Device* device, ID3D12PipelineState** pipelineState, const void* bytecode,
                               size_t bytecodeSize, const char* source);
    static bool CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InResource, D3D12_RESOURCE_STATES InState,
                                     ID3D12Resource** OutResource, D3D12_RESOURCE_FLAGS ResourceFlags,
                                     uint64_t InWidth = 0, uint32_t InHeight = 0,
                                     DXGI_FORMAT InFormat = DXGI_FORMAT_UNKNOWN);
    static void SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState,
                               ID3D12Resource* Buffer, D3D12_RESOURCE_STATES* BufferState);

    void CreateShaderResourceView(ID3D12Device* device, ID3D12Resource* tex, D3D12_CPU_DESCRIPTOR_HANDLE srvDescriptor,
                                  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);

    void CreateUnorderedAccessView(ID3D12Device* device, ID3D12Resource* tex, D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptor,
                                   uint32_t mipLevel);

    void CreateRenderTargetView(ID3D12Device* device, ID3D12Resource* tex, D3D12_CPU_DESCRIPTOR_HANDLE rtvDescriptor,
                                uint32_t mipLevel);

    bool SetupRootSignature(ID3D12Device* InDevice, uint32_t srcCount, uint32_t uavCount, uint32_t cbvCount,
                            uint32_t rtvCount = 0, uint32_t samplerCount = 0, uint32_t staticSamplerCount = 0,
                            const D3D12_STATIC_SAMPLER_DESC* pStaticSamplers = nullptr,
                            D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE);

    bool InitHeaps(ID3D12Device* InDevice, FrameDescriptorHeap* pHeaps, size_t numOFHeaps);

  public:
    bool IsInit() const { return _init; }
    std::string Name() const { return _name; }
    std::optional<double> ReadGpuTime(ID3D12CommandQueue* commandQueue) { return GpuTime->ReadGpuTime(commandQueue); }

    Shader_Dx12(std::string InName, ID3D12Device* InDevice);

    ~Shader_Dx12();
};
