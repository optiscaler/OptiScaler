#pragma once

#include "SysUtils.h"

#include <shaders/format_transfer/FT_Dx12.h>
#include <with_dx12/dx11_with_dx12.h>

#include <ankerl/unordered_dense.h>

#include <d3d11_4.h>
#include <dxgi.h>

#include <atomic>
#include <mutex>
#include <set>

namespace Dx11CaptureInfo
{
constexpr UINT None = 0;
constexpr UINT CreateRTV = 1;
constexpr UINT CreateSRV = 2;
constexpr UINT CreateUAV = 4;
constexpr UINT OMSetRTV = 8;
constexpr UINT Upscaler = 16;
constexpr UINT SetCR = 32;
constexpr UINT SetGR = 64;
constexpr UINT SetOMUAV = 128;
constexpr UINT Dispatch = 256;
constexpr UINT DrawInstanced = 512;
constexpr UINT DrawIndexedInstanced = 1024;
} // namespace Dx11CaptureInfo

enum class Dx11ResourceType : UINT
{
    SRV,
    RTV,
    UAV,
};

struct Dx11ResourceInfo
{
    ID3D11Texture2D* texture = nullptr;
    UINT width = 0;
    UINT height = 0;
    UINT arraySize = 1;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT bindFlags = 0;
    UINT miscFlags = 0;
    UINT sampleCount = 1;
    Dx11ResourceType type = Dx11ResourceType::SRV;
    bool extended = false;
    UINT captureInfo = Dx11CaptureInfo::None;
};

struct Dx11HudlessInfo
{
    UINT64 lastUsedFrame = 0;
    UINT64 retryStartFrame = 0;
    UINT64 lastTriedFrame = 0;
    UINT64 retryCount = 0;
    UINT64 reuseCount = 0;
    UINT64 useCount = 0;
    bool ignore = false;
    bool dontReuse = false;
};

class Hudfix_Dx11
{
  private:
    inline static UINT64 _upscaleCounter = 0;
    inline static UINT64 _fgCounter = 0;

    inline static double _frameTime = 0.0;
    inline static bool _skipTracking = false;
    inline static bool _skipHudlessChecks = false;

    inline static ID3D11Texture2D* _captureBuffer[BUFFER_COUNT] = {};
    inline static Dx11WithDx12::D3D11_TEXTURE2D_RESOURCE_C _sharedCapture[BUFFER_COUNT] = {};
    inline static FT_Dx12* _formatTransfer[BUFFER_COUNT] = {};

    // D3D11 has no descriptor/resource Release tracking in this implementation. Use a private-data identity token
    // instead of the raw pointer for persistent bookkeeping so a recycled COM address cannot inherit old HUDless state.
    inline static ankerl::unordered_dense::map<UINT64, Dx11HudlessInfo> _hudlessList;
    inline static std::set<UINT64> _captureList;
    inline static ankerl::unordered_dense::map<void*, UINT64> _capturedIdentity;
    inline static std::atomic<UINT64> _nextIdentity { 1 };
    inline static std::atomic<bool> _identityFallbackWarning { false };
    inline static std::atomic<bool> _msaaWarning { false };

    inline static std::mutex _checkMutex;
    inline static std::mutex _captureMutex;
    inline static std::mutex _counterMutex;
    inline static INT64 _captureCounter[BUFFER_COUNT] = {};

    static bool CheckCapture();
    static void HudlessFound();
    static int GetIndex();

    static UINT64 GetResourceIdentity(ID3D11Texture2D* texture);
    static void ValidateCapturedIdentity(ID3D11Texture2D* texture, UINT64 identity);

    static bool CreateCaptureResource(ID3D11DeviceContext* context, const Dx11ResourceInfo& source, int index);
    static bool CopyCaptureResource(ID3D11DeviceContext* context, Dx11ResourceInfo* source, int index);
    static bool PublishCaptureToFg(ID3D11DeviceContext* context, Dx11ResourceInfo* source, int index);

  public:
    static void UpscaleStart();
    static void UpscaleEnd(UINT64 frameId, double lastFGFrameTime);
    static void PresentStart();
    static void PresentEnd();

    static UINT64 ActiveUpscaleFrame();
    static UINT64 ActivePresentFrame();

    static bool IsResourceCheckActive();
    static bool SkipHudlessChecks();

    static bool FillResourceInfo(ID3D11Texture2D* texture, Dx11ResourceType type, UINT captureInfo,
                                 Dx11ResourceInfo* outInfo);
    static bool CheckResource(Dx11ResourceInfo* resource);
    static bool CheckForHudless(ID3D11DeviceContext* context, Dx11ResourceInfo* resource, bool ignoreBlocked = false);

    static void ResetCounters();
    static void ReleaseResources();

    static bool GetSkipStatus() { return _skipTracking; }
    static void SetSkipStatus(bool status) { _skipTracking = status; }
};
