#pragma once

#include "SysUtils.h"

#include <flag-set-cpp/flag_set.hpp>

// Game specific hacks that are checked deeper in code
// Per-game config defaults and the exe -> quirk table live in GameConfigs.cpp
enum class GameQuirk : uint64_t
{
    CyberpunkHudlessState,
    FSRFGHudlessMismatchFixup, // Shader extracts UI from swapchain, alpha cut off and apply to hudless
    SkipFsr3Method,
    FastFeatureReset,
    LoadD3D12Manually,
    LoadVulkanManually,
    KernelBaseHooks,
    VulkanDLSSBarrierFixup,
    ForceUnrealEngine,
    NoFSRFGFirstSwapchain,
    FixSlSimulationMarkers,
    HitmanReflexHacks,
    SkipD3D11FeatureLevelElevation,
    CreateD3D12DeviceForLuma,
    ForceCreateD3D12Device,
    ForceDepthD32S8,
    PregmataFixDLSSModes,
    IgnoreValidUntilEvaluateForFG,
    IgnoreTagsWithoutHudlessForFG,
    ForceFGRenderSizeMVs,
    CreateSLOnThe2ndDevice,
    // Don't forget to add the new entry to printQuirks
    _
};
