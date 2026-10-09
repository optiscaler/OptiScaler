#pragma once
#include "SysUtils.h"
#include <NVNGX_Parameter.h>
#include <upscalers/IFeature_Dx12.h>
#include <framegen/IFGFeature_Dx12.h>
#include <proxies/XeFG_Proxy.h>
#include <shaders/camera_motion/CM_Dx12.h>
#include <DirectXMath.h>

// XeFG input: the game's libxess_fg calls feed the selected FG output
class XeFGInputs
{
    struct HookEntry
    {
        const char* name;
        PVOID function;
        PVOID target = nullptr;
    };

    // XeFG output: the game's own XeFG and XeLL run on OptiScaler's libraries, as they would without OptiScaler.
    // Like Override DLSSG Ratio, "Override XeFG Ratio" replaces the game's count (0 = off), within what the game's
    // context was created with. Without an override the game's own count applies; XeFG itself would use its maximum.
    inline static xefg_swapchain_handle_t _passContext = nullptr;
    inline static uint32_t _passMax = 0;
    inline static uint32_t _passGameCount = 1;
    inline static uint32_t _passGameEnabled = 0;
    inline static uint32_t _passCount = 0;
    inline static uint32_t _passEnabled = UINT32_MAX;

    // Other FG outputs: the game is only offered 1 interpolated frame, the FG output decides the real count
    inline static ID3D12Device* _device = nullptr;
    inline static IDXGISwapChain* _swapChain = nullptr; // Handed over by the game on init, released on destroy
    inline static xefg_swapchain_d3d12_init_params_t _initParams {};
    inline static FG_Constants _constants {};
    inline static xefg_swapchain_2d_t _mvSize {};
    inline static bool _enabled = false;
    inline static uint64_t _frameIds[BUFFER_COUNT] {}; // Present id + 1 of each buffer index, 0 = none
    inline static float _frameTimeScale = 0.0f;        // 1000 when the game passes seconds, 0 until decided
    inline static float _frameTimeMax = 0.0f;
    inline static int _frameTimeSamples = 0;
    inline static bool _gameDepthMV[BUFFER_COUNT] {}; // Frames without them from the game use the upscaler's
    inline static bool _upscalerMVs = false;

    // The Witcher 3 only writes velocity where objects move, alpha is 0 where only the camera moved. Those pixels get
    // the camera's motion from depth, the velocity waits for the frame's depth and both go through the shader.
    inline static std::unique_ptr<CM_Dx12> _cameraMotion;
    inline static ID3D12Device* _cameraMotionDevice = nullptr;
    inline static DirectX::XMFLOAT4X4 _reprojection[BUFFER_COUNT] {};
    inline static bool _reprojectionKnown[BUFFER_COUNT] {};
    inline static DirectX::XMFLOAT4X4 _viewProjection {}, _previousViewProjection {};
    inline static bool _cameraKnown = false, _previousCameraKnown = false;
    inline static uint64_t _cameraPresentId = 0; // Present id + 1 of the camera above
    inline static Dx12Resource _pendingDepth[BUFFER_COUNT] {}, _pendingVelocity[BUFFER_COUNT] {};

    inline static PVOID _redirects[128] {};
    inline static size_t _redirectCount = 0;
    inline static bool _redirectIncomplete = false;

    // XeFG output
    static void PassApply(xefg_swapchain_handle_t context);
    static void PassInit(const xefg_swapchain_d3d12_init_params_t* params);
    static xefg_swapchain_result_t PassInitFromSwapChain(xefg_swapchain_handle_t context, ID3D12CommandQueue* queue,
                                                         const xefg_swapchain_d3d12_init_params_t* params);
    static xefg_swapchain_result_t PassInitFromSwapChainDesc(xefg_swapchain_handle_t context, HWND hwnd,
                                                             const DXGI_SWAP_CHAIN_DESC1* desc,
                                                             const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc,
                                                             ID3D12CommandQueue* queue, IDXGIFactory2* factory,
                                                             const xefg_swapchain_d3d12_init_params_t* params);
    static xefg_swapchain_result_t PassSetEnabled(xefg_swapchain_handle_t context, uint32_t enable);
    static xefg_swapchain_result_t PassSetNumInterpolatedFrames(xefg_swapchain_handle_t context, uint32_t count);
    static xefg_swapchain_result_t PassSetPresentId(xefg_swapchain_handle_t context, uint32_t presentId);

    // Other FG outputs
    static int FrameIndex(IFGFeature_Dx12* fg, uint32_t presentId);
    static void UpdateReprojection(uint32_t presentId, const xefg_swapchain_frame_constant_data_t* data, int index);
    static bool FillVelocity(IFGFeature_Dx12* fg, ID3D12GraphicsCommandList* cmdList, int index);
    static float FrameTimeMs(float frameTime);
    static void SetGameMVFlags();

    static xefg_swapchain_result_t CreateContext(ID3D12Device* device, xefg_swapchain_handle_t* handle);
    static xefg_swapchain_result_t GetProperties(xefg_swapchain_handle_t, xefg_swapchain_properties_t* properties);
    static xefg_swapchain_result_t D3D12GetProperties(xefg_swapchain_handle_t handle,
                                                      const xefg_swapchain_d3d12_init_params_t*, uint32_t, uint32_t,
                                                      DXGI_FORMAT, xefg_swapchain_properties_t* properties);
    static xefg_swapchain_result_t InitFromSwapChain(xefg_swapchain_handle_t, ID3D12CommandQueue*,
                                                     const xefg_swapchain_d3d12_init_params_t* params);
    static xefg_swapchain_result_t InitFromSwapChainDesc(xefg_swapchain_handle_t handle, HWND hwnd,
                                                         const DXGI_SWAP_CHAIN_DESC1* desc,
                                                         const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc,
                                                         ID3D12CommandQueue* queue, IDXGIFactory2* factory,
                                                         const xefg_swapchain_d3d12_init_params_t* params);
    static xefg_swapchain_result_t GetSwapChainPtr(xefg_swapchain_handle_t, REFIID riid, void** swapChain);
    static xefg_swapchain_result_t GetInitializationParameters(xefg_swapchain_handle_t,
                                                               xefg_swapchain_d3d12_init_params_t* params);
    static xefg_swapchain_result_t SetEnabled(xefg_swapchain_handle_t, uint32_t enable);
    static xefg_swapchain_result_t TagFrameConstants(xefg_swapchain_handle_t, uint32_t presentId,
                                                     const xefg_swapchain_frame_constant_data_t* data);
    static xefg_swapchain_result_t TagFrameResource(xefg_swapchain_handle_t, ID3D12CommandList* cmdList,
                                                    uint32_t presentId,
                                                    const xefg_swapchain_d3d12_resource_data_t* data);
    static xefg_swapchain_result_t GetLastPresentStatus(xefg_swapchain_handle_t,
                                                        xefg_swapchain_present_status_t* status);
    static xefg_swapchain_result_t Destroy(xefg_swapchain_handle_t);

    // The game's own XeFG without the XeFG input
    static xefg_swapchain_result_t GameSetLatencyReduction(xefg_swapchain_handle_t hSwapChain, void* hXeLLContext);
    static xefg_swapchain_result_t GameSetEnabled(xefg_swapchain_handle_t hSwapChain, uint32_t enable);

    static xefg_swapchain_result_t Ok() { return XEFG_SWAPCHAIN_RESULT_SUCCESS; }

    inline static HookEntry _xefgHooks[] = {
        { "xefgSwapChainD3D12CreateContext", (PVOID) &CreateContext },
        { "xefgSwapChainGetProperties", (PVOID) &GetProperties },
        { "xefgSwapChainD3D12GetProperties", (PVOID) &D3D12GetProperties },
        { "xefgSwapChainD3D12InitFromSwapChain", (PVOID) &InitFromSwapChain },
        { "xefgSwapChainD3D12InitFromSwapChainDesc", (PVOID) &InitFromSwapChainDesc },
        { "xefgSwapChainD3D12GetSwapChainPtr", (PVOID) &GetSwapChainPtr },
        { "xefgSwapChainD3D12GetInitializationParameters", (PVOID) &GetInitializationParameters },
        { "xefgSwapChainSetEnabled", (PVOID) &SetEnabled },
        { "xefgSwapChainTagFrameConstants", (PVOID) &TagFrameConstants },
        { "xefgSwapChainD3D12TagFrameResource", (PVOID) &TagFrameResource },
        { "xefgSwapChainGetLastPresentStatus", (PVOID) &GetLastPresentStatus },
        { "xefgSwapChainDestroy", (PVOID) &Destroy },

        // Nothing for the FG output to do
        { "xefgSwapChainSetPresentId", (PVOID) &Ok },
        { "xefgSwapChainSetLatencyReduction", (PVOID) &Ok },
        { "xefgSwapChainSetLoggingCallback", (PVOID) &Ok },
        { "xefgSwapChainSetSceneChangeThreshold", (PVOID) &Ok },
        { "xefgSwapChainGetPipelineBuildStatus", (PVOID) &Ok },
        { "xefgSwapChainSetNumInterpolatedFrames", (PVOID) &Ok },
        { "xefgSwapChainSetUiCompositionState", (PVOID) &Ok },
        { "xefgSwapChainD3D12BuildPipelines", (PVOID) &Ok },
        { "xefgSwapChainD3D12SetDescriptorHeap", (PVOID) &Ok },
        { "xefgSwapChainD3D12UpdateExternalHeapOnResize", (PVOID) &Ok },
        { "xefgSwapChainEnableDebugFeature", (PVOID) &Ok },
    };

    // The game's own XeFG without the XeFG input, only for its XeLL (the XeFG passthrough knows it from the start)
    inline static HookEntry _nativeHooks[] = {
        { "xefgSwapChainSetLatencyReduction", (PVOID) &GameSetLatencyReduction },
        { "xefgSwapChainSetEnabled", (PVOID) &GameSetEnabled },
    };

    // The calls that carry the game's frame generation choices go through the override
    inline static HookEntry _passHooks[] = {
        { "xefgSwapChainD3D12InitFromSwapChain", (PVOID) &PassInitFromSwapChain },
        { "xefgSwapChainD3D12InitFromSwapChainDesc", (PVOID) &PassInitFromSwapChainDesc },
        { "xefgSwapChainSetEnabled", (PVOID) &PassSetEnabled },
        { "xefgSwapChainSetNumInterpolatedFrames", (PVOID) &PassSetNumInterpolatedFrames },
        { "xefgSwapChainSetPresentId", (PVOID) &PassSetPresentId },
    };

    template <size_t N> static bool Attach(HMODULE module, HookEntry (&hooks)[N]);
    static BOOL CALLBACK RedirectExport(PVOID library, ULONG, LPCSTR name, PVOID target);
    static bool Redirect(HMODULE from, HMODULE to);

  public:
    static void Hook(HMODULE libxessFg);

    // Depth and motion vectors from the upscaler for frames the game doesn't tag them (Cyberpunk 2077 with DLSS)
    static void SetUpscalerInputs(ID3D12GraphicsCommandList* cmdList, NVSDK_NGX_Parameter* parameters,
                                  IFeature_Dx12* feature);

    // XeFG output: the game's own XeFG runs; Override XeFG Ratio sets its count
    static bool Passthrough();
    static uint32_t MaxInterpolations() { return _passMax; }
};
