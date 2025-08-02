#pragma once
#include <pch.h>

#include <shaders/format_transfer/FT_Dx12.h>

#include <ankerl/unordered_dense.h>

#include <set>
#include <dxgi.h>
#include <d3d12.h>
#include <shared_mutex>

enum ResourceType
{
    SRV,
    RTV,
    UAV
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
    inline static UINT64 _upscaleCounter = 0;

    // Last presented frame
    inline static UINT64 _fgCounter = 0;

    // Limit until calling FG without hudless
    inline static double _lastDiffTime = 0.0;
    inline static double _upscaleEndTime = 0.0;
    inline static double _targetTime = 0.0;
    inline static double _frameTime = 0.0;

    // Buffer for Format Transfer
    inline static ID3D12Resource* _captureBuffer[BUFFER_COUNT] = { nullptr, nullptr, nullptr, nullptr };

    // used hudless list
    inline static ankerl::unordered_dense::map<ID3D12Resource*, HudlessInfo> _hudlessList;

    // Capture List
    inline static std::set<ID3D12Resource*> _captureList;

    inline static std::mutex _checkMutex;
    inline static std::mutex _captureMutex;
    inline static std::mutex _counterMutex;
    inline static INT64 _captureCounter[BUFFER_COUNT] = { 0, 0, 0, 0 };
    inline static FT_Dx12* _formatTransfer[BUFFER_COUNT] = { nullptr, nullptr, nullptr, nullptr };

    inline static ID3D12CommandQueue* _commandQueue = nullptr;
    inline static ID3D12GraphicsCommandList* _commandList[BUFFER_COUNT] = { nullptr, nullptr, nullptr, nullptr };
    inline static ID3D12CommandAllocator* _commandAllocator[BUFFER_COUNT] = { nullptr, nullptr, nullptr, nullptr };
    inline static ID3D12Fence* _fence[BUFFER_COUNT] = { nullptr, nullptr, nullptr, nullptr };

    inline static bool _skipHudlessChecks = false;

    static bool CreateObjects();
    static bool CreateBufferResource(ID3D12Device* InDevice, ResourceInfo* InSource, D3D12_RESOURCE_STATES InState,
                                     ID3D12Resource** OutResource);
    static bool CreateBufferResourceWithSize(ID3D12Device* InDevice, ResourceInfo* InSource,
                                             D3D12_RESOURCE_STATES InState, ID3D12Resource** OutResource, UINT InWidth,
                                             UINT InHeight);
    static void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);

    // Check _captureCounter for current frame
    static bool CheckCapture();

    static void HudlessFound(ID3D12GraphicsCommandList* cmdList);

    static int GetIndex();

    inline static IID streamlineRiid {};
    static bool CheckForRealObject(std::string functionName, IUnknown* pObject, IUnknown** ppRealObject);

  public:
    // Trig for upscaling start
    static void UpscaleStart();

    // Trig for upscaling end
    static void UpscaleEnd(UINT64 frameId, float lastFrameTime);

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
    static bool CheckForHudless(std::string callerName, ID3D12GraphicsCommandList* cmdList, ResourceInfo* resource,
                                D3D12_RESOURCE_STATES state, bool ignoreBlocked = false);
    static bool CheckResource(ResourceInfo* resource);

    // Reset frame counters
    static void ResetCounters();
};
