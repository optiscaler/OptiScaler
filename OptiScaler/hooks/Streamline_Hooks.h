#pragma once
#include "SysUtils.h"

#include <sl.h>
#include <sl1.h>
#include <sl_dlss_g.h>
#include <sl_dlss.h>
#include <sl_pcl.h>
#include <sl_reflex.h>

#include "include/sl.param/parameters.h"

#include "Hook_Utils.h"

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

enum class BufferType : uint64_t
{
    Depth = 0,
    MotionVectors = 1,
    HUDLessColor = 2,
    ScalingInputColor = 3,
    ScalingOutputColor = 4,
    Normals = 5,
    Roughness = 6,
    Albedo = 7,
    SpecularAlbedo = 8,
    IndirectAlbedo = 9,
    SpecularMotionVectors = 10,
    DisocclusionMask = 11,
    Emissive = 12,
    Exposure = 13,
    NormalRoughness = 14,
    DiffuseHitNoisy = 15,
    DiffuseHitDenoised = 16,
    SpecularHitNoisy = 17,
    SpecularHitDenoised = 18,
    ShadowNoisy = 19,
    ShadowDenoised = 20,
    AmbientOcclusionNoisy = 21,
    AmbientOcclusionDenoised = 22,
    UIColorAndAlpha = 23,
    ShadowHint = 24,
    ReflectionHint = 25,
    ParticleHint = 26,
    TransparencyHint = 27,
    AnimatedTextureHint = 28,
    BiasCurrentColorHint = 29,
    RaytracingDistance = 30,
    ReflectionMotionVectors = 31,
    Position = 32,
    InvalidDepthMotionHint = 33,
    Alpha = 34,
    OpaqueColor = 35,
    ReactiveMaskHint = 36,
    TransparencyAndCompositionMaskHint = 37,
    ReflectedAlbedo = 38,
    ColorBeforeParticles = 39,
    ColorBeforeTransparency = 40,
    ColorBeforeFog = 41,
    SpecularHitDistance = 42,
    SpecularRayDirectionHitDistance = 43,
    SpecularRayDirection = 44,
    DiffuseHitDistance = 45,
    DiffuseRayDirectionHitDistance = 46,
    DiffuseRayDirection = 47,
    HiResDepth = 48,
    LinearDepth = 49,
    BidirectionalDistortionField = 50,
    TransparencyLayer = 51,
    TransparencyLayerOpacity = 52,
    Backbuffer = 53,
    NoWarpMask = 54,
    ColorAfterParticles = 55,
    ColorAfterTransparency = 56,
    ColorAfterFog = 57,
    ScreenSpaceSubsurfaceScatteringGuide = 58,
    ColorBeforeScreenSpaceSubsurfaceScattering = 59,
    ColorAfterScreenSpaceSubsurfaceScattering = 60,
    ScreenSpaceRefractionGuide = 61,
    ColorBeforeScreenSpaceRefraction = 62,
    ColorAfterScreenSpaceRefraction = 63,
    DepthOfFieldGuide = 64,
    ColorBeforeDepthOfField = 65,
    ColorAfterDepthOfField = 66,
    ScalingOutputAlpha = 67
};

class StreamlineHooks
{
  public:
    typedef void* (*PFN_slGetPluginFunction)(const char* functionName);
    typedef bool (*PFN_slOnPluginLoad)(sl::param::IParameters* params, const char* loaderJSON, const char** pluginJSON);
    typedef sl::Result (*PFN_slSetData)(const sl::BaseStructure* inputs, sl::CommandBuffer* cmdBuffer);
    typedef bool (*PFN_slSetConstants_sl1)(const void* data, uint32_t frameIndex, uint32_t id);
    typedef void (*PFN_slSetParameters_sl1)(void* params);
    typedef bool (*PFN_setVoid)(void* self, const char* key, void** value);
    typedef const char* (*PFN_slGetPluginJSONConfig_sl1)();

    static void updateForceReflex();
    static void updateDlssgOptions();

    // MenuOverlayVk submits on a queue it picks itself, into the present path DLSS-G's pacer owns;
    // the two cannot run together. Forces options.mode to eOff while the menu is up. Applies to every
    // DLSS-G option push, including the ones OptiScaler makes through StreamlineProxy.
    static void applyMenuDlssgInterlock(sl::DLSSGOptions& options, bool dlssgPotentiallyActive);

    static void unhookInterposer();
    static void hookInterposer(HMODULE slInterposer);

    static void unhookDlss();
    static void hookDlss(HMODULE slDlss);

    static void unhookDlssg();
    static void hookDlssg(HMODULE slDlssg);

    static void unhookLocalDlssg();
    static void hookLocalDlssg(HMODULE slDlssg);

    static void unhookReflex();
    static void hookReflex(HMODULE slReflex);

    static void unhookPcl();
    static void hookPcl(HMODULE slPcl);

    static void unhookCommon();
    static void hookCommon(HMODULE slCommon);

    static bool isInterposerHooked();
    static bool isDlssHooked();
    static bool isDlssgHooked();
    static bool isLocalDlssgHooked();
    static bool isCommonHooked();
    static bool isPclHooked();
    static bool isReflexHooked();

  private:
    inline static sl::RenderAPI renderApi = sl::RenderAPI::eCount;
    inline static std::mutex setConstantsMutex {};

    // System caps
    inline static SystemCaps* systemCaps = nullptr;
    inline static SystemCapsSl15* systemCapsSl15 = nullptr;
    static void hookSystemCaps(sl::param::IParameters* params);
    static uint32_t getSystemCapsArch(SystemCaps* altSystemCaps = nullptr);
    static void setArch(uint32_t arch, SystemCaps* altSystemCaps = nullptr);
    static void spoofArch(uint32_t currentArch, sl::Feature feature, SystemCaps* altSystemCaps = nullptr);

    // Interposer
    inline static decltype(&slInit) o_slInit = nullptr;
    inline static decltype(&slSetTag) o_slSetTag = nullptr;
    inline static decltype(&slSetTagForFrame) o_slSetTagForFrame = nullptr;
    inline static decltype(&slEvaluateFeature) o_slEvaluateFeature = nullptr;
    inline static decltype(&slAllocateResources) o_slAllocateResources = nullptr;
    inline static decltype(&slSetConstants) o_slSetConstants = nullptr;
    inline static decltype(&slGetNativeInterface) o_slGetNativeInterface = nullptr;
    inline static decltype(&slSetD3DDevice) o_slSetD3DDevice = nullptr;
    inline static decltype(&slGetNewFrameToken) o_slGetNewFrameToken = nullptr;
    inline static decltype(&slIsFeatureSupported) o_slIsFeatureSupported = nullptr;
    inline static decltype(&slIsFeatureLoaded) o_slIsFeatureLoaded = nullptr;
    inline static decltype(&slGetFeatureRequirements) o_slGetFeatureRequirements = nullptr;
    inline static decltype(&slGetFeatureVersion) o_slGetFeatureVersion = nullptr;
    inline static decltype(&slGetFeatureFunction) o_slGetFeatureFunction = nullptr;

    inline static decltype(&sl1::slInit) o_slInit_sl1 = nullptr;
    inline static decltype(&sl1::slSetTag) o_slSetTag_sl1 = nullptr;
    inline static decltype(&sl1::slSetConstants) o_slSetConstants_interposer_sl1 = nullptr;
    inline static decltype(&sl1::slEvaluateFeature) o_slEvaluateFeature_sl1 = nullptr;

    inline static sl::PFun_LogMessageCallback* o_logCallback = nullptr;
    inline static sl1::pfunLogMessageCallback* o_logCallback_sl1 = nullptr;

    static sl::Result hkslInit(const sl::Preferences& pref, uint64_t sdkVersion);
    static sl::Result hkslIsFeatureSupported(sl::Feature feature, const sl::AdapterInfo& adapterInfo);
    static sl::Result hkslIsFeatureLoaded(sl::Feature feature, bool& loaded);
    static sl::Result hkslGetFeatureRequirements(sl::Feature feature, sl::FeatureRequirements& requirements);
    static sl::Result hkslGetFeatureVersion(sl::Feature feature, sl::FeatureVersion& version);
    static sl::Result hkslGetFeatureFunction(sl::Feature feature, const char* functionName, void*& function);
    static bool hkslInit_sl1(const sl1::Preferences& pref, int applicationId);
    static bool hkslSetTag_sl1(const sl1::Resource* resource, sl1::BufferType tag, uint32_t id,
                               const sl1::Extent* extent);
    static bool hkslSetConstants_sl1(const sl1::Constants& values, uint32_t frameIndex, uint32_t id);
    static bool hkslEvaluateFeature_sl1(sl1::CommandBuffer* cmdBuffer, sl1::Feature feature, uint32_t frameIndex,
                                        uint32_t id);
    static sl::Result hkslSetTag(const sl::ViewportHandle& viewport, const sl::ResourceTag* tags, uint32_t numTags,
                                 sl::CommandBuffer* cmdBuffer);

    static sl::Result hkslSetTagForFrame(const sl::FrameToken& frame, const sl::ViewportHandle& viewport,
                                         const sl::ResourceTag* resources, uint32_t numResources,
                                         sl::CommandBuffer* cmdBuffer);

    static sl::Result hkslEvaluateFeature(sl::Feature feature, const sl::FrameToken& frame,
                                          const sl::BaseStructure** inputs, uint32_t numInputs,
                                          sl::CommandBuffer* cmdBuffer);

    static sl::Result hkslAllocateResources(sl::CommandBuffer* cmdBuffer, sl::Feature feature,
                                            const sl::ViewportHandle& viewport);

    static sl::Result hkslGetNativeInterface(void* proxyInterface, void** baseInterface);

    static sl::Result hkslSetD3DDevice(void* d3dDevice);

    // DLSS
    inline static PFN_slGetPluginFunction o_dlss_slGetPluginFunction = nullptr;
    inline static PFN_slOnPluginLoad o_dlss_slOnPluginLoad = nullptr;
    inline static decltype(&slDLSSGetOptimalSettings) o_slDLSSGetOptimalSettings = nullptr;

    static bool hkdlss_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON, const char** pluginJSON);
    static sl::Result hkslDLSSGetOptimalSettings(const sl::DLSSOptions& options, sl::DLSSOptimalSettings& settings);
    static void* hkdlss_slGetPluginFunction(const char* functionName);

    // DLSSG
    inline static PFN_slGetPluginFunction o_dlssg_slGetPluginFunction = nullptr;
    inline static PFN_slOnPluginLoad o_dlssg_slOnPluginLoad = nullptr;
    inline static PFN_slGetPluginJSONConfig_sl1 o_dlssg_slGetPluginJSONConfig_sl1 = nullptr;
    inline static decltype(&slDLSSGSetOptions) o_slDLSSGSetOptions = nullptr;
    inline static decltype(&slDLSSGGetState) o_slDLSSGGetState = nullptr;
    static inline sl::ViewportHandle lastDlssgViewport {}; // For updating options when we change them
    static inline sl::DLSSGOptions lastDlssgOptions {};

    static bool hkdlssg_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON, const char** pluginJSON);
    static sl::Result hkslSetConstants(const sl::Constants& values, const sl::FrameToken& frame,
                                       const sl::ViewportHandle& viewport);
    static sl::Result hkslDLSSGSetOptions(const sl::ViewportHandle& viewport, const sl::DLSSGOptions& options);
    static sl::Result hkslDLSSGGetState(const sl::ViewportHandle& viewport, sl::DLSSGState& state,
                                        const sl::DLSSGOptions* options);
    static void* hkdlssg_slGetPluginFunction(const char* functionName);
    static const char* hkdlssg_slGetPluginJSONConfig_sl1();

    // Local DLSSG
    inline static PFN_slGetPluginFunction o_local_dlssg_slGetPluginFunction = nullptr;
    inline static PFN_slOnPluginLoad o_local_dlssg_slOnPluginLoad = nullptr;

    static bool hklocal_dlssg_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON,
                                             const char** pluginJSON);
    static void* hklocal_dlssg_slGetPluginFunction(const char* functionName);

    // Reflex
    inline static sl::ReflexMode reflexGamesLastMode = sl::ReflexMode::eOff;
    inline static PFN_slGetPluginFunction o_reflex_slGetPluginFunction = nullptr;
    inline static PFN_slSetConstants_sl1 o_reflex_slSetConstants_sl1 = nullptr;
    inline static PFN_slOnPluginLoad o_reflex_slOnPluginLoad = nullptr;
    inline static decltype(&slReflexSetOptions) o_slReflexSetOptions = nullptr;
    inline static decltype(&slReflexSleep) o_slReflexSleep = nullptr;

    static bool hkreflex_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON,
                                        const char** pluginJSON);
    static sl::Result hkslReflexSetOptions(const sl::ReflexOptions& options);
    static sl::Result hkslReflexSleep(const sl::FrameToken& frame);
    static bool hkreflex_slSetConstants_sl1(const void* data, uint32_t frameIndex, uint32_t id);
    static void* hkreflex_slGetPluginFunction(const char* functionName);

    // PCL
    inline static PFN_slGetPluginFunction o_pcl_slGetPluginFunction = nullptr;
    inline static PFN_slOnPluginLoad o_pcl_slOnPluginLoad = nullptr;
    inline static decltype(&slPCLSetMarker) o_slPCLSetMarker = nullptr;

    static bool hkpcl_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON, const char** pluginJSON);
    static void* hkpcl_slGetPluginFunction(const char* functionName);
    static sl::Result hkslPCLSetMarker(sl::PCLMarker marker, const sl::FrameToken& frame);

    // Common
    inline static PFN_slGetPluginFunction o_common_slGetPluginFunction = nullptr;
    inline static PFN_slOnPluginLoad o_common_slOnPluginLoad = nullptr;
    inline static PFN_slSetParameters_sl1 o_common_slSetParameters_sl1 = nullptr;
    inline static PFN_setVoid o_setVoid = nullptr;

    static bool hkcommon_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON,
                                        const char** pluginJSON);
    static void* hkcommon_slGetPluginFunction(const char* functionName);
    static void hkcommon_slSetParameters_sl1(void* params);
    static bool hk_setVoid(void* self, const char* key, void** value);

    // Logging
    static char* trimStreamlineLog(const char* msg);
    static void streamlineLogCallback(sl::LogType type, const char* msg);
    static void streamlineLogCallback_sl1(sl1::LogType type, const char* msg);

    // Function signature checking
    VALIDATE_MEMBER_HOOK(hkslInit, decltype(&slInit))
    VALIDATE_MEMBER_HOOK(hkslInit_sl1, decltype(&sl1::slInit))
    VALIDATE_MEMBER_HOOK(hkslSetTag_sl1, decltype(&sl1::slSetTag))
    VALIDATE_MEMBER_HOOK(hkslSetConstants_sl1, decltype(&sl1::slSetConstants))
    VALIDATE_MEMBER_HOOK(hkslEvaluateFeature_sl1, decltype(&sl1::slEvaluateFeature))
    VALIDATE_MEMBER_HOOK(hkslSetTag, decltype(&slSetTag))
    VALIDATE_MEMBER_HOOK(hkslSetTagForFrame, decltype(&slSetTagForFrame))
    VALIDATE_MEMBER_HOOK(hkslEvaluateFeature, decltype(&slEvaluateFeature))
    VALIDATE_MEMBER_HOOK(hkslAllocateResources, decltype(&slAllocateResources))
    VALIDATE_MEMBER_HOOK(hkslGetNativeInterface, decltype(&slGetNativeInterface))
    VALIDATE_MEMBER_HOOK(hkslSetD3DDevice, decltype(&slSetD3DDevice))
    VALIDATE_MEMBER_HOOK(hkdlss_slOnPluginLoad, PFN_slOnPluginLoad)
    VALIDATE_MEMBER_HOOK(hkslDLSSGetOptimalSettings, decltype(&slDLSSGetOptimalSettings))
    VALIDATE_MEMBER_HOOK(hkdlss_slGetPluginFunction, PFN_slGetPluginFunction)
    VALIDATE_MEMBER_HOOK(hkdlssg_slOnPluginLoad, PFN_slOnPluginLoad)
    VALIDATE_MEMBER_HOOK(hkslSetConstants, decltype(&slSetConstants))
    VALIDATE_MEMBER_HOOK(hkslDLSSGSetOptions, decltype(&slDLSSGSetOptions))
    VALIDATE_MEMBER_HOOK(hkslDLSSGGetState, decltype(&slDLSSGGetState))
    VALIDATE_MEMBER_HOOK(hkdlssg_slGetPluginFunction, PFN_slGetPluginFunction)
    VALIDATE_MEMBER_HOOK(hkreflex_slOnPluginLoad, PFN_slOnPluginLoad)
    VALIDATE_MEMBER_HOOK(hkslReflexSetOptions, decltype(&slReflexSetOptions))
    VALIDATE_MEMBER_HOOK(hkreflex_slSetConstants_sl1, PFN_slSetConstants_sl1)
    VALIDATE_MEMBER_HOOK(hkreflex_slGetPluginFunction, PFN_slGetPluginFunction)
    VALIDATE_MEMBER_HOOK(hkpcl_slOnPluginLoad, PFN_slOnPluginLoad)
    VALIDATE_MEMBER_HOOK(hkpcl_slGetPluginFunction, PFN_slGetPluginFunction)
    VALIDATE_MEMBER_HOOK(hkslPCLSetMarker, decltype(&slPCLSetMarker))
    VALIDATE_MEMBER_HOOK(hkcommon_slOnPluginLoad, PFN_slOnPluginLoad)
    VALIDATE_MEMBER_HOOK(hkcommon_slGetPluginFunction, PFN_slGetPluginFunction)
    VALIDATE_MEMBER_HOOK(hkcommon_slSetParameters_sl1, PFN_slSetParameters_sl1)
    VALIDATE_MEMBER_HOOK(hk_setVoid, PFN_setVoid)
};
