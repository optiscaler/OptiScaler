#pragma once

#include "pch.h"

#include <flag-set-cpp/flag_set.hpp>

enum class GameQuirk : uint64_t
{
    // Config-level quirks, de facto customized defaults
    ForceNoUpscalerFGInputs,
    DisableFSR3Inputs,
    DisableFSR2Inputs,
    DisableFFXInputs,
    RestoreComputeSigOnNonNvidia,
    ForceAutoExposure,
    DisableReactiveMasks,
    DisableDxgiSpoofing,
    DisableUseFsrInputValues,
    EnableVulkanSpoofing,
    EnableVulkanExtensionSpoofing,
    DisableOptiXessPipelineCreation,
    DontUseNTShared,
    DontUseUnrealBarriers,
    SkipFirst10Frames,

    // Quirks that are applied deeper in code
    CyberpunkHudlessStateOverride,
    SkipFsr3Method,
    FastFeatureReset,
    LoadD3D12Manually,
    KernelBaseHooks,
    VulkanDLSSBarrierFixup,
    ForceUnrealEngine,
    NoFSRFGFirstSwapchain,
    SetConstantsMarksNewFrame,
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
    QUIRK_ENTRY("rdr.exe", GameQuirk::SkipFsr3Method, GameQuirk::SetConstantsMarksNewFrame,
                GameQuirk::NoFSRFGFirstSwapchain),
    QUIRK_ENTRY("playrdr.exe", GameQuirk::SkipFsr3Method, GameQuirk::SetConstantsMarksNewFrame,
                GameQuirk::NoFSRFGFirstSwapchain),

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
    QUIRK_ENTRY("monsterhunterrise.exe", GameQuirk::RestoreComputeSigOnNonNvidia), // Seems to fix real DLSS

    // Crysis 3 Remastered
    // no spoof needed for DLSS inputs
    QUIRK_ENTRY("crysis3remastered.exe", GameQuirk::DisableDxgiSpoofing),

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

    // Forza Horizon 5
    // SL spoof enough to unlock everything DLSS
    QUIRK_ENTRY("forzahorizon5.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs,
                GameQuirk::DisableDxgiSpoofing),

    // Revenge of the Savage Planet
    QUIRK_ENTRY("towers-win64-shipping.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // Avatar: Frontiers of Pandora
    QUIRK_ENTRY("afop.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs, GameQuirk::DisableDxgiSpoofing),

    // Forza Motorsport 8
    // Steam
    QUIRK_ENTRY("forza_steamworks_release_final.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),
    // MS Store
    QUIRK_ENTRY("forza_gaming.desktop.x64_release_final.exe", GameQuirk::DisableFSR2Inputs,
                GameQuirk::DisableFSR3Inputs),

    // F1 22
    QUIRK_ENTRY("f1_22.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // Metal Eden
    QUIRK_ENTRY("metaleden-win64-shipping.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // Until Dawn
    QUIRK_ENTRY("bates-win64-shipping.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // Bloom and Rage
    QUIRK_ENTRY("bloom&rage.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // 171
    QUIRK_ENTRY("bcg-win64-shipping.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // Microsoft Flight Simulator (2020), MSFS2020
    QUIRK_ENTRY("flightsimulator.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // Star Wars: Outlaws
    QUIRK_ENTRY("outlaws.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),
    QUIRK_ENTRY("outlaws_plus.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // Banishers
    QUIRK_ENTRY("banishers-win64-shipping.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // Tiny Tina's Wonderlands
    // FSR2 only, no spoof needed
    QUIRK_ENTRY("wonderlands.exe", GameQuirk::DisableReactiveMasks, GameQuirk::DisableDxgiSpoofing),

    // Dead Island 2
    // FSR2 only, no spoof needed
    QUIRK_ENTRY("deadisland-win64-shipping.exe", GameQuirk::DisableReactiveMasks, GameQuirk::DisableDxgiSpoofing),

    // Rune Factory Guardians of Azuma
    QUIRK_ENTRY("game-win64-shipping.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // STAR WARS Jedi: Survivor
    QUIRK_ENTRY("jedisurvivor.exe", GameQuirk::ForceAutoExposure),

    // Death Stranding and Directors Cut
    // no spoof needed for DLSS inputs
    QUIRK_ENTRY("ds.exe", GameQuirk::DisableDxgiSpoofing),

    // The Callisto Protocol
    // FSR2 only, no spoof needed
    QUIRK_ENTRY("thecallistoprotocol-win64-shipping.exe", GameQuirk::DisableUseFsrInputValues,
                GameQuirk::DisableDxgiSpoofing, GameQuirk::DisableReactiveMasks, GameQuirk::ForceAutoExposure),

    // HITMAN World of Assassination
    // SL spoof enough to unlock everything DLSS
    QUIRK_ENTRY("hitman3.exe", GameQuirk::DisableDxgiSpoofing),

    // Marvel's Guardians of the Galaxy
    // no spoof needed for DLSS inputs
    QUIRK_ENTRY("gotg.exe", GameQuirk::DisableDxgiSpoofing),

    // ELDEN RING NIGHTREIGN
    // no spoof needed for DLSS inputs
    QUIRK_ENTRY("nightreign.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::DisableOptiXessPipelineCreation),

    // Returnal
    // no spoof needed for DLSS inputs, but no DLSSG and Reflex
    QUIRK_ENTRY("returnal-win64-shipping.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::DontUseUnrealBarriers),

    // WUCHANG: Fallen Feathers
    // Skip 1 frame use of upscaler which cause crash
    QUIRK_ENTRY("project_plague-deck-shipping.exe", GameQuirk::SkipFirst10Frames),
    QUIRK_ENTRY("project_plague-win64-shipping.exe", GameQuirk::SkipFirst10Frames),

    // UNCHARTED: Legacy of Thieves Collection
    // no spoof needed for DLSS inputs
    QUIRK_ENTRY("u4.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("u4-l.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("tll.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("tll-l.exe", GameQuirk::DisableDxgiSpoofing),

    // SL spoof enough to unlock everything DLSS
    QUIRK_ENTRY("cyberpunk2077.exe", GameQuirk::CyberpunkHudlessStateOverride,
                GameQuirk::ForceNoUpscalerFGInputs, // TODO: maybe disable just hudfix?
                GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("witcher3.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("alanwake2.exe", GameQuirk::DisableDxgiSpoofing),

    // Self-explanatory
    QUIRK_ENTRY("persistence-win64-shipping.exe", GameQuirk::ForceUnrealEngine),
    QUIRK_ENTRY("splitfiction.exe", GameQuirk::FastFeatureReset),
    QUIRK_ENTRY("minecraft.windows.exe", GameQuirk::KernelBaseHooks),
    QUIRK_ENTRY("prey.exe", GameQuirk::DontUseNTShared, GameQuirk::DisableOptiXessPipelineCreation,
                GameQuirk::DisableDxgiSpoofing),

    // VULKAN
    // ------

    // No Man's Sky
    QUIRK_ENTRY("nms.exe", GameQuirk::EnableVulkanSpoofing, GameQuirk::EnableVulkanExtensionSpoofing),

    // RTX Remix
    QUIRK_ENTRY("nvremixbridge.exe", GameQuirk::EnableVulkanSpoofing, GameQuirk::EnableVulkanExtensionSpoofing),

    // Enshrouded
    QUIRK_ENTRY("enshrouded.exe", GameQuirk::EnableVulkanSpoofing, GameQuirk::EnableVulkanExtensionSpoofing),

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
    if (quirks & GameQuirk::ForceNoUpscalerFGInputs)
        spdlog::info("Quirk: Disabling OptiFG (upscaler inputs for FG)");
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
    if (quirks & GameQuirk::DisableDxgiSpoofing)
        spdlog::info("Quirk: Disable Use FSR Input Values");
    if (quirks & GameQuirk::DisableOptiXessPipelineCreation)
        spdlog::info("Quirk: Disable custom pipeline creation for XeSS");
    if (quirks & GameQuirk::DontUseNTShared)
        spdlog::info("Quirk: Don't use NTShared enabled");
    if (quirks & GameQuirk::DontUseUnrealBarriers)
        spdlog::info("Quirk: Don't use resource barrier fix for Unreal Engine games");
    if (quirks & GameQuirk::SkipFirst10Frames)
        spdlog::info("Quirk: Skipping upscaling for first 10 frames");
    if (quirks & GameQuirk::NoFSRFGFirstSwapchain)
        spdlog::info("Quirk: Skip turning the first swapchain created into an FSR swapchain");
    if (quirks & GameQuirk::SetConstantsMarksNewFrame)
        spdlog::info("Quirk: slSetConstants marks new frame");

    return;
}
