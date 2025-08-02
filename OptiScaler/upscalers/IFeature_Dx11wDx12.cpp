#include "IFeature_Dx11wDx12.h"

#include <Config.h>

#define ASSIGN_DESC(dest, src)                                                                                         \
    dest.Width = src.Width;                                                                                            \
    dest.Height = src.Height;                                                                                          \
    dest.Format = src.Format;                                                                                          \
    dest.BindFlags = src.BindFlags;                                                                                    \
    dest.MiscFlags = src.MiscFlags;

#define SAFE_RELEASE(p)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        if (p && p != nullptr)                                                                                         \
        {                                                                                                              \
            (p)->Release();                                                                                            \
            (p) = nullptr;                                                                                             \
        }                                                                                                              \
    } while ((void) 0, 0)

void IFeature_Dx11wDx12::ResourceBarrier(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource,
                                         D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = beforeState;
    barrier.Transition.StateAfter = afterState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
}

bool IFeature_Dx11wDx12::CopyTextureFrom11To12(ID3D11Resource* InResource, D3D11_TEXTURE2D_RESOURCE_C* OutResource,
                                               bool InCopy, bool InDontUseNTShared)
{
    ID3D11Texture2D* originalTexture = nullptr;
    D3D11_TEXTURE2D_DESC desc {};

    auto result = InResource->QueryInterface(IID_PPV_ARGS(&originalTexture));

    if (result != S_OK || originalTexture == nullptr)
        return false;

    originalTexture->GetDesc(&desc);

    // check shared nt handle usage later
    if (!(desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED) && !(desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) &&
        !InDontUseNTShared)
    {
        if (desc.Width != OutResource->Desc.Width || desc.Height != OutResource->Desc.Height ||
            desc.Format != OutResource->Desc.Format || desc.BindFlags != OutResource->Desc.BindFlags ||
            OutResource->SharedTexture == nullptr ||
            !(OutResource->Desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE))
        {
            if (OutResource->SharedTexture != nullptr)
            {
                OutResource->SharedTexture->Release();

                if (OutResource->Dx12Handle != NULL &&
                    (OutResource->Desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE))
                    CloseHandle(OutResource->Dx12Handle);

                OutResource->Dx11Handle = NULL;
                OutResource->Dx12Handle = NULL;
            }

            desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
            desc.Usage = D3D11_USAGE_DEFAULT;

            ASSIGN_DESC(OutResource->Desc, desc);

            result = Dx11Device->CreateTexture2D(&desc, nullptr, &OutResource->SharedTexture);

            if (result != S_OK)
            {
                LOG_ERROR("CreateTexture2D error: {0:x}", result);
                return false;
            }

            IDXGIResource1* resource;

            result = OutResource->SharedTexture->QueryInterface(IID_PPV_ARGS(&resource));

            if (result != S_OK)
            {
                LOG_ERROR("QueryInterface(resource) error: {0:x}", result);
                return false;
            }

            // Get shared handle
            DWORD access = DXGI_SHARED_RESOURCE_READ;

            if (!InCopy)
                access |= DXGI_SHARED_RESOURCE_WRITE;

            result = resource->CreateSharedHandle(NULL, access, NULL, &OutResource->Dx11Handle);

            if (result != S_OK)
            {
                LOG_ERROR("GetSharedHandle error: {0:x}", result);
                return false;
            }

            resource->Release();
        }

        if (InCopy && OutResource->SharedTexture != nullptr)
            Dx11DeviceContext->CopyResource(OutResource->SharedTexture, InResource);
    }
    else if ((desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED) == 0 && InDontUseNTShared)
    {
        if (desc.Format == DXGI_FORMAT_R24G8_TYPELESS || desc.Format == DXGI_FORMAT_R32G8X24_TYPELESS)
        {
            if (DT == nullptr || DT.get() == nullptr)
                DT = std::make_unique<DepthTransfer_Dx11>("DT", Dx11Device);

            if (DT->Buffer() == nullptr)
                DT->CreateBufferResource(Dx11Device, InResource);

            if (DT->Dispatch(Dx11Device, Dx11DeviceContext, originalTexture, DT->Buffer()))
            {
                IDXGIResource1* resource = nullptr;
                result = DT->Buffer()->QueryInterface(IID_PPV_ARGS(&resource));

                if (result != S_OK || resource == nullptr)
                {
                    LOG_ERROR("QueryInterface(resource) error: {0:x}", result);
                    return false;
                }

                // Get shared handle
                result = resource->GetSharedHandle(&OutResource->Dx11Handle);

                if (result != S_OK)
                {
                    LOG_ERROR("GetSharedHandle error: {0:x}", result);
                    resource->Release();
                    return false;
                }

                resource->Release();
            }
        }
        else
        {
            if (desc.Width != OutResource->Desc.Width || desc.Height != OutResource->Desc.Height ||
                desc.Format != OutResource->Desc.Format || desc.BindFlags != OutResource->Desc.BindFlags ||
                OutResource->SharedTexture == nullptr ||
                (OutResource->Desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE))
            {
                if (OutResource->SharedTexture != nullptr)
                {
                    OutResource->SharedTexture->Release();

                    if (OutResource->Dx12Handle != NULL &&
                        (OutResource->Desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE))
                        CloseHandle(OutResource->Dx12Handle);

                    OutResource->Dx11Handle = NULL;
                    OutResource->Dx12Handle = NULL;
                }

                if (desc.Format == DXGI_FORMAT_R24G8_TYPELESS)
                    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

                desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
                ASSIGN_DESC(OutResource->Desc, desc);
                desc.Usage = D3D11_USAGE_DEFAULT;

                result = Dx11Device->CreateTexture2D(&desc, nullptr, &OutResource->SharedTexture);

                IDXGIResource1* resource;
                result = OutResource->SharedTexture->QueryInterface(IID_PPV_ARGS(&resource));

                if (result != S_OK)
                {
                    LOG_ERROR("QueryInterface(resource) error: {0:x}", result);
                    return false;
                }

                // Get shared handle
                result = resource->GetSharedHandle(&OutResource->Dx11Handle);

                if (result != S_OK)
                {
                    LOG_ERROR("GetSharedHandle error: {0:x}", result);
                    resource->Release();
                    return false;
                }

                resource->Release();
            }

            if (InCopy && OutResource->SharedTexture != nullptr)
                Dx11DeviceContext->CopyResource(OutResource->SharedTexture, InResource);
        }
    }
    else
    {
        if (OutResource->SharedTexture != InResource)
        {
            IDXGIResource1* resource;

            result = originalTexture->QueryInterface(IID_PPV_ARGS(&resource));

            if (result != S_OK || resource == nullptr)
            {
                LOG_ERROR("QueryInterface(resource) error: {0:x}", result);
                return false;
            }

            // Get shared handle
            if ((desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED) != 0 &&
                (desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) != 0)
            {
                DWORD access = DXGI_SHARED_RESOURCE_READ;

                if (!InCopy)
                    access |= DXGI_SHARED_RESOURCE_WRITE;

                result = resource->CreateSharedHandle(NULL, access, NULL, &OutResource->Dx11Handle);
            }
            else
            {
                result = resource->GetSharedHandle(&OutResource->Dx11Handle);
            }

            if (result != S_OK)
            {
                LOG_ERROR("GetSharedHandle error: {0:x}", result);
                return false;
            }

            resource->Release();

            OutResource->SharedTexture = (ID3D11Texture2D*) InResource;
        }
    }

    originalTexture->Release();
    return true;
}

void IFeature_Dx11wDx12::ReleaseSharedResources()
{
    SAFE_RELEASE(dx11Color.SharedTexture);
    SAFE_RELEASE(dx11Mv.SharedTexture);
    SAFE_RELEASE(dx11Out.SharedTexture);
    SAFE_RELEASE(dx11Depth.SharedTexture);
    SAFE_RELEASE(dx11Reactive.SharedTexture);
    SAFE_RELEASE(dx11Exp.SharedTexture);
    SAFE_RELEASE(dx11Color.Dx12Resource);
    SAFE_RELEASE(dx11Mv.Dx12Resource);
    SAFE_RELEASE(dx11Out.Dx12Resource);
    SAFE_RELEASE(dx11Depth.Dx12Resource);
    SAFE_RELEASE(dx11Reactive.Dx12Resource);
    SAFE_RELEASE(dx11Exp.Dx12Resource);

    ReleaseSyncResources();

    SAFE_RELEASE(Dx12CommandList[0]);
    SAFE_RELEASE(Dx12CommandList[1]);
    SAFE_RELEASE(Dx12CommandQueue);
    SAFE_RELEASE(Dx12CommandAllocator[0]);
    SAFE_RELEASE(Dx12CommandAllocator[1]);
    SAFE_RELEASE(Dx12Fence);

    if (Dx12FenceEvent)
    {
        CloseHandle(Dx12FenceEvent);
        Dx12FenceEvent = nullptr;
    }

    // SAFE_RELEASE(Dx12Device);
}

void IFeature_Dx11wDx12::ReleaseSyncResources()
{
    SAFE_RELEASE(dx11FenceTextureCopy);
    SAFE_RELEASE(dx12FenceTextureCopy);

    if (dx11SHForTextureCopy != NULL)
    {
        CloseHandle(dx11SHForTextureCopy);
        dx11SHForTextureCopy = NULL;
    }
}

void IFeature_Dx11wDx12::GetHardwareAdapter(IDXGIFactory1* InFactory, IDXGIAdapter** InAdapter,
                                            D3D_FEATURE_LEVEL InFeatureLevel, bool InRequestHighPerformanceAdapter)
{
    LOG_FUNC();

    *InAdapter = nullptr;

    IDXGIAdapter1* adapter;

    IDXGIFactory6* factory6;
    if (SUCCEEDED(InFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
    {
        LOG_DEBUG("Using IDXGIFactory6 & EnumAdapterByGpuPreference");

        for (UINT adapterIndex = 0;
             DXGI_ERROR_NOT_FOUND != factory6->EnumAdapterByGpuPreference(adapterIndex,
                                                                          InRequestHighPerformanceAdapter == true
                                                                              ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE
                                                                              : DXGI_GPU_PREFERENCE_UNSPECIFIED,
                                                                          IID_PPV_ARGS(&adapter));
             ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;

            *InAdapter = adapter;
            break;

            // Check to see whether the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            // auto result = D3D12CreateDevice(adapter, InFeatureLevel, _uuidof(ID3D12Device), nullptr);
            // LOG_DEBUG("D3D12CreateDevice test result: {:X}", (UINT) result);

            // if (result == S_FALSE)
            //{
            //     *InAdapter = adapter
            //     break;
            //}
        }
    }
    else
    {
        LOG_DEBUG("Using InFactory & EnumAdapters1");
        for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != InFactory->EnumAdapters1(adapterIndex, &adapter);
             ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;

            // Check to see whether the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            auto result = D3D12CreateDevice(adapter, InFeatureLevel, _uuidof(ID3D12Device), nullptr);

            if (result == S_FALSE)
            {
                LOG_DEBUG("D3D12CreateDevice test result: {:X}", (UINT) result);
                *InAdapter = adapter;
                break;
            }
        }
    }
}

HRESULT IFeature_Dx11wDx12::CreateDx12Device(D3D_FEATURE_LEVEL InFeatureLevel)
{
    LOG_FUNC();

    State::Instance().vulkanSkipHooks = true;
    State::Instance().skipSpoofing = true;

    HRESULT result;

    if (State::Instance().currentD3D12Device == nullptr)
    {
        IDXGIFactory4* factory;
        result = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));

        if (result != S_OK)
        {
            LOG_ERROR("Can't create factory: {0:x}", result);
            State::Instance().vulkanSkipHooks = false;
            State::Instance().skipSpoofing = false;
            return result;
        }

        IDXGIAdapter* hardwareAdapter = nullptr;
        GetHardwareAdapter(factory, &hardwareAdapter, InFeatureLevel, true);

        if (hardwareAdapter == nullptr)
            LOG_WARN("Can't get hardwareAdapter, will try nullptr!");

        result =
            D3D12CreateDevice(hardwareAdapter, InFeatureLevel, IID_PPV_ARGS(&State::Instance().currentD3D12Device));

        if (result != S_OK)
        {
            LOG_ERROR("Can't create device: {:X}", (UINT) result);
            State::Instance().vulkanSkipHooks = false;
            State::Instance().skipSpoofing = false;
            return result;
        }

        if (hardwareAdapter != nullptr)
        {
            DXGI_ADAPTER_DESC desc {};
            if (hardwareAdapter->GetDesc(&desc) == S_OK)
            {
                auto adapterDesc = wstring_to_string(desc.Description);
                LOG_INFO("D3D12Device created with adapter: {}", adapterDesc);
                State::Instance().DeviceAdapterNames[State::Instance().currentD3D12Device] = adapterDesc;
            }
        }
    }

    if (Dx12CommandQueue == nullptr)
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

        // CreateCommandQueue
        result = State::Instance().currentD3D12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&Dx12CommandQueue));

        if (result != S_OK || Dx12CommandQueue == nullptr)
        {
            LOG_DEBUG("CreateCommandQueue result: {0:x}", result);
            State::Instance().vulkanSkipHooks = false;
            State::Instance().skipSpoofing = false;
            return E_NOINTERFACE;
        }
    }

    if (Dx12CommandAllocator[0] == nullptr)
    {
        result = State::Instance().currentD3D12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                              IID_PPV_ARGS(&Dx12CommandAllocator[0]));

        if (result != S_OK)
        {
            LOG_ERROR("CreateCommandAllocator error: {0:x}", result);
            State::Instance().vulkanSkipHooks = false;
            State::Instance().skipSpoofing = false;
            return E_NOINTERFACE;
        }
    }

    if (Dx12CommandAllocator[1] == nullptr)
    {
        result = State::Instance().currentD3D12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                              IID_PPV_ARGS(&Dx12CommandAllocator[1]));

        if (result != S_OK)
        {
            LOG_ERROR("CreateCommandAllocator error: {0:x}", result);
            State::Instance().vulkanSkipHooks = false;
            State::Instance().skipSpoofing = false;
            return E_NOINTERFACE;
        }
    }

    if (Dx12CommandList[0] == nullptr)
    {
        // CreateCommandList
        result = State::Instance().currentD3D12Device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, Dx12CommandAllocator[0], nullptr, IID_PPV_ARGS(&Dx12CommandList[0]));

        if (result != S_OK)
        {
            LOG_ERROR("CreateCommandList error: {0:x}", result);
            State::Instance().vulkanSkipHooks = false;
            State::Instance().skipSpoofing = false;
            return E_NOINTERFACE;
        }
    }

    if (Dx12CommandList[1] == nullptr)
    {
        // CreateCommandList
        result = State::Instance().currentD3D12Device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, Dx12CommandAllocator[1], nullptr, IID_PPV_ARGS(&Dx12CommandList[1]));

        if (result != S_OK)
        {
            LOG_ERROR("CreateCommandList error: {0:x}", result);
            State::Instance().vulkanSkipHooks = false;
            State::Instance().skipSpoofing = false;
            return E_NOINTERFACE;
        }
    }

    if (Dx12Fence == nullptr)
    {
        result = State::Instance().currentD3D12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Dx12Fence));

        if (result != S_OK)
        {
            LOG_ERROR("CreateFence error: {0:X}", result);
            State::Instance().vulkanSkipHooks = false;
            State::Instance().skipSpoofing = false;
            return E_NOINTERFACE;
        }

        Dx12FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        if (Dx12FenceEvent == nullptr)
        {
            LOG_ERROR("CreateEvent error!");
            State::Instance().vulkanSkipHooks = false;
            State::Instance().skipSpoofing = false;
            return E_NOINTERFACE;
        }
    }

    State::Instance().vulkanSkipHooks = false;
    State::Instance().skipSpoofing = false;
    return S_OK;
}

bool IFeature_Dx11wDx12::ProcessDx11Textures(const NVSDK_NGX_Parameter* InParameters)
{
    // Wait for last frame
    if (Dx12Fence->GetCompletedValue() < _frameCount)
    {
        Dx12Fence->SetEventOnCompletion(_frameCount, Dx12FenceEvent);
        WaitForSingleObject(Dx12FenceEvent, INFINITE);
    }

    auto frame = _frameCount % 2;

    Dx12CommandAllocator[frame]->Reset();
    Dx12CommandList[frame]->Reset(Dx12CommandAllocator[frame], nullptr);

    HRESULT result;

    auto dontUseNTS = Config::Instance()->DontUseNTShared.value_or_default();

#pragma region Texture copies

    ID3D11Resource* paramColor;
    if (InParameters->Get(NVSDK_NGX_Parameter_Color, &paramColor) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_Color, (void**) &paramColor);

    if (paramColor)
    {
        LOG_DEBUG("Color exist..");
        if (CopyTextureFrom11To12(paramColor, &dx11Color, true, dontUseNTS) == false)
            return false;
    }
    else
    {
        LOG_ERROR("Color not exist!!");
        return false;
    }

    ID3D11Resource* paramMv;
    if (InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, &paramMv) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &paramMv);

    if (paramMv)
    {
        LOG_DEBUG("MotionVectors exist..");
        if (CopyTextureFrom11To12(paramMv, &dx11Mv, true, dontUseNTS) == false)
            return false;
    }
    else
    {
        LOG_ERROR("MotionVectors not exist!!");
        return false;
    }

    if (InParameters->Get(NVSDK_NGX_Parameter_Output, &paramOutput[_frameCount % 2]) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_Output, (void**) &paramOutput[_frameCount % 2]);

    if (paramOutput[_frameCount % 2])
    {
        LOG_DEBUG("Output exist..");
        if (CopyTextureFrom11To12(paramOutput[_frameCount % 2], &dx11Out, false,
                                  Config::Instance()->DontUseNTShared.value_or(true)) == false)
            return false;
    }
    else
    {
        LOG_ERROR("Output not exist!!");
        return false;
    }

    ID3D11Resource* paramDepth;
    if (InParameters->Get(NVSDK_NGX_Parameter_Depth, &paramDepth) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_Depth, (void**) &paramDepth);

    if (paramDepth)
    {
        LOG_DEBUG("Depth exist..");

        if (CopyTextureFrom11To12(paramDepth, &dx11Depth, true, true) == false)
            return false;
    }
    else
        LOG_ERROR("IFeature_Dx11wDx12::Evaluate Depth not exist!!");

    ID3D11Resource* paramExposure = nullptr;
    if (AutoExposure())
    {
        LOG_DEBUG("AutoExposure enabled!");
    }
    else
    {
        if (InParameters->Get(NVSDK_NGX_Parameter_ExposureTexture, &paramExposure) != NVSDK_NGX_Result_Success)
            InParameters->Get(NVSDK_NGX_Parameter_ExposureTexture, (void**) &paramExposure);

        if (paramExposure)
        {
            LOG_DEBUG("ExposureTexture exist..");

            if (CopyTextureFrom11To12(paramExposure, &dx11Exp, true, dontUseNTS) == false)
                return false;
        }
        else
        {
            LOG_WARN("AutoExposure disabled but ExposureTexture is not exist, it may cause problems!!");
            State::Instance().AutoExposure = true;
            State::Instance().changeBackend[Handle()->Id] = true;
        }
    }

    ID3D11Resource* paramReactiveMask = nullptr;
    if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, &paramReactiveMask) !=
        NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, (void**) &paramReactiveMask);

    if (!Config::Instance()->DisableReactiveMask.value_or(paramReactiveMask == nullptr))
    {
        if (paramReactiveMask)
        {
            Config::Instance()->DisableReactiveMask.set_volatile_value(false);
            LOG_DEBUG("Input Bias mask exist..");

            if (CopyTextureFrom11To12(paramReactiveMask, &dx11Reactive, true, dontUseNTS) == false)
                return false;
        }
        // This is only needed for XeSS
        else if (Config::Instance()->Dx11Upscaler.value_or_default() == "xess")
        {
            LOG_WARN("bias mask not exist and it's enabled in config, it may cause problems!!");
            Config::Instance()->DisableReactiveMask.set_volatile_value(true);
            State::Instance().changeBackend[Handle()->Id] = true;
        }
    }
    else
        LOG_DEBUG("DisableReactiveMask enabled!");

#pragma endregion

    {
        if (dx11FenceTextureCopy == nullptr)
        {
            result = Dx11Device->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&dx11FenceTextureCopy));

            if (result != S_OK)
            {
                LOG_ERROR("Can't create dx11FenceTextureCopy {0:x}", result);
                return false;
            }
        }

        if (dx11SHForTextureCopy == nullptr)
        {
            result = dx11FenceTextureCopy->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &dx11SHForTextureCopy);

            if (result != S_OK)
            {
                LOG_ERROR("Can't create sharedhandle for dx11FenceTextureCopy {0:x}", result);
                return false;
            }

            result = State::Instance().currentD3D12Device->OpenSharedHandle(dx11SHForTextureCopy,
                                                                            IID_PPV_ARGS(&dx12FenceTextureCopy));

            if (result != S_OK)
            {
                LOG_ERROR("Can't create open sharedhandle for dx12FenceTextureCopy {0:x}", result);
                return false;
            }
        }

        // Fence
        LOG_DEBUG("Dx11 Signal & Dx12 Wait!");

        result = Dx11DeviceContext->Signal(dx11FenceTextureCopy, _fenceValue);
        Dx11DeviceContext->Flush();

        if (result != S_OK)
        {
            LOG_ERROR("Dx11DeviceContext->Signal(dx11FenceTextureCopy, 10) : {0:x}!", result);
            return false;
        }

        // Gpu Sync
        result = Dx12CommandQueue->Wait(dx12FenceTextureCopy, _fenceValue);
        _fenceValue++;

        if (result != S_OK)
        {
            LOG_ERROR("Dx12CommandQueue->Wait(dx12fence_1, 10) : {0:x}!", result);
            return false;
        }
    }

#pragma region shared handles

    LOG_DEBUG("SharedHandles start!");

    if (paramColor && dx11Color.Dx12Handle != dx11Color.Dx11Handle)
    {
        if (dx11Color.Dx12Handle != NULL)
            CloseHandle(dx11Color.Dx12Handle);

        result = State::Instance().currentD3D12Device->OpenSharedHandle(dx11Color.Dx11Handle,
                                                                        IID_PPV_ARGS(&dx11Color.Dx12Resource));

        if (result != S_OK)
        {
            LOG_ERROR("Color OpenSharedHandle error: {0:x}", result);
            return false;
        }

        dx11Color.Dx12Handle = dx11Color.Dx11Handle;
    }

    if (paramMv && dx11Mv.Dx12Handle != dx11Mv.Dx11Handle)
    {
        if (dx11Mv.Dx12Handle != NULL)
            CloseHandle(dx11Mv.Dx12Handle);

        result = State::Instance().currentD3D12Device->OpenSharedHandle(dx11Mv.Dx11Handle,
                                                                        IID_PPV_ARGS(&dx11Mv.Dx12Resource));

        if (result != S_OK)
        {
            LOG_ERROR("MotionVectors OpenSharedHandle error: {0:x}", result);
            return false;
        }

        dx11Mv.Dx11Handle = dx11Mv.Dx12Handle;
    }

    if (paramOutput[_frameCount % 2] && dx11Out.Dx12Handle != dx11Out.Dx11Handle)
    {
        if (dx11Out.Dx12Handle != NULL)
            CloseHandle(dx11Out.Dx12Handle);

        result = State::Instance().currentD3D12Device->OpenSharedHandle(dx11Out.Dx11Handle,
                                                                        IID_PPV_ARGS(&dx11Out.Dx12Resource));

        if (result != S_OK)
        {
            LOG_ERROR("Output OpenSharedHandle error: {0:x}", result);
            return false;
        }

        dx11Out.Dx12Handle = dx11Out.Dx11Handle;
    }

    if (paramDepth && dx11Depth.Dx12Handle != dx11Depth.Dx11Handle)
    {
        if (dx11Depth.Dx12Handle != NULL)
            CloseHandle(dx11Depth.Dx12Handle);

        result = State::Instance().currentD3D12Device->OpenSharedHandle(dx11Depth.Dx11Handle,
                                                                        IID_PPV_ARGS(&dx11Depth.Dx12Resource));

        if (result != S_OK)
        {
            LOG_ERROR("Depth OpenSharedHandle error: {0:x}", result);
            return false;
        }

        auto desc = dx11Depth.Dx12Resource->GetDesc();

        dx11Depth.Dx12Handle = dx11Depth.Dx11Handle;
    }

    if (AutoExposure())
    {
        LOG_DEBUG("AutoExposure enabled!");
    }
    else if (paramExposure && dx11Exp.Dx12Handle != dx11Exp.Dx11Handle)
    {
        if (dx11Exp.Dx12Handle != NULL)
            CloseHandle(dx11Exp.Dx12Handle);

        result = State::Instance().currentD3D12Device->OpenSharedHandle(dx11Exp.Dx11Handle,
                                                                        IID_PPV_ARGS(&dx11Exp.Dx12Resource));

        if (result != S_OK)
        {
            LOG_ERROR("ExposureTexture OpenSharedHandle error: {0:x}", result);
            return false;
        }

        dx11Exp.Dx12Handle = dx11Exp.Dx11Handle;
    }

    if (!Config::Instance()->DisableReactiveMask.value_or(false) && paramReactiveMask &&
        dx11Reactive.Dx12Handle != dx11Reactive.Dx11Handle)
    {
        if (dx11Reactive.Dx12Handle != NULL)
            CloseHandle(dx11Reactive.Dx12Handle);

        result = State::Instance().currentD3D12Device->OpenSharedHandle(dx11Reactive.Dx11Handle,
                                                                        IID_PPV_ARGS(&dx11Reactive.Dx12Resource));

        if (result != S_OK)
        {
            LOG_ERROR("TransparencyMask OpenSharedHandle error: {0:x}", result);
            return false;
        }

        dx11Reactive.Dx12Handle = dx11Reactive.Dx11Handle;
    }

#pragma endregion

    return true;
}

bool IFeature_Dx11wDx12::CopyBackOutput()
{
    // Fence ones
    {
        // wait for fsr on dx12
        Dx11DeviceContext->Wait(dx11FenceTextureCopy, _fenceValue);
        _fenceValue++;

        // Copy Back
        Dx11DeviceContext->CopyResource(paramOutput[_frameCount % 2], dx11Out.SharedTexture);
    }

    return true;
}

bool IFeature_Dx11wDx12::BaseInit(ID3D11Device* InDevice, ID3D11DeviceContext* InContext,
                                  NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (IsInited())
        return true;

    Device = InDevice;
    DeviceContext = InContext;

    if (!InContext)
    {
        LOG_ERROR("context is null!");
        return false;
    }

    auto contextResult = InContext->QueryInterface(IID_PPV_ARGS(&Dx11DeviceContext));
    if (contextResult != S_OK)
    {
        LOG_ERROR("QueryInterface ID3D11DeviceContext4 result: {0:x}", contextResult);
        return false;
    }

    if (!InDevice)
        Dx11DeviceContext->GetDevice(&InDevice);

    auto dx11DeviceResult = InDevice->QueryInterface(IID_PPV_ARGS(&Dx11Device));

    if (dx11DeviceResult != S_OK)
    {
        LOG_ERROR("QueryInterface ID3D11Device5 result: {0:x}", dx11DeviceResult);
        return false;
    }

    auto fl = Dx11Device->GetFeatureLevel();
    auto result = CreateDx12Device(fl);

    if (result != S_OK || State::Instance().currentD3D12Device == nullptr)
    {
        LOG_ERROR("QueryInterface Dx12Device result: {0:x}", result);
        return false;
    }

    return true;
}

IFeature_Dx11wDx12::IFeature_Dx11wDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : IFeature(InHandleId, InParameters), IFeature_Dx11(InHandleId, InParameters)
{
}

IFeature_Dx11wDx12::~IFeature_Dx11wDx12()
{
    if (State::Instance().isShuttingDown)
        return;

    ReleaseSharedResources();

    if (Imgui != nullptr && Imgui.get() != nullptr)
    {
        Imgui.reset();
        Imgui = nullptr;
    }

    if (DT != nullptr && DT.get() != nullptr)
    {
        DT.reset();
        DT = nullptr;
    }

    if (OutputScaler != nullptr && OutputScaler.get() != nullptr)
    {
        OutputScaler.reset();
        OutputScaler = nullptr;
    }

    if (RCAS != nullptr && RCAS.get() != nullptr)
    {
        RCAS.reset();
        RCAS = nullptr;
    }

    if (Bias != nullptr && Bias.get() != nullptr)
    {
        Bias.reset();
        Bias = nullptr;
    }
}
