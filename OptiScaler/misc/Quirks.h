#pragma once

#include "SysUtils.h"

#include <flag-set-cpp/flag_set.hpp>

enum class GameQuirk : uint64_t
{
    // Config-level quirks, de facto customized defaults
    DisableHudfix,
    DisableFSR3Inputs,
    DisableFSR2Inputs,
    DisableFFXInputs,
    RestoreComputeSigOnNonNvidia,
    RestoreComputeSigOnNvidia,
    ExtendedSigRestore,
    ForceAutoExposure,
    DisableReactiveMasks,
    DisableDxgiSpoofing,
    DisableUseFsrInputValues,
    EnableVulkanSpoofing,
    EnableVulkanExtensionSpoofing,
    DisableOptiXessPipelineCreation,
    DontUseNTShared,
    SkipFirst10Frames,
    DisableVsyncOverride,
    DontUseNtDllHooks,
    UseFSR2PatternMatching,
    AlwaysCaptureFSRFGSwapchain,
    AllowedFrameAhead2,
    DisableXeFGChecks,
    UseFsr2Dx11Inputs,
    UseFsr2VulkanInputs,
    ForceBorderlessWhenUsingXeFG,
    OverrideVsyncWhenUsingXeFG,
    DisableResizeSkip,
    SpoofRegistry,
    DisableFakenvapi,
    DoNotPreserveFGSwapChain,
    DoNotSkipResize,
    OldOverlayMenu,
    UseManualInputs,
    DoNotLoadAmdxc64,
    DontUseUnrealMVBarriers,
    DontUseUnrealColorBarriers,

    // Quirks that are applied deeper in code
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
    ForceFGRenderSizeMVs,
    // Don't forget to add the new entry to printQuirks
    _
};

struct QuirkEntry
{
    const char* exeName;
    std::initializer_list<GameQuirk> quirks;
};

// For regular exes
#define QUIRK_ENTRY(name, ...)                                                                                         \
    {                                                                                                                  \
        name, { __VA_ARGS__ }                                                                                          \
    }

// For UE exes
#define QUIRK_ENTRY_UE(name, ...)                                                                                      \
    { #name "-win64-shipping.exe", { __VA_ARGS__ } },                                                                  \
    {                                                                                                                  \
        #name "-wingdk-shipping.exe", { __VA_ARGS__ }                                                                  \
    }

// exeName has to be lowercase
static const QuirkEntry quirkTable[] = {

    // Red Dead Redemption 2
    // Spoofing causes FSR2 inputs crash, DLSS inputs need OptiPatcher to avoid artifacts/crashes anyway
    QUIRK_ENTRY("rdr2.exe", GameQuirk::DisableFSR3Inputs, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("playrdr2.exe", GameQuirk::DisableFSR3Inputs, GameQuirk::DisableDxgiSpoofing),

    // Red Dead Redemption
    // Preserving the FG swapchain causes crashes with DLSSG via SL
    QUIRK_ENTRY("rdr.exe", GameQuirk::SkipFsr3Method, GameQuirk::NoFSRFGFirstSwapchain, GameQuirk::DisableDxgiSpoofing,
                GameQuirk::DoNotPreserveFGSwapChain),
    QUIRK_ENTRY("playrdr.exe", GameQuirk::SkipFsr3Method, GameQuirk::NoFSRFGFirstSwapchain,
                GameQuirk::DisableDxgiSpoofing, GameQuirk::DoNotPreserveFGSwapChain),

    // Visions of Mana
    // Use FSR2 Pattern Matching to fix broken FSR2 detection
    QUIRK_ENTRY_UE(visionsofmana, GameQuirk::UseFSR2PatternMatching, GameQuirk::DisableDxgiSpoofing),

    // Silent Hill f
    QUIRK_ENTRY_UE(shf, GameQuirk::AlwaysCaptureFSRFGSwapchain),

    // Tainted Grail - Fall of Avalon
    QUIRK_ENTRY("fall of avalon.exe", GameQuirk::ForceAutoExposure),

    // Path of Exile 2
    QUIRK_ENTRY("pathofexile.exe", GameQuirk::LoadD3D12Manually, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("pathofexile_x64.exe", GameQuirk::LoadD3D12Manually, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("pathofexile_kg.exe", GameQuirk::LoadD3D12Manually, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("pathofexile_x64_kg.exe", GameQuirk::LoadD3D12Manually, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("pathofexilesteam.exe", GameQuirk::LoadD3D12Manually, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("pathofexile_x64steam.exe", GameQuirk::LoadD3D12Manually, GameQuirk::DisableDxgiSpoofing),

    // Where Winds Meet
    // SL spoof enough to unlock everything DLSS, required to avoid forced DLSS dilated MVs
    QUIRK_ENTRY("wwm.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::ForceFGRenderSizeMVs),

    // Arknights: Endfield
    // Reflex hooking crashes the game
    QUIRK_ENTRY("endfield.exe", GameQuirk::ForceCreateD3D12Device, GameQuirk::DisableFakenvapi),

    // Neverness to Everness
    // Kernel hooks required to unlock DLSS inputs
    QUIRK_ENTRY("htgame.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs, GameQuirk::DontUseNtDllHooks),

    // Wuthering Waves
    // Kernel hooks required to unlock DLSS inputs, DoNotPreserveFGSwapChain for fixing XeFG
    QUIRK_ENTRY_UE(client, GameQuirk::DontUseNtDllHooks, GameQuirk::DoNotPreserveFGSwapChain),

    // Zenless Zone Zero
    // IgnoreValidUntilEvaluateForFG fixes flipped Unity MVs/Depth
    QUIRK_ENTRY("zenlesszonezero.exe", GameQuirk::IgnoreValidUntilEvaluateForFG),

    // Trails in the Sky 1st Chapter
    QUIRK_ENTRY("sora_1st.exe", GameQuirk::UseFsr2Dx11Inputs, GameQuirk::DisableDxgiSpoofing),

    // NINJA GAIDEN 4
    // No spoof needed for DLSS inputs
    QUIRK_ENTRY("ninjagaiden4-steam.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::DisableResizeSkip,
                GameQuirk::DoNotPreserveFGSwapChain),
    QUIRK_ENTRY("ninjagaiden4-wingdk.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::DisableResizeSkip,
                GameQuirk::DoNotPreserveFGSwapChain),

    // DEAD OR ALIVE 6 Last Round
    // No spoof needed for DLSS inputs
    QUIRK_ENTRY("doa6lr.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::DisableResizeSkip,
                GameQuirk::DoNotPreserveFGSwapChain),

    // The Last of Us Part I
    QUIRK_ENTRY("tlou-i.exe", GameQuirk::AllowedFrameAhead2, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("tlou-i-l.exe", GameQuirk::AllowedFrameAhead2, GameQuirk::DisableDxgiSpoofing),

    // Horizon Forbidden West
    QUIRK_ENTRY("horizonforbiddenwest.exe", GameQuirk::AllowedFrameAhead2),

    // Crapcom Games, DLSS without dxgi spoofing needs restore compute in those
    //
    // Kunitsu-Gami: Path of the Goddess, Monster Hunter Wilds, MONSTER HUNTER RISE, Dead Rising Deluxe Remaster
    // (including the demo), Dragon's Dogma 2, PRAGMATA Demo, Resident Evil Requiem, Monster Hunter Stories 3: Twisted
    // Reflection, PRAGMATA, Onimusha: Way of the Sword Demo
    QUIRK_ENTRY("kunitsugami.exe", GameQuirk::RestoreComputeSigOnNonNvidia, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("kunitsugamidemo.exe", GameQuirk::RestoreComputeSigOnNonNvidia, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("monsterhunterwilds.exe", GameQuirk::RestoreComputeSigOnNonNvidia, GameQuirk::DisableDxgiSpoofing,
                GameQuirk::RestoreComputeSigOnNvidia),
    QUIRK_ENTRY("monsterhunterrise.exe", GameQuirk::RestoreComputeSigOnNvidia,
                GameQuirk::RestoreComputeSigOnNonNvidia), // AMD/Intel need spoofing, Restoresig seems to fix real DLSS
    QUIRK_ENTRY("drdr.exe", GameQuirk::RestoreComputeSigOnNonNvidia, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("dd2ccs.exe", GameQuirk::RestoreComputeSigOnNonNvidia, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("dd2.exe", GameQuirk::RestoreComputeSigOnNonNvidia, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("pragmata_sketchbook.exe", GameQuirk::RestoreComputeSigOnNonNvidia, GameQuirk::DisableDxgiSpoofing,
                GameQuirk::AllowedFrameAhead2, GameQuirk::PregmataFixDLSSModes),
    QUIRK_ENTRY("re9.exe", GameQuirk::RestoreComputeSigOnNonNvidia, GameQuirk::DisableDxgiSpoofing,
                GameQuirk::RestoreComputeSigOnNvidia),
    QUIRK_ENTRY("monster_hunter_stories_3_twisted_reflection.exe", GameQuirk::RestoreComputeSigOnNonNvidia,
                GameQuirk::DisableDxgiSpoofing, GameQuirk::RestoreComputeSigOnNvidia),
    QUIRK_ENTRY("pragmata.exe", GameQuirk::RestoreComputeSigOnNonNvidia, GameQuirk::DisableDxgiSpoofing,
                GameQuirk::RestoreComputeSigOnNvidia, GameQuirk::PregmataFixDLSSModes),
    QUIRK_ENTRY("onimushawots_demo.exe", GameQuirk::RestoreComputeSigOnNonNvidia, GameQuirk::DisableDxgiSpoofing,
                GameQuirk::RestoreComputeSigOnNvidia),

    // REF PDUpscaler branch
    // Old menu needed to avoid the invisible overlay while upscaling is active
    QUIRK_ENTRY("re2.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::OldOverlayMenu),
    QUIRK_ENTRY("re3.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::OldOverlayMenu),
    QUIRK_ENTRY("re4.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::OldOverlayMenu),
    QUIRK_ENTRY("re7.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::OldOverlayMenu),
    QUIRK_ENTRY("re8.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::OldOverlayMenu),
    QUIRK_ENTRY("devilmaycry5.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::OldOverlayMenu),
    QUIRK_ENTRY("streetfighter6.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::OldOverlayMenu),

    // Cyberpunk 2077
    // SL spoof enough to unlock everything DLSS
    QUIRK_ENTRY("cyberpunk2077.exe", GameQuirk::CyberpunkHudlessState, GameQuirk::FSRFGHudlessMismatchFixup,
                GameQuirk::DisableHudfix, GameQuirk::DisableDxgiSpoofing),

    // Forza Horizon 5
    // SL spoof enough to unlock everything DLSS
    QUIRK_ENTRY("forzahorizon5.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs,
                GameQuirk::DisableDxgiSpoofing),

    // Avatar: Frontiers of Pandora
    // SL spoof enough to unlock DLSSG, blocked spoofing due to broken RT/performance overhead
    QUIRK_ENTRY("afop.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("afop_plus.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs,
                GameQuirk::DisableDxgiSpoofing),

    // Forza Motorsport 8
    // Steam
    QUIRK_ENTRY("forza_steamworks_release_final.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),
    // MS Store
    QUIRK_ENTRY("forza_gaming.desktop.x64_release_final.exe", GameQuirk::DisableFSR2Inputs,
                GameQuirk::DisableFSR3Inputs),

    // Death Stranding and Directors Cut
    // no spoof needed for DLSS inputs
    QUIRK_ENTRY("ds.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // Duet Night Abyss
    QUIRK_ENTRY("em-win64-shipping.exe", GameQuirk::DontUseNtDllHooks),

    // The Talos Principle 2
    QUIRK_ENTRY("talos2-win64-shipping.exe", GameQuirk::DoNotPreserveFGSwapChain, GameQuirk::DoNotSkipResize),

    // The Callisto Protocol
    // FSR2 only, no spoof needed
    QUIRK_ENTRY_UE(thecallistoprotocol, GameQuirk::DisableUseFsrInputValues, GameQuirk::DisableDxgiSpoofing,
                   GameQuirk::DisableReactiveMasks, GameQuirk::ForceAutoExposure),

    // HITMAN World of Assassination
    // SL spoof enough to unlock everything DLSS
    QUIRK_ENTRY("hitman3.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::HitmanReflexHacks,
                GameQuirk::DisableFSR2Inputs),

    // 007 First Light
    // SL spoof enough to unlock everything DLSS, uses bindless so restoring compute is complicated
    QUIRK_ENTRY("007firstlight.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::RestoreComputeSigOnNonNvidia,
                GameQuirk::RestoreComputeSigOnNvidia, GameQuirk::ExtendedSigRestore,
                GameQuirk::IgnoreValidUntilEvaluateForFG, GameQuirk::DoNotLoadAmdxc64),

    // ELDEN RING (for ERSS mod) and ER NIGHTREIGN (for NRSS mod)
    // no spoof needed for DLSS inputs
    QUIRK_ENTRY("eldenring.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("nightreign.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::DisableOptiXessPipelineCreation),

    // Returnal
    // no spoof needed for DLSS inputs, but no DLSSG and Reflex
    QUIRK_ENTRY_UE(returnal, GameQuirk::DisableDxgiSpoofing, GameQuirk::DontUseUnrealMVBarriers,
                   GameQuirk::DontUseUnrealColorBarriers),

    // WUCHANG: Fallen Feathers
    // Skip 1 frame use of upscaler which cause crash
    QUIRK_ENTRY("project_plague-deck-shipping.exe", GameQuirk::SkipFirst10Frames),
    QUIRK_ENTRY("project_plague-win64-shipping.exe", GameQuirk::SkipFirst10Frames),

    // Final Fantasy XIV
    QUIRK_ENTRY("ffxiv_dx11.exe", GameQuirk::DisableVsyncOverride),
    QUIRK_ENTRY("graphadapterdesc.exe", GameQuirk::SkipD3D11FeatureLevelElevation),

    // Prey 2017
    // Requires Prey Luma Remastered mod for upscalers
    QUIRK_ENTRY("prey.exe", GameQuirk::DontUseNTShared, GameQuirk::DisableOptiXessPipelineCreation,
                GameQuirk::DisableDxgiSpoofing),

    // Black Myth: Wukong
    // To enable DLSS-FG option
    QUIRK_ENTRY_UE(b1, GameQuirk::SpoofRegistry),

    // Avowed
    // NoColorBarrier needed to avoid post-loading crash with DLSS, AE required to fix FSR4 ghosting
    QUIRK_ENTRY_UE(avowed, GameQuirk::ForceAutoExposure, GameQuirk::DontUseUnrealColorBarriers,
                   GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // Starfield
    // SL spoof enough to unlock everything DLSS, Depth and Velocity needed to avoid FG artifacts
    QUIRK_ENTRY("starfield.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs,
                GameQuirk::DisableDxgiSpoofing, GameQuirk::ForceAutoExposure),

    // Nixxes Sony ports - Dxgi spoofing disabled due to RT crashes
    //
    // Ratchet & Clank: Rift Apart, Marvel’s Spider-Man Remastered, Marvel’s Spider-Man: Miles Morales, Marvel's
    // Spider-Man 2, DEATH STRANDING 2: ON THE BEACH
    QUIRK_ENTRY("riftapart.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("spider-man.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("milesmorales.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("spider-man2.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("ds2.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::FSRFGHudlessMismatchFixup),
    //
    // Dxgi spoofing disabled, DLSS-FG available, standalone Reflex can be unlocked with -unlockReflexOptions launch
    // option if needed
    // Horizon Zero Dawn Remastered, Horizon Forbidden West Complete Edition, Ghost of Tsushima DIRECTOR'S CUT, The Last
    // of Us Part II Remastered
    QUIRK_ENTRY("horizonzerodawnremastered.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("horizonforbiddenwest.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("ghostoftsushima.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("tlou-ii.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("tlou-ii-l.exe", GameQuirk::DisableDxgiSpoofing),

    // Dead Space Remake
    // Override Vsync required to avoid crash on boot
    QUIRK_ENTRY("dead space.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::OverrideVsyncWhenUsingXeFG,
                GameQuirk::ForceBorderlessWhenUsingXeFG),

    // Metro Exodus Enhanced Edition
    // ForceBorderless required to avoid black screen with XeFG, Manual Input polling for fixing invisible Opti Overlay
    QUIRK_ENTRY("metroexodus.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::ForceBorderlessWhenUsingXeFG,
                GameQuirk::ForceAutoExposure, GameQuirk::UseManualInputs),

    // Star Wars: Outlaws
    // SL spoof enough to unlock everything DLSS
    QUIRK_ENTRY("outlaws.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs,
                GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("outlaws_plus.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs,
                GameQuirk::DisableDxgiSpoofing),

    // Lies of P
    // Spoofing disabled as no Streamline and OptiPatcher unlocks DLSS anyway
    QUIRK_ENTRY_UE(lop, GameQuirk::DisableDxgiSpoofing),

    // Crimson Desert
    // Spoofing disabled due to "unsupported GPU" error
    QUIRK_ENTRY("crimsondesert.exe", GameQuirk::DisableDxgiSpoofing),

    // Assassin's Creed Mirage
    // Game not loading SL plugin even while spoofing, also avoids the "unsupported video driver" notification
    QUIRK_ENTRY("acmirage.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("acmirage_plus.exe", GameQuirk::DisableDxgiSpoofing),

    // DCS World
    // Fakenvapi seems to cause a crash when switching to FSR4 (INT8 only?)
    QUIRK_ENTRY("dcs.exe", GameQuirk::DisableFakenvapi),

    // S.T.A.L.K.E.R.: Legends of the Zone Trilogy - Enhanced Editions
    // Manual input polling for fixing invisible Opti Overlay, no spoof needed for DLSS inputs
    QUIRK_ENTRY("xrengine.exe", GameQuirk::UseManualInputs, GameQuirk::DisableDxgiSpoofing),

    // Dying Light 2: Reloaded Edition
    // SL spoof enough to unlock everything DLSS, manual input polling for fixing unclickable Opti Overlay
    QUIRK_ENTRY("dyinglightgame_x64_rwdi.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::UseManualInputs),

    // Dying Light: The Beast
    // SL spoof enough to unlock everything DLSS, manual input polling for fixing unclickable Opti Overlay
    QUIRK_ENTRY("dyinglightgame_thebeast_x64_rwdi.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::UseManualInputs),

    // Assetto Corsa EVO
    // SL spoof enough to unlock everything DLSS, AE required to fix FSR4 ghosting
    QUIRK_ENTRY("assettocorsaevo.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::ForceAutoExposure),

    // Hellblade: Senua's Sacrifice
    // No UE barriers to fix crash on upscaler init
    QUIRK_ENTRY_UE(hellbladegame, GameQuirk::DisableDxgiSpoofing, GameQuirk::DontUseUnrealColorBarriers,
                   GameQuirk::DontUseUnrealMVBarriers),

    // Sackboy: A Big Adventure
    // No UE barriers to fix crash on upscaler init
    QUIRK_ENTRY_UE(sackboy, GameQuirk::DisableDxgiSpoofing, GameQuirk::ForceAutoExposure,
                   GameQuirk::DontUseUnrealColorBarriers, GameQuirk::DontUseUnrealMVBarriers),

    // OUTRIDERS
    // No UE barriers to fix crash on upscaler init
    QUIRK_ENTRY_UE(outriders, GameQuirk::DisableDxgiSpoofing, GameQuirk::DontUseUnrealColorBarriers,
                   GameQuirk::DontUseUnrealMVBarriers),

    // Watch Dogs: Legion
    // AE required to fix FSR4 ghosting
    QUIRK_ENTRY("watchdogslegion.exe", GameQuirk::ForceAutoExposure),
    QUIRK_ENTRY("watchdogslegion_plus.exe", GameQuirk::ForceAutoExposure),

    // Assassin’s Creed Shadows
    // SL spoof enough to unlock everything DLSS
    QUIRK_ENTRY("acshadows.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("acshadows_plus.exe", GameQuirk::DisableDxgiSpoofing),

    // Assassin's Creed Black Flag Resynced
    // SL spoof enough to unlock everything DLSS
    QUIRK_ENTRY("acblackflag.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("acblackflag_plus.exe", GameQuirk::DisableDxgiSpoofing),

    // SL spoof enough to unlock everything DLSS/No spoof needed for DLSS inputs
    //
    // The Witcher 3, Alan Wake 2, Crysis 3 Remastered, Marvel's Guardians of the Galaxy, UNCHARTED: Legacy of Thieves
    // Collection, Warhammer 40,000: Darktide, Observer: System Redux, Pumpkin Jack, Rise of the Ronin, DYNASTY
    // WARRIORS: ORIGINS, Crysis Remastered, Crysis 2 Remastered, Mortal Shell, Sekiro: Shadows Die Twice (for SekiroTSR
    // mod), The Medium, God of War (2018), Europa Universalis V, Need for Speed Unbound, Nioh 2 – The Complete Edition,
    // Control Ultimate Edition, Deathloop, FINAL FANTASY VII REMAKE INTERGRADE (for Luma mod), Farming Simulator 2025,
    // Nioh 3, FATAL FRAME II: Crimson Butterfly REMAKE, MOUSE: P.I. For Hire, Yet Another Zombie Survivors, Voodoo
    // Fishin', Forza Horizon 6, Over the Hill (demo), SHROT (demo)
    QUIRK_ENTRY("witcher3.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("alanwake2.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("crysis3remastered.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("gotg.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("u4.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("u4-l.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("tll.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("tll-l.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("darktide.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("observersystemredux.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::ForceAutoExposure),
    QUIRK_ENTRY_UE(pumpkinjack, GameQuirk::DisableDxgiSpoofing, GameQuirk::ForceAutoExposure),
    QUIRK_ENTRY("ronin.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("dworigins.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("crysisremastered.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("crysis2remastered.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY_UE(dungeonhaven, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("sekiro.exe", GameQuirk::DisableDxgiSpoofing), // Sekiro TSR mod required for upscalers
    QUIRK_ENTRY_UE(medium, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("gow.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("eu5.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("needforspeedunbound.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("nioh2.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::ForceAutoExposure),
    QUIRK_ENTRY("control_dx12.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::ForceAutoExposure),
    QUIRK_ENTRY("deathloop.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("ff7remake_.exe", GameQuirk::DisableDxgiSpoofing), // Luma mod required for upscalers
    QUIRK_ENTRY("farmingsimulator2025game.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("nioh3.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("fatalframeii.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("mouse.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("yet another zombie survivors.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("voodoo fishin'.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("forzahorizon6.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("over the hill.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("shrot.exe", GameQuirk::DisableDxgiSpoofing),

    // FSR2/3 only, no spoof needed
    //
    // Tiny Tina's Wonderlands, Dead Island 2, The Outer Worlds: Spacer's Choice Edition, Scorn, Thymesia, Company of
    // Heroes 3, Caravan Sandwitch, Asterigos: Curse of the Stars, Saints Row (2022)
    QUIRK_ENTRY("wonderlands.exe", GameQuirk::DisableReactiveMasks, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY_UE(deadisland, GameQuirk::DisableReactiveMasks, GameQuirk::DisableDxgiSpoofing,
                   GameQuirk::ForceAutoExposure),
    QUIRK_ENTRY_UE(indiana, GameQuirk::DisableReactiveMasks, GameQuirk::DisableDxgiSpoofing,
                   GameQuirk::ForceAutoExposure),
    QUIRK_ENTRY_UE(scorn, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY_UE(plagueproject, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("reliccoh3.exe", GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY_UE(caravansandwitch, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY_UE(genesis, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY("saintsrow_dx12.exe", GameQuirk::DisableDxgiSpoofing),

    // Disable FSR2/3 inputs due to crashing/custom implementations
    //
    // Forgive Me Father 2, Revenge of the Savage Planet, F1 22, Metal Eden, Until Dawn, Bloomand Rage, 171, Microsoft
    // Flight Simulator (2020) - MSFS2020, Banishers: Ghosts of New Eden,Rune Factory Guardians of Azuma, Supraworld, F1
    // Manager 2024, Keeper (+ WinGDK PaganIdol version), Assetto Corsa Rally
    QUIRK_ENTRY_UE(fmf2, GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),
    QUIRK_ENTRY_UE(towers, GameQuirk::DisableFSR2Inputs,
                   GameQuirk::DisableFSR3Inputs), // Revenge of the Savage Planet
    QUIRK_ENTRY("f1_22.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),
    QUIRK_ENTRY_UE(metaleden, GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),
    QUIRK_ENTRY_UE(bates, GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),
    QUIRK_ENTRY("bloom&rage.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),
    QUIRK_ENTRY_UE(bcg, GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs), // 171
    QUIRK_ENTRY("flightsimulator.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),
    QUIRK_ENTRY_UE(banishers, GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),
    QUIRK_ENTRY_UE(game, GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs), // Rune
    QUIRK_ENTRY_UE(supraworld, GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),
    QUIRK_ENTRY("f1manager24.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),
    QUIRK_ENTRY_UE(keeper, GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),
    QUIRK_ENTRY_UE(paganidol, GameQuirk::DisableFSR2Inputs,
                   GameQuirk::DisableFSR3Inputs), // Keeper WinGDK PaganIdol
    QUIRK_ENTRY("acr.exe", GameQuirk::DisableFSR2Inputs, GameQuirk::DisableFSR3Inputs),

    // XeSS only, no spoof needed
    //
    // Redout 2, Disney Epic Mickey: Rebrushed
    QUIRK_ENTRY_UE(redout2, GameQuirk::DisableDxgiSpoofing),
    QUIRK_ENTRY_UE(recolored, GameQuirk::DisableDxgiSpoofing),

    // Self-explanatory
    //
    // The Persistence, Split Fiction, Minecraft Bedrock, Ghostwire: Tokyo, RoadCraft, STAR WARS Jedi:
    // Survivor, FINAL FANTASY VII REBIRTH, Witchfire, MechWarrior 5: Mercenaries, Ghostrunner, Ghostrunner 2
    QUIRK_ENTRY_UE(persistence, GameQuirk::ForceUnrealEngine),
    QUIRK_ENTRY("splitfiction.exe", GameQuirk::FastFeatureReset),
    QUIRK_ENTRY("minecraft.windows.exe", GameQuirk::KernelBaseHooks),
    QUIRK_ENTRY("gwt.exe", GameQuirk::ForceUnrealEngine),
    QUIRK_ENTRY("roadcraft - retail.exe", GameQuirk::FixSlSimulationMarkers),
    QUIRK_ENTRY("jedisurvivor.exe", GameQuirk::ForceAutoExposure),
    QUIRK_ENTRY("ff7rebirth_.exe", GameQuirk::ForceUnrealEngine),
    QUIRK_ENTRY_UE(witchfire, GameQuirk::DisableUseFsrInputValues),
    QUIRK_ENTRY_UE(mechwarrior, GameQuirk::ForceUnrealEngine),
    QUIRK_ENTRY_UE(ghostrunner, GameQuirk::ForceUnrealEngine),
    QUIRK_ENTRY_UE(ghostrunner2, GameQuirk::ForceUnrealEngine),
    QUIRK_ENTRY("soulstice.exe", GameQuirk::ForceUnrealEngine, GameQuirk::ForceAutoExposure),

    // VULKAN
    // ------

    // No Man's Sky
    QUIRK_ENTRY("nms.exe", GameQuirk::KernelBaseHooks, GameQuirk::VulkanDLSSBarrierFixup,
                GameQuirk::EnableVulkanSpoofing, GameQuirk::EnableVulkanExtensionSpoofing),

    // RTX Remix
    QUIRK_ENTRY("nvremixbridge.exe", GameQuirk::DisableDxgiSpoofing, GameQuirk::LoadVulkanManually,
                GameQuirk::EnableVulkanExtensionSpoofing, GameQuirk::VulkanDLSSBarrierFixup),

    // Enshrouded
    QUIRK_ENTRY("enshrouded.exe", GameQuirk::EnableVulkanSpoofing, GameQuirk::EnableVulkanExtensionSpoofing,
                GameQuirk::LoadVulkanManually),

    // World War Z
    QUIRK_ENTRY("wwzretail.exe", GameQuirk::UseFsr2VulkanInputs, GameQuirk::EnableVulkanExtensionSpoofing,
                GameQuirk::DisableDxgiSpoofing, GameQuirk::ForceDepthD32S8),

    QUIRK_ENTRY("wwzretailegs.exe", GameQuirk::UseFsr2VulkanInputs, GameQuirk::EnableVulkanExtensionSpoofing,
                GameQuirk::DisableDxgiSpoofing, GameQuirk::ForceDepthD32S8),

    // Baldur's Gate 3
    // VK Ext spoof needed for FSR3
    QUIRK_ENTRY("bg3.exe", GameQuirk::EnableVulkanExtensionSpoofing),

    // Arknights: Endfield (Vulkan)
    QUIRK_ENTRY("endfield.exe", GameQuirk::EnableVulkanSpoofing, GameQuirk::EnableVulkanExtensionSpoofing,
                GameQuirk::VulkanDLSSBarrierFixup),

    // Indiana Jones and the Great Circle
    // VK Ext spoof needed for unlocking DLSS and DLSS-FG (atleast for AMD)
    QUIRK_ENTRY("thegreatcircle.exe", GameQuirk::EnableVulkanExtensionSpoofing, GameQuirk::DisableDxgiSpoofing),

    // DOOM: The Dark Ages
    // Disabled Dxgi spoofing to avoid crash on boot
    QUIRK_ENTRY("doomthedarkages.exe", GameQuirk::DisableDxgiSpoofing),

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
