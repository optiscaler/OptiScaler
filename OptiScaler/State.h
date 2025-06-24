#pragma once
#include "pch.h"

#include "upscalers/IFeature.h"
#include "framegen/IFGFeature_Dx12.h"

#include <deque>
#include <vulkan/vulkan.h>
#include <ankerl/unordered_dense.h>
#include <flag-set-cpp/flag_set.hpp>

typedef enum API
{
    NotSelected = 0,
    DX11,
    DX12,
    Vulkan,
} API;

enum class GameQuirk
{
    CyberpunkHudlessStateOverride,
    SkipFsr3Method,
    FastFeatureReset,
    LoadD3D12Manually,
    KernelBaseHooks,
    VulkanDLSSBarrierFixup,
    _
};

typedef enum FGType : uint32_t
{
    NoFG,
    OptiFG,
    Nukems
} FGType;

class State
{
  public:
    static State& Instance()
    {
        static State instance;
        return instance;
    }

    std::string GameName;
    std::string GameExe;
    std::string GpuName;

    bool NvngxDx11Inited = false;
    bool NvngxDx12Inited = false;
    bool NvngxVkInited = false;

    flag_set<GameQuirk> gameQuirks;

    // Reseting on creation of new feature
    std::optional<bool> AutoExposure;

    // DLSSG
    bool NukemsFilesAvailable = false;
    bool DLSSGDebugView = false;
    bool DLSSGInterpolatedOnly = false;
    uint32_t delayMenuRenderBy = 0;

    // FSR Common
    float lastFsrCameraNear = 0.0f;
    float lastFsrCameraFar = 0.0f;

    // Frame Generation
    FGType activeFgType = FGType::NoFG;

    // OptiFG
    bool FGonlyGenerated = false;
    bool FGchanged = false;
    bool SCchanged = false;
    bool skipHeapCapture = false;

    bool FGcaptureResources = false;
    int FGcapturedResourceCount = false;
    bool FGresetCapturedResources = false;
    bool FGonlyUseCapturedResources = false;

    bool FSRFGFTPchanged = false;

    // NVNGX init parameters
    uint64_t NVNGX_ApplicationId = 1337;
    std::wstring NVNGX_ApplicationDataPath;
    std::string NVNGX_ProjectId;
    NVSDK_NGX_Version NVNGX_Version {};
    const NVSDK_NGX_FeatureCommonInfo* NVNGX_FeatureInfo = nullptr;
    std::vector<std::wstring> NVNGX_FeatureInfo_Paths;
    NVSDK_NGX_LoggingInfo NVNGX_Logger { nullptr, NVSDK_NGX_LOGGING_LEVEL_OFF, false };
    NVSDK_NGX_EngineType NVNGX_Engine = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
    std::string NVNGX_EngineVersion;

    // NGX OTA
    std::string NGX_OTA_Dlss;
    std::string NGX_OTA_Dlssd;

    API api = API::NotSelected;
    API swapchainApi = API::NotSelected;

    // DLSS Enabler
    bool enablerAvailable = false;

    // Framerate
    bool reflexLimitsFps = false;
    bool reflexShowWarning = false;

    // for realtime changes
    ankerl::unordered_dense::map<unsigned int, bool> changeBackend;
    std::string newBackend = "";

    // XeSS debug stuff
    bool xessDebug = false;
    int xessDebugFrames = 5;
    float lastMipBias = 100.0f;
    float lastMipBiasMax = -100.0f;

    // DLSS
    bool dlssPresetsOverriddenExternally = false;
    bool dlssdPresetsOverriddenExternally = false;

    // Spoofing
    bool skipSpoofing = false;
    // For DXVK, it calls DXGI which cause softlock
    bool skipDxgiLoadChecks = false;

    // FSR3.x
    std::vector<const char*> fsr3xVersionNames {};
    std::vector<uint64_t> fsr3xVersionIds {};
    uint32_t currentFsr4Model {};

    // Linux check
    bool isRunningOnLinux = false;
    bool isRunningOnDXVK = false;
    bool isRunningOnNvidia = false;
    bool isDxgiMode = false;
    bool isD3D12Mode = false;
    bool isWorkingAsNvngx = false;

    // Vulkan stuff
    bool vulkanCreatingSC = false;
    bool vulkanSkipHooks = false;
    VkInstance VulkanInstance = nullptr;

    // Framegraph
    std::deque<double> upscaleTimes;
    std::deque<double> frameTimes;
    double lastFrameTime = 0.0;
    std::mutex frameTimeMutex;
    std::string fgTrigSource = "";

    // Swapchain info
    float screenWidth = 800.0;
    float screenHeight = 450.0;

    // HDR
    std::vector<IUnknown*> SCbuffers;
    bool isHdrActive = false;

    std::string setInputApiName;
    std::string currentInputApiName;

    bool isShuttingDown = false;

    // menu warnings
    bool showRestartWarning = false;
    bool nvngxIniDetected = false;

    bool nvngxExists = false;
    std::optional<std::wstring> nvngxReplacement = std::nullopt;
    bool libxessExists = false;
    bool fsrHooks = false;

    IFeature* currentFeature = nullptr;

    IFGFeature_Dx12* currentFG = nullptr;
    IDXGISwapChain* currentSwapchain = nullptr;
    ID3D12Device* currentD3D12Device = nullptr;
    ID3D11Device* currentD3D11Device = nullptr;
    ID3D12CommandQueue* currentCommandQueue = nullptr;

    std::vector<ID3D12Device*> d3d12Devices;
    std::vector<ID3D11Device*> d3d11Devices;
    std::map<UINT64, std::string> adapterDescs;

    bool mhInited = false;

    // Moved checks here to prevent circular includes
    /// <summary>
    /// Enables skipping of LoadLibrary checks
    /// </summary>
    /// <param name="dllName">Lower case dll name without `.dll` at the end. Leave blank for skipping all dll's</param>
    static void DisableChecks(UINT owner, std::string dllName = "")
    {
        if (_skipOwner == 0)
        {
            _skipOwner = owner;
            _skipChecks = true;
            _skipDllName = dllName;
        }
        else
        {
            _skipDllName = ""; // Hack for multiple skip calls
        }
    };

    static void EnableChecks(UINT owner)
    {
        if (_skipOwner == 0 || _skipOwner == owner)
        {
            _skipChecks = false;
            _skipDllName = "";
            _skipOwner = 0;
        }
    };

    static void DisableServeOriginal(UINT owner)
    {
        if (_serveOwner == 0 || _serveOwner == owner)
        {
            _serveOriginal = false;
            _skipOwner = 0;
        }
    };

    static void EnableServeOriginal(UINT owner)
    {
        if (_serveOwner == 0 || _serveOwner == owner)
        {
            _serveOriginal = true;
            _skipOwner = owner;
        }
    };

    static bool SkipDllChecks() { return _skipChecks; }
    static std::string SkipDllName() { return _skipDllName; }
    static bool ServeOriginal() { return _serveOriginal; }

  private:
    inline static bool _skipChecks = false;
    inline static std::string _skipDllName = "";
    inline static UINT _skipOwner = 0;

    inline static bool _serveOriginal = false;
    inline static UINT _serveOwner = 0;

    State() = default;
};
