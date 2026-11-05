#pragma once

#include "SysUtils.h"

#include <hudfix/Hudfix_Dx11.h>

#include <d3d11_4.h>

#include <array>
#include <atomic>
#include <mutex>
#include <vector>

enum class Dx11ShaderStage : UINT
{
    PS = 0,
    VS,
    GS,
    HS,
    DS,
    Count,
};

class ResTrack_Dx11
{
  private:
    inline static ID3D11Device* _device = nullptr;
    inline static ID3D11DeviceContext* _immediateContext = nullptr;

    inline static std::mutex _stateMutex;
    inline static std::array<std::vector<UINT>, static_cast<size_t>(Dx11ShaderStage::Count)> _graphicsSrvSlots;
    inline static std::vector<UINT> _csSrvSlots;
    inline static std::atomic<UINT> _omRtvCount = 0; // Set on every OMSetRenderTargets, no lock
    inline static UINT _uavSlotCount = D3D11_PS_CS_UAV_REGISTER_COUNT;

    inline static std::mutex _hookMutex;
    inline static std::atomic<bool> _deferredContextWarning { false };
    inline static thread_local UINT _internalCallDepth = 0;

    static bool IsHudFixActive(ID3D11DeviceContext* context);
    static bool CheckView(ID3D11DeviceContext* context, ID3D11View* view, Dx11ResourceType type, UINT captureInfo);
    static void RememberSlots(std::vector<UINT>& slots, UINT startSlot, UINT count, UINT maxSlots);

  public:
    static void NoteGraphicsShaderResources(ID3D11DeviceContext* context, Dx11ShaderStage stage, UINT startSlot,
                                            UINT numViews, ID3D11ShaderResourceView* const* views);
    static void NoteCSShaderResources(ID3D11DeviceContext* context, UINT startSlot, UINT numViews,
                                      ID3D11ShaderResourceView* const* views);
    static void NoteCSUnorderedAccessViews(ID3D11DeviceContext* context, UINT startSlot, UINT numViews,
                                           ID3D11UnorderedAccessView* const* views);
    static void NoteOMRenderTargets(ID3D11DeviceContext* context, UINT numViews, ID3D11RenderTargetView* const* views);
    static void NoteOMRenderTargetsAndUAVs(ID3D11DeviceContext* context, UINT numRTVs,
                                           ID3D11RenderTargetView* const* rtvs, UINT uavStartSlot, UINT numUAVs,
                                           ID3D11UnorderedAccessView* const* uavs);
    static void ProcessCandidates(ID3D11DeviceContext* context, UINT dispatchInfo);

    static void HookDevice(ID3D11Device* device);
    static void OnDeviceReleased(ID3D11Device* device);
    static void ReleaseHooks();
    static void ClearPossibleHudless();

    static void EnterInternalCall() { ++_internalCallDepth; }
    static void LeaveInternalCall()
    {
        if (_internalCallDepth > 0)
            --_internalCallDepth;
    }
    static bool InternalCallActive() { return _internalCallDepth != 0; }
};

class ScopedSkipDx11HudfixTracking
{
  public:
    ScopedSkipDx11HudfixTracking() { ResTrack_Dx11::EnterInternalCall(); }
    ~ScopedSkipDx11HudfixTracking() { ResTrack_Dx11::LeaveInternalCall(); }

    ScopedSkipDx11HudfixTracking(const ScopedSkipDx11HudfixTracking&) = delete;
    ScopedSkipDx11HudfixTracking& operator=(const ScopedSkipDx11HudfixTracking&) = delete;
};
