// Real D3D12 queues and fences on WARP; no game or NVIDIA model required.
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <optional>
#include <cstdio>
#include <stdexcept>
#include "../OptiScaler/include/d3dx/d3dx12.h"

namespace Util { bool CheckForRealObject(const char*, IUnknown*, IUnknown**) { return false; } }
#define LOG_INFO(...) ((void) 0)
#include "../OptiScaler/shaders/dlssnr/DlssNr_GpuTime.h"

using Microsoft::WRL::ComPtr;
void check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("D3D12 call failed"); }
void expect(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
int main()
try
{
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<ID3D12Device> device;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC desc {};
    check(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12Fence> gate, completed;
    check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)));
    check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&completed)));
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    expect(event != nullptr, "event creation failed");
    DlssNrGpuTime timer(device.Get(), "smoke");
    std::array<ComPtr<ID3D12CommandAllocator>, 9> allocators;
    std::array<ComPtr<ID3D12GraphicsCommandList>, 9> lists;
    for (unsigned i = 0; i < lists.size(); ++i)
    {
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators[i])));
        check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[i].Get(), nullptr,
                                       IID_PPV_ARGS(&lists[i])));
        check(lists[i]->Close());
    }
    for (UINT64 round = 1; round <= 3; ++round)
    {
        const auto previous = timer.ReadGpuTime(queue.Get());
        // Hold the GPU behind a CPU-controlled gate. A CPU frame count cannot make queries ready.
        check(queue->Wait(gate.Get(), round));
        for (unsigned i = 0; i < lists.size(); ++i)
        {
            check(allocators[i]->Reset());
            check(lists[i]->Reset(allocators[i].Get(), nullptr));
            timer.ResetRecording(lists[i].Get());
            timer.Start(lists[i].Get());
            timer.End(lists[i].Get());
            check(lists[i]->Close());
            ID3D12CommandList* submitted[] { lists[i].Get() };
            queue->ExecuteCommandLists(1, submitted);
            timer.Submitted(queue.Get(), 1, submitted);
            expect(timer.ReadGpuTime(queue.Get()) == previous, "read an unfinished GPU sample");
        }
        if (round == 2)
        {
            timer.ClearLast();
            expect(!timer.ReadGpuTime(queue.Get()), "placement change retained an old timing");
        }
        check(queue->Signal(completed.Get(), round));
        check(gate->Signal(round));
        check(completed->SetEventOnCompletion(round, event));
        expect(WaitForSingleObject(event, 10000) == WAIT_OBJECT_0, "GPU completion timed out");
        auto result = timer.ReadGpuTime(queue.Get());
        if (round == 2)
            expect(!result, "an older in-flight sample repopulated the cleared display");
        else
            expect(result.has_value() && *result >= 0 && *result < 1000, "completed timing was invalid");
    }
    // Repeatedly discard recordings without submission; these must not exhaust the query ring.
    for (unsigned i = 0; i < 20; ++i)
    {
        check(allocators[0]->Reset());
        check(lists[0]->Reset(allocators[0].Get(), nullptr));
        timer.ResetRecording(lists[0].Get());
        timer.Start(lists[0].Get());
        timer.End(lists[0].Get());
        check(lists[0]->Close());
    }
    timer.ResetRecording(lists[0].Get());
    CloseHandle(event);
    std::puts("PASS: unfinished queries are not read; full rings skip timing; completed slots can be reused.");
}
catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1; }
