#pragma once
#include "SysUtils.h"

#include <NVNGX_Parameter.h>

#include <upscalers/IFeature_Dx11.h>

#include <with_dx12/dx11_with_dx12.h>

class UpscalerInputsDx11wDx12
{
  private:
    inline static ID3D12Device* _dx12Device = nullptr;
    inline static ID3D12CommandQueue* _dx12CommandQueue = nullptr;

  public:
    static void Init(ID3D11Device* dx11Device, ID3D11DeviceContext* dx11Context, ID3D12Device* dx12Device,
                     ID3D12CommandQueue* dx12CommandQueue);
    static void Reset();
    static void Shutdown();

    // Input parameters are D3D11 resources. This bridge copies/prepares them through Dx11WithDx12 before FG use.
    static void UpscaleStart(NVSDK_NGX_Parameter* InParameters, IFeature_Dx11* feature);
    static void UpscaleEnd(NVSDK_NGX_Parameter* InParameters, IFeature_Dx11* feature);
};
