#include "Streamline_Hooks.h"

#include <json.hpp>
#include "detours/detours.h"

#include <Util.h>
#include <Config.h>
#include <proxies/KernelBase_Proxy.h>
#include <menu/menu_overlay_base.h>
#include <nvapi/ReflexHooks.h>
#include <magic_enum.hpp>
#include <sl1_reflex.h>
#include "include/sl.param/parameters.h"

sl::RenderAPI StreamlineHooks::renderApi = sl::RenderAPI::eCount;

// interposer
decltype(&slInit) StreamlineHooks::o_slInit = nullptr;
decltype(&slSetTag) StreamlineHooks::o_slSetTag = nullptr;
decltype(&slEvaluateFeature) StreamlineHooks::o_slEvaluateFeature = nullptr;
decltype(&slAllocateResources) StreamlineHooks::o_slAllocateResources = nullptr;
decltype(&slSetConstants) StreamlineHooks::o_slSetConstants = nullptr;
decltype(&slGetNativeInterface) StreamlineHooks::o_slGetNativeInterface = nullptr;
decltype(&slSetD3DDevice) StreamlineHooks::o_slSetD3DDevice = nullptr;

decltype(&sl1::slInit) StreamlineHooks::o_slInit_sl1 = nullptr;

sl::PFun_LogMessageCallback* StreamlineHooks::o_logCallback = nullptr;
sl1::pfunLogMessageCallback* StreamlineHooks::o_logCallback_sl1 = nullptr;

// DLSS
StreamlineHooks::PFN_slGetPluginFunction StreamlineHooks::o_dlss_slGetPluginFunction = nullptr;
StreamlineHooks::PFN_slOnPluginLoad StreamlineHooks::o_dlss_slOnPluginLoad = nullptr;

// DLSSG
StreamlineHooks::PFN_slGetPluginFunction StreamlineHooks::o_dlssg_slGetPluginFunction = nullptr;
StreamlineHooks::PFN_slOnPluginLoad StreamlineHooks::o_dlssg_slOnPluginLoad = nullptr;
decltype(&slDLSSGSetOptions) StreamlineHooks::o_slDLSSGSetOptions = nullptr;

// Reflex
StreamlineHooks::PFN_slGetPluginFunction StreamlineHooks::o_reflex_slGetPluginFunction = nullptr;
StreamlineHooks::PFN_slSetConstants_sl1 StreamlineHooks::o_reflex_slSetConstants_sl1 = nullptr;
StreamlineHooks::PFN_slOnPluginLoad StreamlineHooks::o_reflex_slOnPluginLoad = nullptr;
decltype(&slReflexSetOptions) StreamlineHooks::o_slReflexSetOptions = nullptr;
sl::ReflexMode StreamlineHooks::reflexGamesLastMode = sl::ReflexMode::eOff;

// Common
StreamlineHooks::PFN_slGetPluginFunction StreamlineHooks::o_common_slGetPluginFunction = nullptr;
StreamlineHooks::PFN_slOnPluginLoad StreamlineHooks::o_common_slOnPluginLoad = nullptr;
StreamlineHooks::PFN_slSetParameters_sl1 StreamlineHooks::o_common_slSetParameters_sl1 = nullptr;
StreamlineHooks::PFN_setVoid StreamlineHooks::o_setVoid = nullptr;

char* StreamlineHooks::trimStreamlineLog(const char* msg)
{
    int bracket_count = 0;

    char* result = (char*) malloc(strlen(msg) + 1);
    if (!result)
        return NULL;

    strcpy(result, msg);

    size_t length = strlen(result);
    if (length > 0 && result[length - 1] == '\n')
    {
        result[length - 1] = '\0';
    }

    return result;
}

void StreamlineHooks::streamlineLogCallback(sl::LogType type, const char* msg)
{
    char* trimmed_msg = trimStreamlineLog(msg);

    switch (type)
    {
    case sl::LogType::eWarn:
        LOG_WARN("{}", trimmed_msg);
        break;
    case sl::LogType::eInfo:
        LOG_INFO("{}", trimmed_msg);
        break;
    case sl::LogType::eError:
        LOG_ERROR("{}", trimmed_msg);
        break;
    case sl::LogType::eCount:
        LOG_ERROR("{}", trimmed_msg);
        break;
    }

    free(trimmed_msg);

    if (o_logCallback != nullptr)
        o_logCallback(type, msg);
}

sl::Result StreamlineHooks::hkslInit(sl::Preferences* pref, uint64_t sdkVersion)
{
    LOG_FUNC();
    if (pref->logMessageCallback != &streamlineLogCallback)
        o_logCallback = pref->logMessageCallback;
    pref->logLevel = sl::LogLevel::eCount;
    pref->logMessageCallback = &streamlineLogCallback;

    // renderAPI is optional so need to be careful, should only matter for Vulkan
    renderApi = pref->renderAPI;
    State::Instance().slFGInputs.reportEngineType(pref->engine);

    return o_slInit(*pref, sdkVersion);
}

// TODO: add support for slSetTagForFrame
sl::Result StreamlineHooks::hkslSetTag(sl::ViewportHandle& viewport, sl::ResourceTag* tags, uint32_t numTags,
                                       sl::CommandBuffer* cmdBuffer)
{
    if (renderApi != sl::RenderAPI::eD3D12)
    {
        LOG_ERROR("hkslSetTag only supports DX12");
        return o_slSetTag(viewport, tags, numTags, cmdBuffer);
    }

    for (uint32_t i = 0; i < numTags; i++)
    {
        // Cyberpunk hudless state fix for RDNA 2
        if (State::Instance().gameQuirks & GameQuirk::CyberpunkHudlessStateOverride &&
            tags[i].resource->state ==
                (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) &&
            tags[i].type == sl::kBufferTypeHUDLessColor)
        {
            tags[i].resource->state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            LOG_TRACE("Changing hudless resource state");
        }

        if (State::Instance().activeFgInput == FGInput::DLSSG &&
            (tags[i].type == sl::kBufferTypeHUDLessColor || tags[i].type == sl::kBufferTypeDepth ||
             tags[i].type == sl::kBufferTypeHiResDepth || tags[i].type == sl::kBufferTypeLinearDepth ||
             tags[i].type == sl::kBufferTypeMotionVectors || tags[i].type == sl::kBufferTypeUIColorAndAlpha))
        {
            State::Instance().slFGInputs.reportResource(tags[i], (ID3D12GraphicsCommandList*) cmdBuffer);
        }
    }
    auto result = o_slSetTag(viewport, tags, numTags, cmdBuffer);
    return result;
}

sl::Result StreamlineHooks::hkslEvaluateFeature(sl::Feature feature, const sl::FrameToken& frame,
                                                const sl::BaseStructure** inputs, uint32_t numInputs,
                                                sl::CommandBuffer* cmdBuffer)
{
    LOG_FUNC();
    auto result = o_slEvaluateFeature(feature, frame, inputs, numInputs, cmdBuffer);
    return result;
}

sl::Result StreamlineHooks::hkslAllocateResources(sl::CommandBuffer* cmdBuffer, sl::Feature feature,
                                                  const sl::ViewportHandle& viewport)
{
    LOG_FUNC();
    auto result = o_slAllocateResources(cmdBuffer, feature, viewport);
    return result;
}

sl::Result StreamlineHooks::hkslGetNativeInterface(void* proxyInterface, void** baseInterface)
{
    LOG_FUNC();
    auto result = o_slGetNativeInterface(proxyInterface, baseInterface);
    return result;
}

sl::Result StreamlineHooks::hkslSetD3DDevice(void* d3dDevice)
{
    LOG_FUNC();
    auto result = o_slSetD3DDevice(d3dDevice);
    return result;
}

void StreamlineHooks::streamlineLogCallback_sl1(sl1::LogType type, const char* msg)
{
    char* trimmed_msg = trimStreamlineLog(msg);

    switch (type)
    {
    case sl1::LogType::eLogTypeWarn:
        LOG_WARN("{}", trimmed_msg);
        break;
    case sl1::LogType::eLogTypeInfo:
        LOG_INFO("{}", trimmed_msg);
        break;
    case sl1::LogType::eLogTypeError:
        LOG_ERROR("{}", trimmed_msg);
        break;
    case sl1::LogType::eLogTypeCount:
        LOG_ERROR("{}", trimmed_msg);
        break;
    }

    free(trimmed_msg);

    if (o_logCallback_sl1)
        o_logCallback_sl1(type, msg);
}

bool StreamlineHooks::hkslInit_sl1(sl1::Preferences* pref, int applicationId)
{
    LOG_FUNC();
    if (pref->logMessageCallback != &streamlineLogCallback_sl1)
        o_logCallback_sl1 = pref->logMessageCallback;
    pref->logLevel = sl1::LogLevel::eLogLevelCount;
    pref->logMessageCallback = &streamlineLogCallback_sl1;
    return o_slInit_sl1(*pref, applicationId);
}

struct Adapter
{
    LUID id {};
    VendorId::Value vendor {};
    uint32_t bit; // in the adapter bit-mask
    uint32_t architecture {};
    uint32_t implementation {};
    uint32_t revision {};
    uint32_t deviceId {};
    void* nativeInterface {};
};

constexpr uint32_t kMaxNumSupportedGPUs = 8;

struct SystemCaps
{
    uint32_t gpuCount {};
    uint32_t osVersionMajor {};
    uint32_t osVersionMinor {};
    uint32_t osVersionBuild {};
    uint32_t driverVersionMajor {};
    uint32_t driverVersionMinor {};
    Adapter adapters[kMaxNumSupportedGPUs] {};
    uint32_t gpuLoad[kMaxNumSupportedGPUs] {}; // percentage
    bool hwsSupported {};                      // OS wide setting, not per adapter
    bool laptopDevice {};
};

struct SystemCapsSl15
{
    uint32_t gpuCount {};
    uint32_t osVersionMajor {};
    uint32_t osVersionMinor {};
    uint32_t osVersionBuild {};
    uint32_t driverVersionMajor {};
    uint32_t driverVersionMinor {};
    uint32_t architecture[kMaxNumSupportedGPUs] {};
    uint32_t implementation[kMaxNumSupportedGPUs] {};
    uint32_t revision[kMaxNumSupportedGPUs] {};
    uint32_t gpuLoad[kMaxNumSupportedGPUs] {}; // percentage
    bool hwSchedulingEnabled {};
};

bool StreamlineHooks::hkdlss_slOnPluginLoad(void* params, const char* loaderJSON, const char** pluginJSON)
{
    LOG_FUNC();

    // TODO: do it better than "static" and hoping for the best
    static std::string config;

    auto result = o_dlss_slOnPluginLoad(params, loaderJSON, pluginJSON);

    if (Config::Instance()->VulkanExtensionSpoofing.value_or_default())
    {
        nlohmann::json configJson = nlohmann::json::parse(*pluginJSON);

        configJson["external"]["vk"]["instance"]["extensions"].clear();
        configJson["external"]["vk"]["device"]["extensions"].clear();
        configJson["external"]["vk"]["device"]["1.2_features"].clear();

        config = configJson.dump();

        *pluginJSON = config.c_str();
    }

    return result;
}

void setSystemCapsArch(sl::param::IParameters* params, uint32_t arch)
{
    if (State::Instance().streamlineVersion.major > 1)
    {
        SystemCaps* caps = {};
        sl::param::getPointerParam(params, sl::param::common::kSystemCaps, &caps);

        if (caps)
        {
            for (auto& adapter : caps->adapters)
            {
                if ((uint32_t) adapter.vendor != 0)
                {
                    adapter.vendor = VendorId::Nvidia;
                    adapter.architecture = arch;
                }
            }

            caps->driverVersionMajor = 999;
            caps->hwsSupported = true;
        }
    }
    else if (State::Instance().streamlineVersion.major == 1)
    {
        // This should be Streamline 1.5 as previous versions don't even have slOnPluginLoad

        LOG_TRACE("Attempting to change system caps for Streamline v1, this could fail depending on the exact version");
        SystemCapsSl15* caps = {};
        sl::param::getPointerParam(params, sl::param::common::kSystemCaps, &caps);

        if (caps)
        {
            caps->architecture[0] = arch;
            caps->driverVersionMajor = 999;

            // This will write outside the struct if SystemCaps is smaller than expected
            // Witcher 3 (sl 1.5) uses this layout
            // Layout from Streamline 1.3 is somehow bigger than this so it should be fine
            caps->hwSchedulingEnabled = true;
        }
    }
}

bool StreamlineHooks::hkdlssg_slOnPluginLoad(void* params, const char* loaderJSON, const char** pluginJSON)
{
    LOG_FUNC();

    // TODO: do it better than "static" and hoping for the best
    static std::string config;

    bool skipArchSpoof =
        Config::Instance()->StreamlineSpoofing.value_or_default() &&
        (Config::Instance()->FGInput != FGInput::Nukems || Config::Instance()->FGInput != FGInput::DLSSG);

    if (skipArchSpoof)
        setSystemCapsArch((sl::param::IParameters*) params, 0);

    auto result = o_dlssg_slOnPluginLoad(params, loaderJSON, pluginJSON);

    if (skipArchSpoof)
        setSystemCapsArch((sl::param::IParameters*) params, UINT_MAX);

    nlohmann::json configJson = nlohmann::json::parse(*pluginJSON);

    // Kill the DLSSG streamline swapchain hooks
    if (State::Instance().activeFgInput == FGInput::DLSSG)
    {
        configJson["hooks"].clear();
        configJson["exclusive_hooks"].clear();
    }

    if (Config::Instance()->VulkanExtensionSpoofing.value_or_default())
    {
        configJson["external"]["vk"]["instance"]["extensions"].clear();
        configJson["external"]["vk"]["device"]["extensions"].clear();
        configJson["external"]["vk"]["device"]["1.2_features"].clear();
    }

    config = configJson.dump();

    *pluginJSON = config.c_str();

    return result;
}

sl::Result StreamlineHooks::hkslSetConstants(const sl::Constants& values, const sl::FrameToken& frame,
                                             const sl::ViewportHandle& viewport)
{
    unsigned int frameIndex = frame;

    LOG_TRACE("called with frameIndex: {}", frameIndex);

    State::Instance().slFGInputs.setConstants(values);

    // With sl that's the best "fg starts soon-ish" we get
    auto fg = State::Instance().currentFG;
    if (fg != nullptr)
    {
        fg->Present();
        if (State::Instance().gameQuirks & GameQuirk::SetConstantsMarksNewFrame)
            fg->StartNewFrame();
    }

    return o_slSetConstants(values, frame, viewport);
}

bool StreamlineHooks::hkcommon_slOnPluginLoad(void* params, const char* loaderJSON, const char** pluginJSON)
{
    LOG_FUNC();
    auto result = o_common_slOnPluginLoad(params, loaderJSON, pluginJSON);

    if (Config::Instance()->StreamlineSpoofing.value_or_default())
        setSystemCapsArch((sl::param::IParameters*) params, UINT_MAX);

    return result;
}

sl::Result StreamlineHooks::hkslDLSSGSetOptions(const sl::ViewportHandle& viewport, const sl::DLSSGOptions& options)
{
    if (State::Instance().api != API::Vulkan)
        return o_slDLSSGSetOptions(viewport, options);

    // Only matters for Vulkan, DX doesn't use this delay
    if (options.mode != sl::DLSSGMode::eOff && !MenuOverlayBase::IsVisible())
        State::Instance().delayMenuRenderBy = 10;

    if (MenuOverlayBase::IsVisible())
    {
        sl::DLSSGOptions newOptions = options;
        newOptions.mode = sl::DLSSGMode::eOff;
        newOptions.flags |= sl::DLSSGFlags::eRetainResourcesWhenOff;

        LOG_TRACE("DLSSG Modified Mode: {}", (uint32_t) newOptions.mode);
        ReflexHooks::setDlssgDetectedState(false);
        return o_slDLSSGSetOptions(viewport, newOptions);
    }

    // Can't tell if eAuto means enabled or disabled
    ReflexHooks::setDlssgDetectedState(options.mode == sl::DLSSGMode::eOn);
    return o_slDLSSGSetOptions(viewport, options);
}

bool StreamlineHooks::hkreflex_slOnPluginLoad(void* params, const char* loaderJSON, const char** pluginJSON)
{
    auto result = o_reflex_slOnPluginLoad(params, loaderJSON, pluginJSON);
    return result;
}

sl::Result StreamlineHooks::hkslReflexSetOptions(const sl::ReflexOptions& options)
{
    reflexGamesLastMode = options.mode;

    sl::ReflexOptions newOptions = options;

    if (Config::Instance()->FN_ForceReflex == 2)
        newOptions.mode = sl::ReflexMode::eLowLatencyWithBoost;

    // Will cause a pink screen when used with DLSSG
    // if (Config::Instance()->FN_ForceReflex == 1)
    //     newOptions.mode = sl::ReflexMode::eOff;

    return o_slReflexSetOptions(newOptions);
}

void* StreamlineHooks::hkdlss_slGetPluginFunction(const char* functionName)
{
    // LOG_DEBUG("{}", functionName);

    if (strcmp(functionName, "slOnPluginLoad") == 0)
    {
        o_dlss_slOnPluginLoad = (PFN_slOnPluginLoad) o_dlss_slGetPluginFunction(functionName);
        return &hkdlss_slOnPluginLoad;
    }

    return o_dlss_slGetPluginFunction(functionName);
}

void* StreamlineHooks::hkdlssg_slGetPluginFunction(const char* functionName)
{
    // LOG_DEBUG("{}", functionName);

    if (strcmp(functionName, "slOnPluginLoad") == 0)
    {
        o_dlssg_slOnPluginLoad = (PFN_slOnPluginLoad) o_dlssg_slGetPluginFunction(functionName);
        return &hkdlssg_slOnPluginLoad;
    }

    if (strcmp(functionName, "slDLSSGSetOptions") == 0 && State::Instance().api == API::Vulkan)
    {
        o_slDLSSGSetOptions = (decltype(&slDLSSGSetOptions)) o_dlssg_slGetPluginFunction(functionName);
        return &hkslDLSSGSetOptions;
    }

    return o_dlssg_slGetPluginFunction(functionName);
}

bool StreamlineHooks::hkreflex_slSetConstants_sl1(const void* data, uint32_t frameIndex, uint32_t id)
{
    // Streamline v1's version of slReflexSetOptions + slPCLSetMarker
    static sl1::ReflexConstants constants {};
    constants = *(const sl1::ReflexConstants*) data;

    reflexGamesLastMode = (sl::ReflexMode) constants.mode;

    LOG_DEBUG("mode: {}, frameIndex: {}, id: {}", (uint32_t) constants.mode, frameIndex, id);

    if (Config::Instance()->FN_ForceReflex == 2)
        constants.mode = sl1::ReflexMode::eReflexModeLowLatencyWithBoost;

    // Will cause a pink screen when used with DLSSG
    // else if (Config::Instance()->FN_ForceReflex == 1)
    //     constants.mode = sl1::ReflexMode::eReflexModeOff;

    return o_reflex_slSetConstants_sl1(&constants, frameIndex, id);
}

void* StreamlineHooks::hkreflex_slGetPluginFunction(const char* functionName)
{
    // LOG_DEBUG("{}", functionName);

    if (strcmp(functionName, "slSetConstants") == 0 && State::Instance().streamlineVersion.major == 1)
    {
        o_reflex_slSetConstants_sl1 = (PFN_slSetConstants_sl1) o_reflex_slGetPluginFunction(functionName);
        return &hkreflex_slSetConstants_sl1;
    }

    if (strcmp(functionName, "slOnPluginLoad") == 0)
    {
        o_reflex_slOnPluginLoad = (PFN_slOnPluginLoad) o_reflex_slGetPluginFunction(functionName);
        return &hkreflex_slOnPluginLoad;
    }

    if (strcmp(functionName, "slReflexSetOptions") == 0)
    {
        o_slReflexSetOptions = (decltype(&slReflexSetOptions)) o_reflex_slGetPluginFunction(functionName);
        return &hkslReflexSetOptions;
    }

    return o_reflex_slGetPluginFunction(functionName);
}

bool StreamlineHooks::hk_setVoid(void* self, const char* key, void** value)
{
    // LOG_DEBUG("{}", key);

    if (strcmp(key, sl::param::common::kSystemCaps) == 0)
    {
        LOG_TRACE("Attempting to change system caps for Streamline v1, this could fail depending on the exact version");

        // SystemCapsSl15 is not entirely correct for Streamline 1.3
        // But we here only use the beginning that matches + extra
        auto caps = (SystemCapsSl15*) value;

        if (caps)
        {
            caps->gpuCount = 1;
            caps->architecture[0] = UINT_MAX;
            caps->driverVersionMajor = 999;

            // HAGS
            *((char*) value + 56) = (char) 0x01;
        }
    }

    return o_setVoid(self, key, value);
}

void StreamlineHooks::hkcommon_slSetParameters_sl1(void* params)
{
    LOG_FUNC();

    if (o_setVoid == nullptr && params)
    {
        void** vtable = *(void***) params;

        // It's flipped, 0 -> set void*, 7 -> get void*
        o_setVoid = (PFN_setVoid) vtable[0];

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_setVoid != nullptr)
            DetourAttach(&(PVOID&) o_setVoid, hk_setVoid);

        DetourTransactionCommit();
    }

    o_common_slSetParameters_sl1(params);
}

void* StreamlineHooks::hkcommon_slGetPluginFunction(const char* functionName)
{
    // LOG_DEBUG("{}", functionName);

    if (strcmp(functionName, "slOnPluginLoad") == 0)
    {
        o_common_slOnPluginLoad = (PFN_slOnPluginLoad) o_common_slGetPluginFunction(functionName);
        return &hkcommon_slOnPluginLoad;
    }

    // Used around Streamline v1.3, as 1.5 doesn't seem to have it anymore
    if (strcmp(functionName, "slSetParameters") == 0)
    {
        o_common_slSetParameters_sl1 = (PFN_slSetParameters_sl1) o_common_slGetPluginFunction(functionName);
        return &hkcommon_slSetParameters_sl1;
    }

    return o_common_slGetPluginFunction(functionName);
}

void StreamlineHooks::updateForceReflex()
{
    // Not needed for Streamline v1 as slSetConstants is sent every frame
    if (o_slReflexSetOptions)
    {
        sl::ReflexOptions options;

        auto forceReflex = Config::Instance()->FN_ForceReflex.value_or_default();

        if (forceReflex == 2)
            options.mode = sl::ReflexMode::eLowLatencyWithBoost;
        else if (forceReflex == 1)
            options.mode = sl::ReflexMode::eOff;
        else if (forceReflex == 0)
            options.mode = reflexGamesLastMode;

        auto result = o_slReflexSetOptions(options);
    }
}

// SL INTERPOSER

void StreamlineHooks::unhookInterposer()
{
    LOG_FUNC();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_slSetTag)
    {
        DetourDetach(&(PVOID&) o_slSetTag, hkslSetTag);
        o_slSetTag = nullptr;
    }

    if (o_slInit)
    {
        DetourDetach(&(PVOID&) o_slInit, hkslInit);
        o_slInit = nullptr;
    }

    if (o_slInit_sl1)
    {
        DetourDetach(&(PVOID&) o_slInit_sl1, hkslInit_sl1);
        o_slInit_sl1 = nullptr;
    }

    o_logCallback_sl1 = nullptr;
    o_logCallback = nullptr;

    DetourTransactionCommit();
}

// Call it just after sl.interposer's load or if sl.interposer is already loaded
void StreamlineHooks::hookInterposer(HMODULE slInterposer)
{
    LOG_FUNC();

    if (!slInterposer)
    {
        LOG_WARN("Streamline module in NULL");
        return;
    }

    // Interposer needs this or it might end in an infinite loop calling itself
    static HMODULE last_slInterposer = nullptr;

    if (last_slInterposer == slInterposer)
        return;

    last_slInterposer = slInterposer;

    // Looks like when reading DLL version load methods are called
    // To prevent loops disabling checks for sl.interposer.dll
    State::DisableChecks(7, "sl.interposer");

    if (o_slSetTag || o_slInit || o_slInit_sl1)
        unhookInterposer();

    {
        char dllPath[MAX_PATH];
        GetModuleFileNameA(slInterposer, dllPath, MAX_PATH);

        Util::version_t sl_version;
        Util::GetDLLVersion(string_to_wstring(dllPath), &sl_version);

        State::Instance().streamlineVersion.major = sl_version.major;
        State::Instance().streamlineVersion.minor = sl_version.minor;
        State::Instance().streamlineVersion.patch = sl_version.patch;

        LOG_INFO("Streamline version: {}.{}.{}", sl_version.major, sl_version.minor, sl_version.patch);

        if (sl_version.major >= 2)
        {
            o_slSetTag =
                reinterpret_cast<decltype(&slSetTag)>(KernelBaseProxy::GetProcAddress_()(slInterposer, "slSetTag"));
            o_slInit = reinterpret_cast<decltype(&slInit)>(KernelBaseProxy::GetProcAddress_()(slInterposer, "slInit"));
            o_slEvaluateFeature = reinterpret_cast<decltype(&slEvaluateFeature)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slEvaluateFeature"));
            o_slAllocateResources = reinterpret_cast<decltype(&slAllocateResources)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slAllocateResources"));
            o_slSetConstants = reinterpret_cast<decltype(&slSetConstants)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slSetConstants"));
            o_slGetNativeInterface = reinterpret_cast<decltype(&slGetNativeInterface)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slGetNativeInterface"));
            o_slSetD3DDevice = reinterpret_cast<decltype(&slSetD3DDevice)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slSetD3DDevice"));

            if (o_slInit != nullptr)
            {
                LOG_TRACE("Hooking v2");
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                if (o_slSetTag != nullptr && (Config::Instance()->FGInput.value_or_default() == FGInput::Nukems ||
                                              Config::Instance()->FGInput.value_or_default() == FGInput::DLSSG))
                    DetourAttach(&(PVOID&) o_slSetTag, hkslSetTag);

                DetourAttach(&(PVOID&) o_slInit, hkslInit);

                if (o_slEvaluateFeature != nullptr)
                    DetourAttach(&(PVOID&) o_slEvaluateFeature, hkslEvaluateFeature);

                if (o_slAllocateResources != nullptr)
                    DetourAttach(&(PVOID&) o_slAllocateResources, hkslAllocateResources);

                if (o_slSetConstants != nullptr)
                    DetourAttach(&(PVOID&) o_slSetConstants, hkslSetConstants);

                if (o_slGetNativeInterface != nullptr)
                    DetourAttach(&(PVOID&) o_slGetNativeInterface, hkslGetNativeInterface);

                if (o_slSetD3DDevice != nullptr)
                    DetourAttach(&(PVOID&) o_slSetD3DDevice, hkslSetD3DDevice);

                DetourTransactionCommit();
            }
        }
        else if (sl_version.major == 1)
        {
            o_slInit_sl1 =
                reinterpret_cast<decltype(&sl1::slInit)>(KernelBaseProxy::GetProcAddress_()(slInterposer, "slInit"));

            if (o_slInit_sl1)
            {
                LOG_TRACE("Hooking v1");
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                DetourAttach(&(PVOID&) o_slInit_sl1, hkslInit_sl1);

                DetourTransactionCommit();
            }
        }
    }

    State::EnableChecks(7);
}

// SL DLSS

void StreamlineHooks::unhookDlss()
{
    LOG_FUNC();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_dlss_slGetPluginFunction)
    {
        DetourDetach(&(PVOID&) o_dlss_slGetPluginFunction, hkdlss_slGetPluginFunction);
        o_dlss_slGetPluginFunction = nullptr;
    }

    DetourTransactionCommit();
}

void StreamlineHooks::hookDlss(HMODULE slDlss)
{
    LOG_FUNC();

    if (!slDlss)
    {
        LOG_WARN("Dlss module in NULL");
        return;
    }

    if (o_dlss_slGetPluginFunction)
        unhookDlss();

    o_dlss_slGetPluginFunction =
        reinterpret_cast<PFN_slGetPluginFunction>(KernelBaseProxy::GetProcAddress_()(slDlss, "slGetPluginFunction"));

    if (o_dlss_slGetPluginFunction != nullptr)
    {
        LOG_TRACE("Hooking slGetPluginFunction in sl.dlss");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_dlss_slGetPluginFunction, hkdlss_slGetPluginFunction);

        DetourTransactionCommit();
    }
}

// SL DLSSG

void StreamlineHooks::unhookDlssg()
{
    LOG_FUNC();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_dlssg_slGetPluginFunction)
    {
        DetourDetach(&(PVOID&) o_dlssg_slGetPluginFunction, hkdlssg_slGetPluginFunction);
        o_dlssg_slGetPluginFunction = nullptr;
    }

    DetourTransactionCommit();
}

void StreamlineHooks::hookDlssg(HMODULE slDlssg)
{
    LOG_FUNC();

    if (!slDlssg)
    {
        LOG_WARN("Dlssg module in NULL");
        return;
    }

    if (o_dlssg_slGetPluginFunction)
        unhookDlssg();

    o_dlssg_slGetPluginFunction =
        reinterpret_cast<PFN_slGetPluginFunction>(KernelBaseProxy::GetProcAddress_()(slDlssg, "slGetPluginFunction"));

    if (o_dlssg_slGetPluginFunction != nullptr)
    {
        LOG_TRACE("Hooking slGetPluginFunction in sl.dlssg");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_dlssg_slGetPluginFunction, hkdlssg_slGetPluginFunction);

        DetourTransactionCommit();
    }
}

// SL REFLEX

void StreamlineHooks::unhookReflex()
{
    LOG_FUNC();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_reflex_slGetPluginFunction)
    {
        DetourDetach(&(PVOID&) o_reflex_slGetPluginFunction, hkreflex_slGetPluginFunction);
        o_reflex_slGetPluginFunction = nullptr;
    }

    DetourTransactionCommit();
}

void StreamlineHooks::hookReflex(HMODULE slReflex)
{
    LOG_FUNC();

    if (!slReflex)
    {
        LOG_WARN("Reflex module in NULL");
        return;
    }

    if (o_reflex_slGetPluginFunction)
        unhookReflex();

    o_reflex_slGetPluginFunction =
        reinterpret_cast<PFN_slGetPluginFunction>(KernelBaseProxy::GetProcAddress_()(slReflex, "slGetPluginFunction"));

    if (o_reflex_slGetPluginFunction != nullptr)
    {
        LOG_TRACE("Hooking slGetPluginFunction in sl.reflex");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_reflex_slGetPluginFunction, hkreflex_slGetPluginFunction);

        DetourTransactionCommit();
    }
}

// SL COMMON

void StreamlineHooks::unhookCommon()
{
    LOG_FUNC();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_common_slGetPluginFunction)
    {
        DetourDetach(&(PVOID&) o_common_slGetPluginFunction, hkcommon_slGetPluginFunction);
        o_common_slGetPluginFunction = nullptr;
    }

    DetourTransactionCommit();
}

void StreamlineHooks::hookCommon(HMODULE slCommon)
{
    LOG_FUNC();

    if (!slCommon)
    {
        LOG_WARN("Common module in NULL");
        return;
    }

    if (o_common_slGetPluginFunction)
        unhookCommon();

    o_common_slGetPluginFunction =
        reinterpret_cast<PFN_slGetPluginFunction>(KernelBaseProxy::GetProcAddress_()(slCommon, "slGetPluginFunction"));

    if (o_common_slGetPluginFunction != nullptr)
    {
        LOG_TRACE("Hooking slGetPluginFunction in sl.common");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_common_slGetPluginFunction, hkcommon_slGetPluginFunction);

        DetourTransactionCommit();
    }
}
