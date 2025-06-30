#pragma once

#include <flag-set-cpp/flag_set.hpp>
#include "pch.h"

enum class GameQuirk
{
    // Config-level quirks, de facto customized defaults
    ForceNoOptiFG,
    DisableFSR3Inputs,
    DisableFSR2PatternInputs,
    RestoreComputeSigOnNonNvidia,

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
    // Red Dead Redemption 2
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
    QUIRK_ENTRY("kunitsugami.exe", GameQuirk::RestoreComputeSigOnNonNvidia),
    QUIRK_ENTRY("kunitsugamidemo.exe", GameQuirk::RestoreComputeSigOnNonNvidia),
    QUIRK_ENTRY("monsterhunterrise.exe", GameQuirk::RestoreComputeSigOnNonNvidia),
    QUIRK_ENTRY("monsterhunterwilds.exe", GameQuirk::RestoreComputeSigOnNonNvidia),
    // Dead Rising Deluxe Remaster (including the demo)
    QUIRK_ENTRY("drdr.exe", GameQuirk::RestoreComputeSigOnNonNvidia),
    // Dragon's Dogma 2
    QUIRK_ENTRY("dd2ccs.exe", GameQuirk::RestoreComputeSigOnNonNvidia),
    QUIRK_ENTRY("dd2.exe", GameQuirk::RestoreComputeSigOnNonNvidia),

    // Forgive Me Father 2
    QUIRK_ENTRY("fmf2-win64-shipping.exe", GameQuirk::DisableFSR3Inputs),

    QUIRK_ENTRY("cyberpunk2077.exe", GameQuirk::CyberpunkHudlessStateOverride, GameQuirk::ForceNoOptiFG),
    QUIRK_ENTRY("persistence-win64-shipping.exe", GameQuirk::ForceUnrealEngine),
    QUIRK_ENTRY("banishers-win64-shipping.exe", GameQuirk::DisableFSR2PatternInputs),
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
    if (quirks & GameQuirk::DisableFSR3Inputs)
        spdlog::info("Quirk: Disable FSR 3.0 Inputs");
    if (quirks & GameQuirk::DisableFSR2PatternInputs)
        spdlog::info("Quirk: Disable FSR 2 Pattern Inputs");
    if (quirks & GameQuirk::RestoreComputeSigOnNonNvidia)
        spdlog::info("Quirk: Enabling restore compute signature on AMD/Intel");

    return;
}