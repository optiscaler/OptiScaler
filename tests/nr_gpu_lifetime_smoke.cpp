// Exercises production NR retirement on WARP, including delayed and replayed command lists.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdio>
#include <stdexcept>
#include <atomic>
#include <barrier>
#include <thread>
#include <vector>
#include <Util.h>
#include "../OptiScaler/dlssnr/DlssNr_GpuLifetime.h"
using Microsoft::WRL::ComPtr;
static void check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("D3D12 call failed"); }
static void expect(bool yes, const char* why) { if (!yes) throw std::runtime_error(why); }
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
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commands)));
    check(commands->Close());
    ID3D12CommandList* lists[] { commands.Get() };
    ComPtr<ID3D12Fence> gate, drain;
    check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)));
    check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&drain)));
    UINT64 serial = 0;
    auto waitOn = [&](ID3D12CommandQueue* submittedQueue)
    {
        check(submittedQueue->Signal(drain.Get(), ++serial));
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        check(drain->SetEventOnCompletion(serial, event));
        const auto result = WaitForSingleObject(event, 5000);
        CloseHandle(event);
        expect(result == WAIT_OBJECT_0, "GPU did not drain");
    };
    auto wait = [&] { waitOn(queue.Get()); };
    int released = 0;
    {
        DlssNr::GpuLifetime life;
        life.Record(commands.Get());
        life.Retire([&] { ++released; });
        for (int epoch = 0; epoch < 100; ++epoch) life.Collect();
        expect(released == 0 && !life.Idle(), "unsubmitted work released by CPU progress");
        life.ResetRecording(commands.Get());
        expect(released == 1 && life.Idle(), "discarded recording not released");
    }
    {
        DlssNr::GpuLifetime life;
        life.Record(commands.Get());
        check(queue->Wait(gate.Get(), 1));
        queue->ExecuteCommandLists(1, lists);
        life.Submitted(queue.Get(), 1, lists);
        life.Retire([&] { ++released; });
        life.ResetRecording(commands.Get());
        expect(released == 1 && !life.Idle(), "reset released submitted GPU work");
        check(gate->Signal(1));
        wait();
        life.Collect();
        expect(released == 2 && life.Idle(), "completed submitted work not released");
    }
    {
        DlssNr::GpuLifetime life;
        life.Record(commands.Get());
        queue->ExecuteCommandLists(1, lists);
        life.Submitted(queue.Get(), 1, lists);
        wait();
        life.Retire([&] { ++released; });
        expect(released == 2, "replayable closed list released after first submission");
        check(queue->Wait(gate.Get(), 2));
        queue->ExecuteCommandLists(1, lists);
        life.Submitted(queue.Get(), 1, lists);
        life.ResetRecording(commands.Get());
        expect(released == 2, "second execution lost its completion dependency");
        check(gate->Signal(2));
        wait();
        life.Collect();
        expect(released == 3, "replayed recording not released after completion");
    }
    {
        DlssNr::GpuLifetime life;
        life.Record(commands.Get());
        life.Retire([&] { ++released; });
    }
    expect(released == 3, "destructor freed unsubmitted ownership");
    {
        DlssNr::GpuLifetime life;
        life.Record(commands.Get());
        life.Submitted(nullptr, 1, lists); // failed completion proof
        life.ResetRecording(commands.Get());
        life.Retire([&] { ++released; });
        expect(!life.Idle(), "failed signal treated as completion");
    }
    expect(released == 3, "failed-signal ownership not abandoned");
    {
        // Alias mapping substitutes only the Streamline unwrap seam; fences/queues remain real.
        ComPtr<ID3D12GraphicsCommandList> wrapped;
        check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                        IID_PPV_ARGS(&wrapped)));
        check(wrapped->Close());
        TestUtil::wrapped = wrapped.Get();
        TestUtil::real = commands.Get();
        DlssNr::GpuLifetime life;
        life.Record(wrapped.Get());
        life.Record(commands.Get()); // same recording through the unwrapped identity
        life.Retire([&] { ++released; });
        queue->ExecuteCommandLists(1, lists);
        ID3D12CommandList* wrappedLists[] { wrapped.Get() };
        life.Submitted(queue.Get(), 1, wrappedLists);
        life.ResetRecording(wrapped.Get());
        wait();
        life.Collect();
        expect(released == 4 && life.Idle(), "wrapped submit/reset identity did not match");
        life.Record(wrapped.Get());
        life.Retire([&] { ++released; });
        life.ResetRecording(commands.Get());
        expect(released == 5 && life.Idle(), "unwrapped reset did not discard wrapped recording");
        TestUtil::wrapped = TestUtil::real = nullptr;
    }
    {
        ComPtr<ID3D12CommandQueue> secondQueue;
        check(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&secondQueue)));
        DlssNr::GpuLifetime life;
        life.Record(commands.Get());
        // Repeated submissions share one queue timeline; another queue needs its own dependency.
        for (int replay = 0; replay < 16; ++replay)
        {
            queue->ExecuteCommandLists(1, lists);
            life.Submitted(queue.Get(), 1, lists);
            wait();
        }
        check(secondQueue->Wait(gate.Get(), 3));
        secondQueue->ExecuteCommandLists(1, lists);
        life.Submitted(secondQueue.Get(), 1, lists);
        life.Retire([&] { ++released; });
        life.ResetRecording(commands.Get());
        expect(released == 5 && !life.Idle(), "first queue completion released second queue work");
        check(gate->Signal(3));
        waitOn(secondQueue.Get());
        life.Collect();
        expect(released == 6 && life.Idle(), "multiple queue dependencies did not complete");
    }
    {
        // Some engines dispose of command lists instead of resetting them. Destruction
        // must close recording ownership, while the submission fence still protects GPU use.
        DlssNr::GpuLifetime life;
        for (int cycle = 0; cycle < 64; ++cycle)
        {
            ComPtr<ID3D12GraphicsCommandList> temporary;
            check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                            IID_PPV_ARGS(&temporary)));
            check(temporary->Close());
            life.Record(temporary.Get());
            life.Retire([&] { ++released; });
            temporary.Reset();
            life.Collect();
            expect(life.Idle() && released == 7 + cycle, "destroyed unsubmitted list retained ownership");
        }
        ComPtr<ID3D12GraphicsCommandList> temporary;
        check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                        IID_PPV_ARGS(&temporary)));
        check(temporary->Close());
        life.Record(temporary.Get());
        check(queue->Wait(gate.Get(), 4));
        ID3D12CommandList* pending[] { temporary.Get() };
        queue->ExecuteCommandLists(1, pending);
        life.Submitted(queue.Get(), 1, pending);
        life.Retire([&] { ++released; });
        temporary.Reset();
        life.Collect();
        expect(released == 70 && !life.Idle(), "destroyed list released in-flight work");
        check(gate->Signal(4));
        wait();
        life.Collect();
        expect(released == 71 && life.Idle(), "destroyed submitted list retained completed work");
    }
    {
        DlssNr::GpuLifetime life;
        unsigned outer = 0, nested = 0;
        life.Retire([&]
        {
            expect(++outer == 1, "retirement callback re-entered itself");
            // NGX feature destruction can re-enter queue/reset hooks and retire more resources.
            // Force the retired vector to grow while the original destruction callback is active.
            for (unsigned i = 0; i < 64; ++i) life.Retire([&] { ++nested; });
            life.Collect();
            expect(!life.Idle(), "collector reported idle inside a destruction callback");
        });
        expect(outer == 1 && nested == 64 && life.Idle(), "reentrant retirement did not drain exactly once");
    }
    {
        // An unrelated open NR recording must not pin a retired private DLSS context.
        DlssNr::GpuLifetime common, privateDlss;
        ComPtr<ID3D12CommandAllocator> localAllocator;
        ComPtr<ID3D12GraphicsCommandList> unrelated, work;
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&localAllocator)));
        check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, localAllocator.Get(), nullptr,
                                        IID_PPV_ARGS(&unrelated)));
        check(unrelated->Close());
        check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, localAllocator.Get(), nullptr,
                                        IID_PPV_ARGS(&work)));
        check(work->Close());
        common.Record(unrelated.Get()); common.Record(work.Get()); privateDlss.Record(work.Get());
        bool commonReleased = false, privateReleased = false;
        common.Retire([&] { commonReleased = true; });
        privateDlss.Retire([&] { privateReleased = true; });
        ID3D12CommandList* submitted[] { work.Get() };
        queue->ExecuteCommandLists(1, submitted);
        common.Submitted(queue.Get(), 1, submitted); privateDlss.Submitted(queue.Get(), 1, submitted);
        wait(); work.Reset(); common.Collect(); privateDlss.Collect();
        expect(privateReleased && privateDlss.Idle() && !commonReleased && !common.Idle(),
               "private DLSS retirement depends on unrelated NR recordings");
        unrelated.Reset(); common.Collect(); expect(commonReleased, "unrelated recording did not retire");
    }
    {
        // Starfield resets lists on worker threads while the NR render path records/collects.
        // Each worker owns a distinct list; only the production lifetime tracker is shared.
        constexpr unsigned workers = 4, cycles = 2000;
        DlssNr::GpuLifetime life;
        std::vector<ComPtr<ID3D12GraphicsCommandList>> work(workers);
        for (auto& list : work)
        {
            check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                            IID_PPV_ARGS(&list)));
            check(list->Close());
        }
        std::barrier start(workers + 1);
        std::atomic_uint callbacks { 0 }, finished { 0 };
        std::vector<std::jthread> threads;
        for (unsigned worker = 0; worker < workers; ++worker)
            threads.emplace_back([&, worker]
            {
                start.arrive_and_wait();
                for (unsigned cycle = 0; cycle < cycles; ++cycle)
                {
                    life.Record(work[worker].Get());
                    life.Retire([&] { ++callbacks; });
                    life.ResetRecording(work[worker].Get());
                }
                ++finished;
            });
        start.arrive_and_wait();
        while (finished != workers)
        {
            life.Collect();
            std::this_thread::yield();
        }
        threads.clear(); // join before checking final ownership
        life.Collect();
        expect(life.Idle() && callbacks == workers * cycles,
               "concurrent record/reset lost, duplicated or retained ownership");
    }
    std::puts("NR GPU lifetime smoke passed (including concurrent record/reset/collection)");
    return 0;
}
catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
