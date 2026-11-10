#include "pch.h"
#include "Nvngx_RR.h"

#include <misc/CustomProviders.h>
#include <misc/IdentifyGpu.h>
#include <upscalers/dlssd/DLSSDFeature.h>
#include <imgui/ImGuiNotify.hpp>

Upscaler Nvngx_RR::Backend(API api)
{
    if (!_fallback)
        return Upscaler::DLSSD;

    return api == API::Vulkan ? Upscaler::FSR22 : Upscaler::FSR21;
}

bool Nvngx_RR::NativeAvailable()
{
    return IdentifyGpu::getPrimaryGpu().dlssCapable && State::Instance().NVNGX_DLSSD_Path.has_value();
}

Nvngx_RRDllProxy* Nvngx_RR::Find(const std::string& name, API api, bool load)
{
    std::scoped_lock lock(_modulesMutex);

    if (_failed.contains(name))
        return nullptr;

    auto it = _modules.find(name);

    if (it == _modules.end())
    {
        if (!load || !CustomProviders::IsDlssd(name))
            return nullptr;

        // Kept when it fails so it's not loaded again
        it = _modules.emplace(name, std::make_unique<Nvngx_RRDllProxy>(name)).first;
    }

    auto* module = it->second.get();
    const bool usable = api == API::Vulkan ? module->IsVulkanAvailable() : module->IsDx12Available();

    return usable ? module : nullptr;
}

std::string Nvngx_RR::SelectedName(API api, bool load)
{
    if (api != API::DX12 && api != API::Vulkan)
        return {};

    // The picked one, the same as auto when it can't run on the api
    if (auto name = Config::Instance()->CustomRRUpscaler.value_or("");
        !name.empty() && Find(name, api, load) != nullptr)
    {
        return name;
    }

    if (NativeAvailable())
        return {};

    for (const auto& name : CustomProviders::Scan().dlssd)
    {
        if (Find(name, api, load) != nullptr)
            return name;
    }

    return {};
}

bool Nvngx_RR::Listable(const std::string& name, API api)
{
    std::scoped_lock lock(_modulesMutex);

    auto it = _modules.find(name);

    // Not loaded yet, it's loaded once picked
    if (it == _modules.end())
        return true;

    return api == API::Vulkan ? it->second->IsVulkanAvailable() : it->second->IsDx12Available();
}

Nvngx_RRDllProxy* Nvngx_RR::Selected(API api, bool load)
{
    auto name = SelectedName(api, load);
    return name.empty() ? nullptr : Find(name, api, false);
}

bool Nvngx_RR::InitFailed(IFeature* feature)
{
    auto* dlssd = dynamic_cast<DLSSDFeature*>(feature);

    if (dlssd == nullptr)
        return false;

    if (const auto& name = dlssd->CustomName(); !name.empty())
    {
        LOG_WARN("Custom DLSSD {} failed, it's not used again", name);
        ImGui::InsertNotification({ ImGuiToastType::Warning, 10000, "Custom DLSSD %s failed", name.c_str() });

        std::scoped_lock lock(_modulesMutex);
        _failed.insert(name);
        return true;
    }

    LOG_WARN("DLSSD failed, ray reconstruction falls back to FSR");
    ImGui::InsertNotification({ ImGuiToastType::Warning, 10000, "DLSSD failed, falling back to FSR" });
    _fallback = true;
    return true;
}

void Nvngx_RR::ResetFailures()
{
    std::scoped_lock lock(_modulesMutex);
    _failed.clear();
    _fallback = false;
}

void Nvngx_RR::PopulateParameters(NVSDK_NGX_Parameter* InParameters, API api)
{
    // A dll resetting the map would come back here
    static thread_local bool populating = false;

    if (populating)
        return;

    auto* custom = Selected(api, false);

    if (custom == nullptr)
        return;

    populating = true;

    // Available unless the dll reports otherwise
    InParameters->Set("SuperSamplingDenoising.Available", 1);
    InParameters->Set("SuperSamplingDenoising.FeatureInitResult", 1);
    InParameters->Set("SuperSamplingDenoising.NeedsUpdatedDriver", 0);

    if (api == API::DX12 && custom->IsDx12Inited() && custom->ngx.D3D12_PopulateParameters_Impl != nullptr)
        custom->ngx.D3D12_PopulateParameters_Impl(InParameters);
    else if (api == API::Vulkan && custom->IsVulkanInited() && custom->ngx.VULKAN_PopulateParameters_Impl != nullptr)
        custom->ngx.VULKAN_PopulateParameters_Impl(InParameters);

    populating = false;
}

NVSDK_NGX_Result Nvngx_RR::ReportSupported(NVSDK_NGX_FeatureRequirement* OutSupported)
{
    OutSupported->FeatureSupported = NVSDK_NGX_FeatureSupportResult_Supported;
    OutSupported->MinHWArchitecture = 0;

    // Some old windows 10 os version
    strcpy_s(OutSupported->MinOSVersion, "10.0.10240.16384");
    return NVSDK_NGX_Result_Success;
}

std::vector<Nvngx_RRDllProxy*> Nvngx_RR::LoadedModules()
{
    std::scoped_lock lock(_modulesMutex);

    std::vector<Nvngx_RRDllProxy*> modules;

    for (auto& [name, module] : _modules)
        modules.push_back(module.get());

    return modules;
}

// Outside the lock, a dll's shutdown can come back to Find
void Nvngx_RR::ShutdownDx12(ID3D12Device* InDevice)
{
    for (auto* module : LoadedModules())
        module->ShutdownDx12(InDevice);
}

void Nvngx_RR::ShutdownVulkan(VkDevice InDevice)
{
    for (auto* module : LoadedModules())
        module->ShutdownVulkan(InDevice);
}
