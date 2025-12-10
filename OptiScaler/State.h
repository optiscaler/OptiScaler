#pragma once
#include "pch.h"

#include "upscalers/IFeature.h"
#include "framegen/IFGFeature_Dx12.h"
#include <inputs/FG/Streamline_Inputs_Dx12.h>
#include "misc/Quirks.h"

#include <set>
#include <deque>
#include <vulkan/vulkan.h>
#include <ankerl/unordered_dense.h>
#include <mutex>

typedef enum API
{
    NotSelected = 0,
    DX11,
    DX12,
    Vulkan,
} API;

enum class FGPreset : uint32_t
{
    NoFG,
    OptiFG,
    Nukems,
};

enum class FGInput : uint32_t
{
    NoFG,
    Nukems,
    FSRFG,
    DLSSG, // technically Streamline inputs
    XeFG,
    Upscaler, // OptiFG
    FSRFG30,
};

enum class FGOutput : uint32_t
{
    NoFG,
    Nukems,
    FSRFG,
    DLSSG,
    XeFG
};

typedef struct CapturedHudlessInfo
{
    UINT64 usageCount = 1;
    UINT captureInfo = 0;
    bool enabled = true;
} captured_hudless_info;

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
    ankerl::unordered_dense::map<void*, std::string> DeviceAdapterNames;

    bool NvngxDx11Inited = false;
    bool NvngxDx12Inited = false;
    bool NvngxVkInited = false;

    flag_set<GameQuirk> gameQuirks;
    bool isOptiPatcherSucceed = false;

    // Reseting on creation of new feature
    std::optional<bool> AutoExposure;

    // FG
    UINT64 FGLastFrame = 0;

    // DLSSG
    bool NukemsFilesAvailable = false;
    bool DLSSGDebugView = false;
    bool DLSSGInterpolatedOnly = false;
    uint32_t delayMenuRenderBy = 0;
    UINT64 DLSSGLastFrame = 0;

    // FSR Common
    float lastFsrCameraNear = 0.0f;
    float lastFsrCameraFar = 0.0f;

    // Frame Generation
    FGInput activeFgInput = FGInput::NoFG;
    FGOutput activeFgOutput = FGOutput::NoFG;

    // Streamline FG inputs
    Sl_Inputs_Dx12 slFGInputs = {};

    // OptiFG
    bool FGonlyGenerated = false;
    bool FGHudlessCompare = false;
    bool FGchanged = false;
    bool SCchanged = false;
    bool skipHeapCapture = false;

    bool FGcaptureResources = false;
    size_t FGcapturedResourceCount = false;
    bool FGresetCapturedResources = false;
    bool FGonlyUseCapturedResources = false;

    bool FSRFGFTPchanged = false;
    bool FSRFGInputActive = false;

    ankerl::unordered_dense::map<void*, CapturedHudlessInfo> CapturedHudlesses;
    bool ClearCapturedHudlesses = false;

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

    feature_version streamlineVersion = { 0, 0, 0 };

    API api = API::NotSelected;
    API swapchainApi = API::NotSelected;

    // Framerate
    bool reflexLimitsFps = false;
    bool reflexShowWarning = false;
    bool rtssReflexInjection = false;
    UINT64 frameCount = 0;

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
    bool skipParentWrapping = false;

    // quirks
    std::vector<std::string> detectedQuirks {};

    // FFX
    std::vector<const char*> ffxUpscalerVersionNames {};
    std::vector<uint64_t> ffxUpscalerVersionIds {};
    std::vector<const char*> ffxFGVersionNames {};
    std::vector<uint64_t> ffxFGVersionIds {};
    uint32_t currentFsr4Model {};

    // Linux checks
    bool isRunningOnLinux = false;
    bool isRunningOnDXVK = false;

    // Other checks
    bool isRunningOnNvidia = false;
    bool isPascalOrOlder = false;
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
    double lastFGFrameTime = 0.0;
    double presentFrameTime = 0.0;
    std::mutex frameTimeMutex;

    // Version check
    std::mutex versionCheckMutex;
    bool versionCheckInProgress = false;
    bool versionCheckCompleted = false;
    bool updateAvailable = false;
    std::string latestVersionTag;
    std::string latestVersionUrl;
    std::string versionCheckError;

    // Swapchain info
    float screenWidth = 800.0;
    float screenHeight = 450.0;
    bool realExclusiveFullscreen = false;
    bool SCExclusiveFullscreen = false;
    bool SCAllowTearing = false;
    UINT SCLastFlags = 0;

    // HDR
    std::vector<IUnknown*> SCbuffers;
    bool isHdrActive = false;

    std::string setInputApiName;
    std::string currentInputApiName;

    bool isShuttingDown = false;
    std::set<PVOID> modulesToFree;

    // menu warnings
    bool fgSettingsChanged = false;
    bool nvngxIniDetected = false;

    bool nvngxExists = false;
    std::optional<std::wstring> nvngxReplacement = std::nullopt;
    bool libxessExists = false;
    bool fsrHooks = false;

    IFeature* currentFeature = nullptr;
    IFGFeature_Dx12* currentFG = nullptr;
    IDXGISwapChain* currentSwapchain = nullptr;
    IDXGISwapChain* currentWrappedSwapchain = nullptr;
    IDXGISwapChain* currentRealSwapchain = nullptr;
    IDXGISwapChain* currentFGSwapchain = nullptr;
    ID3D12Device* currentD3D12Device = nullptr;
    ID3D11Device* currentD3D11Device = nullptr;
    ID3D12CommandQueue* currentCommandQueue = nullptr;
    VkDevice currentVkDevice = nullptr;
    DXGI_SWAP_CHAIN_DESC currentSwapchainDesc {};

    std::vector<ID3D12Device*> d3d12Devices;
    std::vector<ID3D11Device*> d3d11Devices;
    std::unordered_map<UINT64, std::string> adapterDescs;

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
