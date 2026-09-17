#pragma once

#include <d3d11_4.h>
#include <d3d12.h>
#include <wrl/client.h>

// A presentation-only round trip. The upscaler's shared textures are never borrowed here.
// Calls are serialized by the NR owner's mutex. DX11 -> DX12 -> DX11 fence ordering also
// protects reuse of the single shared texture; only resize/teardown needs a CPU wait.
class Dx11FinishedPictureBridge
{
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    ComPtr<ID3D11Device5> device11;
    ComPtr<ID3D11DeviceContext4> context11;
    ComPtr<ID3D12Device> device12;
    ComPtr<ID3D12CommandQueue> queue12;
    ComPtr<ID3D11Fence> fence11;
    ComPtr<ID3D12Fence> fence12;
    ComPtr<ID3D11Texture2D> shared11;
    ComPtr<ID3D12Resource> shared12;
    UINT64 value = 0, completedRoundTrip = 0;
    bool open = false, failed = false;

    bool Check(HRESULT hr)
    {
        failed |= FAILED(hr);
        return SUCCEEDED(hr);
    }

  public:
    Dx11FinishedPictureBridge() = default;
    Dx11FinishedPictureBridge(const Dx11FinishedPictureBridge&) = delete;
    Dx11FinishedPictureBridge& operator=(const Dx11FinishedPictureBridge&) = delete;

    bool Drain()
    {
        if (open || failed)
            return false;
        if (!completedRoundTrip || fence11->GetCompletedValue() >= completedRoundTrip)
            return true;
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event)
            return false;
        const bool ready = SUCCEEDED(fence11->SetEventOnCompletion(completedRoundTrip, event)) &&
                           WaitForSingleObject(event, 5000) == WAIT_OBJECT_0;
        CloseHandle(event);
        return ready;
    }

    ~Dx11FinishedPictureBridge()
    {
        if (!Drain())
        {
            // A failed queue must not turn into destruction of resources still used by the GPU.
            // Retain this failed generation for process teardown, as the NR owner does for its slots.
            shared11.Detach();
            shared12.Detach();
            fence11.Detach();
            fence12.Detach();
            context11.Detach();
            queue12.Detach();
            device11.Detach();
            device12.Detach();
        }
    }

    ID3D12Resource* Begin(ID3D11Texture2D* picture, ID3D12Device* dx12, ID3D12CommandQueue* queue)
    {
        if (!picture || !dx12 || !queue || failed || open)
            return nullptr;
        D3D11_TEXTURE2D_DESC desc {};
        picture->GetDesc(&desc);
        if (desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || desc.MipLevels != 1 ||
            (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format != DXGI_FORMAT_R10G10B10A2_UNORM &&
             desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT))
            return nullptr;
        ComPtr<ID3D11Device> sourceDevice;
        picture->GetDevice(&sourceDevice);
        ComPtr<ID3D11Device5> sourceDevice5;
        if (FAILED(sourceDevice.As(&sourceDevice5)))
            return nullptr;
        if (device11 && (device11 != sourceDevice5 || device12.Get() != dx12 || queue12.Get() != queue))
            return nullptr; // a new NR owner gets a new bridge when the device/backend changes
        if (!device11)
        {
            ComPtr<ID3D12Device> queueDevice;
            if (FAILED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) || queueDevice.Get() != dx12)
                return nullptr;
            device11 = sourceDevice5;
            device12 = dx12;
            queue12 = queue;
            ComPtr<ID3D11DeviceContext> context;
            sourceDevice->GetImmediateContext(&context);
            if (!Check(context.As(&context11)) ||
                !Check(device11->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence11))))
                return nullptr;
            HANDLE handle = nullptr;
            if (!Check(fence11->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle)))
                return nullptr;
            const HRESULT hr = device12->OpenSharedHandle(handle, IID_PPV_ARGS(&fence12));
            CloseHandle(handle);
            if (!Check(hr))
                return nullptr;
        }
        if (shared11)
        {
            D3D11_TEXTURE2D_DESC have {};
            shared11->GetDesc(&have);
            if (have.Width != desc.Width || have.Height != desc.Height || have.Format != desc.Format)
            {
                if (!Drain())
                    return nullptr;
                shared12.Reset();
                shared11.Reset();
            }
        }
        if (!shared11)
        {
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.CPUAccessFlags = 0;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
            if (!Check(device11->CreateTexture2D(&desc, nullptr, &shared11)))
                return nullptr;
            ComPtr<IDXGIResource1> resource;
            if (!Check(shared11.As(&resource)))
                return nullptr;
            HANDLE handle = nullptr;
            if (!Check(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                                    nullptr, &handle)))
                return nullptr;
            const HRESULT hr = device12->OpenSharedHandle(handle, IID_PPV_ARGS(&shared12));
            CloseHandle(handle);
            if (!Check(hr))
                return nullptr;
        }
        open = true;
        context11->CopyResource(shared11.Get(), picture);
        const UINT64 ready = ++value;
        if (!Check(context11->Signal(fence11.Get(), ready)))
            return nullptr;
        context11->Flush();
        if (!Check(queue12->Wait(fence12.Get(), ready)))
            return nullptr;
        // The renderer must return this texture to COMMON (the same value as PRESENT).
        return shared12.Get();
    }

    bool End(ID3D11Texture2D* picture, bool copyBack)
    {
        if (!open || failed)
            return false;
        const UINT64 ready = ++value;
        if (!Check(queue12->Signal(fence12.Get(), ready)) || !Check(context11->Wait(fence11.Get(), ready)))
            return false;
        if (copyBack)
            context11->CopyResource(picture, shared11.Get());
        completedRoundTrip = ++value;
        if (!Check(context11->Signal(fence11.Get(), completedRoundTrip)))
            return false;
        context11->Flush();
        open = false;
        return true;
    }
};
