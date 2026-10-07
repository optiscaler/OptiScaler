#pragma once

#include <shaders/depth_transfer/DT_Dx11.h>

#include <d3d12.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs.h>

#define DX11_WITH_DX12_CACHED_FRAMES 2

class Dx11WithDx12
{
  private:
    inline static ID3D11Device5* Dx11Device = nullptr;
    inline static ID3D11DeviceContext4* Dx11DeviceContext = nullptr;

    inline static ID3D12Device* Dx12Device = nullptr;
    inline static ID3D12CommandQueue* Dx12CommandQueue = nullptr;

    inline static std::unique_ptr<DepthTransfer_Dx11> DT = nullptr;

    inline static ID3D11Fence* Dx11FenceTextureCopy = nullptr;
    inline static ID3D12Fence* Dx12FenceTextureCopy = nullptr;
    inline static HANDLE Dx11SharedHandleForTextureCopy = NULL;
    inline static HANDLE Dx12TextureCopyEvent = NULL;
    inline static UINT64 TextureCopyFenceValue = 1;

    // Sync objects are tied to the current D3D11 device, D3D12 device and D3D12 queue.
    // Backend switching can reset resource caches while FG input is still using the bridge,
    // so fence lifetime must be protected and validated separately from texture-cache lifetime.
    inline static std::mutex SyncMutex = {};
    inline static ID3D11Device5* SyncDx11Device = nullptr;
    inline static ID3D12Device* SyncDx12Device = nullptr;
    inline static ID3D12CommandQueue* SyncDx12CommandQueue = nullptr;

    inline static D3D11_TEXTURE2D_DESC DepthTransferSourceDesc = {};
    inline static bool DepthTransferSourceDescValid = false;

    static void ReleaseSyncResources();
    static void ReleaseSyncResourcesLocked();
    static bool EnsureSyncResourcesLocked();
    static bool UseCpuWaitForDx12();

  public:
    enum class ResourceMask : uint32_t
    {
        None = 0,
        Color = 1 << 0,
        Mv = 1 << 1,
        Depth = 1 << 2,
        Output = 1 << 3,
        Exposure = 1 << 4,
        Reactive = 1 << 5,
    };

    using PrepareResourcesResult = struct PrepareResourcesResult
    {
        bool Success = false;
        bool MissingColor = false;
        bool MissingMv = false;
        bool MissingDepth = false;
        bool MissingOutput = false;
        bool MissingExposure = false;
        bool MissingReactive = false;
    };

    // Dx11w12 part
    using D3D11_TEXTURE2D_DESC_C = struct D3D11_TEXTURE2D_DESC_C
    {
        UINT Width = 0;
        UINT Height = 0;
        UINT MipLevels = 0;
        UINT ArraySize = 0;
        DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
        DXGI_SAMPLE_DESC SampleDesc = {};
        D3D11_USAGE Usage = D3D11_USAGE_DEFAULT;
        UINT BindFlags = 0;
        UINT CPUAccessFlags = 0;
        UINT MiscFlags = 0;
    };

    using D3D11_TEXTURE2D_RESOURCE_C = struct D3D11_TEXTURE2D_RESOURCE_C
    {
        D3D11_TEXTURE2D_DESC_C Desc = {};
        D3D11_TEXTURE2D_DESC_C SourceDesc = {};

        ID3D11Texture2D* SourceTexture = nullptr;

        ID3D11Texture2D* SharedTexture = nullptr;
        ID3D12Resource* Dx12Resource = nullptr;

        HANDLE Dx11Handle = NULL;
        HANDLE Dx12Handle = NULL;
        ID3D12Device* OpenedDx12Device = nullptr;

        bool OwnsSharedTexture = false;
        bool UsesNTHandle = false;

        UINT64 LastPreparedFrame = 0;
        bool LastPreparedCopy = false;
        bool LastPreparedDepth = false;
    };

    using D3D11_UPSCALER_RESOURCE_CACHE_C = struct D3D11_UPSCALER_RESOURCE_CACHE_C
    {
        D3D11_TEXTURE2D_RESOURCE_C Color = {};
        D3D11_TEXTURE2D_RESOURCE_C Mv = {};
        D3D11_TEXTURE2D_RESOURCE_C Depth = {};
        D3D11_TEXTURE2D_RESOURCE_C Reactive = {};
        D3D11_TEXTURE2D_RESOURCE_C Exposure = {};
        D3D11_TEXTURE2D_RESOURCE_C Output[DX11_WITH_DX12_CACHED_FRAMES] = {};
        ID3D11Resource* ParamOutput[DX11_WITH_DX12_CACHED_FRAMES] = {};
        UINT64 frameId = 0;
    };

    inline static D3D11_UPSCALER_RESOURCE_CACHE_C UpscalerResourceCache = {};
    inline static UINT UpscalerFrameIndex = 0;
    inline static UINT64 UpscalerLocalFrameId = 0;
    inline static UINT64 LastPreparedUpscalerFrameId = 0;
    inline static ResourceMask LastPreparedUpscalerMask = ResourceMask::None;

    static bool CopyTextureFrom11To12(ID3D11Resource* InResource, D3D11_TEXTURE2D_RESOURCE_C* OutResource, bool InCopy,
                                      bool InDepth, bool InDontUseNTShared);

    static bool OpenHandle(std::string name, ID3D12Device* dx12Device, ID3D11Resource* resource,
                           D3D11_TEXTURE2D_RESOURCE_C* shared);

    static bool PrepareTextureFrom11To12(const std::string& name, ID3D12Device* dx12Device, ID3D11Resource* resource,
                                         D3D11_TEXTURE2D_RESOURCE_C* shared, bool copy, bool depth,
                                         bool dontUseNTShared, UINT64 frameId);

    static D3D11_UPSCALER_RESOURCE_CACHE_C& GetUpscalerResourceCache();
    static D3D11_TEXTURE2D_RESOURCE_C* GetUpscalerOutputResource(UINT frameIndex);

    static void SetUpscalerFrameIndex(UINT frameIndex);
    static UINT GetUpscalerFrameIndex();

    static UINT64 NextUpscalerFrameId();
    static void ResetUpscalerFrameId();

    static void ReleaseSharedResource(D3D11_TEXTURE2D_RESOURCE_C* resource);
    static void ResetUpscalerResourceCache(bool releaseSyncResources = false);

    static bool SyncDx11ToDx12();
    static bool SyncDx12ToDx11();
    static bool CopyUpscalerOutputToDx11(UINT frameIndex);

    static bool CheckMask(ResourceMask mask, ResourceMask resource);

    static UINT64 GetLastPreparedUpscalerFrameId();
    static ResourceMask GetLastPreparedUpscalerMask();

    static void ClearLastPreparedUpscalerFrameState();
    static bool HasPreparedUpscalerResources(ResourceMask mask, UINT64 frameId = 0);

    static PrepareResourcesResult PrepareUpscalerResources(const NVSDK_NGX_Parameter* parameters, ResourceMask mask,
                                                           UINT frameIndex, UINT64 frameId, bool dontUseNTShared,
                                                           bool reactiveRequired, bool syncAfterPrepare);

    static ID3D11Device5* GetD3D11Device();
    static ID3D11DeviceContext4* GetD3D11DeviceContext();
    static ID3D12Device* GetD3D12Device();
    static ID3D12CommandQueue* GetD3D12CommandQueue();

    static void Init(ID3D11Device* dx11Device, ID3D11DeviceContext* dx11Context);
    static void Init(ID3D11Device* dx11Device, ID3D11DeviceContext* dx11Context, ID3D12Device* dx12Device,
                     ID3D12CommandQueue* dx12CommandQueue);
};

inline Dx11WithDx12::ResourceMask operator|(Dx11WithDx12::ResourceMask a, Dx11WithDx12::ResourceMask b)
{
    return static_cast<Dx11WithDx12::ResourceMask>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline Dx11WithDx12::ResourceMask operator&(Dx11WithDx12::ResourceMask a, Dx11WithDx12::ResourceMask b)
{
    return static_cast<Dx11WithDx12::ResourceMask>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline Dx11WithDx12::ResourceMask& operator|=(Dx11WithDx12::ResourceMask& a, Dx11WithDx12::ResourceMask b)
{
    a = a | b;
    return a;
}
