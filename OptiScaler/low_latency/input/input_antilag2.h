#pragma once

#include <ffx_antilag2_dx12.h>
// #include <ffx_antilag2_dx11.h>

#include "input_common.h"

struct AmdExtAntiLagApi : public AMD::AntiLag2DX12::IAmdExtAntiLagApi
{
    enum AntiLag2eMode : uint32_t
    {
        AntiLag2Mode_Invalid = 0,
        AntiLag2Mode_On = 1,
        AntiLag2Mode_Off = 2,
    };

    InputContext inputContext { .caller = LowLatencyInput::AntiLag2,
                                .localContext = false,
                                .noFrameId = true,
                                .markerMode = InputMarkerMode::SimStartOnly,
                                .api = API::DX12 };

    ID3D12Device* device = nullptr;
    uint64_t pseudoFrameId = 0;

    HRESULT STDMETHODCALLTYPE UpdateAntiLagState(VOID* pData) override;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override { return E_NOTIMPL; }
    ULONG STDMETHODCALLTYPE AddRef(void) override { return 0; }
    ULONG STDMETHODCALLTYPE Release(void) override { return 0; }
};

class InputAntiLag2
{
    struct AntiLag2Data
    {
        AMD::AntiLag2DX12::Context* context;
        bool enabled;
    } data;

    inline static AntiLag2Data antiLag2DataForFG {};
    inline static const GUID IID_IFfxAntiLag2Data = {
        0x5083ae5b, 0x8070, 0x4fca, { 0x8e, 0xe5, 0x35, 0x82, 0xdd, 0x36, 0x7d, 0x13 }
    };

  public:
    static void injectAl2Context(IDXGISwapChain* pSwapChain, bool fg_state);
};
