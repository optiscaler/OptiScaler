// Installed-runtime regression for Streamline's active-plugin binding contract.
// Build: cl /std:c++20 /EHsc /I external/streamline tests/streamline_active_plugin_smoke.cpp /link d3d12.lib dxgi.lib
// Run: streamline_active_plugin_smoke.exe <directory containing sl.interposer.dll> <game bin directory>
// Runs without a game. It checks startup/plugin identity, not presentation or frame generation.
#include <windows.h>
#include <d3d12.h>
#include <sl.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>
#include <sl_pcl.h>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

static void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

template <typename T> static T Export(HMODULE module, const char* name)
{
    auto result = reinterpret_cast<T>(GetProcAddress(module, name));
    Require(result != nullptr, name);
    return result;
}

static void PrintModule(const char* label, void* function)
{
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(function), &module);
    wchar_t path[MAX_PATH] {};
    GetModuleFileNameW(module, path, MAX_PATH);
    std::printf("%s: %ls\n", label, path);
}

// Contain the old-path access violation to demonstrate the regression without launching a game.
static unsigned CallReflex(decltype(&slReflexSetOptions) function, const sl::ReflexOptions& options)
{
    __try { return static_cast<unsigned>(function(options)); }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    { return EXCEPTION_ACCESS_VIOLATION; }
}

int wmain(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try
    {
        Require(argc == 3, "Expected Streamline and game binary directories");
        const std::filesystem::path directory(argv[1]);
        const auto interposer = LoadLibraryW((directory / L"sl.interposer.dll").c_str());
        Require(interposer != nullptr, "Load Streamline interposer");
        const auto init = Export<decltype(&slInit)>(interposer, "slInit");
        const auto setDevice = Export<decltype(&slSetD3DDevice)>(interposer, "slSetD3DDevice");
        const auto getFeature = Export<decltype(&slGetFeatureFunction)>(interposer, "slGetFeatureFunction");
        const auto shutdown = Export<decltype(&slShutdown)>(interposer, "slShutdown");
        const sl::Feature features[] = { sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL };
        const wchar_t* paths[] = { argv[1], argv[2] };
        sl::Preferences pref;
        pref.applicationId = 0x0F71CA1E;
        pref.featuresToLoad = features;
        pref.numFeaturesToLoad = 3;
        pref.pathsToPlugins = paths;
        pref.numPathsToPlugins = 2;
        pref.renderAPI = sl::RenderAPI::eD3D12;
        pref.flags |= sl::PreferenceFlags::eUseManualHooking;
        pref.flags |= sl::PreferenceFlags::eUseFrameBasedResourceTagging;
        pref.flags |= sl::PreferenceFlags::eUseDXGIFactoryProxy;
        pref.flags &= ~sl::PreferenceFlags::eAllowOTA;
        pref.flags &= ~sl::PreferenceFlags::eLoadDownloadedPlugins;
        Require(init(pref, sl::kSDKVersion) == sl::Result::eOk, "slInit");

        // Mirror the previous binding order: the bundled plugin is loaded before device selection.
        const auto bundled = LoadLibraryW((directory / L"sl.reflex.dll").c_str());
        const auto getPlugin = Export<void* (*)(const char*)>(bundled, "slGetPluginFunction");
        const auto stale = reinterpret_cast<decltype(&slReflexSetOptions)>(getPlugin("slReflexSetOptions"));
        Require(stale != nullptr, "Bundled Reflex export");
        ID3D12Device* device = nullptr;
        Require(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device))), "D3D12 device");
        Require(setDevice(device) == sl::Result::eOk, "slSetD3DDevice");
        void* function = nullptr;
        Require(getFeature(sl::kFeatureReflex, "slReflexSetOptions", function) == sl::Result::eOk && function,
                "Active Reflex function");
        PrintModule("Bundled Reflex", reinterpret_cast<void*>(stale));
        PrintModule("Active Reflex", function);
        sl::ReflexOptions options;
        options.mode = sl::ReflexMode::eOff;
        options.useMarkersToOptimize = false;
        const auto activeReflex = reinterpret_cast<decltype(&slReflexSetOptions)>(function);
        const auto activeResult = CallReflex(activeReflex, options);
        Require(activeResult == static_cast<unsigned>(sl::Result::eOk), "Active Reflex startup call");
        std::puts("PASS: active Reflex startup call");
        for (const auto& binding : { std::pair{sl::kFeatureDLSS_G, "slDLSSGSetOptions"},
                                    {sl::kFeatureDLSS_G, "slDLSSGGetState"},
                                    {sl::kFeatureReflex, "slReflexGetState"},
                                    {sl::kFeatureReflex, "slReflexSleep"},
                                    {sl::kFeaturePCL, "slPCLSetMarker"},
                                    {sl::kFeaturePCL, "slPCLSetOptions"} })
        {
            function = nullptr;
            Require(getFeature(binding.first, binding.second, function) == sl::Result::eOk && function, binding.second);
        }
        std::puts("PASS: all required DLSSG/Reflex/PCL functions resolved from active plugins");
        if (stale != activeReflex)
        {
            const auto staleResult = CallReflex(stale, options);
            std::printf("Old bundled call result: 0x%08x\n", staleResult);
            Require(staleResult == EXCEPTION_ACCESS_VIOLATION, "Expected old-path access violation");
            std::puts("PASS: reproduced original access violation; active-plugin binding avoids it");
        }
        else
            std::puts("No plugin replacement on this runtime: active and bundled functions match");
        shutdown();
        device->Release();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
