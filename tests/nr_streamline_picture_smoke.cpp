// WARP validates app-buffer selection and an edit surviving the native FG output copy.
#include <windows.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <stdexcept>
#include "../OptiScaler/dlssnr/DlssNr_StreamlinePicture.h"
using Microsoft::WRL::ComPtr;
static ID3D12Resource* appBuffers[3] {};
static UINT appIndex = 0;
static bool handledIndex = true, handledBuffer = true;
static UINT Index(IDXGISwapChain*, bool& handled) { handled = handledIndex; return appIndex; }
static HRESULT Buffer(IDXGISwapChain*, UINT index, REFIID iid, void** out, bool& handled)
{
    handled = handledBuffer;
    return appBuffers[index]->QueryInterface(iid, out);
}
static void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("D3D12 call failed"); }
static void Expect(bool yes, const char* why) { if (!yes) throw std::runtime_error(why); }
int main() try
{
    ComPtr<IDXGIFactory4> factory; ComPtr<IDXGIAdapter> adapter; ComPtr<ID3D12Device> device;
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandQueue> queue; D3D12_COMMAND_QUEUE_DESC q {};
    Check(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> cmd;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&cmd)));
    Check(cmd->Close());
    D3D12_RESOURCE_DESC td {}; td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; td.Width = td.Height = 8;
    td.DepthOrArraySize = td.MipLevels = td.SampleDesc.Count = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap {}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> textures[4];
    ComPtr<ID3D12DescriptorHeap> rtvs; D3D12_DESCRIPTOR_HEAP_DESC hd {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors = 4;
    Check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtvs)));
    const auto stride = device->GetDescriptorHandleIncrementSize(hd.Type);
    auto rtv = [&](UINT n) { auto h = rtvs->GetCPUDescriptorHandleForHeapStart(); h.ptr += n * stride; return h; };
    for (UINT i = 0; i < 4; ++i)
    {
        Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COMMON,
                                              nullptr, IID_PPV_ARGS(&textures[i])));
        device->CreateRenderTargetView(textures[i].Get(), nullptr, rtv(i));
        if (i < 3) appBuffers[i] = textures[i].Get();
    }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp {}; UINT64 size = 0;
    device->GetCopyableFootprints(&td, 0, 1, 0, &fp, nullptr, nullptr, &size);
    D3D12_RESOURCE_DESC bd {}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = size;
    bd.Height = bd.DepthOrArraySize = bd.MipLevels = bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type = D3D12_HEAP_TYPE_READBACK; ComPtr<ID3D12Resource> readback;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST,
                                          nullptr, IID_PPV_ARGS(&readback)));
    ComPtr<ID3D12Fence> fence; Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    auto barrier = [&](ID3D12Resource* res, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
    {
        D3D12_RESOURCE_BARRIER b {}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = { res, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to }; cmd->ResourceBarrier(1, &b);
    };
    // The production reader calls only the supplied plugin hooks; the native display chain must never be queried.
    auto* chainIdentity = reinterpret_cast<IDXGISwapChain*>(1);
    for (UINT frame = 0; frame < 12; ++frame)
    {
        appIndex = frame % 3;
        auto picture = DlssNr::StreamlinePicture::Read(chainIdentity, Index, Buffer);
        Expect(picture.Get() == appBuffers[appIndex], "Selected display buffer instead of app buffer");
        const bool correctHandoff = frame >= 3;
        Check(allocator->Reset()); Check(cmd->Reset(allocator.Get(), nullptr));
        const float clean[] = {0, 0, 0, 1}, edited[] = {0, 1, 0, 1};
        barrier(picture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        barrier(textures[3].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmd->ClearRenderTargetView(rtv(appIndex), clean, 0, nullptr);
        cmd->ClearRenderTargetView(rtv(correctHandoff ? appIndex : 3), edited, 0, nullptr);
        barrier(picture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        barrier(textures[3].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyResource(textures[3].Get(), picture.Get()); // FG's subsequent app-to-display copy
        barrier(textures[3].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION from {}, to {};
        from.pResource = textures[3].Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.pResource = readback.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; to.PlacedFootprint = fp;
        cmd->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        barrier(textures[3].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        barrier(picture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        Check(cmd->Close()); ID3D12CommandList* lists[] {cmd.Get()}; queue->ExecuteCommandLists(1, lists);
        Check(queue->Signal(fence.Get(), frame + 1)); Check(fence->SetEventOnCompletion(frame + 1, event));
        Expect(WaitForSingleObject(event, 5000) == WAIT_OBJECT_0, "GPU timeout");
        unsigned char* bytes = nullptr; Check(readback->Map(0, nullptr, reinterpret_cast<void**>(&bytes)));
        const bool visible = bytes[1] == 255; readback->Unmap(0, nullptr);
        Expect(visible == correctHandoff, "Edit did not follow the selected presentation handoff");
    }
    CloseHandle(event);
    handledIndex = false;
    Expect(!DlssNr::StreamlinePicture::Read(chainIdentity, Index, Buffer), "Fell back after rejected index");
    handledIndex = true; handledBuffer = false;
    Expect(!DlssNr::StreamlinePicture::Read(chainIdentity, Index, Buffer), "Fell back after rejected app buffer");
    puts("PASS native FG handoff: rotating app buffers; old edit overwritten, pre-FG edit retained; no display fallback");
    return 0;
}
catch (const std::exception& e) { puts(e.what()); return 1; }
