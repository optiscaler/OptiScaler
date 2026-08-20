#include <pch.h>

#include "dx11_with_dx12.h"
#include "with_dx12.h"

#define ASSIGN_DESC(dest, src)                                                                                         \
    dest.Width = src.Width;                                                                                            \
    dest.Height = src.Height;                                                                                          \
    dest.MipLevels = src.MipLevels;                                                                                    \
    dest.ArraySize = src.ArraySize;                                                                                    \
    dest.Format = src.Format;                                                                                          \
    dest.SampleDesc = src.SampleDesc;                                                                                  \
    dest.Usage = src.Usage;                                                                                            \
    dest.BindFlags = src.BindFlags;                                                                                    \
    dest.CPUAccessFlags = src.CPUAccessFlags;                                                                          \
    dest.MiscFlags = src.MiscFlags;

bool TextureDescEquals(const Dx11WithDx12::D3D11_TEXTURE2D_DESC_C& a, const Dx11WithDx12::D3D11_TEXTURE2D_DESC_C& b)
{
    return a.Width == b.Width && a.Height == b.Height && a.MipLevels == b.MipLevels && a.ArraySize == b.ArraySize &&
           a.Format == b.Format && a.SampleDesc.Count == b.SampleDesc.Count &&
           a.SampleDesc.Quality == b.SampleDesc.Quality && a.Usage == b.Usage && a.BindFlags == b.BindFlags &&
           a.CPUAccessFlags == b.CPUAccessFlags && a.MiscFlags == b.MiscFlags;
}

bool D3D11DescEquals(const D3D11_TEXTURE2D_DESC& a, const D3D11_TEXTURE2D_DESC& b)
{
    return a.Width == b.Width && a.Height == b.Height && a.MipLevels == b.MipLevels && a.ArraySize == b.ArraySize &&
           a.Format == b.Format && a.SampleDesc.Count == b.SampleDesc.Count &&
           a.SampleDesc.Quality == b.SampleDesc.Quality && a.Usage == b.Usage && a.BindFlags == b.BindFlags &&
           a.CPUAccessFlags == b.CPUAccessFlags && a.MiscFlags == b.MiscFlags;
}

Dx11WithDx12::D3D11_TEXTURE2D_DESC_C CaptureDesc(const D3D11_TEXTURE2D_DESC& desc)
{
    Dx11WithDx12::D3D11_TEXTURE2D_DESC_C result = {};
    ASSIGN_DESC(result, desc);
    return result;
}

Dx11WithDx12::D3D11_UPSCALER_RESOURCE_CACHE_C& Dx11WithDx12::GetUpscalerResourceCache()
{
    return UpscalerResourceCache;
}

Dx11WithDx12::D3D11_TEXTURE2D_RESOURCE_C* Dx11WithDx12::GetUpscalerOutputResource(UINT frameIndex)
{
    return &UpscalerResourceCache.Output[frameIndex % DX11_WITH_DX12_CACHED_FRAMES];
}

void Dx11WithDx12::SetUpscalerFrameIndex(UINT frameIndex)
{
    UpscalerFrameIndex = frameIndex % DX11_WITH_DX12_CACHED_FRAMES;
}

UINT Dx11WithDx12::GetUpscalerFrameIndex() { return UpscalerFrameIndex; }

UINT64 Dx11WithDx12::NextUpscalerFrameId() { return ++UpscalerLocalFrameId; }

void Dx11WithDx12::ResetUpscalerFrameId() { UpscalerLocalFrameId = 0; }

UINT64 Dx11WithDx12::GetLastPreparedUpscalerFrameId() { return LastPreparedUpscalerFrameId; }

Dx11WithDx12::ResourceMask Dx11WithDx12::GetLastPreparedUpscalerMask() { return LastPreparedUpscalerMask; }

void Dx11WithDx12::ClearLastPreparedUpscalerFrameState()
{
    LastPreparedUpscalerFrameId = 0;
    LastPreparedUpscalerMask = ResourceMask::None;
}

ID3D11Device5* Dx11WithDx12::GetD3D11Device() { return Dx11Device; }

ID3D11DeviceContext4* Dx11WithDx12::GetD3D11DeviceContext() { return Dx11DeviceContext; }

ID3D12Device* Dx11WithDx12::GetD3D12Device() { return Dx12Device; }

ID3D12CommandQueue* Dx11WithDx12::GetD3D12CommandQueue() { return Dx12CommandQueue; }

void Dx11WithDx12::ReleaseSharedResource(D3D11_TEXTURE2D_RESOURCE_C* resource)
{
    if (resource == nullptr)
        return;

    SAFE_RELEASE(resource->Dx12Resource);

    if (resource->Dx11Handle != NULL && resource->UsesNTHandle)
        CloseHandle(resource->Dx11Handle);

    if (resource->OwnsSharedTexture)
        SAFE_RELEASE(resource->SharedTexture);

    resource->Desc = {};
    resource->SourceDesc = {};
    resource->SourceTexture = nullptr;
    resource->SharedTexture = nullptr;
    resource->Dx12Resource = nullptr;
    resource->Dx11Handle = NULL;
    resource->Dx12Handle = NULL;
    resource->OpenedDx12Device = nullptr;
    resource->OwnsSharedTexture = false;
    resource->UsesNTHandle = false;
    resource->LastPreparedFrame = 0;
    resource->LastPreparedCopy = false;
    resource->LastPreparedDepth = false;
}

void Dx11WithDx12::ReleaseSyncResourcesLocked()
{
    SAFE_RELEASE(Dx11FenceTextureCopy);
    SAFE_RELEASE(Dx12FenceTextureCopy);

    if (Dx11SharedHandleForTextureCopy != NULL)
    {
        CloseHandle(Dx11SharedHandleForTextureCopy);
        Dx11SharedHandleForTextureCopy = NULL;
    }

    TextureCopyFenceValue = 1;
    SyncDx11Device = nullptr;
    SyncDx12Device = nullptr;
    SyncDx12CommandQueue = nullptr;
}

void Dx11WithDx12::ReleaseSyncResources()
{
    std::lock_guard<std::mutex> lock(SyncMutex);
    ReleaseSyncResourcesLocked();
}

bool Dx11WithDx12::EnsureSyncResourcesLocked()
{
    if (Dx11Device == nullptr || Dx11DeviceContext == nullptr)
    {
        LOG_ERROR("Dx11WithDx12 sync called without resolved D3D11 device/context");
        return false;
    }

    if (WithDx12::IsInited())
    {
        Dx12Device = WithDx12::GetD3D12Device();
        Dx12CommandQueue = WithDx12::GetD3D12CommandQueue();
    }

    if (Dx12Device == nullptr || Dx12CommandQueue == nullptr)
    {
        LOG_ERROR("Dx11WithDx12 sync called without resolved D3D12 device/queue");
        return false;
    }

    if (SyncDx11Device != nullptr &&
        (SyncDx11Device != Dx11Device || SyncDx12Device != Dx12Device || SyncDx12CommandQueue != Dx12CommandQueue))
    {
        LOG_INFO("Dx11WithDx12 sync objects belong to an old device/queue; recreating them");
        ReleaseSyncResourcesLocked();
    }

    HRESULT result = S_OK;

    if (Dx11FenceTextureCopy == nullptr)
    {
        result = Dx11Device->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&Dx11FenceTextureCopy));

        if (result != S_OK || Dx11FenceTextureCopy == nullptr)
        {
            LOG_ERROR("Can't create Dx11WithDx12 texture copy fence: {:X}", (UINT) result);
            ReleaseSyncResourcesLocked();
            return false;
        }
    }

    if (Dx11SharedHandleForTextureCopy == NULL)
    {
        result =
            Dx11FenceTextureCopy->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &Dx11SharedHandleForTextureCopy);

        if (result != S_OK || Dx11SharedHandleForTextureCopy == NULL)
        {
            LOG_ERROR("Can't create shared handle for Dx11WithDx12 texture copy fence: {:X}", (UINT) result);
            ReleaseSyncResourcesLocked();
            return false;
        }
    }

    if (Dx12FenceTextureCopy == nullptr)
    {
        result = Dx12Device->OpenSharedHandle(Dx11SharedHandleForTextureCopy, IID_PPV_ARGS(&Dx12FenceTextureCopy));

        if (result != S_OK || Dx12FenceTextureCopy == nullptr)
        {
            LOG_ERROR("Can't open Dx11WithDx12 texture copy fence on D3D12: {:X}, device: {:X}, queue: {:X}",
                      (UINT) result, (size_t) Dx12Device, (size_t) Dx12CommandQueue);
            ReleaseSyncResourcesLocked();
            return false;
        }
    }

    if (Dx12FenceTextureCopy == nullptr)
    {
        LOG_ERROR("Dx11WithDx12 D3D12 fence is null before sync");
        ReleaseSyncResourcesLocked();
        return false;
    }

    SyncDx11Device = Dx11Device;
    SyncDx12Device = Dx12Device;
    SyncDx12CommandQueue = Dx12CommandQueue;
    return true;
}

bool Dx11WithDx12::SyncDx11ToDx12()
{
    std::lock_guard<std::mutex> lock(SyncMutex);

    if (!EnsureSyncResourcesLocked())
        return false;

    const auto fenceValue = TextureCopyFenceValue++;

    auto result = Dx11DeviceContext->Signal(Dx11FenceTextureCopy, fenceValue);
    if (result != S_OK)
    {
        LOG_ERROR("Dx11WithDx12 Dx11 signal failed for fence {}: {:X}", fenceValue, (UINT) result);
        return false;
    }

    Dx11DeviceContext->Flush();

    result = Dx12CommandQueue->Wait(Dx12FenceTextureCopy, fenceValue);
    if (result != S_OK)
    {
        LOG_ERROR("Dx11WithDx12 Dx12 wait failed for fence {}: {:X}", fenceValue, (UINT) result);
        ReleaseSyncResourcesLocked();
        return false;
    }

    return true;
}

bool Dx11WithDx12::SyncDx12ToDx11()
{
    std::lock_guard<std::mutex> lock(SyncMutex);

    if (!EnsureSyncResourcesLocked())
        return false;

    const auto fenceValue = TextureCopyFenceValue++;

    auto result = Dx12CommandQueue->Signal(Dx12FenceTextureCopy, fenceValue);
    if (result != S_OK)
    {
        LOG_ERROR("Dx11WithDx12 Dx12 signal failed for fence {}: {:X}", fenceValue, (UINT) result);
        ReleaseSyncResourcesLocked();
        return false;
    }

    result = Dx11DeviceContext->Wait(Dx11FenceTextureCopy, fenceValue);
    if (result != S_OK)
    {
        LOG_ERROR("Dx11WithDx12 Dx11 wait failed for fence {}: {:X}", fenceValue, (UINT) result);
        ReleaseSyncResourcesLocked();
        return false;
    }

    return true;
}

void Dx11WithDx12::ResetUpscalerResourceCache(bool releaseSyncResources)
{
    ReleaseSharedResource(&UpscalerResourceCache.Color);
    ReleaseSharedResource(&UpscalerResourceCache.Mv);
    ReleaseSharedResource(&UpscalerResourceCache.Depth);
    ReleaseSharedResource(&UpscalerResourceCache.Reactive);
    ReleaseSharedResource(&UpscalerResourceCache.Exposure);

    for (UINT i = 0; i < DX11_WITH_DX12_CACHED_FRAMES; ++i)
    {
        ReleaseSharedResource(&UpscalerResourceCache.Output[i]);
        UpscalerResourceCache.ParamOutput[i] = nullptr;
    }

    ClearLastPreparedUpscalerFrameState();

    if (releaseSyncResources)
        ReleaseSyncResources();

    ClearLastPreparedUpscalerFrameState();
    ResetUpscalerFrameId();
}

void Dx11WithDx12::Init(ID3D11Device* dx11Device, ID3D11DeviceContext* dx11Context)
{
    if (!WithDx12::PrepareD3D12ForD3D11(dx11Device, D3D_FEATURE_LEVEL_11_0))
    {
        LOG_ERROR("Dx11WithDx12::Init failed to resolve D3D12 device/queue");
        return;
    }

    Init(dx11Device, dx11Context, WithDx12::GetD3D12Device(), WithDx12::GetD3D12CommandQueue());
}

void Dx11WithDx12::Init(ID3D11Device* dx11Device, ID3D11DeviceContext* dx11Context, ID3D12Device* dx12Device,
                        ID3D12CommandQueue* dx12CommandQueue)
{
    if (dx11Context == nullptr)
    {
        LOG_ERROR("Dx11WithDx12::Init called with null D3D11 context");
        return;
    }

    if (dx12Device == nullptr || dx12CommandQueue == nullptr)
    {
        if (!WithDx12::PrepareD3D12ForD3D11(dx11Device, D3D_FEATURE_LEVEL_11_0))
        {
            LOG_ERROR("Dx11WithDx12::Init called without resolved D3D12 device/queue");
            return;
        }

        dx12Device = WithDx12::GetD3D12Device();
        dx12CommandQueue = WithDx12::GetD3D12CommandQueue();
    }

    ID3D11Device5* newDx11Device = nullptr;
    ID3D11DeviceContext4* newDx11Context = nullptr;

    auto contextResult = dx11Context->QueryInterface(IID_PPV_ARGS(&newDx11Context));
    if (contextResult != S_OK || newDx11Context == nullptr)
    {
        LOG_ERROR("QueryInterface ID3D11DeviceContext4 result: {0:x}", contextResult);
        return;
    }

    newDx11Context->Release();

    if (dx11Device == nullptr)
    {
        newDx11Context->GetDevice(&dx11Device);

        if (dx11Device != nullptr)
            dx11Device->Release();
    }

    auto dx11DeviceResult = dx11Device->QueryInterface(IID_PPV_ARGS(&newDx11Device));
    if (dx11DeviceResult != S_OK || newDx11Device == nullptr)
    {
        LOG_ERROR("QueryInterface ID3D11Device5 result: {0:x}", dx11DeviceResult);
        newDx11Context->Release();
        return;
    }

    newDx11Device->Release();

    const bool dx11Changed = newDx11Device != Dx11Device || newDx11Context != Dx11DeviceContext;
    const bool dx12Changed = dx12Device != Dx12Device || dx12CommandQueue != Dx12CommandQueue;

    if (dx11Changed || dx12Changed)
    {
        ResetUpscalerResourceCache(true);

        if (DT != nullptr || DT.get() != nullptr)
            DT.reset();

        DepthTransferSourceDesc = {};
        DepthTransferSourceDescValid = false;

        DT = std::make_unique<DepthTransfer_Dx11>("DT", newDx11Device);
    }

    Dx11DeviceContext = newDx11Context;
    Dx11Device = newDx11Device;
    Dx12Device = dx12Device;
    Dx12CommandQueue = dx12CommandQueue;
}

bool Dx11WithDx12::CopyTextureFrom11To12(ID3D11Resource* InResource, D3D11_TEXTURE2D_RESOURCE_C* OutResource,
                                         bool InCopy, bool InDepth, bool InDontUseNTShared)
{
    if (InResource == nullptr || OutResource == nullptr || Dx11Device == nullptr || Dx11DeviceContext == nullptr)
        return false;

    ID3D11Texture2D* originalTexture = nullptr;
    D3D11_TEXTURE2D_DESC sourceDesc = {};

    auto result = InResource->QueryInterface(IID_PPV_ARGS(&originalTexture));

    if (result != S_OK || originalTexture == nullptr)
        return false;

    originalTexture->GetDesc(&sourceDesc);
    const auto sourceDescCache = CaptureDesc(sourceDesc);

    const bool sourceHasLegacyShared = (sourceDesc.MiscFlags & D3D11_RESOURCE_MISC_SHARED) != 0;
    const bool sourceHasNTShared = (sourceDesc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) != 0;

    if (!sourceHasLegacyShared && !sourceHasNTShared && !InDontUseNTShared)
    {
        D3D11_TEXTURE2D_DESC sharedDesc = sourceDesc;
        sharedDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        sharedDesc.Usage = D3D11_USAGE_DEFAULT;
        sharedDesc.CPUAccessFlags = 0;

        const auto sharedDescCache = CaptureDesc(sharedDesc);
        const bool sharedTextureMatches = TextureDescEquals(OutResource->Desc, sharedDescCache) &&
                                          OutResource->SharedTexture != nullptr && OutResource->Dx11Handle != NULL &&
                                          OutResource->LastPreparedCopy == InCopy &&
                                          OutResource->LastPreparedDepth == InDepth &&
                                          (OutResource->Desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) != 0;

        if (!sharedTextureMatches)
        {
            ReleaseSharedResource(OutResource);
            OutResource->Desc = sharedDescCache;

            result = Dx11Device->CreateTexture2D(&sharedDesc, nullptr, &OutResource->SharedTexture);

            if (result != S_OK || OutResource->SharedTexture == nullptr)
            {
                LOG_ERROR("CreateTexture2D error: {0:x}", result);
                originalTexture->Release();
                return false;
            }

            OutResource->OwnsSharedTexture = true;
            OutResource->UsesNTHandle = true;

            IDXGIResource1* resource = nullptr;
            result = OutResource->SharedTexture->QueryInterface(IID_PPV_ARGS(&resource));

            if (result != S_OK || resource == nullptr)
            {
                LOG_ERROR("QueryInterface(resource) error: {0:x}", result);
                originalTexture->Release();
                return false;
            }

            DWORD access = DXGI_SHARED_RESOURCE_READ;

            if (!InCopy)
                access |= DXGI_SHARED_RESOURCE_WRITE;

            result = resource->CreateSharedHandle(NULL, access, NULL, &OutResource->Dx11Handle);
            resource->Release();

            if (result != S_OK || OutResource->Dx11Handle == NULL)
            {
                LOG_ERROR("CreateSharedHandle error: {0:x}", result);
                originalTexture->Release();
                return false;
            }
        }

        if (InCopy && OutResource->SharedTexture != nullptr)
            Dx11DeviceContext->CopyResource(OutResource->SharedTexture, InResource);
    }
    else if (!sourceHasLegacyShared && InDontUseNTShared)
    {
        if (InDepth &&
            (sourceDesc.Format == DXGI_FORMAT_R24G8_TYPELESS || sourceDesc.Format == DXGI_FORMAT_R32G8X24_TYPELESS))
        {
            const bool depthTransferDescChanged =
                !DepthTransferSourceDescValid || !D3D11DescEquals(DepthTransferSourceDesc, sourceDesc);

            if (depthTransferDescChanged)
            {
                if (DT != nullptr && OutResource->SharedTexture == DT->Buffer())
                    ReleaseSharedResource(OutResource);

                DT.reset();
                DepthTransferSourceDesc = sourceDesc;
                DepthTransferSourceDescValid = true;
            }

            if (DT == nullptr || DT.get() == nullptr)
                DT = std::make_unique<DepthTransfer_Dx11>("DT", Dx11Device);

            if (DT->Buffer() == nullptr)
                DT->CreateBufferResource(Dx11Device, InResource);

            if (DT->Buffer() == nullptr)
            {
                LOG_ERROR("DepthTransfer_Dx11 buffer creation failed");
                originalTexture->Release();
                return false;
            }

            if (DT->Dispatch(Dx11Device, Dx11DeviceContext, originalTexture, DT->Buffer()))
            {
                if (OutResource->SharedTexture != DT->Buffer())
                    ReleaseSharedResource(OutResource);

                IDXGIResource1* resource = nullptr;
                result = DT->Buffer()->QueryInterface(IID_PPV_ARGS(&resource));

                if (result != S_OK || resource == nullptr)
                {
                    LOG_ERROR("QueryInterface(resource) error: {0:x}", result);
                    originalTexture->Release();
                    return false;
                }

                D3D11_TEXTURE2D_DESC transferDesc = {};
                DT->Buffer()->GetDesc(&transferDesc);
                ASSIGN_DESC(OutResource->Desc, transferDesc);
                OutResource->SharedTexture = DT->Buffer();
                OutResource->OwnsSharedTexture = false;
                OutResource->UsesNTHandle = false;

                result = resource->GetSharedHandle(&OutResource->Dx11Handle);
                resource->Release();

                if (result != S_OK || OutResource->Dx11Handle == NULL)
                {
                    LOG_ERROR("GetSharedHandle error: {0:x}", result);
                    originalTexture->Release();
                    return false;
                }
            }
            else
            {
                LOG_ERROR("DepthTransfer_Dx11 dispatch failed");
                originalTexture->Release();
                return false;
            }
        }
        else
        {
            D3D11_TEXTURE2D_DESC sharedDesc = sourceDesc;

            if (sharedDesc.Format == DXGI_FORMAT_R24G8_TYPELESS)
                sharedDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            sharedDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
            sharedDesc.Usage = D3D11_USAGE_DEFAULT;
            sharedDesc.CPUAccessFlags = 0;

            const auto sharedDescCache = CaptureDesc(sharedDesc);
            const bool sharedTextureMatches =
                TextureDescEquals(OutResource->Desc, sharedDescCache) && OutResource->SharedTexture != nullptr &&
                OutResource->Dx11Handle != NULL && OutResource->LastPreparedCopy == InCopy &&
                OutResource->LastPreparedDepth == InDepth &&
                (OutResource->Desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) == 0;

            if (!sharedTextureMatches)
            {
                ReleaseSharedResource(OutResource);
                OutResource->Desc = sharedDescCache;

                result = Dx11Device->CreateTexture2D(&sharedDesc, nullptr, &OutResource->SharedTexture);
                if (result != S_OK || OutResource->SharedTexture == nullptr)
                {
                    LOG_ERROR("CreateTexture2D error: {0:x}", result);
                    originalTexture->Release();
                    return false;
                }

                OutResource->OwnsSharedTexture = true;
                OutResource->UsesNTHandle = false;

                IDXGIResource1* resource = nullptr;
                result = OutResource->SharedTexture->QueryInterface(IID_PPV_ARGS(&resource));

                if (result != S_OK || resource == nullptr)
                {
                    LOG_ERROR("QueryInterface(resource) error: {0:x}", result);
                    originalTexture->Release();
                    return false;
                }

                result = resource->GetSharedHandle(&OutResource->Dx11Handle);
                resource->Release();

                if (result != S_OK || OutResource->Dx11Handle == NULL)
                {
                    LOG_ERROR("GetSharedHandle error: {0:x}", result);
                    originalTexture->Release();
                    return false;
                }
            }

            if (InCopy && OutResource->SharedTexture != nullptr)
                Dx11DeviceContext->CopyResource(OutResource->SharedTexture, InResource);
        }
    }
    else
    {
        // Source is already shared
        const bool accessModeChanged =
            OutResource->LastPreparedCopy != InCopy || OutResource->LastPreparedDepth != InDepth;
        const bool sourceDescChanged = !TextureDescEquals(OutResource->SourceDesc, sourceDescCache);

        if (OutResource->SharedTexture != originalTexture || accessModeChanged || sourceDescChanged ||
            OutResource->Dx11Handle == NULL)
        {
            ReleaseSharedResource(OutResource);

            IDXGIResource1* resource = nullptr;
            result = originalTexture->QueryInterface(IID_PPV_ARGS(&resource));

            if (result != S_OK || resource == nullptr)
            {
                LOG_ERROR("QueryInterface(resource) error: {0:x}", result);
                originalTexture->Release();
                return false;
            }

            if (sourceHasNTShared)
            {
                DWORD access = DXGI_SHARED_RESOURCE_READ;

                if (!InCopy)
                    access |= DXGI_SHARED_RESOURCE_WRITE;

                result = resource->CreateSharedHandle(NULL, access, NULL, &OutResource->Dx11Handle);
                OutResource->UsesNTHandle = true;
            }
            else
            {
                result = resource->GetSharedHandle(&OutResource->Dx11Handle);
                OutResource->UsesNTHandle = false;
            }

            resource->Release();

            if (result != S_OK || OutResource->Dx11Handle == NULL)
            {
                LOG_ERROR("GetSharedHandle/CreateSharedHandle error: {0:x}", result);
                originalTexture->Release();
                return false;
            }

            OutResource->Desc = sourceDescCache;
            OutResource->SharedTexture = originalTexture;
            OutResource->OwnsSharedTexture = false;
        }
    }

    OutResource->SourceTexture = originalTexture;
    OutResource->SourceDesc = sourceDescCache;

    originalTexture->Release();
    return true;
}

bool Dx11WithDx12::PrepareTextureFrom11To12(const std::string& name, ID3D12Device* dx12Device, ID3D11Resource* resource,
                                            D3D11_TEXTURE2D_RESOURCE_C* shared, bool copy, bool depth,
                                            bool dontUseNTShared, UINT64 frameId)
{
    if (resource == nullptr || shared == nullptr || dx12Device == nullptr)
        return false;

    ID3D11Texture2D* texture = nullptr;
    auto result = resource->QueryInterface(IID_PPV_ARGS(&texture));
    if (result != S_OK || texture == nullptr)
    {
        LOG_ERROR("{} is not a D3D11 texture: {:X}", name, (UINT) result);
        return false;
    }

    D3D11_TEXTURE2D_DESC sourceDesc = {};
    texture->GetDesc(&sourceDesc);
    const auto sourceDescCache = CaptureDesc(sourceDesc);

    const bool sameSource = shared->SourceTexture == texture;
    const bool sameSourceDesc = TextureDescEquals(shared->SourceDesc, sourceDescCache);
    const bool sameFrame = frameId != 0 && shared->LastPreparedFrame == frameId;
    const bool sameUsage = shared->LastPreparedCopy == copy && shared->LastPreparedDepth == depth;
    const bool sameDx12Device = shared->OpenedDx12Device == dx12Device;
    const bool hasOpenDx12Resource = shared->Dx12Resource != nullptr && shared->Dx12Handle == shared->Dx11Handle;

    texture->Release();

    if (sameSource && sameSourceDesc && sameFrame && sameUsage && sameDx12Device && hasOpenDx12Resource)
    {
        LOG_TRACE("{} cache hit for frame {}", name, frameId);
        return true;
    }

    if (!CopyTextureFrom11To12(resource, shared, copy, depth, dontUseNTShared))
    {
        LOG_ERROR("{} D3D11 to D3D12 copy/prepare failed", name);
        return false;
    }

    if (!OpenHandle(name, dx12Device, resource, shared))
    {
        LOG_ERROR("{} D3D12 shared handle open failed", name);
        return false;
    }

    // mark resource info
    shared->LastPreparedFrame = frameId;
    shared->LastPreparedCopy = copy;
    shared->LastPreparedDepth = depth;

    return true;
}

bool Dx11WithDx12::OpenHandle(std::string name, ID3D12Device* dx12Device, ID3D11Resource* resource,
                              D3D11_TEXTURE2D_RESOURCE_C* shared)
{
    if (resource == nullptr || shared == nullptr || dx12Device == nullptr)
        return false;

    if (shared->Dx11Handle == NULL)
    {
        LOG_ERROR("{} shared handle is null", name);
        return false;
    }

    if (shared->Dx12Resource == nullptr || shared->Dx12Handle != shared->Dx11Handle ||
        shared->OpenedDx12Device != dx12Device)
    {
        SAFE_RELEASE(shared->Dx12Resource);
        shared->Dx12Handle = NULL;
        shared->OpenedDx12Device = nullptr;

        auto result = dx12Device->OpenSharedHandle(shared->Dx11Handle, IID_PPV_ARGS(&shared->Dx12Resource));

        if (result != S_OK || shared->Dx12Resource == nullptr)
        {
            LOG_ERROR("{} OpenSharedHandle error: {:X}", name, result);
            shared->Dx12Handle = NULL;
            shared->OpenedDx12Device = nullptr;
            return false;
        }

        // Do not close Dx12Handle
        // Close Dx11Handle only when UsesNTHandle is true.
        shared->Dx12Handle = shared->Dx11Handle;
        shared->OpenedDx12Device = dx12Device;
    }

    return true;
}

bool Dx11WithDx12::CheckMask(ResourceMask mask, ResourceMask resource)
{
    return (mask & resource) != ResourceMask::None;
}

bool Dx11WithDx12::HasPreparedUpscalerResources(ResourceMask mask, UINT64 frameId)
{
    const auto currentFrameId = frameId != 0 ? frameId : LastPreparedUpscalerFrameId;

    if (currentFrameId == 0)
        return false;

    if ((LastPreparedUpscalerMask & mask) != mask)
        return false;

    auto& cache = GetUpscalerResourceCache();
    const auto outputIndex = UpscalerFrameIndex % DX11_WITH_DX12_CACHED_FRAMES;

    if (CheckMask(mask, ResourceMask::Color) &&
        (cache.Color.Dx12Resource == nullptr || cache.Color.LastPreparedFrame != currentFrameId))
    {
        return false;
    }

    if (CheckMask(mask, ResourceMask::Mv) &&
        (cache.Mv.Dx12Resource == nullptr || cache.Mv.LastPreparedFrame != currentFrameId))
    {
        return false;
    }

    if (CheckMask(mask, ResourceMask::Depth) &&
        (cache.Depth.Dx12Resource == nullptr || cache.Depth.LastPreparedFrame != currentFrameId))
    {
        return false;
    }

    if (CheckMask(mask, ResourceMask::Output) && (cache.Output[outputIndex].Dx12Resource == nullptr ||
                                                  cache.Output[outputIndex].LastPreparedFrame != currentFrameId))
    {
        return false;
    }

    if (CheckMask(mask, ResourceMask::Exposure) &&
        (cache.Exposure.Dx12Resource == nullptr || cache.Exposure.LastPreparedFrame != currentFrameId))
    {
        return false;
    }

    if (CheckMask(mask, ResourceMask::Reactive) &&
        (cache.Reactive.Dx12Resource == nullptr || cache.Reactive.LastPreparedFrame != currentFrameId))
    {
        return false;
    }

    return true;
}

namespace
{
bool GetNgxD3D11Resource(const NVSDK_NGX_Parameter* parameters, const char* paramName, ID3D11Resource** outResource)
{
    if (parameters == nullptr || paramName == nullptr || outResource == nullptr)
        return false;

    *outResource = nullptr;

    if (parameters->Get(paramName, outResource) != NVSDK_NGX_Result_Success)
        parameters->Get(paramName, (void**) outResource);

    return *outResource != nullptr;
}

bool PrepareCachedResource(const char* name, const NVSDK_NGX_Parameter* parameters, const char* paramName,
                           Dx11WithDx12::D3D11_TEXTURE2D_RESOURCE_C* cacheEntry, bool copy, bool depth, bool required,
                           bool dontUseNTShared, UINT64 frameId, ID3D12Device* dx12Device, bool* missing)
{
    if (missing != nullptr)
        *missing = false;

    if (name == nullptr || parameters == nullptr || paramName == nullptr || cacheEntry == nullptr ||
        dx12Device == nullptr)
    {
        return false;
    }

    ID3D11Resource* resource = nullptr;
    if (!GetNgxD3D11Resource(parameters, paramName, &resource))
    {
        if (missing != nullptr)
            *missing = true;

        if (required)
            LOG_ERROR("{} D3D11 resource is missing", name);
        else
            LOG_DEBUG("{} D3D11 resource is missing", name);

        return !required;
    }

    return Dx11WithDx12::PrepareTextureFrom11To12(name, dx12Device, resource, cacheEntry, copy, depth, dontUseNTShared,
                                                  frameId);
}
} // namespace

Dx11WithDx12::PrepareResourcesResult Dx11WithDx12::PrepareUpscalerResources(const NVSDK_NGX_Parameter* parameters,
                                                                            ResourceMask mask, UINT frameIndex,
                                                                            UINT64 frameId, bool dontUseNTShared,
                                                                            bool reactiveRequired,
                                                                            bool syncAfterPrepare)
{
    PrepareResourcesResult result = {};

    if (parameters == nullptr)
    {
        LOG_ERROR("PrepareUpscalerResources called with null parameters");
        return result;
    }

    if (WithDx12::IsInited())
    {
        Dx12Device = WithDx12::GetD3D12Device();
        Dx12CommandQueue = WithDx12::GetD3D12CommandQueue();
    }

    if (Dx12Device == nullptr || Dx12CommandQueue == nullptr)
    {
        LOG_ERROR("PrepareUpscalerResources called without resolved D3D12 device/queue");
        return result;
    }

    auto& cache = GetUpscalerResourceCache();
    const auto outputIndex = frameIndex % DX11_WITH_DX12_CACHED_FRAMES;
    cache.frameId = frameId;
    bool ok = true;
    bool missing = false;

    if (CheckMask(mask, ResourceMask::Color))
    {
        missing = false;
        ok &= PrepareCachedResource("Color", parameters, NVSDK_NGX_Parameter_Color, &cache.Color, true, false, true,
                                    dontUseNTShared, frameId, Dx12Device, &missing);
        result.MissingColor = missing;
    }

    if (CheckMask(mask, ResourceMask::Mv))
    {
        missing = false;
        ok &= PrepareCachedResource("MotionVectors", parameters, NVSDK_NGX_Parameter_MotionVectors, &cache.Mv, true,
                                    false, true, dontUseNTShared, frameId, Dx12Device, &missing);
        result.MissingMv = missing;
    }

    if (CheckMask(mask, ResourceMask::Depth))
    {
        missing = false;
        ok &= PrepareCachedResource("Depth", parameters, NVSDK_NGX_Parameter_Depth, &cache.Depth, true, true, true,
                                    dontUseNTShared, frameId, Dx12Device, &missing);
        result.MissingDepth = missing;
    }

    if (CheckMask(mask, ResourceMask::Output))
    {
        ID3D11Resource* paramOutput = nullptr;
        if (GetNgxD3D11Resource(parameters, NVSDK_NGX_Parameter_Output, &paramOutput))
            cache.ParamOutput[outputIndex] = paramOutput;

        missing = false;
        ok &= PrepareCachedResource("Output", parameters, NVSDK_NGX_Parameter_Output, &cache.Output[outputIndex], false,
                                    false, true, dontUseNTShared, frameId, Dx12Device, &missing);
        result.MissingOutput = missing;
    }

    if (CheckMask(mask, ResourceMask::Exposure))
    {
        missing = false;
        ok &= PrepareCachedResource("Exposure", parameters, NVSDK_NGX_Parameter_ExposureTexture, &cache.Exposure, true,
                                    false, true, dontUseNTShared, frameId, Dx12Device, &missing);
        result.MissingExposure = missing;
    }

    if (CheckMask(mask, ResourceMask::Reactive))
    {
        missing = false;
        ok &= PrepareCachedResource("ReactiveMask", parameters, NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask,
                                    &cache.Reactive, true, false, reactiveRequired, dontUseNTShared, frameId,
                                    Dx12Device, &missing);
        result.MissingReactive = missing;
    }

    if (ok && syncAfterPrepare && !SyncDx11ToDx12())
    {
        LOG_ERROR("PrepareUpscalerResources sync failed");
        ok = false;
    }

    result.Success = ok;

    if (ok)
    {
        UpscalerFrameIndex = outputIndex;
        LastPreparedUpscalerFrameId = frameId;
        LastPreparedUpscalerMask = mask;
    }

    return result;
}

bool Dx11WithDx12::CopyUpscalerOutputToDx11(UINT frameIndex)
{
    if (Dx11DeviceContext == nullptr)
    {
        LOG_ERROR("CopyUpscalerOutputToDx11 called without resolved D3D11 context");
        return false;
    }

    auto& cache = GetUpscalerResourceCache();
    const auto outputIndex = frameIndex % DX11_WITH_DX12_CACHED_FRAMES;

    if (cache.ParamOutput[outputIndex] == nullptr || cache.Output[outputIndex].SharedTexture == nullptr)
    {
        LOG_ERROR("CopyUpscalerOutputToDx11 missing output resources for frame {}", outputIndex);
        return false;
    }

    if (!SyncDx12ToDx11())
    {
        LOG_ERROR("CopyUpscalerOutputToDx11 sync failed for frame {}", outputIndex);
        return false;
    }

    Dx11DeviceContext->CopyResource(cache.ParamOutput[outputIndex], cache.Output[outputIndex].SharedTexture);
    Dx11DeviceContext->Flush();
    return true;
}
