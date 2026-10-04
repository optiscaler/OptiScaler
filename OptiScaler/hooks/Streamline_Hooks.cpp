#include <pch.h>

#include "Streamline_Hooks.h"

#include <Util.h>
#include <Config.h>

#include <nvapi/fakenvapi.h>
#include <misc/IdentifyGpu.h>
#include <hooks/Reflex_Hooks.h>
#include <menu/menu_overlay_base.h>
#include <framegen/nvngx/Nvngx_FG.h>
#include <proxies/KernelBase_Proxy.h>
#include <imgui/ImGuiNotify.hpp>

#include <json.hpp>
#include <sl1_reflex.h>
#include <magic_enum.hpp>
#include "detours/detours.h"

#include <intrin.h>
#pragma intrinsic(_ReturnAddress)

static std::wstring GetModulePath(HMODULE module)
{
    if (module == nullptr)
        return {};

    wchar_t buffer[MAX_PATH * 2] {};
    const auto len = GetModuleFileNameW(module, buffer, static_cast<DWORD>(std::size(buffer)));
    return std::wstring(buffer, len);
}

// Plugin may fail (denylist etc.) and leave pluginJSON empty
static bool HasPluginJson(bool result, const char** pluginJSON)
{
    return result && pluginJSON != nullptr && *pluginJSON != nullptr && **pluginJSON != '\0';
}

// SystemCaps lives inside sl.common's image. When Streamline drops one sl.common copy and keeps another,
// a pointer we got earlier can point into an unmapped (or reused) range, so check before every access.
static bool IsWritableMemory(const void* address, size_t size)
{
    if (address == nullptr)
        return false;

    MEMORY_BASIC_INFORMATION mbi {};
    if (VirtualQuery(address, &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;

    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;

    constexpr DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if ((mbi.Protect & writable) == 0)
        return false;

    const auto end = reinterpret_cast<uintptr_t>(address) + size;
    const auto regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    return end <= regionEnd;
}

static bool IsSL1AndDLSSGActive()
{
    return State::Instance().streamlineVersion.major == 1 && State::Instance().activeFgInput == FGInput::DLSSG &&
           (State::Instance().activeFgOutput == FGOutput::FSRFG || State::Instance().activeFgOutput == FGOutput::XeFG);
}

static bool IsSL1AndFGActive()
{
    const auto& state = State::Instance();

    return state.streamlineVersion.major == 1 && state.activeFgInput == FGInput::DLSSG;
}

static void PatchSL1PluginJson(nlohmann::json& configJson)
{
    if (!IsSL1AndFGActive())
        return;

    LOG_DEBUG("Patching SL1 plugin JSON for external FG management");

    if (configJson.contains("/hooks"_json_pointer))
        configJson["hooks"].clear();

    if (configJson.contains("/exclusive_hooks"_json_pointer))
        configJson["exclusive_hooks"].clear();

    if (configJson.contains("/external/feature/tags"_json_pointer))
        configJson["external"]["feature"]["tags"].clear();

    if (configJson.contains("/vsync/supported"_json_pointer))
        configJson["vsync"]["supported"] = true;

    if (configJson.contains("/external/hws/required"_json_pointer))
        configJson["external"]["hws"]["required"] = false;
}

char* StreamlineHooks::trimStreamlineLog(const char* msg)
{
    char* result = (char*) malloc(strlen(msg) + 1);
    if (!result)
        return nullptr;

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
    if (msg == nullptr)
        return;

    char* trimmed_msg = trimStreamlineLog(msg);
    if (trimmed_msg != nullptr)
    {
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
    }

    if (o_logCallback != nullptr)
        o_logCallback(type, msg);
}

sl::Result StreamlineHooks::hkslInit(const sl::Preferences& pref, uint64_t sdkVersion)
{
    LOG_FUNC();

    sl::Preferences localPref = pref;

    if (localPref.logMessageCallback != &streamlineLogCallback)
        o_logCallback = localPref.logMessageCallback;
    localPref.logLevel = sl::LogLevel::eCount;
    localPref.logMessageCallback = &streamlineLogCallback;

    // renderAPI is optional so need to be careful, should only matter for Vulkan
    renderApi = localPref.renderAPI;

    State::Instance().slFGInputs.reportEngineType(localPref.engine);

    // Treat engine type set in Streamline as ground truth
    if (localPref.engine == sl::EngineType::eUnreal)
        State::Instance().gameQuirks |= GameQuirk::ForceUnrealEngine;

    std::filesystem::path localSlPath(Config::Instance()->MainDllPath.value());
    localSlPath = localSlPath / L"streamline"; // Hardcoded streamline folder

    auto localSlPathStr = localSlPath.wstring();

    std::vector<const wchar_t*> storage;

    // Replace the SL files to allow for MFG
    if (State::Instance().activeFgInput == FGInput::NvngxFG && std::filesystem::exists(localSlPath / L"sl.common.dll"))
    {
        storage.assign(localPref.pathsToPlugins, localPref.pathsToPlugins + localPref.numPathsToPlugins);

        std::filesystem::path pluginsDir;

        // Find the first path that contains sl.common.dll
        // If storage is empty, look in the exe folder. pathsToPlugins is an optional field
        if (storage.empty())
        {
            std::filesystem::path exeFolder = Util::ExePath().parent_path();
            if (std::filesystem::exists(exeFolder / L"sl.common.dll"))
            {
                pluginsDir = exeFolder;
            }
        }
        else
        {
            for (const wchar_t* pathStr : storage)
            {
                if (!pathStr)
                    continue;

                std::filesystem::path p = pathStr;
                if (std::filesystem::exists(p / L"sl.common.dll"))
                {
                    pluginsDir = p;
                    break;
                }
            }
        }

        std::vector<std::string> missingDlls;
        bool hasNewerPlugin = false;

        // If we found the plugins folder, scan its contents
        if (!pluginsDir.empty() && std::filesystem::exists(pluginsDir))
        {
            for (const auto& entry : std::filesystem::directory_iterator(pluginsDir))
            {
                if (!entry.is_regular_file())
                    continue;

                std::wstring filename = entry.path().filename().wstring();

                std::wstring lowerName = filename;
                to_lower_in_place(lowerName);

                // Skip interposer
                if (lowerName == L"sl.interposer.dll")
                    continue;

                const bool isSlDll = lowerName.starts_with(L"sl.") && lowerName.ends_with(L".dll");
                const bool isNvLowLatency = lowerName == L"nvlowlatencyvk.dll";

                if (isSlDll || isNvLowLatency)
                {
                    std::filesystem::path localDllPath = localSlPath / filename;

                    // Check if localSlPath also has this DLL
                    if (!std::filesystem::exists(localDllPath))
                    {
                        missingDlls.push_back(entry.path().filename().string());
                    }
                    else
                    {
                        // Compare versions
                        version_t pluginVer, pluginProdVer;
                        version_t localVer, localProdVer;

                        bool gotPluginVer = Util::GetFileVersion(entry.path().wstring(), &pluginVer, &pluginProdVer);
                        bool gotLocalVer = Util::GetFileVersion(localDllPath.wstring(), &localVer, &localProdVer);

                        if (gotPluginVer && gotLocalVer)
                        {
                            if (localVer > pluginVer)
                            {
                                hasNewerPlugin = true;
                            }
                        }
                    }
                }
            }
        }

        // Insert local path only if a newer plugin was found
        if (hasNewerPlugin)
        {
            LOG_DEBUG("Making the game use local streamline files");

            storage.insert(storage.begin(), localSlPathStr.c_str());
            localPref.pathsToPlugins = storage.data();
            localPref.numPathsToPlugins = (uint32_t) storage.size();

            if (!missingDlls.empty())
            {
                std::string toastMsg = "You are missing the following dlls from the streamline folder:\n";
                for (const auto& missingDll : missingDlls)
                {
                    toastMsg += "- " + missingDll + "\n";
                }

                ImGui::InsertNotification({ ImGuiToastType::Warning, 20000, toastMsg.c_str() });
            }
        }
    }

    if (State::Instance().activeFgInput == FGInput::DLSSG || State::Instance().activeFgOutput == FGOutput::DLSSG)
    {
        std::vector<sl::Feature> localFeaturesToLoad(pref.featuresToLoad, pref.featuresToLoad + pref.numFeaturesToLoad);
        std::erase(localFeaturesToLoad, sl::kFeatureDLSS_G);

        localPref.featuresToLoad = localFeaturesToLoad.data();
        localPref.numFeaturesToLoad = (uint32_t) localFeaturesToLoad.size();

        // return so that localFeaturesToLoad is valid
        return o_slInit(localPref, sdkVersion);
    }

    // bool hookSetTag =
    //     (State::Instance().activeFgInput == FGInput::NvngxFG || State::Instance().activeFgInput == FGInput::DLSSG);

    // if (hookSetTag)
    //     localPref->flags &= ~(sl::PreferenceFlags::eAllowOTA | sl::PreferenceFlags::eLoadDownloadedPlugins);

    // To prevent mixed up OTA situations
    if (Config::Instance()->DisableOTA.value_or_default())
    {
        localPref.flags &= ~sl::PreferenceFlags::eAllowOTA;
        localPref.flags &= ~sl::PreferenceFlags::eLoadDownloadedPlugins;
    }

    return o_slInit(localPref, sdkVersion);
}

sl::Result StreamlineHooks::hkslIsFeatureSupported(sl::Feature feature, const sl::AdapterInfo& adapterInfo)
{
    if (feature == sl::kFeatureDLSS_G)
        return sl::Result::eOk;

    return o_slIsFeatureSupported(feature, adapterInfo);
}

sl::Result StreamlineHooks::hkslIsFeatureLoaded(sl::Feature feature, bool& loaded)
{
    if (feature == sl::kFeatureDLSS_G)
    {
        loaded = true;
        return sl::Result::eOk;
    }

    return o_slIsFeatureLoaded(feature, loaded);
}

sl::Result StreamlineHooks::hkslGetFeatureRequirements(sl::Feature feature, sl::FeatureRequirements& requirements)
{
    if (feature == sl::kFeatureDLSS_G)
        return sl::Result::eOk;

    return o_slGetFeatureRequirements(feature, requirements);
}

sl::Result StreamlineHooks::hkslGetFeatureVersion(sl::Feature feature, sl::FeatureVersion& version)
{
    if (feature == sl::kFeatureDLSS_G)
    {
        version.versionSL = { State::Instance().streamlineVersion.major, State::Instance().streamlineVersion.minor,
                              State::Instance().streamlineVersion.patch };
        version.versionNGX = { 4, 2, 0 };

        return sl::Result::eOk;
    }

    return o_slGetFeatureVersion(feature, version);
}

static sl::Result dummy_slDLSSGGetState(const sl::ViewportHandle& viewport, sl::DLSSGState& state,
                                        const sl::DLSSGOptions* options)
{
    state.numFramesActuallyPresented = 1; // TODO: can do better
    state.numFramesToGenerateMax = 1;
    state.bIsVsyncSupportAvailable = sl::Boolean::eTrue;
    state.estimatedVRAMUsageInBytes = 300 * 1024 * 1024;

    return sl::Result::eOk;
}

static sl::Result dummy_slDLSSGSetOptions(const sl::ViewportHandle& viewport, const sl::DLSSGOptions& options)
{
    return sl::Result::eOk;
}

sl::Result StreamlineHooks::hkslGetFeatureFunction(sl::Feature feature, const char* functionName, void*& function)
{
    if (feature == sl::kFeatureDLSS_G)
    {
        if (strcmp(functionName, "slDLSSGSetOptions") == 0)
        {
            function = &dummy_slDLSSGSetOptions;

            return sl::Result::eOk;
        }

        if (strcmp(functionName, "slDLSSGGetState") == 0)
        {
            function = &dummy_slDLSSGGetState;

            return sl::Result::eOk;
        }
    }

    return o_slGetFeatureFunction(feature, functionName, function);
}

sl::Result StreamlineHooks::hkslSetFeatureLoaded(sl::Feature feature, bool loaded)
{
    if (feature == sl::kFeatureDLSS_G)
    {
        return sl::Result::eOk;
    }

    return o_slSetFeatureLoaded(feature, loaded);
}

sl::Result StreamlineHooks::hkslSetTag(const sl::ViewportHandle& viewport, const sl::ResourceTag* tags,
                                       uint32_t numTags, sl::CommandBuffer* cmdBuffer)
{
    if (renderApi == sl::RenderAPI::eD3D11 || renderApi == sl::RenderAPI::eVulkan)
    {
        LOG_ERROR("hkslSetTag only supports DX12");
        return o_slSetTag(viewport, tags, numTags, cmdBuffer);
    }

    if (renderApi == sl::RenderAPI::eCount)
        LOG_WARN("Incomplete Streamline hooks");

    if (tags == nullptr)
    {
        LOG_WARN("Game trying to remove a tag");
        return o_slSetTag(viewport, tags, numTags, cmdBuffer);
    }

    if (State::Instance().activeFgInput == FGInput::DLSSG &&
        State::Instance().gameQuirks[GameQuirk::IgnoreTagsWithoutHudlessForFG])
    {
        bool hasDepth = false;
        bool hasMVs = false;
        bool hasHudless = false;

        for (uint32_t i = 0; i < numTags; i++)
        {
            if (tags[i].resource == nullptr || tags[i].resource->native == nullptr)
                continue;

            if (tags[i].type == sl::kBufferTypeDepth)
                hasDepth = true;

            if (tags[i].type == sl::kBufferTypeMotionVectors)
                hasMVs = true;

            if (tags[i].type == sl::kBufferTypeHUDLessColor)
                hasHudless = true;
        }

        // Try to skip a DLSS call
        if (hasDepth && hasMVs && !hasHudless)
        {
            LOG_DEBUG("Skipping the FG tagging of potential DLSS resources");
            return o_slSetTag(viewport, tags, numTags, cmdBuffer);
        }
    }

    for (uint32_t i = 0; i < numTags; i++)
    {
        const auto typeEnum = (BufferType) tags[i].type;

        if (tags[i].resource == nullptr || tags[i].resource->native == nullptr)
        {
            LOG_TRACE("Resource of type: {} is null, continuing", magic_enum::enum_name(typeEnum));
            continue;
        }

        // Cyberpunk hudless state fix for RDNA 2
        if (State::Instance().gameQuirks & GameQuirk::CyberpunkHudlessState &&
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
             tags[i].type == sl::kBufferTypeMotionVectors || tags[i].type == sl::kBufferTypeUIColorAndAlpha ||
             tags[i].type == sl::kBufferTypeBidirectionalDistortionField))
        {
            State::Instance().slFGInputs.reportResource(tags[i], (ID3D12GraphicsCommandList*) cmdBuffer, 0);
        }
        else if (State::Instance().activeFgInput == FGInput::NvngxFG)
        {
            LOG_TRACE("Tagging resource of type: {}", magic_enum::enum_name(typeEnum));
        }
    }

    auto result = o_slSetTag(viewport, tags, numTags, cmdBuffer);
    return result;
}

sl::Result StreamlineHooks::hkslSetTagForFrame(const sl::FrameToken& frame, const sl::ViewportHandle& viewport,
                                               const sl::ResourceTag* resources, uint32_t numResources,
                                               sl::CommandBuffer* cmdBuffer)
{
    if (renderApi == sl::RenderAPI::eD3D11 || renderApi == sl::RenderAPI::eVulkan)
    {
        LOG_ERROR("hkslSetTagForFrame only supports DX12");
        return o_slSetTagForFrame(frame, viewport, resources, numResources, cmdBuffer);
    }

    if (renderApi == sl::RenderAPI::eCount)
        LOG_WARN("Incomplete Streamline hooks");

    if (resources == nullptr)
    {
        LOG_WARN("Game trying to remove a tag");
        return o_slSetTagForFrame(frame, viewport, resources, numResources, cmdBuffer);
    }

    LOG_DEBUG("frameIndex: {}", static_cast<uint32_t>(frame));

    if (State::Instance().activeFgInput == FGInput::DLSSG &&
        State::Instance().gameQuirks[GameQuirk::IgnoreTagsWithoutHudlessForFG])
    {
        bool hasDepth = false;
        bool hasMVs = false;
        bool hasHudless = false;

        for (uint32_t i = 0; i < numResources; i++)
        {
            if (resources[i].resource == nullptr || resources[i].resource->native == nullptr)
                continue;

            if (resources[i].type == sl::kBufferTypeDepth)
                hasDepth = true;

            if (resources[i].type == sl::kBufferTypeMotionVectors)
                hasMVs = true;

            if (resources[i].type == sl::kBufferTypeHUDLessColor)
                hasHudless = true;
        }

        // Try to skip a DLSS call
        if (hasDepth && hasMVs && !hasHudless)
        {
            LOG_DEBUG("Skipping the FG tagging of potential DLSS resources");
            return o_slSetTagForFrame(frame, viewport, resources, numResources, cmdBuffer);
        }
    }

    for (uint32_t i = 0; i < numResources; i++)
    {
        const auto typeEnum = (BufferType) resources[i].type;

        if (resources[i].resource == nullptr || resources[i].resource->native == nullptr)
        {
            LOG_TRACE("Resource of type: {} is null, continuing", magic_enum::enum_name(typeEnum));
            continue;
        }

        if (State::Instance().activeFgInput == FGInput::DLSSG &&
            (resources[i].type == sl::kBufferTypeHUDLessColor || resources[i].type == sl::kBufferTypeDepth ||
             resources[i].type == sl::kBufferTypeHiResDepth || resources[i].type == sl::kBufferTypeLinearDepth ||
             resources[i].type == sl::kBufferTypeMotionVectors || resources[i].type == sl::kBufferTypeUIColorAndAlpha ||
             resources[i].type == sl::kBufferTypeBidirectionalDistortionField))
        {
            State::Instance().slFGInputs.reportResource(resources[i], (ID3D12GraphicsCommandList*) cmdBuffer,
                                                        (uint32_t) frame);
        }
        else if (State::Instance().activeFgInput == FGInput::NvngxFG)
        {
            LOG_TRACE("Tagging resource of type: {}", magic_enum::enum_name(typeEnum));
        }
    }

    auto result = o_slSetTagForFrame(frame, viewport, resources, numResources, cmdBuffer);
    return result;
}

sl::Result StreamlineHooks::hkslEvaluateFeature(sl::Feature feature, const sl::FrameToken& frame,
                                                const sl::BaseStructure** inputs, uint32_t numInputs,
                                                sl::CommandBuffer* cmdBuffer)
{
    LOG_DEBUG("frameIndex: {}", static_cast<uint32_t>(frame));

    if (State::Instance().activeFgInput == FGInput::DLSSG && numInputs > 0 && inputs != nullptr)
    {
        for (uint32_t i = 0; i < numInputs; i++)
        {
            if (inputs[i] == nullptr)
                continue;

            if (inputs[i]->structType == sl::ResourceTag::s_structType)
            {
                auto tag = (const sl::ResourceTag*) inputs[i];

                if (tag->type == sl::kBufferTypeHUDLessColor || tag->type == sl::kBufferTypeDepth ||
                    tag->type == sl::kBufferTypeHiResDepth || tag->type == sl::kBufferTypeLinearDepth ||
                    tag->type == sl::kBufferTypeMotionVectors || tag->type == sl::kBufferTypeUIColorAndAlpha ||
                    tag->type == sl::kBufferTypeBidirectionalDistortionField)
                {
                    State::Instance().slFGInputs.reportResource(*tag, (ID3D12GraphicsCommandList*) cmdBuffer,
                                                                (uint32_t) frame);
                }
            }
        }
    }

    auto result = o_slEvaluateFeature(feature, frame, inputs, numInputs, cmdBuffer);
    return result;
}

sl::Result StreamlineHooks::hkslAllocateResources(sl::CommandBuffer* cmdBuffer, sl::Feature feature,
                                                  const sl::ViewportHandle& viewport)
{
    if (feature == sl::kFeatureDLSS_G)
        return sl::Result::eOk;

    return o_slAllocateResources(cmdBuffer, feature, viewport);
}

sl::Result StreamlineHooks::hkslFreeResources(sl::Feature feature, const sl::ViewportHandle& viewport)
{
    if (feature == sl::kFeatureDLSS_G)
        return sl::Result::eOk;

    return o_slFreeResources(feature, viewport);
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
    if (msg == nullptr)
        return;

    char* trimmed_msg = trimStreamlineLog(msg);

    if (trimmed_msg != nullptr)
    {
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
    }

    if (o_logCallback_sl1)
        o_logCallback_sl1(type, msg);
}

bool StreamlineHooks::hkslInit_sl1(const sl1::Preferences& pref, int applicationId)
{
    LOG_FUNC();

    sl1::Preferences localPref = pref;

    if (localPref.logMessageCallback != &streamlineLogCallback_sl1)
        o_logCallback_sl1 = localPref.logMessageCallback;
    localPref.logLevel = sl1::LogLevel::eLogLevelCount;
    localPref.logMessageCallback = &streamlineLogCallback_sl1;
    return o_slInit_sl1(localPref, applicationId);
}

bool StreamlineHooks::hkslSetTag_sl1(const sl1::Resource* resource, sl1::BufferType tag, uint32_t id,
                                     const sl1::Extent* extent)
{
    if (IsSL1AndFGActive())
        State::Instance().s_sl1FGInputs.setTag(resource, tag, id, extent);

    return o_slSetTag_sl1(resource, tag, id, extent);
}

bool StreamlineHooks::hkslSetConstants_sl1(const sl1::Constants& values, uint32_t frameIndex, uint32_t id)
{
    std::scoped_lock lock(setConstantsMutex);

    LOG_TRACE("SL1 slSetConstants frameIndex: {}, id: {}", frameIndex, id);

    if (IsSL1AndFGActive())
        State::Instance().s_sl1FGInputs.setConstants(values, frameIndex, id);

    return o_slSetConstants_interposer_sl1(values, frameIndex, id);
}

bool StreamlineHooks::hkslEvaluateFeature_sl1(sl1::CommandBuffer* cmdBuffer, sl1::Feature feature, uint32_t frameIndex,
                                              uint32_t id)
{
    LOG_TRACE("SL1 slEvaluateFeature feature: {}, frameIndex: {}, id: {}", magic_enum::enum_name(feature), frameIndex,
              id);

    if (IsSL1AndFGActive() && feature == sl1::Feature::eFeatureReflex)
    {
        const auto marker = (sl1::ReflexMarker) id;

        if (marker == sl1::ReflexMarker::eReflexMarkerRenderSubmitStart)
        {
            State::Instance().s_sl1FGInputs.evaluateState();
            State::Instance().s_sl1FGInputs.evaluateFeature(cmdBuffer, feature, frameIndex, id);
        }
        else if (marker == sl1::ReflexMarker::eReflexMarkerPresentStart)
        {
            State::Instance().s_sl1FGInputs.markPresent(frameIndex);
        }
    }

    return o_slEvaluateFeature_sl1(cmdBuffer, feature, frameIndex, id);
}

// Always re-read from params: the caps struct belongs to the sl.common copy Streamline currently uses,
// which can change (game plugins vs OTA plugins), so a cached pointer can go stale.
void StreamlineHooks::hookSystemCaps(sl::param::IParameters* params)
{
    if (params == nullptr)
        return;

    if (State::Instance().streamlineVersion.major > 1)
    {
        SystemCaps* caps = nullptr;
        sl::param::getPointerParam(params, sl::param::common::kSystemCaps, &caps);

        if (caps != nullptr && !IsWritableMemory(caps, sizeof(SystemCaps)))
        {
            LOG_WARN("SystemCaps pointer {:X} is not valid memory, skipping arch spoofing", (size_t) caps);
            caps = nullptr;
        }

        systemCaps = caps;
    }
    else if (State::Instance().streamlineVersion.major == 1)
    {
        // This should be Streamline 1.5 as previous versions don't even have slOnPluginLoad
        LOG_TRACE("Attempting to get system caps for Streamline v1, this could fail depending on the exact version");

        SystemCapsSl15* caps = nullptr;
        sl::param::getPointerParam(params, sl::param::common::kSystemCaps, &caps);

        if (caps != nullptr && !IsWritableMemory(caps, sizeof(SystemCapsSl15)))
        {
            LOG_WARN("SystemCaps (SL1) pointer {:X} is not valid memory, skipping arch spoofing", (size_t) caps);
            caps = nullptr;
        }

        systemCapsSl15 = caps;
    }
}

uint32_t StreamlineHooks::getSystemCapsArch(SystemCaps* altSystemCaps)
{
    uint32_t highestArch = 0;

    auto primaryGpu = IdentifyGpu::getPrimaryGpu();
    if (!fakenvapi::isUsingAsMainNvapi() && primaryGpu.vendorId == VendorId::Nvidia)
    {
        if (State::Instance().streamlineVersion.major > 1)
        {
            auto caps = altSystemCaps != nullptr ? altSystemCaps : systemCaps;
            if (caps && IsWritableMemory(caps, sizeof(SystemCaps)))
            {
                for (auto& adapter : caps->adapters)
                {
                    if (adapter.architecture > highestArch)
                        highestArch = adapter.architecture;
                }
            }
        }
        else if (State::Instance().streamlineVersion.major == 1)
        {
            if (systemCapsSl15 && IsWritableMemory(systemCapsSl15, sizeof(SystemCapsSl15)))
            {
                const auto count = (std::min) (systemCapsSl15->gpuCount, kMaxNumSupportedGPUs);
                for (uint32_t i = 0; i < count; i++)
                {
                    if (systemCapsSl15->architecture[i] > highestArch)
                        highestArch = systemCapsSl15->architecture[i];
                }
            }
        }
    }

    // By default spoof Pascal, gets Reflex but not DLSSD
    // Could be problematic if not using fakenvapi but nvapi might not be initialized yet
    if (highestArch == 0)
        highestArch = NV_GPU_ARCHITECTURE_GP100;

    return highestArch;
}

void StreamlineHooks::setArch(uint32_t arch, SystemCaps* altSystemCaps)
{
    auto primaryGpu = IdentifyGpu::getPrimaryGpu();

    // altSystemCaps has to be sl2+
    if (State::Instance().streamlineVersion.major > 1 || altSystemCaps)
    {
        // Assumes that altCaps are always for SL2+
        auto caps = altSystemCaps != nullptr ? altSystemCaps : systemCaps;
        if (caps && !IsWritableMemory(caps, sizeof(SystemCaps)))
        {
            LOG_WARN("SystemCaps at {:X} is no longer valid, not writing arch", (size_t) caps);

            if (caps == systemCaps)
                systemCaps = nullptr;

            caps = nullptr;
        }

        if (caps)
        {
            const auto count = (std::min) (caps->gpuCount, kMaxNumSupportedGPUs);
            for (uint32_t i = 0; i < count; i++)
            {
                caps->adapters[i].architecture = arch;
                caps->adapters[i].vendor = VendorId::Nvidia;
            }

            if (fakenvapi::isUsingAsMainNvapi() || primaryGpu.vendorId != VendorId::Nvidia)
                caps->driverVersionMajor = 999;

            caps->hwsSupported = true;
        }
    }
    else if (State::Instance().streamlineVersion.major == 1)
    {
        if (systemCapsSl15 && !IsWritableMemory(systemCapsSl15, sizeof(SystemCapsSl15)))
        {
            LOG_WARN("SystemCaps (SL1) at {:X} is no longer valid, not writing arch", (size_t) systemCapsSl15);
            systemCapsSl15 = nullptr;
        }

        if (systemCapsSl15)
        {
            const auto count = (std::min) (systemCapsSl15->gpuCount, kMaxNumSupportedGPUs);
            for (uint32_t i = 0; i < count; i++)
                systemCapsSl15->architecture[i] = arch;

            if (fakenvapi::isUsingAsMainNvapi() || primaryGpu.vendorId != VendorId::Nvidia)
                systemCapsSl15->driverVersionMajor = 999;

            systemCapsSl15->hwSchedulingEnabled = true;
        }
    }
}

// Spoof arch based on feature and current arch
void StreamlineHooks::spoofArch(uint32_t currentArch, sl::Feature feature, SystemCaps* altSystemCaps)
{
    constexpr uint32_t maxArch = 0xFFFFFFFF;

    // Don't change arch for DLSS/DLSSD with turing and above
    if (feature == sl::kFeatureDLSS)
    {
        if (currentArch < NV_GPU_ARCHITECTURE_TU100)
            return setArch(maxArch, altSystemCaps);
    }

    // Don't spoof DLSSD at all
    else if (feature == sl::kFeatureDLSS_RR)
    {
        return;
    }

    // Don't change arch for DLSSG with ada and above
    else if (feature == sl::kFeatureDLSS_G)
    {
        if (State::Instance().activeFgNvngx != FGNvngxReplacement::None)
        {
            if (!Nvngx_FG::isDx12Available() && !Nvngx_FG::isVulkanAvailable())
                return setArch(0, altSystemCaps);
        }

        if (currentArch < NV_GPU_ARCHITECTURE_AD100)
            return setArch(maxArch, altSystemCaps);
    }

    else if (feature == sl::kFeatureReflex || feature == sl::kFeaturePCL)
    {
        if (fakenvapi::isUsingAsMainNvapi())
            return setArch(maxArch, altSystemCaps);
    }
}

bool StreamlineHooks::hkdlss_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON,
                                            const char** pluginJSON)
{
    LOG_FUNC();

    // TODO: do it better than "static" and hoping for the best
    static std::string config;

    uint32_t currentArch = 0;
    if (Config::Instance()->StreamlineSpoofing.value_or_default())
    {
        hookSystemCaps(params);
        currentArch = getSystemCapsArch();
        spoofArch(currentArch, sl::kFeatureDLSS);
    }

    auto result = o_dlss_slOnPluginLoad != nullptr && o_dlss_slOnPluginLoad(params, loaderJSON, pluginJSON);

    if (Config::Instance()->StreamlineSpoofing.value_or_default())
        setArch(currentArch);

    if (!HasPluginJson(result, pluginJSON))
        return result;

    nlohmann::json configJson = nlohmann::json::parse(*pluginJSON, nullptr, false);
    if (configJson.is_discarded())
    {
        LOG_ERROR("Failed to parse plugin JSON");
        return result;
    }

    auto primaryGpu = IdentifyGpu::getPrimaryGpu();
    if (primaryGpu.vendorId != VendorId::Nvidia || !primaryGpu.dlssCapable)
    {
        if (configJson.contains("/external/vk/instance/extensions"_json_pointer))
            configJson["external"]["vk"]["instance"]["extensions"].clear();

        if (configJson.contains("/external/vk/device/extensions"_json_pointer))
            configJson["external"]["vk"]["device"]["extensions"].clear();

        if (configJson.contains("/external/vk/device/1.2_features"_json_pointer))
            configJson["external"]["vk"]["device"]["1.2_features"].clear();

        if (configJson.contains("/external/vk/device/1.3_features"_json_pointer))
            configJson["external"]["vk"]["device"]["1.3_features"].clear();
    }

    PatchSL1PluginJson(configJson);

    config = configJson.dump();

    *pluginJSON = config.c_str();

    return result;
}

sl::Result StreamlineHooks::hkslDLSSGetOptimalSettings(const sl::DLSSOptions& options,
                                                       sl::DLSSOptimalSettings& settings)
{
    if (o_slDLSSGetOptimalSettings == nullptr)
        return sl::Result::eErrorFeatureMissing;

    static bool modesBroken = false;

    auto localOptions = options;

    if (localOptions.mode == sl::DLSSMode::eOff)
        modesBroken = true;

    if (modesBroken)
    {
        if (localOptions.mode == sl::DLSSMode::eMaxPerformance)
            localOptions.mode = sl::DLSSMode::eUltraPerformance;
        else if (localOptions.mode == sl::DLSSMode::eBalanced)
            localOptions.mode = sl::DLSSMode::eMaxPerformance;
        else if (localOptions.mode == sl::DLSSMode::eMaxQuality)
            localOptions.mode = sl::DLSSMode::eBalanced;
        else if (localOptions.mode == sl::DLSSMode::eUltraQuality)
            localOptions.mode = sl::DLSSMode::eMaxQuality;
        else if (localOptions.mode == sl::DLSSMode::eUltraPerformance)
            localOptions.mode = sl::DLSSMode::eDLAA;
    }

    return o_slDLSSGetOptimalSettings(localOptions, settings);
}

bool StreamlineHooks::hkdlssg_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON,
                                             const char** pluginJSON)
{
    LOG_FUNC();

    // TODO: do it better than "static" and hoping for the best
    static std::string config;

    bool shouldSpoofArch =
        Config::Instance()->StreamlineSpoofing.value_or_default() &&
        (State::Instance().activeFgInput == FGInput::NvngxFG || State::Instance().activeFgInput == FGInput::DLSSG);

    uint32_t currentArch = 0;
    if (shouldSpoofArch)
    {
        hookSystemCaps(params);
        currentArch = getSystemCapsArch();
        spoofArch(currentArch, sl::kFeatureDLSS_G);
    }

    auto result = o_dlssg_slOnPluginLoad != nullptr && o_dlssg_slOnPluginLoad(params, loaderJSON, pluginJSON);

    if (shouldSpoofArch)
        setArch(currentArch);

    if (!HasPluginJson(result, pluginJSON))
        return result;

    nlohmann::json configJson = nlohmann::json::parse(*pluginJSON, nullptr, false);
    if (configJson.is_discarded())
    {
        LOG_ERROR("Failed to parse plugin JSON");
        return result;
    }

    // Kill the DLSSG streamline swapchain hooks
    if (State::Instance().activeFgInput == FGInput::DLSSG || State::Instance().activeFgOutput == FGOutput::DLSSG)
    {
        if (configJson.contains("/hooks"_json_pointer))
            configJson["hooks"].clear();

        if (configJson.contains("/exclusive_hooks"_json_pointer))
            configJson["exclusive_hooks"].clear();

        if (configJson.contains("/external/feature/tags"_json_pointer))
            configJson["external"]["feature"]["tags"].clear(); // We handle the DLSSG resources

        if (configJson.contains("/external/vk/device/queues/compute/count"_json_pointer))
            configJson["external"]["vk"]["device"]["queues"]["compute"]["count"] = 0;

        if (configJson.contains("/external/vk/device/queues/graphics/count"_json_pointer))
            configJson["external"]["vk"]["device"]["queues"]["graphics"]["count"] = 0;

        if (configJson.contains("/external/vk/device/1.2_features"_json_pointer))
            configJson["external"]["vk"]["device"]["1.2_features"].clear();

        if (configJson.contains("/external/vk/device/1.3_features"_json_pointer))
            configJson["external"]["vk"]["device"]["1.3_features"].clear();
    }

    if (State::Instance().activeFgInput == FGInput::DLSSG || State::Instance().activeFgInput == FGInput::NvngxFG)
    {
        if (configJson.contains("/vsync/supported"_json_pointer))
            configJson["vsync"]["supported"] = true; // disable eVSyncOffRequired

        if (configJson.contains("/external/hws/required"_json_pointer))
            configJson["external"]["hws"]["required"] = false; // disable eHardwareSchedulingRequired

        // if (configJson.contains("/external/vk/opticalflow/supported"_json_pointer))
        //     configJson["external"]["vk"]["opticalflow"]["supported"] = true;
    }

    auto primaryGpu = IdentifyGpu::getPrimaryGpu();
    if (primaryGpu.vendorId != VendorId::Nvidia || !primaryGpu.dlssCapable)
    {
        if (configJson.contains("/external/vk/instance/extensions"_json_pointer))
            configJson["external"]["vk"]["instance"]["extensions"].clear();

        if (configJson.contains("/external/vk/device/extensions"_json_pointer))
            configJson["external"]["vk"]["device"]["extensions"].clear();

        if (configJson.contains("/external/vk/device/1.2_features"_json_pointer))
            configJson["external"]["vk"]["device"]["1.2_features"].clear();

        if (configJson.contains("/external/vk/device/1.3_features"_json_pointer))
            configJson["external"]["vk"]["device"]["1.3_features"].clear();
    }

    PatchSL1PluginJson(configJson);

    config = configJson.dump();

    *pluginJSON = config.c_str();

    return result;
}

const char* StreamlineHooks::hkdlssg_slGetPluginJSONConfig_sl1()
{
    if (o_dlssg_slGetPluginJSONConfig_sl1 == nullptr)
        return nullptr;

    static std::string patchedConfig;

    const char* originalConfig = o_dlssg_slGetPluginJSONConfig_sl1();

    if (originalConfig == nullptr)
        return originalConfig;

    try
    {
        auto configJson = nlohmann::json::parse(originalConfig);

        LOG_DEBUG("SL1 DLSSG JSON before patch: {}", configJson.dump());

        PatchSL1PluginJson(configJson);
        // RemoveSL1DLSSGHookEntriesRecursive(configJson);

        patchedConfig = configJson.dump();

        LOG_DEBUG("SL1 DLSSG JSON after patch: {}", patchedConfig);

        return patchedConfig.c_str();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Failed to patch SL1 DLSSG JSON config: {}", e.what());
        return originalConfig;
    }
}

bool StreamlineHooks::hklocal_dlssg_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON,
                                                   const char** pluginJSON)
{
    LOG_FUNC();

    // TODO: do it better than "static" and hoping for the best
    static std::string config;

    bool shouldSpoofArch = Config::Instance()->StreamlineSpoofing.value_or_default();

    uint32_t currentArch = 0;
    SystemCaps* localSystemCaps = nullptr;
    if (shouldSpoofArch)
    {
        sl::param::getPointerParam(params, sl::param::common::kSystemCaps, &localSystemCaps);

        if (localSystemCaps && !IsWritableMemory(localSystemCaps, sizeof(SystemCaps)))
            localSystemCaps = nullptr;

        if (localSystemCaps)
        {
            currentArch = getSystemCapsArch(localSystemCaps);
            spoofArch(currentArch, sl::kFeatureDLSS_G, localSystemCaps);
        }
    }

    auto result =
        o_local_dlssg_slOnPluginLoad != nullptr && o_local_dlssg_slOnPluginLoad(params, loaderJSON, pluginJSON);

    if (shouldSpoofArch && localSystemCaps)
        setArch(currentArch, localSystemCaps);

    if (!HasPluginJson(result, pluginJSON))
        return result;

    nlohmann::json configJson = nlohmann::json::parse(*pluginJSON, nullptr, false);
    if (configJson.is_discarded())
    {
        LOG_ERROR("Failed to parse plugin JSON");
        return result;
    }

    if (configJson.contains("/external/hws/required"_json_pointer))
        configJson["external"]["hws"]["required"] = false; // disable eHardwareSchedulingRequired

    auto primaryGpu = IdentifyGpu::getPrimaryGpu();
    if (primaryGpu.vendorId != VendorId::Nvidia || !primaryGpu.dlssCapable)
    {
        if (configJson.contains("/external/vk/instance/extensions"_json_pointer))
            configJson["external"]["vk"]["instance"]["extensions"].clear();

        if (configJson.contains("/external/vk/device/extensions"_json_pointer))
            configJson["external"]["vk"]["device"]["extensions"].clear();

        if (configJson.contains("/external/vk/device/1.2_features"_json_pointer))
            configJson["external"]["vk"]["device"]["1.2_features"].clear();

        if (configJson.contains("/external/vk/device/1.3_features"_json_pointer))
            configJson["external"]["vk"]["device"]["1.3_features"].clear();
    }

    config = configJson.dump();

    *pluginJSON = config.c_str();

    return result;
}

sl::Result StreamlineHooks::hkslSetConstants(const sl::Constants& values, const sl::FrameToken& frame,
                                             const sl::ViewportHandle& viewport)
{
    std::scoped_lock lock(setConstantsMutex);
    LOG_TRACE("called with frameIndex: {}, viewport: {}", (unsigned int) frame, (unsigned int) viewport);

    State::Instance().slFGInputs.setConstants(values, (uint32_t) frame);

    return o_slSetConstants(values, frame, viewport);
}

bool StreamlineHooks::hkcommon_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON,
                                              const char** pluginJSON)
{
    LOG_FUNC();

    // TODO: do it better than "static" and hoping for the best
    static std::string config;

    auto result = o_common_slOnPluginLoad != nullptr && o_common_slOnPluginLoad(params, loaderJSON, pluginJSON);

    if (!HasPluginJson(result, pluginJSON))
    {
        LOG_DEBUG("Plugin load failed or returned no JSON, skipping patching");
        return result;
    }

    try
    {
        nlohmann::json configJson = nlohmann::json::parse(*pluginJSON);

        auto& slVersion = State::Instance().streamlineVersion;

        // Grab a version of the potentially updated sl.common
        // Opti assumes that all plugins will have this version
        configJson.at("version").at("major").get_to(slVersion.major);
        configJson.at("version").at("minor").get_to(slVersion.minor);
        configJson.at("version").at("build").get_to(slVersion.patch);

        // Completely disables Streamline hooks
        // if (true)
        //    configJson["hooks"].clear();
        //    configJson["exclusive_hooks"].clear();
        //}

        PatchSL1PluginJson(configJson);

        config = configJson.dump();
        *pluginJSON = config.c_str();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Failed to patch sl.common JSON: {}", e.what());
    }

    return result;
}

sl::Result StreamlineHooks::hkslDLSSGSetOptions(const sl::ViewportHandle& viewport, const sl::DLSSGOptions& options)
{
    if (o_slDLSSGSetOptions == nullptr)
        return sl::Result::eErrorFeatureMissing;

    lastDlssgViewport = viewport;
    lastDlssgOptions = options;

    // Avoid reading past the game's struct's size
    sl::DLSSGOptions newOptions {};
    auto newStructVer = newOptions.structVersion;

    if (options.structVersion == 1)
        memcpy(&newOptions, &options, 104);
    else if (options.structVersion == 2 || options.structVersion == 3)
        memcpy(&newOptions, &options, 112);
    else if (options.structVersion == 4 || options.structVersion == 5)
        memcpy(&newOptions, &options, 120);
    else
        newOptions = options;

    newOptions.structVersion = newStructVer;

    auto& state = State::Instance();

    // Disable game's DLSSG when we are trying to create our own instance of DLSSG
    if (state.activeFgInput != FGInput::DLSSG && state.activeFgOutput == FGOutput::DLSSG)
    {
        newOptions.mode = sl::DLSSGMode::eOff;
        return o_slDLSSGSetOptions(viewport, newOptions);
    }

    // Make DLSSG auto always mean On
    if (newOptions.mode == sl::DLSSGMode::eAuto)
        newOptions.mode = sl::DLSSGMode::eOn;

    const auto dlssgPotentiallyActive = newOptions.mode == sl::DLSSGMode::eOn ||
                                        newOptions.mode == sl::DLSSGMode::eAuto ||
                                        newOptions.mode == sl::DLSSGMode::eDynamic;

    bool enableDynamicMode = Config::Instance()->FGDLSSGOverrideForceDMFG.value_or_default() &&
                             state.dlssgGameDMFGSupported && dlssgPotentiallyActive;

    if (enableDynamicMode)
    {
        newOptions.mode = sl::DLSSGMode::eDynamic;
    }

    if (newOptions.mode == sl::DLSSGMode::eDynamic && Config::Instance()->FGDLSSGFramerateTargetDMFG.has_value())
    {
        newOptions.dynamicTargetFrameRate = Config::Instance()->FGDLSSGFramerateTargetDMFG.value();
    }

    if (state.swapchainApi == API::Vulkan)
    {
        // Only matters for Vulkan, DX doesn't use this delay
        if (dlssgPotentiallyActive && !MenuOverlayBase::IsVisible())
            state.delayMenuRenderBy = 10;

        if (MenuOverlayBase::IsVisible())
        {
            newOptions.mode = sl::DLSSGMode::eOff;
            newOptions.flags |= sl::DLSSGFlags::eRetainResourcesWhenOff;
            ReflexHooks::setDlssgFrameCount(0);
        }
    }

    LOG_TRACE("DLSSG Modified Mode: {}", magic_enum::enum_name(newOptions.mode));

    if (dlssgPotentiallyActive && state.streamlineVersion >= feature_version { 2, 7, 1 })
    {
        // Populate dlssgMfgMax once
        if (!state.dlssgMfgMax.has_value())
        {
            sl::DLSSGState localState {};
            sl::DLSSGOptions localOptions {};
            if (o_slDLSSGGetState(viewport, localState, &localOptions) == sl::Result::eOk &&
                localState.numFramesToGenerateMax > 0 && localState.numFramesToGenerateMax < 6)
            {
                state.dlssgMfgMax = localState.numFramesToGenerateMax;
                LOG_TRACE("Saving original numFramesToGenerateMax: {}", state.dlssgMfgMax.value());

                if (Config::Instance()->FGDLSSGOverrideInterpolationCount.has_value() &&
                    Config::Instance()->FGDLSSGOverrideInterpolationCount.value() > state.dlssgMfgMax.value())
                {
                    Config::Instance()->FGDLSSGOverrideInterpolationCount = state.dlssgMfgMax.value();
                }
            }
        }

        // Won't take effect with Dynamic
        if (Config::Instance()->FGDLSSGOverrideInterpolationCount.has_value())
        {
            auto overrideCount = Config::Instance()->FGDLSSGOverrideInterpolationCount.value();
            if (overrideCount != 0)
                newOptions.numFramesToGenerate = overrideCount;
            else if (!enableDynamicMode)
                newOptions.mode = sl::DLSSGMode::eOff;
        }
    }

    state.dlssgLastSetMode = newOptions.mode;

    return o_slDLSSGSetOptions(viewport, newOptions);
}

sl::Result StreamlineHooks::hkslDLSSGGetState(const sl::ViewportHandle& viewport, sl::DLSSGState& state,
                                              const sl::DLSSGOptions* options)
{
    if (o_slDLSSGGetState == nullptr)
        return sl::Result::eErrorFeatureMissing;

    sl::Result result {};

    const auto originalStructVersion = state.structVersion;
    if (originalStructVersion < 4)
    {
        sl::DLSSGState newState {};

        // We might be feeding a newer struct to an older SL but that seems to work just fine for this Get function
        result = o_slDLSSGGetState(viewport, dynamic_cast<sl::DLSSGState&>(newState), options);

        // Copy back data to game's struct
        memcpy(&state, &newState, 56); // struct ver 1 size
        state.structVersion = originalStructVersion;

        if (originalStructVersion >= 2)
        {
            state.numFramesToGenerateMax = newState.numFramesToGenerateMax;
            state.bReserved4 = newState.bReserved4;
            state.bIsVsyncSupportAvailable = newState.bIsVsyncSupportAvailable;
        }

        if (originalStructVersion >= 3)
        {
            state.inputsProcessingCompletionFence = newState.inputsProcessingCompletionFence;
            state.lastPresentInputsProcessingCompletionFenceValue =
                newState.lastPresentInputsProcessingCompletionFenceValue;
        }

        State::Instance().dlssgGameDMFGSupported = newState.bIsDynamicMFGSupported == sl::eTrue;
    }
    else
    {
        result = o_slDLSSGGetState(viewport, state, options);
        State::Instance().dlssgGameDMFGSupported = state.bIsDynamicMFGSupported == sl::eTrue;
    }

    if (!State::Instance().dlssgGameDMFGSupported)
    {
        Config::Instance()->FGDLSSGOverrideForceDMFG.set_volatile_value(false);
    }

    auto& optiState = State::Instance();

    if (optiState.streamlineVersion >= feature_version { 2, 7, 1 })
    {
        if (!optiState.dlssgMfgMax.has_value())
        {
            sl::DLSSGState localState {};
            sl::DLSSGOptions localOptions {};
            if (o_slDLSSGGetState(viewport, localState, &localOptions) == sl::Result::eOk &&
                localState.numFramesToGenerateMax > 0 && localState.numFramesToGenerateMax < 6)
            {
                optiState.dlssgMfgMax = localState.numFramesToGenerateMax;
                LOG_TRACE("Saving original numFramesToGenerateMax: {}", optiState.dlssgMfgMax.value());

                if (Config::Instance()->FGDLSSGOverrideInterpolationCount.has_value() &&
                    Config::Instance()->FGDLSSGOverrideInterpolationCount.value() > optiState.dlssgMfgMax.value())
                {
                    Config::Instance()->FGDLSSGOverrideInterpolationCount = optiState.dlssgMfgMax.value();
                }
            }
        }
    }

    if (optiState.activeFgInput == FGInput::DLSSG)
    {
        auto fg = optiState.currentFG;

        if (fg != nullptr)
        {
            if (options != nullptr && options->flags & sl::DLSSGFlags::eRequestVRAMEstimate)
                state.estimatedVRAMUsageInBytes = static_cast<uint64_t>(256 * 1024) * 1024;

            if (fg->IsActive() && !fg->IsPaused())
            {
                state.numFramesActuallyPresented = fg->GetInterpolatedFrameCount() + 1;
            }
            else
            {
                state.numFramesActuallyPresented = 1;
            }
        }
        else
        {
            state.numFramesActuallyPresented = 1;
        }

        state.numFramesToGenerateMax = 1;

        LOG_DEBUG("Status: {}, numFramesActuallyPresented: {}", magic_enum::enum_name(state.status),
                  state.numFramesActuallyPresented);
    }

    return result;
}

bool StreamlineHooks::hkreflex_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON,
                                              const char** pluginJSON)
{
    LOG_FUNC();

    // TODO: do it better than "static" and hoping for the best
    static std::string config;

    uint32_t currentArch = 0;
    if (Config::Instance()->StreamlineSpoofing.value_or_default())
    {
        hookSystemCaps(params);
        currentArch = getSystemCapsArch();
        spoofArch(currentArch, sl::kFeatureReflex);
    }

    auto result = o_reflex_slOnPluginLoad != nullptr && o_reflex_slOnPluginLoad(params, loaderJSON, pluginJSON);

    if (Config::Instance()->StreamlineSpoofing.value_or_default())
        setArch(currentArch);

    if (!HasPluginJson(result, pluginJSON))
        return result;

    nlohmann::json configJson = nlohmann::json::parse(*pluginJSON, nullptr, false);
    if (configJson.is_discarded())
    {
        LOG_ERROR("Failed to parse plugin JSON");
        return result;
    }

    auto primaryGpu = IdentifyGpu::getPrimaryGpu();
    if (primaryGpu.vendorId != VendorId::Nvidia || !primaryGpu.dlssCapable)
    {
        if (configJson.contains("/external/vk/instance/extensions"_json_pointer))
            configJson["external"]["vk"]["instance"]["extensions"].clear();

        if (configJson.contains("/external/vk/device/extensions"_json_pointer))
            configJson["external"]["vk"]["device"]["extensions"].clear();

        if (configJson.contains("/external/vk/device/1.2_features"_json_pointer))
            configJson["external"]["vk"]["device"]["1.2_features"].clear();

        if (configJson.contains("/external/vk/device/1.3_features"_json_pointer))
            configJson["external"]["vk"]["device"]["1.3_features"].clear();
    }

    PatchSL1PluginJson(configJson);

    config = configJson.dump();

    *pluginJSON = config.c_str();

    return result;
}

sl::Result StreamlineHooks::hkslReflexSetOptions(const sl::ReflexOptions& options)
{
    if (o_slReflexSetOptions == nullptr)
        return sl::Result::eErrorFeatureMissing;

    reflexGamesLastMode = options.mode;

    sl::ReflexOptions newOptions = options;

    if (Config::Instance()->FN_ForceReflex == ForceReflex::ForceEnable)
        newOptions.mode = sl::ReflexMode::eLowLatencyWithBoost;

    // Will cause a pink screen when used with DLSSG
    // if (Config::Instance()->FN_ForceReflex == 1)
    //     newOptions.mode = sl::ReflexMode::eOff;

    return o_slReflexSetOptions(newOptions);
}

sl::Result StreamlineHooks::hkslReflexSleep(const sl::FrameToken& frame)
{
    if (o_slReflexSleep == nullptr)
        return sl::Result::eErrorFeatureMissing;

    // if (State::Instance().activeFgOutput == FGOutput::DLSSG && StreamlineProxy::IsD3D12Inited() &&
    //     Config::Instance()->FGDLSSGUseGamesReflexMarkers.value_or_default())
    //{
    //     return StreamlineProxy::ReflexSleep()(frame);
    // }

    return o_slReflexSleep(frame);
}

void* StreamlineHooks::hkdlss_slGetPluginFunction(PFN_slGetPluginFunction original, const char* functionName,
                                                  void* caller)
{
    LOG_DEBUG("{}", functionName);

    if (strcmp(functionName, "slOnPluginLoad") == 0)
    {
        o_dlss_slOnPluginLoad = (PFN_slOnPluginLoad) original(functionName);
        return &hkdlss_slOnPluginLoad;
    }

    if (strcmp(functionName, "slDLSSGetOptimalSettings") == 0 &&
        State::Instance().gameQuirks & GameQuirk::PregmataFixDLSSModes)
    {
        o_slDLSSGetOptimalSettings = (decltype(&slDLSSGetOptimalSettings)) original(functionName);
        return &hkslDLSSGetOptimalSettings;
    }

    return original(functionName);
}

void* StreamlineHooks::hkdlssg_slGetPluginFunction(PFN_slGetPluginFunction original, const char* functionName,
                                                   void* caller)
{
    LOG_DEBUG("{}", functionName);

    if (strcmp(functionName, "slOnPluginLoad") == 0)
    {
        o_dlssg_slOnPluginLoad = (PFN_slOnPluginLoad) original(functionName);
        return &hkdlssg_slOnPluginLoad;
    }

    if (strcmp(functionName, "slDLSSGSetOptions") == 0)
    {
        o_slDLSSGSetOptions = (decltype(&slDLSSGSetOptions)) original(functionName);

        // Give steam overlay the original as it seems to be hooking it
        auto steamOverlay = KernelBaseProxy::GetModuleHandleA_()("gameoverlayrenderer64.dll");
        if (steamOverlay != nullptr)
        {
            if (HMODULE callerModule = Util::GetCallerModule(caller); callerModule == steamOverlay)
                return o_slDLSSGSetOptions;
        }

        return &hkslDLSSGSetOptions;
    }

    if (strcmp(functionName, "slDLSSGGetState") == 0)
    {
        o_slDLSSGGetState = (decltype(&slDLSSGGetState)) original(functionName);

        // Give steam overlay the original as it seems to be hooking it
        auto steamOverlay = KernelBaseProxy::GetModuleHandleA_()("gameoverlayrenderer64.dll");
        if (steamOverlay != nullptr)
        {
            if (HMODULE callerModule = Util::GetCallerModule(caller); callerModule == steamOverlay)
                return o_slDLSSGGetState;
        }

        return &hkslDLSSGGetState;
    }

    if (strcmp(functionName, "slGetPluginJSONConfig") == 0 && IsSL1AndDLSSGActive())
    {
        o_dlssg_slGetPluginJSONConfig_sl1 = reinterpret_cast<PFN_slGetPluginJSONConfig_sl1>(original(functionName));

        if (o_dlssg_slGetPluginJSONConfig_sl1 != nullptr)
        {
            LOG_WARN("Hooking SL1 DLSSG slGetPluginJSONConfig");
            return &hkdlssg_slGetPluginJSONConfig_sl1;
        }
    }

    // Ensure that we have those DLSSG calls
    if (!o_slDLSSGSetOptions)
        o_slDLSSGSetOptions = (decltype(&slDLSSGSetOptions)) original("slDLSSGSetOptions");

    if (!o_slDLSSGGetState)
        o_slDLSSGGetState = (decltype(&slDLSSGGetState)) original("slDLSSGGetState");

    return original(functionName);
}

void* StreamlineHooks::hklocal_dlssg_slGetPluginFunction(PFN_slGetPluginFunction original, const char* functionName,
                                                         void* caller)
{
    // LOG_DEBUG("{}", functionName);

    if (strcmp(functionName, "slOnPluginLoad") == 0 && State::Instance().activeFgNvngx != FGNvngxReplacement::None)
    {
        o_local_dlssg_slOnPluginLoad = (PFN_slOnPluginLoad) original(functionName);
        return &hklocal_dlssg_slOnPluginLoad;
    }

    return original(functionName);
}

bool StreamlineHooks::hkreflex_slSetConstants_sl1(const void* data, uint32_t frameIndex, uint32_t id)
{
    if (o_reflex_slSetConstants_sl1 == nullptr)
        return false;

    // Streamline v1's version of slReflexSetOptions + slPCLSetMarker
    static sl1::ReflexConstants constants {};
    constants = *(const sl1::ReflexConstants*) data;

    reflexGamesLastMode = (sl::ReflexMode) constants.mode;

    LOG_DEBUG("mode: {}, frameIndex: {}, id: {}", (uint32_t) constants.mode, frameIndex, id);

    if (Config::Instance()->FN_ForceReflex == ForceReflex::ForceEnable)
        constants.mode = sl1::ReflexMode::eReflexModeLowLatencyWithBoost;

    // Will cause a pink screen when used with DLSSG
    // else if (Config::Instance()->FN_ForceReflex == 1)
    //     constants.mode = sl1::ReflexMode::eReflexModeOff;

    return o_reflex_slSetConstants_sl1(&constants, frameIndex, id);
}

void* StreamlineHooks::hkreflex_slGetPluginFunction(PFN_slGetPluginFunction original, const char* functionName,
                                                    void* caller)
{
    // LOG_DEBUG("{}", functionName);

    if (strcmp(functionName, "slSetConstants") == 0 && State::Instance().streamlineVersion.major == 1)
    {
        o_reflex_slSetConstants_sl1 = (PFN_slSetConstants_sl1) original(functionName);
        return &hkreflex_slSetConstants_sl1;
    }

    if (strcmp(functionName, "slOnPluginLoad") == 0)
    {
        o_reflex_slOnPluginLoad = (PFN_slOnPluginLoad) original(functionName);
        return &hkreflex_slOnPluginLoad;
    }

    if (strcmp(functionName, "slReflexSetOptions") == 0)
    {
        o_slReflexSetOptions = (decltype(&slReflexSetOptions)) original(functionName);
        return &hkslReflexSetOptions;
    }

    if (strcmp(functionName, "slReflexSleep") == 0)
    {
        o_slReflexSleep = (decltype(&slReflexSleep)) original(functionName);
        return &hkslReflexSleep;
    }

    // TODO: Hopefully a game doesn't call both, maybe separate
    if (strcmp(functionName, "slReflexSetMarker") == 0 &&
        (State::Instance().gameQuirks & GameQuirk::FixSlSimulationMarkers ||
         State::Instance().activeFgInput == FGInput::DLSSG))
    {
        o_slPCLSetMarker = (decltype(&slPCLSetMarker)) original(functionName);
        return &hkslPCLSetMarker;
    }

    return original(functionName);
}

sl::Result StreamlineHooks::hkslPCLSetMarker(sl::PCLMarker marker, const sl::FrameToken& frame)
{
    if (o_slPCLSetMarker == nullptr)
        return sl::Result::eErrorFeatureMissing;

    // if (State::Instance().activeFgOutput == FGOutput::DLSSG && StreamlineProxy::IsD3D12Inited() &&
    //     Config::Instance()->FGDLSSGUseGamesReflexMarkers.value_or_default())
    //{
    //     return StreamlineProxy::PCLSetMarker()(marker, frame);
    // }

    // HACK for broken games
    if (State::Instance().gameQuirks & GameQuirk::FixSlSimulationMarkers)
    {
        static uint64_t last_simulation_end_id = 0;
        if (marker == sl::PCLMarker::eSimulationEnd)
        {
            last_simulation_end_id = frame;
        }

        if (marker == sl::PCLMarker::eSimulationStart && last_simulation_end_id >= frame && o_slGetNewFrameToken)
        {
            const uint64_t correction_offset = last_simulation_end_id - frame + 1;
            uint32_t newFrameId = static_cast<uint32_t>(frame + correction_offset);

            sl::FrameToken* newFramePointer {};
            auto result = o_slGetNewFrameToken(newFramePointer, &newFrameId);

            LOG_WARN("Simulation start marker sent after end marker, offset: {}", correction_offset);

            result = o_slPCLSetMarker(marker, *newFramePointer);
            return result;
        }
    }

    if (State::Instance().activeFgInput == FGInput::DLSSG)
    {
        if (State::Instance().streamlineVersion.major == 1)
        {
            if (marker == sl::PCLMarker::eRenderSubmitStart)
            {
                State::Instance().s_sl1FGInputs.evaluateState();
            }
            else if (marker == sl::PCLMarker::ePresentStart)
            {
                State::Instance().s_sl1FGInputs.markPresent(frame);
            }
        }
        else
        {
            if (marker == sl::PCLMarker::eRenderSubmitStart)
            {
                State::Instance().slFGInputs.evaluateState();
            }
            else if (marker == sl::PCLMarker::ePresentStart)
            {
                State::Instance().slFGInputs.markPresent(frame);
            }
        }
    }

    return o_slPCLSetMarker(marker, frame);
}

bool StreamlineHooks::hkpcl_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON,
                                           const char** pluginJSON)
{
    LOG_FUNC();

    uint32_t currentArch = 0;
    if (Config::Instance()->StreamlineSpoofing.value_or_default())
    {
        hookSystemCaps(params);
        currentArch = getSystemCapsArch();
        spoofArch(currentArch, sl::kFeaturePCL);
    }

    auto result = o_pcl_slOnPluginLoad != nullptr && o_pcl_slOnPluginLoad(params, loaderJSON, pluginJSON);

    if (Config::Instance()->StreamlineSpoofing.value_or_default())
        setArch(currentArch);

    return result;
}

void* StreamlineHooks::hkpcl_slGetPluginFunction(PFN_slGetPluginFunction original, const char* functionName,
                                                 void* caller)
{
    // LOG_DEBUG("{}", functionName);

    if (strcmp(functionName, "slPCLSetMarker") == 0 &&
        (State::Instance().gameQuirks & GameQuirk::FixSlSimulationMarkers ||
         State::Instance().activeFgInput == FGInput::DLSSG))
    {
        o_slPCLSetMarker = (decltype(&slPCLSetMarker)) original(functionName);
        return &hkslPCLSetMarker;
    }

    if (strcmp(functionName, "slOnPluginLoad") == 0)
    {
        o_pcl_slOnPluginLoad = (PFN_slOnPluginLoad) original(functionName);
        return &hkpcl_slOnPluginLoad;
    }

    return original(functionName);
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

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook setVoid: {:X}", detourResult);
            o_setVoid = nullptr;
        }
    }

    if (o_common_slSetParameters_sl1 != nullptr)
        o_common_slSetParameters_sl1(params);
}

void* StreamlineHooks::hkcommon_slGetPluginFunction(PFN_slGetPluginFunction original, const char* functionName,
                                                    void* caller)
{
    // LOG_DEBUG("{}", functionName);

    if (strcmp(functionName, "slOnPluginLoad") == 0)
    {
        o_common_slOnPluginLoad = (PFN_slOnPluginLoad) original(functionName);
        return &hkcommon_slOnPluginLoad;
    }

    // Used around Streamline v1.3, as 1.5 doesn't seem to have it anymore
    if (strcmp(functionName, "slSetParameters") == 0)
    {
        o_common_slSetParameters_sl1 = (PFN_slSetParameters_sl1) original(functionName);
        return &hkcommon_slSetParameters_sl1;
    }

    return original(functionName);
}

void StreamlineHooks::updateForceReflex()
{
    // Not needed for Streamline v1 as slSetConstants is sent every frame
    if (o_slReflexSetOptions)
    {
        sl::ReflexOptions options;

        auto forceReflex = Config::Instance()->FN_ForceReflex.value_or_default();

        if (forceReflex == ForceReflex::ForceEnable)
            options.mode = sl::ReflexMode::eLowLatencyWithBoost;
        else if (forceReflex == ForceReflex::ForceDisable)
            options.mode = sl::ReflexMode::eOff;
        else if (forceReflex == ForceReflex::InGame)
            options.mode = reflexGamesLastMode;

        auto result = o_slReflexSetOptions(options);
        if (result != sl::Result::eOk)
        {
            LOG_WARN("Failed to update Reflex mode with error code: {} ({:X})", magic_enum::enum_name(result),
                     (UINT) result);
        }
    }
}

void StreamlineHooks::updateDlssgOptions()
{
    if (o_slDLSSGSetOptions)
    {
        LOG_FUNC();
        hkslDLSSGSetOptions(lastDlssgViewport, lastDlssgOptions);
    }
}

// SL INTERPOSER

void StreamlineHooks::unhookInterposer()
{
    LOG_FUNC();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_slSetTag)
        DetourDetach(&(PVOID&) o_slSetTag, hkslSetTag);

    if (o_slSetTagForFrame)
        DetourDetach(&(PVOID&) o_slSetTagForFrame, hkslSetTagForFrame);

    if (o_slSetConstants)
        DetourDetach(&(PVOID&) o_slSetConstants, hkslSetConstants);

    if (o_slEvaluateFeature)
        DetourDetach(&(PVOID&) o_slEvaluateFeature, hkslEvaluateFeature);

    if (o_slInit)
        DetourDetach(&(PVOID&) o_slInit, hkslInit);

    if (o_slInit_sl1)
        DetourDetach(&(PVOID&) o_slInit_sl1, hkslInit_sl1);

    if (o_slSetTag_sl1)
        DetourDetach(&(PVOID&) o_slSetTag_sl1, hkslSetTag_sl1);

    if (o_slSetConstants_interposer_sl1)
        DetourDetach(&(PVOID&) o_slSetConstants_interposer_sl1, hkslSetConstants_sl1);

    if (o_slEvaluateFeature_sl1)
        DetourDetach(&(PVOID&) o_slEvaluateFeature_sl1, hkslEvaluateFeature_sl1);

    // if (o_logCallback)
    //     DetourDetach(&(PVOID&) o_logCallback, streamlineLogCallback);
    // else if (o_logCallback_sl1)
    //     DetourDetach(&(PVOID&) o_logCallback_sl1, streamlineLogCallback);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("DetourTransactionCommit error: {:X}", detourResult);
    }
    else
    {
        o_slInit = nullptr;
        o_slInit_sl1 = nullptr;
        o_slSetTag = nullptr;
        o_slSetTagForFrame = nullptr;
        o_slEvaluateFeature = nullptr;
        o_slSetConstants = nullptr;
        o_slSetTag_sl1 = nullptr;
        o_slSetConstants_interposer_sl1 = nullptr;
        o_slEvaluateFeature_sl1 = nullptr;
        o_logCallback = nullptr;
        o_logCallback_sl1 = nullptr;
        hookedInterposerModule = nullptr;
    }
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
    if (hookedInterposerModule == slInterposer)
        return;

    // Looks like when reading DLL version load methods are called
    // To prevent loops disabling checks for sl.interposer.dll
    auto owner = State::GetOwner();
    State::DisableChecks(owner, "sl.interposer");

    if (o_slSetTag || o_slInit || o_slInit_sl1 || o_slSetTag_sl1 || o_slSetConstants_interposer_sl1 ||
        o_slEvaluateFeature_sl1)
        unhookInterposer();

    // After unhookInterposer, it resets hookedInterposerModule
    hookedInterposerModule = slInterposer;

    {
        char dllPath[MAX_PATH];
        GetModuleFileNameA(slInterposer, dllPath, MAX_PATH);

        LOG_TRACE("slInterposer path: {}", dllPath);

        version_t sl_version;
        Util::GetFileVersion(string_to_wstring(dllPath), &sl_version);

        State::Instance().streamlineVersion.major = sl_version.major;
        State::Instance().streamlineVersion.minor = sl_version.minor;
        State::Instance().streamlineVersion.patch = sl_version.patch;

        LOG_INFO("Streamline version: {}.{}.{}", sl_version.major, sl_version.minor, sl_version.patch);

        if (sl_version.major >= 2)
        {
            o_slSetTag =
                reinterpret_cast<decltype(&slSetTag)>(KernelBaseProxy::GetProcAddress_()(slInterposer, "slSetTag"));
            o_slSetTagForFrame = reinterpret_cast<decltype(&slSetTagForFrame)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slSetTagForFrame"));
            o_slInit = reinterpret_cast<decltype(&slInit)>(KernelBaseProxy::GetProcAddress_()(slInterposer, "slInit"));
            o_slEvaluateFeature = reinterpret_cast<decltype(&slEvaluateFeature)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slEvaluateFeature"));
            o_slSetConstants = reinterpret_cast<decltype(&slSetConstants)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slSetConstants"));
            o_slGetNativeInterface = reinterpret_cast<decltype(&slGetNativeInterface)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slGetNativeInterface"));
            o_slSetD3DDevice = reinterpret_cast<decltype(&slSetD3DDevice)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slSetD3DDevice"));
            o_slGetNewFrameToken = reinterpret_cast<decltype(&slGetNewFrameToken)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slGetNewFrameToken")); // Not hooked

            // For making the game think DLSSG is loaded and supported
            // but making SL not actually load the plugin
            o_slIsFeatureSupported = reinterpret_cast<decltype(&slIsFeatureSupported)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slIsFeatureSupported"));
            o_slIsFeatureLoaded = reinterpret_cast<decltype(&slIsFeatureLoaded)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slIsFeatureLoaded"));
            o_slGetFeatureRequirements = reinterpret_cast<decltype(&slGetFeatureRequirements)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slGetFeatureRequirements"));
            o_slGetFeatureVersion = reinterpret_cast<decltype(&slGetFeatureVersion)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slGetFeatureVersion"));
            o_slGetFeatureFunction = reinterpret_cast<decltype(&slGetFeatureFunction)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slGetFeatureFunction"));
            o_slAllocateResources = reinterpret_cast<decltype(&slAllocateResources)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slAllocateResources"));
            o_slFreeResources = reinterpret_cast<decltype(&slFreeResources)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slFreeResources"));

            if (o_slInit != nullptr)
            {
                LOG_TRACE("Hooking v2");
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                DetourAttach(&(PVOID&) o_slInit, hkslInit);

                if (o_slEvaluateFeature != nullptr)
                    DetourAttach(&(PVOID&) o_slEvaluateFeature, hkslEvaluateFeature);

                if (State::Instance().activeFgInput == FGInput::NvngxFG ||
                    State::Instance().activeFgInput == FGInput::DLSSG)
                {
                    if (o_slSetTag != nullptr)
                        DetourAttach(&(PVOID&) o_slSetTag, hkslSetTag);

                    if (o_slSetTagForFrame != nullptr)
                        DetourAttach(&(PVOID&) o_slSetTagForFrame, hkslSetTagForFrame);

                    if (o_slSetConstants != nullptr)
                        DetourAttach(&(PVOID&) o_slSetConstants, hkslSetConstants);
                }

                if (State::Instance().activeFgInput == FGInput::DLSSG)
                {
                    if (o_slIsFeatureSupported != nullptr)
                        DetourAttach(&(PVOID&) o_slIsFeatureSupported, hkslIsFeatureSupported);

                    if (o_slIsFeatureLoaded != nullptr)
                        DetourAttach(&(PVOID&) o_slIsFeatureLoaded, hkslIsFeatureLoaded);

                    if (o_slGetFeatureRequirements != nullptr)
                        DetourAttach(&(PVOID&) o_slGetFeatureRequirements, hkslGetFeatureRequirements);

                    if (o_slGetFeatureVersion != nullptr)
                        DetourAttach(&(PVOID&) o_slGetFeatureVersion, hkslGetFeatureVersion);

                    if (o_slGetFeatureFunction != nullptr)
                        DetourAttach(&(PVOID&) o_slGetFeatureFunction, hkslGetFeatureFunction);

                    if (o_slSetFeatureLoaded != nullptr)
                        DetourAttach(&(PVOID&) o_slSetFeatureLoaded, hkslSetFeatureLoaded);

                    if (o_slAllocateResources != nullptr)
                        DetourAttach(&(PVOID&) o_slAllocateResources, hkslAllocateResources);

                    if (o_slFreeResources != nullptr)
                        DetourAttach(&(PVOID&) o_slFreeResources, hkslFreeResources);
                }

                // if (o_slGetNativeInterface != nullptr)
                //     DetourAttach(&(PVOID&) o_slGetNativeInterface, hkslGetNativeInterface);

                // if (o_slSetD3DDevice != nullptr)
                //     DetourAttach(&(PVOID&) o_slSetD3DDevice, hkslSetD3DDevice);

                auto detourResult = DetourTransactionCommit();
                if (detourResult != NO_ERROR)
                {
                    LOG_ERROR("Failed to hook sl.interposer v2: {:X}", detourResult);
                    o_slSetTag = nullptr;
                    o_slSetTagForFrame = nullptr;
                    o_slInit = nullptr;
                    o_slEvaluateFeature = nullptr;
                    o_slAllocateResources = nullptr;
                    o_slSetConstants = nullptr;
                    o_slGetNativeInterface = nullptr;
                    o_slSetD3DDevice = nullptr;
                    o_slIsFeatureSupported = nullptr;
                    o_slIsFeatureLoaded = nullptr;
                    o_slGetFeatureRequirements = nullptr;
                    o_slGetFeatureVersion = nullptr;
                    o_slGetFeatureFunction = nullptr;
                }
            }
        }
        else if (sl_version.major == 1)
        {
            o_slInit_sl1 =
                reinterpret_cast<decltype(&sl1::slInit)>(KernelBaseProxy::GetProcAddress_()(slInterposer, "slInit"));
            o_slSetTag_sl1 = reinterpret_cast<decltype(&sl1::slSetTag)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slSetTag"));
            o_slSetConstants_interposer_sl1 = reinterpret_cast<decltype(&sl1::slSetConstants)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slSetConstants"));
            o_slEvaluateFeature_sl1 = reinterpret_cast<decltype(&sl1::slEvaluateFeature)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slEvaluateFeature"));

            LOG_INFO("SL1 exports - slInit: {}, slSetTag: {}, slSetConstants: {}, slEvaluateFeature: {}",
                     o_slInit_sl1 != nullptr, o_slSetTag_sl1 != nullptr, o_slSetConstants_interposer_sl1 != nullptr,
                     o_slEvaluateFeature_sl1 != nullptr);

            if (o_slInit_sl1 || o_slSetTag_sl1 || o_slSetConstants_interposer_sl1 || o_slEvaluateFeature_sl1)
            {
                LOG_TRACE("Hooking v1");
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                if (o_slInit_sl1)
                    DetourAttach(&(PVOID&) o_slInit_sl1, hkslInit_sl1);

                if (IsSL1AndFGActive())
                {
                    if (o_slSetTag_sl1)
                        DetourAttach(&(PVOID&) o_slSetTag_sl1, hkslSetTag_sl1);

                    if (o_slSetConstants_interposer_sl1)
                        DetourAttach(&(PVOID&) o_slSetConstants_interposer_sl1, hkslSetConstants_sl1);

                    if (o_slEvaluateFeature_sl1)
                        DetourAttach(&(PVOID&) o_slEvaluateFeature_sl1, hkslEvaluateFeature_sl1);
                }

                auto detourResult = DetourTransactionCommit();
                if (detourResult != NO_ERROR)
                {
                    LOG_ERROR("Failed to hook sl.interposer v1: {:X}", detourResult);
                    o_slInit_sl1 = nullptr;
                    o_slSetTag_sl1 = nullptr;
                    o_slSetConstants_interposer_sl1 = nullptr;
                    o_slEvaluateFeature_sl1 = nullptr;
                }
            }
        }
    }

    State::EnableChecks(owner);
}

// SL PLUGINS (sl.dlss, sl.dlss_g, sl.reflex, sl.pcl, sl.common)
//
// Streamline may load several copies of the same plugin, e.g. the game's sl.reflex.dll and then an OTA
// 1B0_E658703.dll, decide that one of them is not allowed (OTA denylist) and FreeLibrary it while it keeps
// using the other one. So every copy gets its own detour slot and stays hooked for as long as it's mapped.
// A slot is released after the module has been unmapped (onModuleFreed / releaseDeadSlots), which never
// writes into the freed (and possibly reused) address range.

const char* StreamlineHooks::pluginName(SlPlugin plugin)
{
    switch (plugin)
    {
    case SlPlugin::Dlss:
        return "sl.dlss";
    case SlPlugin::Dlssg:
        return "sl.dlss_g";
    case SlPlugin::LocalDlssg:
        return "local sl.dlss_g";
    case SlPlugin::Reflex:
        return "sl.reflex";
    case SlPlugin::Pcl:
        return "sl.pcl";
    case SlPlugin::Common:
        return "sl.common";
    default:
        return "unknown";
    }
}

std::mutex& StreamlineHooks::pluginMutex(SlPlugin plugin)
{
    switch (plugin)
    {
    case SlPlugin::Dlss:
        return mutexHookDlss;
    case SlPlugin::Dlssg:
        return mutexHookDlssg;
    case SlPlugin::LocalDlssg:
        return mutexHookLocalDlssg;
    case SlPlugin::Reflex:
        return mutexHookReflex;
    case SlPlugin::Pcl:
        return mutexHookPcl;
    case SlPlugin::Common:
    default:
        return mutexHookCommon;
    }
}

HMODULE& StreamlineHooks::activePluginModule(SlPlugin plugin)
{
    switch (plugin)
    {
    case SlPlugin::Dlss:
        return hookedDlssModule;
    case SlPlugin::Dlssg:
        return hookedDlssgModule;
    case SlPlugin::LocalDlssg:
        return hookedLocalDlssgModule;
    case SlPlugin::Reflex:
        return hookedReflexModule;
    case SlPlugin::Pcl:
        return hookedPclModule;
    case SlPlugin::Common:
    default:
        return hookedCommonModule;
    }
}

template <StreamlineHooks::SlPlugin P, size_t I> void* StreamlineHooks::slotGetPluginFunction(const char* functionName)
{
    auto original = pluginSlots[(size_t) P][I].original;

    // Can't happen while the module is mapped, but don't crash if it somehow does
    if (original == nullptr)
        return nullptr;

    return dispatchGetPluginFunction(P, original, functionName, _ReturnAddress());
}

void* StreamlineHooks::dispatchGetPluginFunction(SlPlugin plugin, PFN_slGetPluginFunction original,
                                                 const char* functionName, void* caller)
{
    if (functionName == nullptr)
        return original(functionName);

    switch (plugin)
    {
    case SlPlugin::Dlss:
        return hkdlss_slGetPluginFunction(original, functionName, caller);
    case SlPlugin::Dlssg:
        return hkdlssg_slGetPluginFunction(original, functionName, caller);
    case SlPlugin::LocalDlssg:
        return hklocal_dlssg_slGetPluginFunction(original, functionName, caller);
    case SlPlugin::Reflex:
        return hkreflex_slGetPluginFunction(original, functionName, caller);
    case SlPlugin::Pcl:
        return hkpcl_slGetPluginFunction(original, functionName, caller);
    case SlPlugin::Common:
        return hkcommon_slGetPluginFunction(original, functionName, caller);
    default:
        return original(functionName);
    }
}

StreamlineHooks::PFN_slGetPluginFunction StreamlineHooks::slotDetour(SlPlugin plugin, size_t index)
{
    static_assert(kMaxPluginInstances == 4, "Update the detour table below");
    static_assert((size_t) SlPlugin::Count == 6, "Update the detour table below");

    using PFN = PFN_slGetPluginFunction;
    static const PFN table[(size_t) SlPlugin::Count][kMaxPluginInstances] = {
        { &slotGetPluginFunction<SlPlugin::Dlss, 0>, &slotGetPluginFunction<SlPlugin::Dlss, 1>,
          &slotGetPluginFunction<SlPlugin::Dlss, 2>, &slotGetPluginFunction<SlPlugin::Dlss, 3> },
        { &slotGetPluginFunction<SlPlugin::Dlssg, 0>, &slotGetPluginFunction<SlPlugin::Dlssg, 1>,
          &slotGetPluginFunction<SlPlugin::Dlssg, 2>, &slotGetPluginFunction<SlPlugin::Dlssg, 3> },
        { &slotGetPluginFunction<SlPlugin::LocalDlssg, 0>, &slotGetPluginFunction<SlPlugin::LocalDlssg, 1>,
          &slotGetPluginFunction<SlPlugin::LocalDlssg, 2>, &slotGetPluginFunction<SlPlugin::LocalDlssg, 3> },
        { &slotGetPluginFunction<SlPlugin::Reflex, 0>, &slotGetPluginFunction<SlPlugin::Reflex, 1>,
          &slotGetPluginFunction<SlPlugin::Reflex, 2>, &slotGetPluginFunction<SlPlugin::Reflex, 3> },
        { &slotGetPluginFunction<SlPlugin::Pcl, 0>, &slotGetPluginFunction<SlPlugin::Pcl, 1>,
          &slotGetPluginFunction<SlPlugin::Pcl, 2>, &slotGetPluginFunction<SlPlugin::Pcl, 3> },
        { &slotGetPluginFunction<SlPlugin::Common, 0>, &slotGetPluginFunction<SlPlugin::Common, 1>,
          &slotGetPluginFunction<SlPlugin::Common, 2>, &slotGetPluginFunction<SlPlugin::Common, 3> },
    };

    return table[(size_t) plugin][index];
}

// Module still mapped at the same base, same file, and our patch is still in place
// (a freed + reloaded copy of the same dll at the same base would have fresh, unpatched code)
bool StreamlineHooks::isSlotAlive(const PluginHookSlot& slot)
{
    if (slot.module == nullptr || slot.target == nullptr || slot.path.empty())
        return false;

    const auto current = GetModulePath(slot.module);
    if (current.empty() || _wcsicmp(current.c_str(), slot.path.c_str()) != 0)
        return false;

    // Safe to read, the module checked above is mapped
    return memcmp(slot.target, slot.patchBytes, kPatchBytes) == 0;
}

// Plugin provided function pointers that point into the module that is gone
void StreamlineHooks::clearPluginPointersInRange(uintptr_t base, size_t size)
{
    if (base == 0 || size == 0)
        return;

    auto clear = [base, size](auto& ptr, const char* name)
    {
        const auto address = reinterpret_cast<uintptr_t>(ptr);
        if (ptr != nullptr && address >= base && address < base + size)
        {
            LOG_DEBUG("Clearing {}, it pointed into an unloaded plugin", name);
            ptr = nullptr;
        }
    };

    clear(o_dlss_slOnPluginLoad, "o_dlss_slOnPluginLoad");
    clear(o_slDLSSGetOptimalSettings, "o_slDLSSGetOptimalSettings");

    clear(o_dlssg_slOnPluginLoad, "o_dlssg_slOnPluginLoad");
    clear(o_dlssg_slGetPluginJSONConfig_sl1, "o_dlssg_slGetPluginJSONConfig_sl1");
    clear(o_slDLSSGSetOptions, "o_slDLSSGSetOptions");
    clear(o_slDLSSGGetState, "o_slDLSSGGetState");

    clear(o_local_dlssg_slOnPluginLoad, "o_local_dlssg_slOnPluginLoad");

    clear(o_reflex_slOnPluginLoad, "o_reflex_slOnPluginLoad");
    clear(o_reflex_slSetConstants_sl1, "o_reflex_slSetConstants_sl1");
    clear(o_slReflexSetOptions, "o_slReflexSetOptions");
    clear(o_slReflexSleep, "o_slReflexSleep");

    clear(o_pcl_slOnPluginLoad, "o_pcl_slOnPluginLoad");
    clear(o_slPCLSetMarker, "o_slPCLSetMarker");

    clear(o_common_slOnPluginLoad, "o_common_slOnPluginLoad");
    clear(systemCaps, "systemCaps");
    clear(systemCapsSl15, "systemCapsSl15");
    clear(o_common_slSetParameters_sl1, "o_common_slSetParameters_sl1");
}

// Caller holds pluginMutex(plugin)
void StreamlineHooks::releaseSlot(SlPlugin plugin, size_t index, bool detach)
{
    auto& slot = pluginSlots[(size_t) plugin][index];

    if (slot.module == nullptr)
        return;

    const bool alive = isSlotAlive(slot);

    if (detach && alive)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(&(PVOID&) slot.original, (PVOID) slotDetour(plugin, index));

        if (auto r = DetourTransactionCommit(); r != NO_ERROR)
        {
            LOG_ERROR("Failed to unhook {} ({:X}): {:X}", pluginName(plugin), (size_t) slot.module, r);
            return;
        }

        LOG_DEBUG("Unhooked {} at {:X}", pluginName(plugin), (size_t) slot.module);
    }
    else if (!alive)
    {
        // Never touch the memory, it's gone or belongs to another image now
        LOG_DEBUG("{} at {:X} is no longer loaded, releasing its hook slot", pluginName(plugin), (size_t) slot.module);

        clearPluginPointersInRange(reinterpret_cast<uintptr_t>(slot.module), slot.imageSize);
    }

    const auto releasedModule = slot.module;

    slot.original = nullptr;
    slot.target = nullptr;
    slot.module = nullptr;
    slot.imageSize = 0;
    slot.path.clear();
    memset(slot.patchBytes, 0, sizeof(slot.patchBytes));

    // Point "active" module to another live copy, if any
    auto& active = activePluginModule(plugin);
    if (active == releasedModule)
    {
        active = nullptr;

        for (auto& other : pluginSlots[(size_t) plugin])
        {
            if (other.module != nullptr)
            {
                active = other.module;
                break;
            }
        }
    }
}

// Caller holds pluginMutex(plugin)
void StreamlineHooks::releaseDeadSlots(SlPlugin plugin)
{
    for (size_t i = 0; i < kMaxPluginInstances; i++)
    {
        const auto& slot = pluginSlots[(size_t) plugin][i];

        if (slot.module != nullptr && !isSlotAlive(slot))
            releaseSlot(plugin, i, false);
    }
}

// Caller holds pluginMutex(plugin)
void StreamlineHooks::hookPlugin(SlPlugin plugin, HMODULE module)
{
    const auto name = pluginName(plugin);

    if (module == nullptr)
    {
        LOG_WARN("{} module is NULL", name);
        return;
    }

    // Drop copies that Streamline already unloaded (e.g. a rejected OTA plugin)
    releaseDeadSlots(plugin);

    auto& slots = pluginSlots[(size_t) plugin];

    for (auto& slot : slots)
    {
        if (slot.module == module)
        {
            LOG_TRACE("{} at {:X} is already hooked", name, (size_t) module);
            activePluginModule(plugin) = module;
            return;
        }
    }

    auto target =
        reinterpret_cast<PFN_slGetPluginFunction>(KernelBaseProxy::GetProcAddress_()(module, "slGetPluginFunction"));

    if (target == nullptr)
    {
        LOG_WARN("{} at {:X} has no slGetPluginFunction", name, (size_t) module);
        return;
    }

    size_t index = kMaxPluginInstances;
    for (size_t i = 0; i < kMaxPluginInstances; i++)
    {
        if (slots[i].module == nullptr)
        {
            index = i;
            break;
        }
    }

    if (index == kMaxPluginInstances)
    {
        LOG_ERROR("No free hook slot for {} at {:X}, {} copies are already loaded", name, (size_t) module,
                  kMaxPluginInstances);
        return;
    }

    auto& slot = slots[index];
    slot.original = target;

    LOG_TRACE("Hooking slGetPluginFunction in {} (slot {})", name, index);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&) slot.original, (PVOID) slotDetour(plugin, index));

    if (auto r = DetourTransactionCommit(); r != NO_ERROR)
    {
        LOG_ERROR("Failed to hook {}: {:X}", name, r);
        slot.original = nullptr;
        return;
    }

    size_t imageSize = 0;
    {
        auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        if (dos->e_magic == IMAGE_DOS_SIGNATURE)
        {
            auto nt =
                reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(module) + dos->e_lfanew);
            if (nt->Signature == IMAGE_NT_SIGNATURE)
                imageSize = nt->OptionalHeader.SizeOfImage;
        }
    }

    slot.target = reinterpret_cast<void*>(target);
    slot.module = module;
    slot.imageSize = imageSize;
    slot.path = GetModulePath(module);
    memcpy(slot.patchBytes, slot.target, kPatchBytes);

    activePluginModule(plugin) = module;

    LOG_TRACE("Hooked {} at {:X} (slot {}): {}", name, (size_t) module, index, wstring_to_string(slot.path));
}

// Caller holds pluginMutex(plugin)
void StreamlineHooks::unhookPlugin(SlPlugin plugin)
{
    for (size_t i = 0; i < kMaxPluginInstances; i++)
        releaseSlot(plugin, i, true);
}

bool StreamlineHooks::isPluginHooked(SlPlugin plugin)
{
    for (const auto& slot : pluginSlots[(size_t) plugin])
    {
        if (slot.module != nullptr)
            return true;
    }

    return false;
}

void StreamlineHooks::onModuleFreed(PVOID module)
{
    if (module == nullptr)
        return;

    for (size_t p = 0; p < (size_t) SlPlugin::Count; p++)
    {
        const auto plugin = (SlPlugin) p;

        // Cheap pre-check without the lock, worst case we miss it here and releaseDeadSlots catches it later
        bool ours = false;
        for (const auto& slot : pluginSlots[p])
        {
            if (slot.module == (HMODULE) module)
            {
                ours = true;
                break;
            }
        }

        if (!ours)
            continue;

        // try_lock: never block here, FreeLibrary can be called from places we don't control.
        // If it's busy, the hook call holding it will release dead slots itself.
        std::unique_lock lock(pluginMutex(plugin), std::try_to_lock);
        if (!lock.owns_lock())
            continue;

        releaseDeadSlots(plugin);
    }
}

// SL DLSS

void StreamlineHooks::unhookDlss()
{
    LOG_FUNC();
    unhookPlugin(SlPlugin::Dlss);
}

void StreamlineHooks::hookDlss(HMODULE slDlss)
{
    LOG_FUNC();
    hookPlugin(SlPlugin::Dlss, slDlss);
}

// SL DLSSG

void StreamlineHooks::unhookDlssg()
{
    LOG_FUNC();
    unhookPlugin(SlPlugin::Dlssg);
}

void StreamlineHooks::hookDlssg(HMODULE slDlssg)
{
    LOG_FUNC();
    hookPlugin(SlPlugin::Dlssg, slDlssg);
}

// Local SL DLSSG

void StreamlineHooks::unhookLocalDlssg()
{
    LOG_FUNC();
    unhookPlugin(SlPlugin::LocalDlssg);
}

void StreamlineHooks::hookLocalDlssg(HMODULE slDlssg)
{
    LOG_FUNC();
    hookPlugin(SlPlugin::LocalDlssg, slDlssg);
}

// SL REFLEX

void StreamlineHooks::unhookReflex()
{
    LOG_FUNC();
    unhookPlugin(SlPlugin::Reflex);
}

void StreamlineHooks::hookReflex(HMODULE slReflex)
{
    LOG_FUNC();
    hookPlugin(SlPlugin::Reflex, slReflex);
}

// SL PCL

void StreamlineHooks::unhookPcl()
{
    LOG_FUNC();
    unhookPlugin(SlPlugin::Pcl);
}

void StreamlineHooks::hookPcl(HMODULE slPcl)
{
    LOG_FUNC();
    hookPlugin(SlPlugin::Pcl, slPcl);
}

// SL COMMON

void StreamlineHooks::unhookCommon()
{
    LOG_FUNC();
    unhookPlugin(SlPlugin::Common);

    systemCaps = nullptr;
    systemCapsSl15 = nullptr;
}

void StreamlineHooks::hookCommon(HMODULE slCommon)
{
    LOG_FUNC();
    hookPlugin(SlPlugin::Common, slCommon);
}

bool StreamlineHooks::isInterposerHooked() { return o_slInit != nullptr || o_slInit_sl1 != nullptr; }

bool StreamlineHooks::isDlssHooked() { return isPluginHooked(SlPlugin::Dlss); }

bool StreamlineHooks::isDlssgHooked() { return isPluginHooked(SlPlugin::Dlssg); }

bool StreamlineHooks::isLocalDlssgHooked() { return isPluginHooked(SlPlugin::LocalDlssg); }

bool StreamlineHooks::isCommonHooked() { return isPluginHooked(SlPlugin::Common); }

bool StreamlineHooks::isPclHooked() { return isPluginHooked(SlPlugin::Pcl); }

bool StreamlineHooks::isReflexHooked() { return isPluginHooked(SlPlugin::Reflex); }
