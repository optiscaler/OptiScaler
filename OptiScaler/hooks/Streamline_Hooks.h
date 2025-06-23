#pragma once

#include "pch.h"

#include <d3d12.h>
#include <sl.h>
#include <sl1.h>
#include <sl_dlss_g.h>
#include <sl_pcl.h>
#include <sl_reflex.h>

class StreamlineHooks
{
  public:
    typedef void* (*PFN_slGetPluginFunction)(const char* functionName);
    typedef bool (*PFN_slOnPluginLoad)(void* params, const char* loaderJSON, const char** pluginJSON);
    typedef sl::Result (*PFN_slSetData)(const sl::BaseStructure* inputs, sl::CommandBuffer* cmdBuffer);
    typedef bool (*PFN_slSetConstants_sl1)(const void* data, uint32_t frameIndex, uint32_t id);
    typedef void (*PFN_slSetParameters_sl1)(void* params);
    typedef bool (*PFN_setVoid)(void* self, const char* key, void** value);

    static void updateForceReflex();

    static void unhookInterposer();
    static void hookInterposer(HMODULE slInterposer);

    static void unhookDlss();
    static void hookDlss(HMODULE slDlss);

    static void unhookDlssg();
    static void hookDlssg(HMODULE slDlssg);

    static void unhookReflex();
    static void hookReflex(HMODULE slReflex);

    static void unhookCommon();
    static void hookCommon(HMODULE slCommon);

  private:
    // Interposer
    static decltype(&slInit) o_slInit;
    static decltype(&slSetTag) o_slSetTag;
    static decltype(&sl1::slInit) o_slInit_sl1;

    static sl::PFun_LogMessageCallback* o_logCallback;
    static sl1::pfunLogMessageCallback* o_logCallback_sl1;

    static sl::Result hkslInit(sl::Preferences* pref, uint64_t sdkVersion);
    static bool hkslInit_sl1(sl1::Preferences* pref, int applicationId);
    static sl::Result hkslSetTag(sl::ViewportHandle& viewport, sl::ResourceTag* tags, uint32_t numTags,
                                 sl::CommandBuffer* cmdBuffer);

    // DLSS
    static PFN_slGetPluginFunction o_dlss_slGetPluginFunction;
    static PFN_slOnPluginLoad o_dlss_slOnPluginLoad;

    static bool hkdlss_slOnPluginLoad(void* params, const char* loaderJSON, const char** pluginJSON);
    static void* hkdlss_slGetPluginFunction(const char* functionName);

    // DLSSG
    static PFN_slGetPluginFunction o_dlssg_slGetPluginFunction;
    static PFN_slOnPluginLoad o_dlssg_slOnPluginLoad;
    static decltype(&slDLSSGSetOptions) o_slDLSSGSetOptions;

    static bool hkdlssg_slOnPluginLoad(void* params, const char* loaderJSON, const char** pluginJSON);
    static sl::Result hkslDLSSGSetOptions(const sl::ViewportHandle& viewport, const sl::DLSSGOptions& options);
    static void* hkdlssg_slGetPluginFunction(const char* functionName);

    // Reflex
    static sl::ReflexMode reflexGamesLastMode;
    static PFN_slGetPluginFunction o_reflex_slGetPluginFunction;
    static PFN_slSetConstants_sl1 o_reflex_slSetConstants_sl1;
    static PFN_slOnPluginLoad o_reflex_slOnPluginLoad;
    static decltype(&slReflexSetOptions) o_slReflexSetOptions;

    static bool hkreflex_slOnPluginLoad(void* params, const char* loaderJSON, const char** pluginJSON);
    static sl::Result hkslReflexSetOptions(const sl::ReflexOptions& options);
    static bool hkreflex_slSetConstants_sl1(const void* data, uint32_t frameIndex, uint32_t id);
    static void* hkreflex_slGetPluginFunction(const char* functionName);

    // Common
    static PFN_slGetPluginFunction o_common_slGetPluginFunction;
    static PFN_slOnPluginLoad o_common_slOnPluginLoad;
    static PFN_slSetParameters_sl1 o_common_slSetParameters_sl1;
    static PFN_setVoid o_setVoid;

    static bool hkcommon_slOnPluginLoad(void* params, const char* loaderJSON, const char** pluginJSON);
    static void* hkcommon_slGetPluginFunction(const char* functionName);
    static void hkcommon_slSetParameters_sl1(void* params);
    static bool hk_setVoid(void* self, const char* key, void** value);

    // Logging
    static char* trimStreamlineLog(const char* msg);
    static void streamlineLogCallback(sl::LogType type, const char* msg);
    static void streamlineLogCallback_sl1(sl1::LogType type, const char* msg);
};
