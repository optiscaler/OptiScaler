#pragma once

#include "SysUtils.h"
#include <Config.h>

#include "dxgi1_6.h"
#include "d3d11_4.h"
#include "d3d12.h"

#include <vector>

using Microsoft::WRL::ComPtr;

class DECLSPEC_UUID("23b064bb-482d-416c-93b1-829acedfb3d0") Dx11wDx12SC final : public IDXGISwapChain4
{
  public:
    Dx11wDx12SC(IDXGISwapChain* real, IDXGISwapChain4* fgSC, ID3D11Device* pDevice, HWND hWnd, UINT flags);
    virtual ~Dx11wDx12SC();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) override;
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) override;
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** ppParent) override;

    HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppDevice) override;

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

    HRESULT STDMETHODCALLTYPE SetSourceSize(UINT Width, UINT Height) override;
    HRESULT STDMETHODCALLTYPE GetSourceSize(UINT* pWidth, UINT* pHeight) override;
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT MaxLatency) override;
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* pMaxLatency) override;
    HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject(void) override;
    HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F* pMatrix) override;
    HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F* pMatrix) override;

    UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex(void) override;
    HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace,
                                                     UINT* pColorSpaceSupport) override;
    HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace) override;
    HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format,
                                             UINT SwapChainFlags, const UINT* pCreationNodeMask,
                                             IUnknown* const* ppPresentQueue) override;

    HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type, UINT Size, void* pMetaData) override;

  private:
    bool _InitInteropObjects();
    bool _RequestSharedBackBuffer(UINT index);
    bool _CopyDx11BackBufferToShared(UINT index);
    bool _WaitDx11ThenDx12();
    bool _CopyDx11SharedToDx12FGBackBuffer(UINT dx11Index);
    bool _WaitForCopyQueueIdle();
    bool _WaitForCopyAllocator(UINT slot);
    void _ReleaseInteropBackBuffers();
    void _ReleaseInteropObjects();
    void _RefreshCachedSwapchainDesc();
    UINT _GetDx11BackBufferIndexForPresent() const;
    void _AdvanceFakeBackBufferIndex();
    bool _WaitForInteropCopyOnPresentQueue();

    IDXGISwapChain* _real = nullptr;
    IDXGISwapChain1* _real1 = nullptr;
    IDXGISwapChain2* _real2 = nullptr;
    IDXGISwapChain3* _real3 = nullptr;
    IDXGISwapChain4* _real4 = nullptr;

    IDXGISwapChain4* _fgSwapChain = nullptr;

    int _id = 0;
    LONG _refcount = 1;
    UINT _lastFlags = 0;

    IFGFeature_Dx12* _fg = nullptr;

    ID3D11Device* _dx11Device = nullptr;
    ID3D11Device5* _dx11Device5 = nullptr;
    ID3D11DeviceContext* _dx11Context = nullptr;
    ID3D11DeviceContext4* _dx11Context4 = nullptr;

    ID3D12Device* _dx12Device = nullptr;
    ID3D12CommandQueue* _dx12CommandQueue = nullptr;
    std::vector<ID3D12CommandAllocator*> _copyAllocators;
    std::vector<ID3D12GraphicsCommandList*> _copyCommandLists;

    UINT64 _lastInteropCopyFenceValue = 0;

    std::vector<UINT64> _copyAllocatorFenceValues;
    HANDLE _copyFenceEvent = nullptr;

    ID3D12Fence* _copyFence = nullptr;
    UINT64 _copyFenceValue = 1;

    ID3D11Fence* _dx11Fence = nullptr;
    HANDLE _dx11FenceEvent = nullptr;
    ID3D12Fence* _dx12SharedFence = nullptr;
    HANDLE _sharedFenceHandle = nullptr;
    UINT64 _sharedFenceValue = 1;

    std::vector<ID3D11Texture2D*> _sharedDx11BackBufferCopies;
    std::vector<ID3D12Resource*> _openedDx11BackBuffers;
    std::vector<HANDLE> _sharedBackBufferHandles;
    std::vector<D3D12_RESOURCE_STATES> _openedDx11BackBufferStates;

    UINT _bufferCount = 0;
    UINT _currentFakeIndex = 0;
    DXGI_FORMAT _bufferFormat = DXGI_FORMAT_UNKNOWN;
    bool _interopInitialized = false;

    HWND _handle = nullptr;
};
