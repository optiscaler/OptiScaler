#pragma once

#include "SysUtils.h"

#include <hooks/AmdExtFlipMeter.h>

#include <dxgi1_6.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <condition_variable>
#include <deque>
#include <expected>
#include <filesystem>
#include <mutex>
#include <thread>

// Swapchain proxy that shows generated frames, paced by AMD's driver flip metering where it can.
//
// The game renders to buffers owned by the proxy and frame generation writes to buffers of the proxy. A game present
// copies the game's frame aside and hands the frame over to a presenter thread, which copies the generated frames and
// then the game's frame to the real swapchain and presents them.
//
// The driver paces the flips in exclusive fullscreen and windows covering their whole output, like for FSR. It
// measures the game's frame time itself and ignores the target times it gets. 2X uses FSR's adaptive mode, more
// generated frames the frame index mode, which the driver only allows up to its registry setting
// Gmx_DriverFramePacingMaxGenFrames (default 1). It holds one flip at a time, so the presenter sends each present
// after the previous frame's flip: the first at its time, the others half a frame step early.
// Without the driver's pacing only the game's frames are shown.
class DECLSPEC_UUID("6d0a3c0e-4c6b-4a5e-9d3f-0c5d1f6f1b77") FlipMeterSwapChain final : public IDXGISwapChain4
{
  public:
    // High priority queue for the real swapchain, its presents and copies then don't wait behind the game's frames
    // when the GPU is the bottleneck. Like FSR's present queue.
    static Microsoft::WRL::ComPtr<ID3D12CommandQueue> CreatePresentQueue(ID3D12CommandQueue* gameQueue);

    // Takes over the caller's reference of real, which has to be made on presentQueue. queue is the game's.
    // Null when the driver has no usable flip metering for the swapchain, real is left to the caller then.
    static FlipMeterSwapChain* Create(IDXGISwapChain* real, ID3D12CommandQueue* queue, ID3D12CommandQueue* presentQueue,
                                      HWND hwnd, UINT gameBufferCount);

    // Real swapchains get extra buffers when needed, a game frame queues more than one present
    static constexpr UINT MinRealBuffers = 3;

    // Buffer the game presents next, in PRESENT state
    ID3D12Resource* GameBuffer() const { return _buffers.empty() ? nullptr : _buffers[_index]; }

    // Output for a generated frame of the next present, kept in UNORDERED_ACCESS state. Null when it can't be made.
    // Waits until the presenter is done with the buffers of the game frame before last.
    ID3D12Resource* GeneratedBuffer(UINT index);

    // How many generated buffers the next present shows before the game's frame, in buffer order
    void SetGeneratedFrames(UINT count) { _pendingGenerated = count; }

    // Generated frames per game frame, at most MaxGeneratedFrames. 0 turns the metering off.
    void SetGeneratedPerFrame(UINT count);

    // Generated frames per game frame the driver paces, its Gmx_DriverFramePacingMaxGenFrames registry setting
    UINT MaxGeneratedFrames() const { return _caps.maxGeneratedFrames; }

    // Writes a .reg file next to the game's exe that sets Gmx_DriverFramePacingMaxGenFrames to generatedFrames in the
    // driver key of this swapchain's GPU. The file's path, or why it couldn't.
    std::expected<std::filesystem::path, std::string> WriteMaxGenFramesRegFile(UINT generatedFrames) const;

    bool IsMetering() const { return _metering; }

    // What the menu shows about the driver's pacing
    struct Status
    {
        bool metering = false;
        AmdExtFlipMeterMode mode = AmdExtFlipMeterMode_Off;
        UINT generatedFrames = 0;
        bool coversOutput = true;
        HRESULT configResult = S_OK; // Of the last SetConfig
        UINT32 configStatus = AmdExtFlipMeterStatus_Ok;
    };

    Status GetStatus() const
    {
        return { _metering, Mode(), _generatedPerFrame, _coversOutput, _configResult, _configStatus };
    }

    bool SupportsMode(AmdExtFlipMeterMode mode) const;

    // Turns the metering off and frees the buffers, the proxy can't present afterwards.
    // Leaves the reference of the real swapchain to whoever destroys it.
    void Shutdown();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IDXGIObject
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) override;
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) override;
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** ppParent) override;

    // IDXGIDeviceSubObject
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppDevice) override;

    // IDXGISwapChain
    HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval, UINT Flags) override;
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer, REFIID riid, void** ppSurface) override;
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL Fullscreen, IDXGIOutput* pTarget) override;
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* pFullscreen, IDXGIOutput** ppTarget) override;
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc) override;
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat,
                                            UINT SwapChainFlags) override;
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* pNewTargetParameters) override;
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** ppOutput) override;
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats) override;
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* pLastPresentCount) override;

    // IDXGISwapChain1
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* pDesc) override;
    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc) override;
    HRESULT STDMETHODCALLTYPE GetHwnd(HWND* pHwnd) override;
    HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID refiid, void** ppUnk) override;
    HRESULT STDMETHODCALLTYPE Present1(UINT SyncInterval, UINT PresentFlags,
                                       const DXGI_PRESENT_PARAMETERS* pPresentParameters) override;
    BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported(void) override;
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** ppRestrictToOutput) override;
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* pColor) override;
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* pColor) override;
    HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION Rotation) override;
    HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* pRotation) override;

    // IDXGISwapChain2
    HRESULT STDMETHODCALLTYPE SetSourceSize(UINT Width, UINT Height) override;
    HRESULT STDMETHODCALLTYPE GetSourceSize(UINT* pWidth, UINT* pHeight) override;
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT MaxLatency) override;
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* pMaxLatency) override;
    HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject(void) override;
    HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F* pMatrix) override;
    HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F* pMatrix) override;

    // IDXGISwapChain3
    UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex(void) override;
    HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace,
                                                     UINT* pColorSpaceSupport) override;
    HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace) override;
    HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format,
                                             UINT SwapChainFlags, const UINT* pCreationNodeMask,
                                             IUnknown* const* ppPresentQueue) override;

    // IDXGISwapChain4
    HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type, UINT Size, void* pMetaData) override;

  private:
    FlipMeterSwapChain() = default;
    ~FlipMeterSwapChain() = default;

    // Copies on _queue from one thread, an allocator is reused once its copies are done
    struct CopyContext
    {
        ID3D12GraphicsCommandList* list = nullptr;
        ID3D12CommandAllocator* allocators[BUFFER_COUNT] {};
        UINT64 allocatorValues[BUFFER_COUNT] {};
        UINT allocator = 0;
        ID3D12Fence* fence = nullptr;
        HANDLE event = nullptr;
        UINT64 value = 0;

        bool Create(ID3D12Device* device, const wchar_t* name);
        void Release();

        // Waits for the next allocator's previous copies and resets it, End has to follow
        bool Begin();

        // Signals the copies since Begin, returns the fence value they're done at
        UINT64 End(ID3D12CommandQueue* queue);
    };

    // What a game present hands the presenter
    struct QueuedFrame
    {
        UINT set = 0;       // of _generated and _realCopies
        UINT generated = 0; // generated frames before the game's frame
        INT64 step = 0;     // QPC ticks between its frames
        UINT32 frameTimeNs = 0;
        bool metered = false; // the driver paces its flips
        bool discontinuity = false;
        UINT64 presentId = 0;
        UINT syncInterval = 0;
        UINT flags = 0;
        UINT64 readyValue = 0; // _gameCopies fence value its frames are ready at
    };

    bool CreateObjects();
    bool CreateBuffers();
    void ReleaseBuffers();
    void WaitForGpu();

    HRESULT PresentFrames(UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* presentParameters);
    HRESULT Resize(UINT bufferCount, UINT width, UINT height, DXGI_FORMAT format, UINT flags, const UINT* nodeMasks,
                   IUnknown* const* presentQueues);
    bool Copy(CopyContext& context, ID3D12CommandQueue* queue, ID3D12Resource* source,
              D3D12_RESOURCE_STATES sourceState, ID3D12Resource* target, D3D12_RESOURCE_STATES targetState);

    // A copy of the game's frame, kept in COPY_SOURCE state
    ID3D12Resource* RealCopy(UINT set);

    void PresenterLoop();
    HRESULT PresentQueued(const QueuedFrame& frame);
    void WaitUntil(INT64 qpc) const;
    // Waits for the presenter to be done with a set and makes the game's queue wait for its copies of it
    void WaitForSet(UINT set);
    void FlushPresenter();
    void StopPresenter();

    // Every frame of a game present needs a back buffer of its own while the previous frame is still shown
    UINT RealBufferCount() const { return std::max({ _gameBufferCount, MinRealBuffers, _generatedPerFrame + 2 }); }
    void GrowRealBuffers();

    // FSR's adaptive mode for 2X, the frame index mode for more
    AmdExtFlipMeterMode Mode() const
    {
        return _generatedPerFrame > 1 ? AmdExtFlipMeterMode_FrameIndex : AmdExtFlipMeterMode_Adaptive;
    }

    bool CoversOutput() const;
    void UpdateMetering();
    void ApplyConfig(bool enable);

    IDXGISwapChain* _real = nullptr;
    IDXGISwapChain1* _real1 = nullptr;
    IDXGISwapChain2* _real2 = nullptr;
    IDXGISwapChain3* _real3 = nullptr;
    IDXGISwapChain4* _real4 = nullptr;

    ID3D12CommandQueue* _queue = nullptr;        // The game's
    ID3D12CommandQueue* _presentQueue = nullptr; // The real swapchain's and the presenter's copies
    ID3D12Device* _device = nullptr;
    HWND _hwnd = nullptr;
    LONG _refcount = 1;

    // What the game renders to and asks for, the real swapchain can have more buffers
    std::vector<ID3D12Resource*> _buffers;
    UINT _gameBufferCount = 0;
    UINT _realBufferCount = 0;
    UINT _index = 0;

    // Two sets, the game fills one while the presenter shows the other
    std::vector<ID3D12Resource*> _generated[2];
    ID3D12Resource* _realCopies[2] {};
    UINT _set = 0;
    std::atomic<UINT> _pendingGenerated = 0;

    CopyContext _gameCopies;      // Game thread: its frames to _realCopies, also waits for the GPU
    CopyContext _presenterCopies; // Presenter thread: frames to the real back buffers

    std::thread _presenter;
    std::mutex _presenterMutex;
    std::condition_variable _presenterCv;
    std::deque<QueuedFrame> _queued; // The front one is being presented
    UINT _setBusy[2] {};             // Queued frames per set
    UINT64 _setReleased[2] {};       // _presenterCopies fence value the presenter's copies of a set are done at
    bool _stopPresenter = false;
    std::atomic<HRESULT> _lastResult = S_OK;
    HANDLE _timer = nullptr;      // High resolution, for the presenter's waits
    HANDLE _readyEvent = nullptr; // Presenter's wait for a frame's copies
    INT64 _lastTargetQpc = 0;     // Presenter: time the previous game frame's last frame was paced to

    IAmdExtFlipMeter* _flipMeter = nullptr;
    AmdExtFlipMeterCaps _caps {};
    UINT _generatedPerFrame = 0;
    bool _metering = false;
    bool _meteringDirty = true;
    bool _coversOutput = true;
    HRESULT _configResult = S_OK;
    UINT32 _configStatus = AmdExtFlipMeterStatus_Ok;

    UINT64 _presentCount = 0;      // Game presents
    INT64 _qpcFrequency = 0;       // Ticks per second
    INT64 _lastGamePresentQpc = 0; // Start of the previous game present
    INT64 _averageFrameQpc = 0;    // Smoothed time between game presents
    bool _discontinuity = true;    // The previous game present had no generated frames
};
