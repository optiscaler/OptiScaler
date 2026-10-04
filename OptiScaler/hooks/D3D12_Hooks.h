#pragma once
#include "SysUtils.h"
#include <d3d12.h>

class D3D12Hooks
{
  private:
    inline static std::mutex hookMutex;
    inline static std::mutex agilityMutex;

  public:
    static void Hook();
    static void HookAgility(HMODULE module);
    static void HookDevice(ID3D12Device* device);
    static void Unhook();

    // Enables/disables command list state recording for the CALLING THREAD.
    // Call with false before Opti records its own work into the game's command list, true afterwards.
    static void SetRootSignatureTracking(bool enable);

    static bool CanRestoreRootSignature(ID3D12GraphicsCommandList* cmdList);
    static void HookToCommandListLate(ID3D12GraphicsCommandList* commandList);
    static void RestoreRoot(ID3D12GraphicsCommandList* cmdList);
};
