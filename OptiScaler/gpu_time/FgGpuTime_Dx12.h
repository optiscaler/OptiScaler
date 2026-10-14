#pragma once

#include <d3d12.h>

#include <cstdint>
#include <memory>

#include <gpu_time/FgTiming.h>

// A GPU interval on an already-recording DIRECT or COMPUTE command list. The
// result is published only after the observed submitting queue completes and
// the originating allocator generation retires. The first recording is skipped
// until a successful command-list Reset reveals its allocator.
class ScopedFgGpuTime_Dx12
{
    struct Impl;
    std::unique_ptr<Impl> _impl;

  public:
    ScopedFgGpuTime_Dx12(ID3D12GraphicsCommandList* commandList, const std::shared_ptr<FgTiming::Channel>& channel,
                         uint64_t frameId, uint32_t generatedFrames = 0);
    ~ScopedFgGpuTime_Dx12();

    ScopedFgGpuTime_Dx12(const ScopedFgGpuTime_Dx12&) = delete;
    ScopedFgGpuTime_Dx12& operator=(const ScopedFgGpuTime_Dx12&) = delete;

    // Failed/cancelled dispatches still close the marker pair, but do not
    // publish. An omitted End is treated as a failed dispatch.
    void End(bool succeeded = true);
};

class FgGpuTime_Dx12
{
  public:
    static void Poll();
    static bool HookQueue(ID3D12CommandQueue* commandQueue);
};
