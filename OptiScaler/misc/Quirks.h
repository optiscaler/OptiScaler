#pragma once

#include "pch.h"

#include <flag-set-cpp/flag_set.hpp>

enum class GameQuirk : uint64_t
{
    // Config-level quirks, de facto customized defaults
    ForceNoOptiFG,
    DisableFSR3Inputs,
    DisableFSR2Inputs,
    DisableFFXInputs,
    RestoreComputeSigOnNonNvidia,
    ForceAutoExposure,
    DisableReactiveMasks,
    DisableDxgiSpoofing,

    // Quirks that are applied deeper in code
    CyberpunkHudlessStateOverride,
    SkipFsr3Method,
    FastFeatureReset,
    LoadD3D12Manually,
    KernelBaseHooks,
    VulkanDLSSBarrierFixup,
    ForceUnrealEngine,
    // Don't forget to add the new entry to printQuirks
    _
};

struct QuirkEntry
{
    const char* exeName;
    std::initializer_list<GameQuirk> quirks;
};

#define QUIRK_ENTRY(name, ...)                                                                                         \
    {                                                                                                                  \
        name, { __VA_ARGS__ }                                                                                          \
    }

// exeName has to be lowercase
static const QuirkEntry quirkTable[] = {
    // Red Dead Redemption
    QUIRK_ENTRY("rdr.exe", GameQuirk::SkipFsr3Method, GameQuirk::ForceNoOptiFG),
    QUIRK_ENTRY("playrdr.exe", GameQuirk::SkipFsr3Method, GameQuirk::ForceNoOptiFG),

    // No Man's Sky
    QUIRK_ENTRY("nms.exe", GameQuirk::KernelBaseHooks, GameQuirk::VulkanDLSSBarrierFixup),

    // Path of Exile 2
    QUIRK_ENTRY("pathofexile.exe", GameQuirk::LoadD3D12Manually),
    QUIRK_ENTRY("pathofexile_x64.exe", GameQuirk::LoadD3D12Manually),
    QUIRK_ENTRY("pathofexilesteam.exe", GameQuirk::LoadD3D12Manually),
    QUIRK_ENTRY("pathofexile_x64steam.exe", GameQuirk::LoadD3D12Manually),

    // Crapcom Games, DLSS without dxgi spoofing needs restore compute in those
    QUIRK_ENTRY("kunitsugami.exe", GameQuirk::RestoreComputeSigOnNonNvidia, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("kunitsugamidemo.exe", GameQuirk::RestoreComputeSigOnNonNvidia, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("monsterhunterwilds.exe", GameQuirk::RestoreComputeSigOnNonNvidia, GameQuirk::DisableDxgiSpoofing),

    // Dead Rising Deluxe Remaster (including the demo)
    QUIRK_ENTRY("drdr.exe", GameQuirk::RestoreComputeSigOnNonNvidia, GameQuirk::DisableDxgiSpoofing),

    // Dragon's Dogma 2
    QUIRK_ENTRY("dd2ccs.exe", GameQuirk::RestoreComputeSigOnNonNvidia, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("dd2.exe", GameQuirk::RestoreComputeSigOnNonNvidia, GameQuirk::DisableDxgiSpoofing),

    // Red Dead Redemption 2
    QUIRK_ENTRY("rdr2.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),
    QUIRK_ENTRY("playrdr2.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // Forgive Me Father 2
    QUIRK_ENTRY("fmf2-win64-shipping.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // Revenge of the Savage Planet
    QUIRK_ENTRY("towers-win64-shipping.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // Avatar: Frontiers of Pandora
    QUIRK_ENTRY("afop.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs, GameQuirk::DisableDxgiSpoofing),

    // Forza Motorsport 8
    QUIRK_ENTRY("forza_steamworks_release_final.exe", GameQuirk::DisableFSR2Inputs,
                GameQuirk::DisableFSR3Inputs), // Steam
    QUIRK_ENTRY("forza_gaming.desktop.x64_release_final.exe", GameQuirk::DisableFSR2Inputs,
                GameQuirk::DisableFSR3Inputs), // MS Store

    // F1 22
    QUIRK_ENTRY("f1_22.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // Metal Eden
    QUIRK_ENTRY("metaleden-win64-shipping.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // Until Dawn
    QUIRK_ENTRY("bates-win64-shipping.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // Bloom and Rage
    QUIRK_ENTRY("bloom&rage.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // Star Wars: Outlaws
    QUIRK_ENTRY("outlaws.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),
    QUIRK_ENTRY("outlaws_plus.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // Banishers
    QUIRK_ENTRY("banishers-win64-shipping.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // Tiny Tina's Wonderlands
    QUIRK_ENTRY("wonderlands.exe", GameQuirk::DisableReactiveMasks),

    // Dead Island 2
    QUIRK_ENTRY("deadisland-win64-shipping.exe", GameQuirk::DisableReactiveMasks),

    // STAR WARS Jedi: Survivor
    QUIRK_ENTRY("jedisurvivor.exe", GameQuirk::ForceAutoExposure),

    // Self-explanatory
    QUIRK_ENTRY("cyberpunk2077.exe", GameQuirk::CyberpunkHudlessStateOverride, GameQuirk::ForceNoOptiFG,
                GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("witcher3.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("alanwake2.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("persistence-win64-shipping.exe", GameQuirk::ForceUnrealEngine),
    QUIRK_ENTRY("splitfiction.exe", GameQuirk::FastFeatureReset),
    QUIRK_ENTRY("minecraft.windows.exe", GameQuirk::KernelBaseHooks),
};

static flag_set<GameQuirk> getQuirksForExe(std::string exeName)
{
    to_lower_in_place(exeName);
    flag_set<GameQuirk> result;

    for (const auto& entry : quirkTable)
    {
        if (exeName == entry.exeName)
        {
            for (auto quirk : entry.quirks)
                result |= quirk;
        }
    }

    return result;
}

static void printQuirks(flag_set<GameQuirk>& quirks)
{
    if (quirks & GameQuirk::CyberpunkHudlessStateOverride)
        spdlog::info("Quirk: Fixing DLSSG's hudless in Cyberpunk");
    if (quirks & GameQuirk::SkipFsr3Method)
        spdlog::info("Quirk: Skipping first FSR 3 method");
    if (quirks & GameQuirk::FastFeatureReset)
        spdlog::info("Quirk: Quick upscaler reinit");
    if (quirks & GameQuirk::LoadD3D12Manually)
        spdlog::info("Quirk: Load d3d12.dll");
    if (quirks & GameQuirk::KernelBaseHooks)
        spdlog::info("Quirk: Enable KernelBase hooks");
    if (quirks & GameQuirk::VulkanDLSSBarrierFixup)
        spdlog::info("Quirk: Fix DLSS/DLSSG barriers on Vulkan");
    if (quirks & GameQuirk::ForceUnrealEngine)
        spdlog::info("Quirk: Force detected engine as Unreal Engine");
    if (quirks & GameQuirk::ForceNoOptiFG)
        spdlog::info("Quirk: Disabling OptiFG");
    if (quirks & GameQuirk::ForceAutoExposure)
        spdlog::info("Quirk: Enabling AutoExposure");
    if (quirks & GameQuirk::DisableFFXInputs)
        spdlog::info("Quirk: Disable FSR 3.1 Inputs");
    if (quirks & GameQuirk::DisableFSR3Inputs)
        spdlog::info("Quirk: Disable FSR 3.0 Inputs");
    if (quirks & GameQuirk::DisableFSR2Inputs)
        spdlog::info("Quirk: Disable FSR 2.X Inputs");
    if (quirks & GameQuirk::DisableReactiveMasks)
        spdlog::info("Quirk: Disable Reactive Masks");
    if (quirks & GameQuirk::RestoreComputeSigOnNonNvidia)
        spdlog::info("Quirk: Enabling restore compute signature on AMD/Intel");
    if (quirks & GameQuirk::DisableDxgiSpoofing)
        spdlog::info("Quirk: Dxgi spoofing disabled by default");

    return;
}
