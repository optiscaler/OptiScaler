#pragma once
#include <shaders/Shader_Dx12.h>
#include <shaders/Shader_Dx12Utils.h>

#include <d3d12.h>
#include <d3dx/d3dx12.h>

#define OS_NUM_OF_HEAPS 2

// Forward declaration so this header need not pull in Config.h. Scaler is a scoped enum with a fixed
// underlying type, so an opaque declaration is a complete type -- enough for a member and a parameter.
enum class Scaler : uint32_t;

class OS_Dx12 : public Shader_Dx12
{
  private:
    bool _upsample = false;

    // Which downscaler this instance was built for. Scaler::Count means "no override -- read the global
    // OutputScalingDownscaler", so the ordinary Output Scaling constructor behaves exactly as before.
    // Neural Rendering passes its own DlssNrScalingDownscaler here so the two pick filters independently.
    Scaler _scalerOverride;
    Scaler ActiveScaler() const;
    bool DispatchWithSize(ID3D12GraphicsCommandList* commandList, ID3D12Resource* source, ID3D12Resource* output,
                          uint32_t sourceWidth, uint32_t sourceHeight, uint32_t outputWidth, uint32_t outputHeight);

    FrameDescriptorHeap _frameHeaps[OS_NUM_OF_HEAPS];

    ID3D12Resource* _buffer = nullptr;
    D3D12_RESOURCE_STATES _bufferState = D3D12_RESOURCE_STATE_COMMON;

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

  public:
    bool CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, uint32_t InWidth, uint32_t InHeight,
                              D3D12_RESOURCE_STATES InState);
    void SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState);
    bool Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource, ID3D12Resource* OutResource);
    // Resample complete, independently sized resources without consulting the game's active feature.
    bool DispatchResources(ID3D12GraphicsCommandList* commandList, ID3D12Resource* source, ID3D12Resource* output);

    ID3D12Resource* Buffer() { return _buffer; }
    bool IsUpsampling() const { return _upsample; }
    bool CanRender() const { return _init && _buffer != nullptr; }

    OS_Dx12(std::string InName, ID3D12Device* InDevice, bool InUpsample);
    OS_Dx12(std::string InName, ID3D12Device* InDevice, bool InUpsample, Scaler InScalerOverride);

    ~OS_Dx12();
};
