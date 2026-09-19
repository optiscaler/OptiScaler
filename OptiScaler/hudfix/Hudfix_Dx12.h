#pragma once
#include "SysUtils.h"
#include <shaders/format_transfer/FT_Dx12.h>

#include <ankerl/unordered_dense.h>

#include <set>
#include <dxgi.h>
#include <atomic>
#include <d3d12.h>
#include <shared_mutex>
#include <vector>

enum ResourceType
{
    SRV,
    RTV,
    UAV
};

enum CaptureInfo
{
    None = 0,
    CreateRTV = 1,
    CreateSRV = 2,
    CreateUAV = 4,
    OMSetRTV = 8,
    Upscaler = 16,
    SetCR = 32,
    SetGR = 64,
    Dispatch = 256,
    DrawInstanced = 512,
    DrawIndexedInstanced = 1024,
};

typedef struct ResourceInfo
{
    ID3D12Resource* buffer = nullptr;
    UINT64 width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    ResourceType type = SRV;
    double lastUsedFrame = 0;
    bool extended = false;
    bool lifetimeTracked = false;
    UINT captureInfo = 0;
} resource_info;

typedef struct HudlessInfo
{
    UINT64 lastUsedFrame = 0;
    UINT64 retryStartFrame = 0;
    UINT64 lastTriedFrame = 0;
    UINT64 retryCount = 0;
    UINT64 reuseCount = 0;
    UINT64 useCount = 0;
    bool ignore = false;
    bool dontReuse = false;
} hudless_info;

class Hudfix_Dx12
{
  private:
    // Last upscaled frame
    inline static std::atomic<UINT64> _upscaleCounter { 0 };

    // Last presented frame
    inline static std::atomic<UINT64> _fgCounter { 0 };

    // Limit until calling FG without hudless
    inline static double _lastDiffTime = 0.0;
    inline static double _upscaleEndTime = 0.0;
    inline static double _targetTime = 0.0;
    inline static double _frameTime = 0.0;

    inline static std::atomic<bool> _skipTracking { false };

    // Buffer for Format Transfer
    inline static ID3D12Resource* _captureBuffer[BUFFER_COUNT] = { nullptr, nullptr, nullptr, nullptr };

    struct CaptureRemoveInfo
    {
        ID3D12Fence* fence = nullptr;
        UINT64 fenceValue = 0;
        std::vector<ID3D12Resource*> resources;
    };

    inline static std::atomic<bool> _captureRemovalActive { false };
    inline static std::mutex _captureRemoveMutex;
    inline static std::vector<ID3D12Resource*> _pendingCaptureRemovals;
    inline static std::vector<CaptureRemoveInfo> _captureRemoveInfos;

    // used hudless list
    inline static ankerl::unordered_dense::map<ID3D12Resource*, HudlessInfo> _hudlessList;

    // Capture List
    inline static std::set<ID3D12Resource*> _captureList;

    inline static std::mutex _checkMutex;
    inline static std::mutex _captureMutex;
    inline static std::mutex _counterMutex;
    inline static thread_local bool _checkMutexOwned = false;
    inline static INT64 _captureCounter[BUFFER_COUNT] = { 0, 0, 0, 0 };
    inline static FT_Dx12* _formatTransfer[BUFFER_COUNT] = { nullptr, nullptr, nullptr, nullptr };

    inline static std::atomic<bool> _skipHudlessChecks { false };

    static void RemoveCaptureBuffer(ID3D12Resource** resource);
    static void ProcessPendingCaptureRemovals();

    static bool CreateBufferResource(ID3D12Device* InDevice, ResourceInfo* InSource, D3D12_RESOURCE_STATES InState,
                                     ID3D12Resource** OutResource);
    static bool CreateBufferResourceWithSize(ID3D12Device* InDevice, ResourceInfo* InSource,
                                             D3D12_RESOURCE_STATES InState, ID3D12Resource** OutResource, UINT InWidth,
                                             UINT InHeight);
    static void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);

    // Check _captureCounter for current frame
    static bool CheckCapture(int fIndex);

    static void HudlessFound(UINT64 upscaleCounter);

  public:
    // Trig for upscaling start
    static void UpscaleStart();

    // Trig for upscaling end
    static void UpscaleEnd(UINT64 frameId, double lastFGFrameTime);

    // Trig for present start
    static void PresentStart();

    // Trig for present end
    static void PresentEnd();

    static UINT64 ActiveUpscaleFrame();
    static UINT64 ActivePresentFrame();

    // Is Hudfix active
    static bool IsResourceCheckActive();

    // For resource tracking in hooks
    static bool SkipHudlessChecks();

    // Check resource for hudless
    static bool CheckForHudless(ID3D12GraphicsCommandList* cmdList, ResourceInfo* resource, D3D12_RESOURCE_STATES state,
                                bool ignoreBlocked = false);

    static bool CheckResource(ResourceInfo* resource);

    static void RemoveResourceFromTracking(ID3D12Resource* resource);

    // Reset frame counters
    static void ResetCounters();

    static bool GetSkipStatus() { return _skipTracking.load(std::memory_order_acquire); }
    static void SetSkipStatus(bool status) { _skipTracking.store(status, std::memory_order_release); }
};
