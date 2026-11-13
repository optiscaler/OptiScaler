#pragma once

#include <framegen/IFGFeature_Dx12.h>

#include <NVNGX_Parameter.h>
#include <shaders/format_transfer/FT_Dx12.h>

class FlipMeterSwapChain;

// Frame generation output presenting through AMD's driver flip metering (FlipMeterSwapChain).
// The generated frames come from the selected Nvngx replacement, which gets the same evaluate calls Streamline's
// DLSSG plugin makes.
class FlipMeterFG_Dx12 : public virtual IFGFeature_Dx12
{
  private:
    FlipMeterSwapChain* _proxy = nullptr;

    NVNGX_Parameters _parameters { API::DX12, false };
    NVSDK_NGX_Handle* _handle = nullptr;
    bool _ngxInited = false;
    bool _featureFailed = false;

    // What the feature was created for
    UINT64 _featureWidth = 0;
    UINT _featureHeight = 0;
    DXGI_FORMAT _featureFormat = DXGI_FORMAT_UNKNOWN;

    // The parameters only hold pointers to them
    float _cameraViewToClip[4][4] {};
    float _clipToCameraView[4][4] {};
    float _identity[4][4] {};

    // Makes the real swapchain on the present queue with that many buffers
    using CreateRealFn = std::function<HRESULT(IDXGIFactory* realFactory, ID3D12CommandQueue* presentQueue,
                                               UINT bufferCount, IDXGISwapChain** real)>;

    bool CreateProxy(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, HWND hwnd, UINT gameBufferCount,
                     IDXGISwapChain** swapChain, const CreateRealFn& createReal);

    std::unique_ptr<FT_Dx12> _depthTransfer;

    // 24 bit depth as R32_FLOAT, in InputState. Other depth formats as they are
    Dx12Resource FloatDepth(ID3D12GraphicsCommandList* cmdList, Dx12Resource& depth);

    bool EnsureFeature(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* backbuffer);
    void ReleaseFeature();

    // Menus and loading screens keep presenting without frame generation inputs, after this many frames in a row
    // nothing gets generated or metered until they come back
    static constexpr UINT IdleAfterFrames = 4;
    UINT _framesWithoutInputs = 0;

    // Generated frames per game frame the output currently aims for
    UINT TargetCount() const;

    void SetFrameParameters(int fIndex, ID3D12Resource* backbuffer, Dx12Resource* depth, Dx12Resource* velocity,
                            Dx12Resource* hudless);

    // One generated frame: multiFrameIndex of multiFrameCount evenly spaced ones between the previous and the
    // current game frame, 1 based
    bool EvaluateFrame(ID3D12GraphicsCommandList* cmdList, UINT multiFrameCount, UINT multiFrameIndex,
                       ID3D12Resource* output);

    bool Dispatch(int fIndex);

  protected:
    void ReleaseObjects() override final;
    void CreateObjects(ID3D12Device* InDevice) override final;

  public:
    // Most generated frames per game frame the replacement reports in DLSSG.MultiFrameCountMax
    static int ProviderMaxInterpolationCount();

    // Most generated frames per game frame, limited by the replacement and the driver
    int MaxInterpolationCount() const;

    // Null until its swapchain is made
    const FlipMeterSwapChain* SwapChain() const { return _proxy; }

    // IFGFeature
    const char* Name() override final { return "Flip Meter"; };
    feature_version Version() override final;
    HWND Hwnd() override final { return _hwnd; };

    // IFGFeature_Dx12
    bool CreateSwapchainInternal(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, DXGI_SWAP_CHAIN_DESC* desc,
                                 IDXGISwapChain** swapChain) override final;
    bool CreateSwapchain1Internal(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, HWND hwnd,
                                  DXGI_SWAP_CHAIN_DESC1* desc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                  IDXGISwapChain1** swapChain) override final;

    bool ReleaseSwapchain(HWND hwnd) override final;

    void CreateContext(ID3D12Device* device, FG_Constants& fgConstants) override final;
    void Activate() override final;
    void Deactivate() override final;
    void DestroyFGContext() override final;
    bool Shutdown() override final;

    void EvaluateState(ID3D12Device* device, FG_Constants& fgConstants) override final;

    bool Present() override final;

    bool SetResource(Dx12Resource* inputResource) override final;
    void SetCommandQueue(FG_ResourceType type, ID3D12CommandQueue* queue) override final;

    // Inputs only check them for null. The replacement's feature is made with the first frame's resources, so the
    // context counts as created once the output can take them
    void* FrameGenerationContext() override final { return _uiCommandAllocator[0] != nullptr ? this : nullptr; };
    void* SwapchainContext() override final { return _proxy; };

    bool SetInterpolatedFrameCount(UINT interpolatedFrameCount) override final { return true; };

    FlipMeterFG_Dx12();
    ~FlipMeterFG_Dx12() {};
};
