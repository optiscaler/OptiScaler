#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <cstring>
#include "../OptiScaler/dlssnr/DlssNr_FinishedPictureBridge_Dx11.h"
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr)
{
    if (FAILED(hr))
    {
        std::printf("HRESULT %08X\n", (unsigned) hr);
        throw std::runtime_error("GPU call failed");
    }
}
static void Require(bool ok, const char* why)
{
    if (!ok)
        throw std::runtime_error(why);
}
int main()
try
{
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter1> adapter;
    Check(factory->EnumAdapters1(0, &adapter));
    DXGI_ADAPTER_DESC1 adapterDesc {};
    adapter->GetDesc1(&adapterDesc);
    std::wprintf(L"Adapter: %s\n", adapterDesc.Description);
    ComPtr<ID3D11Device> d11;
    ComPtr<ID3D11DeviceContext> c11;
    Check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d11,
                            nullptr, &c11));
    ComPtr<ID3D12Device> d12;
    Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d12)));
    ComPtr<ID3D12InfoQueue> info;
    d12.As(&info);
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC q {};
    Check(d12->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;
    Check(d12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> cmd;
    Check(d12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&cmd)));
    Check(cmd->Close());
    Dx11FinishedPictureBridge bridge;
    for (auto format : { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT })
    {
        const UINT pixelBytes = format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4;
        for (UINT width : { 64u, 113u })
        {
            const UINT height = 37;
            D3D11_TEXTURE2D_DESC td {};
            td.Width = width;
            td.Height = height;
            td.MipLevels = td.ArraySize = td.SampleDesc.Count = 1;
            td.Format = format;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_RENDER_TARGET;
            std::vector<unsigned char> original(width * height * pixelBytes, 0x35), edited(original.size(), 0x72);
            D3D11_SUBRESOURCE_DATA init { original.data(), width * pixelBytes, 0 };
            ComPtr<ID3D11Texture2D> picture;
            Check(d11->CreateTexture2D(&td, &init, &picture));
            auto stageDesc = td;
            stageDesc.BindFlags = 0;
            stageDesc.Usage = D3D11_USAGE_STAGING;
            stageDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging;
            Check(d11->CreateTexture2D(&stageDesc, nullptr, &staging));
            for (int pass = 0; pass < 3; ++pass)
            {
                auto* shared = bridge.Begin(picture.Get(), d12.Get(), queue.Get());
                Require(shared, "Begin failed");
                Require(bridge.Begin(picture.Get(), d12.Get(), queue.Get()) == nullptr, "Nested Begin accepted");
                ComPtr<ID3D12Resource> upload;
                if (pass != 0)
                {
                    auto rd = shared->GetDesc();
                    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp {};
                    UINT64 size = 0;
                    d12->GetCopyableFootprints(&rd, 0, 1, 0, &fp, nullptr, nullptr, &size);
                    D3D12_HEAP_PROPERTIES heap {};
                    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
                    D3D12_RESOURCE_DESC bd {};
                    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                    bd.Width = size;
                    bd.Height = bd.DepthOrArraySize = bd.MipLevels = bd.SampleDesc.Count = 1;
                    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                    Check(d12->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd,
                                                       D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                       IID_PPV_ARGS(&upload)));
                    void* mapped = nullptr;
                    Check(upload->Map(0, nullptr, &mapped));
                    for (UINT y = 0; y < height; ++y)
                        std::memcpy((char*) mapped + fp.Offset + y * fp.Footprint.RowPitch,
                                    edited.data() + y * width * pixelBytes, width * pixelBytes);
                    upload->Unmap(0, nullptr);
                    Check(allocator->Reset());
                    Check(cmd->Reset(allocator.Get(), nullptr));
                    D3D12_RESOURCE_BARRIER barrier {};
                    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    barrier.Transition = { shared, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COMMON,
                                           D3D12_RESOURCE_STATE_COPY_DEST };
                    cmd->ResourceBarrier(1, &barrier);
                    D3D12_TEXTURE_COPY_LOCATION from {};
                    from.pResource = upload.Get();
                    from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    from.PlacedFootprint = fp;
                    D3D12_TEXTURE_COPY_LOCATION to {};
                    to.pResource = shared;
                    to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    cmd->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
                    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
                    cmd->ResourceBarrier(1, &barrier);
                    Check(cmd->Close());
                    ID3D12CommandList* lists[] = { cmd.Get() };
                    queue->ExecuteCommandLists(1, lists);
                }
                // Pass 1 mimics a failed/skipped model: edits in the scratch texture must not escape.
                Require(bridge.End(picture.Get(), pass != 1), "End failed");
                Require(bridge.Drain(), "Drain failed");
                c11->CopyResource(staging.Get(), picture.Get());
                D3D11_MAPPED_SUBRESOURCE read {};
                Check(c11->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &read));
                const auto& expected = pass == 2 ? edited : original;
                for (UINT y = 0; y < height; ++y)
                    Require(std::memcmp((char*) read.pData + y * read.RowPitch,
                                        expected.data() + y * width * pixelBytes, width * pixelBytes) == 0,
                            "Round-trip pixels differ");
                c11->Unmap(staging.Get(), 0);
            }
            // Exercise successive frames without a CPU fence wait between shared-texture reuses.
            for (int n = 0; n < 12; ++n)
            {
                Require(bridge.Begin(picture.Get(), d12.Get(), queue.Get()) != nullptr, "Repeated Begin failed");
                Require(bridge.End(picture.Get(), true), "Repeated End failed");
            }
            Require(bridge.Drain(), "Repeated drain failed");
            std::printf("PASS format %u, %ux%u: unchanged/edited/skip/reuse\n", format, width, height);
        }
    }
    if (info)
        for (UINT64 n = 0; n < info->GetNumStoredMessages(); ++n)
        {
            SIZE_T size = 0;
            info->GetMessage(n, nullptr, &size);
            std::vector<char> data(size);
            auto* m = (D3D12_MESSAGE*) data.data();
            info->GetMessage(n, m, &size);
            if (m->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
            {
                std::puts(m->pDescription);
                Require(m->Severity > D3D12_MESSAGE_SEVERITY_ERROR, "D3D12 validation error");
            }
        }
    std::puts("PASS DX11 finished-picture bridge GPU regression");
    return 0;
}
catch (const std::exception& e)
{
    std::puts(e.what());
    return 1;
}
