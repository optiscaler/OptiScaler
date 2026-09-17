#pragma once
#include "SysUtils.h"
#include <d3d12.h>

class D3D12Hooks
{
  private:
    inline static std::mutex hookMutex;
    inline static std::mutex agilityMutex;

    static bool RestoreDescriptorHeaps(ID3D12GraphicsCommandList* cmdList);
    static bool RestorePipelineState(ID3D12GraphicsCommandList* cmdList);
    static bool RestoreComputeRootState(ID3D12GraphicsCommandList* cmdList);
    static bool RestoreGraphicsRootState(ID3D12GraphicsCommandList* cmdList);

  public:
    static void Hook();
    static void HookAgility(HMODULE module);
    static void HookDevice(ID3D12Device* device);
    static void Unhook();
    static void SetRootSignatureTracking(bool enable);
    static bool IsRootSignatureTrackingEnabled();
    static bool CanRestoreRootSignature(ID3D12GraphicsCommandList* cmdList);
    static void HookToCommandListLate(ID3D12GraphicsCommandList* commandList);
    static void RestoreRoot(ID3D12GraphicsCommandList* cmdList);
};
