#include "pch.h"
#include "FSR4Models.h"

#include <misc/IdentifyGpu.h>
#include <scanner/rtti.h>

namespace
{
struct ModelProvider
{
    FSR4Model model;
    const char* name;
    const char* className;

    // FFX limits some models to a single GPU generation, while every FSR 4 capable GPU has the math they need
    bool ignoreSupportCheck;
};

// In the order FFX checks them, a new model only needs a new entry here and in FSR4Model
constexpr ModelProvider modelProviders[] = {
    { FSR4Model::FP8, "FP8", "ffxProvider_FSR4", false },
    { FSR4Model::INT8, "INT8", "ffxProvider_FSR4_Int8", true }, // FFX allows it only on RDNA 3
    { FSR4Model::INT8_2, "INT8 RDNA2", "ffxProvider_FSR4_Int8_Navi2", true },
    { FSR4Model::INT8Lite, "INT8 Lite", "ffxProvider_FSR4Lite_Int8", true },
};

// ffxProvider::IsSupported(void* device), the vtable layout of FFX SDK 2.0+ providers
constexpr size_t isSupportedIndex = 2;

typedef bool (*PFN_IsSupported)(void* provider, void* device);

struct PatchedProvider
{
    void** vtable;
    const ModelProvider* provider;
    PFN_IsSupported o_IsSupported;
    std::optional<bool> lastReportedSupport;
};

struct HookedModule
{
    HMODULE module;
    std::vector<FSR4Model> models;

    // Only filled when the module has several models to choose from
    std::vector<PatchedProvider> patchedProviders;

    bool HasModel(FSR4Model model) const { return std::ranges::find(models, model) != models.end(); }
};

// Entries never move, the support check keeps a pointer to them while the lock is released
std::deque<HookedModule> hookedModules;
std::mutex hookMutex;

const ModelProvider& Provider(FSR4Model model)
{
    return *std::ranges::find(modelProviders, model, &ModelProvider::model);
}

HookedModule* FindHookedModule(HMODULE module)
{
    auto hooked = std::ranges::find(hookedModules, module, &HookedModule::module);
    return hooked != hookedModules.end() ? &*hooked : nullptr;
}

// The INT8 model from GPU detection, FFX's own checks can reject it (unofficial support, other asic family)
std::optional<FSR4Model> DetectedModel()
{
    switch (IdentifyGpu::getPrimaryGpu().fsr4Support)
    {
    case FSR4Support::INT8:
        return FSR4Model::INT8;
    case FSR4Support::INT8_2:
        return FSR4Model::INT8_2;
    case FSR4Support::INT8Lite:
        return FSR4Model::INT8Lite;
    default:
        return std::nullopt;
    }
}

bool hkIsSupported(void* provider, void* device)
{
    auto vtable = *static_cast<void***>(provider);

    std::unique_lock lock(hookMutex);

    for (auto& hooked : hookedModules)
    {
        auto found = std::ranges::find(hooked.patchedProviders, vtable, &PatchedProvider::vtable);
        if (found == hooked.patchedProviders.end())
            continue;

        auto patched = &*found;

        auto model = patched->provider->model;
        auto o_IsSupported = patched->o_IsSupported;
        // Without a selected model use the detected one, as if it was selected
        auto detected = DetectedModel();
        std::optional<FSR4Model> selected = Config::Instance()->Fsr4Model;
        if (!selected.has_value())
            selected = detected;

        auto useSelected = selected.has_value() && hooked.HasModel(*selected) && FSR4Models::CanGpuRun(*selected);
        lock.unlock();

        auto supported = o_IsSupported(provider, device);

        if (useSelected)
            supported = model == *selected && (supported || Provider(model).ignoreSupportCheck);

        // FFX picks FP8 wherever the device reports it, emulated FP8 on Linux included
        // When the module lacks the detected INT8 model don't let it fall to FP8, FP8 has to be selected
        else if (model == FSR4Model::FP8 && detected.has_value())
            supported = false;

        LOG_DEBUG("{}: {}", Provider(model).name, supported);

        if (device == nullptr)
            return supported;

        lock.lock();
        patched->lastReportedSupport = supported;

        return supported;
    }

    return false;
}

bool PatchVtableEntry(void** entry, void* value)
{
    DWORD oldProtection = 0;
    if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &oldProtection))
        return false;

    *entry = value;

    VirtualProtect(entry, sizeof(*entry), oldProtection, &oldProtection);
    return true;
}

} // namespace

void FSR4Models::Hook(HMODULE module)
{
    if (module == nullptr)
        return;

    std::scoped_lock lock(hookMutex);

    if (FindHookedModule(module) != nullptr)
        return;

    HookedModule hooked { module };
    std::vector<PatchedProvider> found;

    for (auto& provider : modelProviders)
    {
        if (auto vtable = rtti::FindVtable(module, provider.className))
        {
            hooked.models.push_back(provider.model);
            found.push_back({ vtable, &provider, nullptr });
        }
    }

    auto moduleName = wstring_to_string(Util::ModulePath(module).filename().wstring());
    LOG_DEBUG("{} has {} FSR 4 models", moduleName, hooked.models.size());

    // Older modules have a single model and a different vtable layout
    if (found.size() >= 2)
    {
        for (auto& provider : found)
        {
            auto entry = &provider.vtable[isSupportedIndex];
            provider.o_IsSupported = reinterpret_cast<PFN_IsSupported>(*entry);

            if (!PatchVtableEntry(entry, reinterpret_cast<void*>(&hkIsSupported)))
            {
                LOG_ERROR("Failed to patch the support check of {} in {}", provider.provider->className, moduleName);
                continue;
            }

            LOG_INFO("Hooked the support check of {} in {}", provider.provider->className, moduleName);
            hooked.patchedProviders.push_back(provider);
        }
    }

    hookedModules.push_back(std::move(hooked));
}

bool FSR4Models::ModuleHasModel(HMODULE module, FSR4Model model)
{
    {
        std::scoped_lock lock(hookMutex);

        if (auto hooked = FindHookedModule(module))
            return hooked->HasModel(model);
    }

    return rtti::FindVtable(module, Provider(model).className) != nullptr;
}

bool FSR4Models::CanGpuRun(FSR4Model model)
{
    auto gpu = IdentifyGpu::getPrimaryGpu();

    switch (model)
    {
    case FSR4Model::FP8:
        // FP8 forced through Fsr4GpuSupport, RDNA 4, or emulated on Linux (e.g. RDNA 3 with the WMMA workaround)
        return gpu.fsr4Support == FSR4Support::FP8 || gpu.realFsr4Support == FSR4Support::FP8;

    case FSR4Model::INT8:
    case FSR4Model::INT8_2:
        return gpu.fsr4Support != FSR4Support::None;

    case FSR4Model::INT8Lite:
        return true;

    default:
        return false;
    }
}

std::vector<FSR4Model> FSR4Models::SelectableModels(HMODULE module)
{
    std::scoped_lock lock(hookMutex);

    std::vector<FSR4Model> models;

    if (auto hooked = FindHookedModule(module))
    {
        for (auto& patched : hooked->patchedProviders)
        {
            if (CanGpuRun(patched.provider->model))
                models.push_back(patched.provider->model);
        }
    }

    if (models.size() < 2)
        models.clear();

    return models;
}

std::optional<FSR4Model> FSR4Models::ActiveModel(HMODULE module)
{
    std::scoped_lock lock(hookMutex);

    auto hooked = FindHookedModule(module);
    if (hooked == nullptr)
        return std::nullopt;

    // Nothing to pick from, FFX runs the only model the module has
    if (hooked->patchedProviders.empty())
    {
        if (hooked->models.size() == 1 && CanGpuRun(hooked->models.front()))
            return hooked->models.front();

        return std::nullopt;
    }

    // FFX stops at the first supported model, the providers are in the order it checks them
    for (auto& patched : hooked->patchedProviders)
    {
        if (patched.lastReportedSupport.value_or(false))
            return patched.provider->model;
    }

    return std::nullopt;
}

const char* FSR4Models::Name(FSR4Model model) { return Provider(model).name; }
