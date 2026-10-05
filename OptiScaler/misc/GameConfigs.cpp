#include "pch.h"
#include "GameConfigs.h"

#include "State.h"
#include "Config.h"
#include "Logger.h"

// HOW TO ADD A GAME
// -----------------
// Add an entry to gameConfigs below:
//
//   // Game name
//   // Why the game needs these settings
//   {
//       .exes = { "game.exe", "game_dx12.exe" }, // lowercase exe names
//       .ueExes = { "project" },                 // UE games, matches project-win64-shipping.exe & -wingdk-shipping.exe
//       .defaults = { DxgiSpoofing = false, FGAllowedFrameAhead = 2 },
//       .quirks = { ForceUnrealEngine },
//   },
//
// Every field is optional, but they have to stay in the order shown above.
//
// .defaults - Config options changed for this game, written as "Setting = value".
//             Setting names are the same as in Config.h, values in OptiScaler.ini always take priority.
//             If a setting you need is missing, add it to the GAME_SETTING list below.
//             Wrap a setting to only apply it in some cases:
//                 OnNvidia(...), OnNonNvidia(...)  - only on Nvidia / only on AMD & Intel GPUs
//                 WithUpscalerFGInput(...)         - only when FG Input is set to Upscaler
//                 WithXeFG(...)                    - only when XeFG is used as the FG Output
//
// .quirks   - Game specific hacks from Quirks.h, these need code changes to add new ones.

namespace
{
template <auto Member> struct Setting
{
    using ValueType = typename std::remove_reference_t<decltype(std::declval<Config&>().*Member)>::value_type;

    const char* name;

    // Allows writing "Setting = value" in the table
    template <class V> ConfigDefault operator=(V value) const
    {
        const auto typedValue = static_cast<ValueType>(value);

        return { std::format("{} = {}", name, typedValue), [typedValue]
                 {
                     auto& option = Config::Instance()->*Member;

                     if (option.has_value())
                         return false;

                     option.set_volatile_value(typedValue);
                     return true;
                 } };
    }
};

#define GAME_SETTING(member)                                                                                           \
    const Setting<&Config::member> member { #member }

GAME_SETTING(AutoExposure);
GAME_SETTING(BuildPipelines);
GAME_SETTING(ColorResourceBarrier);
GAME_SETTING(CreateHeaps);
GAME_SETTING(DisableOTA);
GAME_SETTING(DisableReactiveMask);
GAME_SETTING(DontUseNTShared);
GAME_SETTING(DxgiSpoofing);
GAME_SETTING(EnableFfxInputs);
GAME_SETTING(EnableFsr2Inputs);
GAME_SETTING(EnableFsr3Inputs);
GAME_SETTING(ExtendedStateRestore);
GAME_SETTING(FGAllowedFrameAhead);
GAME_SETTING(FGAlwaysCaptureFSRFGSwapchain);
GAME_SETTING(FGDisableHUDFix);
GAME_SETTING(FGPreserveSwapChain);
GAME_SETTING(FGSkipResizeBuffers);
GAME_SETTING(FGXeFGForceBorderless);
GAME_SETTING(FGXeFGIgnoreInitChecks);
GAME_SETTING(Fsr2Pattern);
GAME_SETTING(Fsr4DoNotLoadAmdxc64);
GAME_SETTING(FsrUseFsrInputValues);
GAME_SETTING(MVResourceBarrier);
GAME_SETTING(OverlayMenu);
GAME_SETTING(OverrideVsync);
GAME_SETTING(RestoreComputeSignature);
GAME_SETTING(SkipFirstFrames);
GAME_SETTING(SpoofRegistry);
GAME_SETTING(UseFakenvapi);
GAME_SETTING(UseFsr2Dx11Inputs);
GAME_SETTING(UseFsr2VulkanInputs);
GAME_SETTING(UseNtdllHooks);
GAME_SETTING(VulkanExtensionSpoofing);
GAME_SETTING(VulkanSpoofing);
GAME_SETTING(ReprojectionDepthCutoff);

#undef GAME_SETTING

ConfigDefault When(bool (*condition)(const GameConfigContext&), ConfigDefault setting)
{
    setting.condition = condition;
    return setting;
}

ConfigDefault OnNvidia(ConfigDefault setting)
{
    return When([](const GameConfigContext& context) { return context.isNvidia; }, std::move(setting));
}

ConfigDefault OnNonNvidia(ConfigDefault setting)
{
    return When([](const GameConfigContext& context) { return !context.isNvidia; }, std::move(setting));
}

ConfigDefault WithUpscalerFGInput(ConfigDefault setting)
{
    return When([](const GameConfigContext&)
                { return Config::Instance()->FGInput.value_or_default() == FGInput::Upscaler; }, std::move(setting));
}

ConfigDefault WithXeFG(ConfigDefault setting)
{
    return When(
        [](const GameConfigContext&)
        {
            auto& state = State::Instance();
            return state.activeFgOutput == FGOutput::XeFG && state.activeFgInput != FGInput::NoFG &&
                   state.activeFgInput != FGInput::NvngxFG;
        },
        std::move(setting));
}

using enum GameQuirk;

const std::vector<GameConfig> gameConfigs = {

    // Red Dead Redemption 2
    // Spoofing causes FSR2 inputs crash, DLSS inputs need OptiPatcher to avoid artifacts/crashes anyway
    {
        .exes = { "rdr2.exe", "playrdr2.exe" },
        .defaults = { EnableFsr3Inputs = false, DxgiSpoofing = false },
    },

    // Red Dead Redemption
    // Preserving the FG swapchain causes crashes with DLSSG via SL
    {
        .exes = { "rdr.exe", "playrdr.exe" },
        .defaults = { DxgiSpoofing = false, FGPreserveSwapChain = false },
        .quirks = { SkipFsr3Method, NoFSRFGFirstSwapchain },
    },

    // Visions of Mana
    // Use FSR2 Pattern Matching to fix broken FSR2 detection
    {
        .ueExes = { "visionsofmana" },
        .defaults = { Fsr2Pattern = true, DxgiSpoofing = false },
    },

    // Silent Hill f
    {
        .ueExes = { "shf" },
        .defaults = { FGAlwaysCaptureFSRFGSwapchain = true },
    },

    // Beast of Reincarnation
    {
        .ueExes = { "beastofreincarnation" },
        .defaults = { EnableFsr2Inputs = false, EnableFsr3Inputs = false, FGAlwaysCaptureFSRFGSwapchain = true },
    },

    // Tainted Grail - Fall of Avalon
    {
        .exes = { "fall of avalon.exe" },
        .defaults = { AutoExposure = true },
    },

    // Granblue Fantasy Relink
    // Disabled fakenvapi to fix broken rendering
    {
        .exes = { "granblue_fantasy_relink.exe" },
        .defaults = { UseFakenvapi = false },
    },

    // Path of Exile 2
    {
        .exes = { "pathofexile.exe", "pathofexile_x64.exe", "pathofexile_kg.exe", "pathofexile_x64_kg.exe",
                  "pathofexilesteam.exe", "pathofexile_x64steam.exe" },
        .defaults = { DxgiSpoofing = false },
        .quirks = { LoadD3D12Manually },
    },

    // Where Winds Meet
    // SL spoof enough to unlock everything DLSS, required to avoid forced DLSS dilated MVs
    {
        .exes = { "wwm.exe" },
        .defaults = { DxgiSpoofing = false },
        .quirks = { ForceFGRenderSizeMVs },
    },

    // Arknights: Endfield
    // DX12: Reflex hooking crashes the game
    {
        .exes = { "endfield.exe" },
        .defaults = { UseFakenvapi = false, OnNonNvidia(VulkanSpoofing = true),
                      OnNonNvidia(VulkanExtensionSpoofing = true) },
        .quirks = { ForceCreateD3D12Device, VulkanDLSSBarrierFixup },
    },

    // Neverness to Everness
    // Kernel hooks required to unlock DLSS inputs
    {
        .exes = { "htgame.exe" },
        .defaults = { EnableFsr2Inputs = false, EnableFsr3Inputs = false, UseNtdllHooks = false },
    },

    // Wuthering Waves
    // Kernel hooks required to unlock DLSS inputs, FGPreserveSwapChain = false for fixing XeFG
    {
        .ueExes = { "client" },
        .defaults = { UseNtdllHooks = false, FGPreserveSwapChain = false },
    },

    // Zenless Zone Zero
    // IgnoreTagsWithoutHudlessForFG fixes flipped Unity MVs/Depth
    {
        .exes = { "zenlesszonezero.exe" },
        .quirks = { IgnoreTagsWithoutHudlessForFG },
    },

    // Trails in the Sky 1st Chapter, Trails in the Sky 2nd Chapter
    {
        .exes = { "sora_1st.exe", "sora_2nd.exe" },
        .defaults = { UseFsr2Dx11Inputs = true, DxgiSpoofing = false },
    },

    // NINJA GAIDEN 4
    // No spoof needed for DLSS inputs, Hudfix incompatible
    {
        .exes = { "ninjagaiden4-steam.exe", "ninjagaiden4-wingdk.exe" },
        .defaults = { DxgiSpoofing = false, FGSkipResizeBuffers = false, FGPreserveSwapChain = false,
                      WithUpscalerFGInput(FGDisableHUDFix = true) },
    },

    // DEAD OR ALIVE 6 Last Round
    // No spoof needed for DLSS inputs
    {
        .exes = { "doa6lr.exe" },
        .defaults = { DxgiSpoofing = false, FGSkipResizeBuffers = false, FGPreserveSwapChain = false },
    },

    // The Last of Us Part I
    // Hudfix incompatible
    {
        .exes = { "tlou-i.exe", "tlou-i-l.exe" },
        .defaults = { FGAllowedFrameAhead = 2, DxgiSpoofing = false },
    },

    // Crapcom Games, DLSS without dxgi spoofing needs restore compute in those
    //
    // Kunitsu-Gami: Path of the Goddess (+ demo), Dead Rising Deluxe Remaster
    {
        .exes = { "kunitsugami.exe", "kunitsugamidemo.exe", "drdr.exe" },
        .defaults = { OnNonNvidia(RestoreComputeSignature = true), DxgiSpoofing = false },
    },
    // Monster Hunter Wilds, Resident Evil Requiem (+ demo), Monster Hunter Stories 3: Twisted Reflection,
    // Onimusha: Way of the Sword (+ demo)
    {
        .exes = { "monsterhunterwilds.exe", "re9.exe", "re9demo.exe", "monster_hunter_stories_3_twisted_reflection.exe",
                  "onimushawots_demo.exe", "onimushawots.exe" },
        .defaults = { RestoreComputeSignature = true, DxgiSpoofing = false },
    },
    // MONSTER HUNTER RISE
    // AMD/Intel need spoofing, Restoresig seems to fix real DLSS
    {
        .exes = { "monsterhunterrise.exe" },
        .defaults = { RestoreComputeSignature = true },
    },
    // Dragon's Dogma 2 Character Creator & Storage
    {
        .exes = { "dd2ccs.exe" },
        .defaults = { OnNonNvidia(RestoreComputeSignature = true), DxgiSpoofing = false,
                      WithUpscalerFGInput(FGDisableHUDFix = true) },
    },
    // Dragon's Dogma 2
    {
        .exes = { "dd2.exe" },
        .defaults = { RestoreComputeSignature = true, DxgiSpoofing = false,
                      WithUpscalerFGInput(FGDisableHUDFix = true) },
        .quirks = { PregmataFixDLSSModes },
    },
    // PRAGMATA Demo
    {
        .exes = { "pragmata_sketchbook.exe" },
        .defaults = { RestoreComputeSignature = true, DxgiSpoofing = false, FGAllowedFrameAhead = 2 },
        .quirks = { PregmataFixDLSSModes },
    },
    // PRAGMATA
    {
        .exes = { "pragmata.exe" },
        .defaults = { RestoreComputeSignature = true, DxgiSpoofing = false },
        .quirks = { PregmataFixDLSSModes },
    },

    // REF PDUpscaler branch
    // Old menu needed to avoid the invisible overlay while upscaling is active
    {
        .exes = { "re2.exe", "re3.exe", "re4.exe", "re7.exe", "re8.exe", "devilmaycry5.exe", "streetfighter6.exe" },
        .defaults = { DxgiSpoofing = false, OverlayMenu = false },
    },

    // Cyberpunk 2077
    // SL spoof enough to unlock everything DLSS
    {
        .exes = { "cyberpunk2077.exe" },
        .defaults = { WithUpscalerFGInput(FGDisableHUDFix = true), DxgiSpoofing = false,
                      ReprojectionDepthCutoff = 0.02f },
        .quirks = { CyberpunkHudlessState, FSRFGHudlessMismatchFixup },
    },

    // Forza Horizon 5
    // SL spoof enough to unlock everything DLSS
    {
        .exes = { "forzahorizon5.exe" },
        .defaults = { EnableFsr2Inputs = false, EnableFsr3Inputs = false, DxgiSpoofing = false },
    },

    // Avatar: Frontiers of Pandora
    // SL spoof enough to unlock DLSSG, blocked spoofing due to broken RT/performance overhead, Hudfix incompatible
    {
        .exes = { "afop.exe" },
        .defaults = { EnableFsr2Inputs = false, EnableFsr3Inputs = false, DxgiSpoofing = false,
                      WithUpscalerFGInput(FGDisableHUDFix = true) },
    },

    // Forza Motorsport 8
    // Steam, MS Store
    {
        .exes = { "forza_steamworks_release_final.exe", "forza_gaming.desktop.x64_release_final.exe" },
        .defaults = { EnableFsr2Inputs = false, EnableFsr3Inputs = false },
    },

    // Death Stranding and Directors Cut
    // no spoof needed for DLSS inputs
    {
        .exes = { "ds.exe" },
        .defaults = { DxgiSpoofing = false, EnableFsr2Inputs = false, EnableFsr3Inputs = false },
    },

    // Duet Night Abyss
    {
        .exes = { "em-win64-shipping.exe" },
        .defaults = { UseNtdllHooks = false },
    },

    // The Talos Principle 2
    {
        .exes = { "talos2-win64-shipping.exe" },
        .defaults = { FGSkipResizeBuffers = false, FGPreserveSwapChain = false },
    },

    // The Callisto Protocol
    // FSR2 only, no spoof needed
    {
        .ueExes = { "thecallistoprotocol" },
        .defaults = { FsrUseFsrInputValues = false, DxgiSpoofing = false, DisableReactiveMask = true,
                      AutoExposure = true },
    },

    // HITMAN World of Assassination
    // SL spoof enough to unlock everything DLSS
    {
        .exes = { "hitman3.exe" },
        .defaults = { DxgiSpoofing = false, EnableFsr2Inputs = false },
        .quirks = { HitmanReflexHacks },
    },

    // 007 First Light
    // SL spoof enough to unlock everything DLSS, uses bindless so restoring compute is complicated
    {
        .exes = { "007firstlight.exe" },
        .defaults = { DxgiSpoofing = false, RestoreComputeSignature = true, ExtendedStateRestore = true,
                      Fsr4DoNotLoadAmdxc64 = true },
        .quirks = { IgnoreValidUntilEvaluateForFG },
    },

    // ELDEN RING (for ERSS mod)
    // no spoof needed for DLSS inputs
    {
        .exes = { "eldenring.exe" },
        .defaults = { DxgiSpoofing = false },
    },

    // ELDEN RING NIGHTREIGN (for NRSS mod)
    // no spoof needed for DLSS inputs
    {
        .exes = { "nightreign.exe" },
        .defaults = { DxgiSpoofing = false, CreateHeaps = false, BuildPipelines = false },
    },

    // Returnal
    // no spoof needed for DLSS inputs, but no DLSSG and Reflex
    {
        .ueExes = { "returnal" },
        .defaults = { DxgiSpoofing = false, MVResourceBarrier = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                      ColorResourceBarrier = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE },
    },

    // WUCHANG: Fallen Feathers
    // Skip 1 frame use of upscaler which cause crash
    {
        .exes = { "project_plague-deck-shipping.exe", "project_plague-win64-shipping.exe" },
        .defaults = { SkipFirstFrames = 10 },
    },

    // Final Fantasy XIV
    {
        .exes = { "ffxiv_dx11.exe" },
        .defaults = { OverrideVsync = false },
    },
    {
        .exes = { "graphadapterdesc.exe" },
        .quirks = { SkipD3D11FeatureLevelElevation },
    },

    // Prey 2017
    // Requires Prey Luma Remastered mod for upscalers
    {
        .exes = { "prey.exe" },
        .defaults = { DontUseNTShared = true, CreateHeaps = false, BuildPipelines = false, DxgiSpoofing = false },
    },

    // Black Myth: Wukong
    // To enable DLSS-FG option
    {
        .ueExes = { "b1" },
        .defaults = { SpoofRegistry = true },
    },

    // Avowed
    // NoColorBarrier needed to avoid post-loading crash with DLSS, AE required to fix FSR4 ghosting
    {
        .ueExes = { "avowed" },
        .defaults = { AutoExposure = true, ColorResourceBarrier = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                      EnableFsr2Inputs = false, EnableFsr3Inputs = false },
    },

    // Starfield
    // SL spoof enough to unlock everything DLSS, Depth and Velocity needed to avoid FG artifacts
    {
        .exes = { "starfield.exe" },
        .defaults = { EnableFsr2Inputs = false, EnableFsr3Inputs = false, DxgiSpoofing = false, AutoExposure = true },
    },

    // Nixxes Sony ports - Dxgi spoofing disabled due to RT crashes
    //
    // Ratchet & Clank: Rift Apart
    {
        .exes = { "riftapart.exe" },
        .defaults = { DxgiSpoofing = false },
    },
    // Marvel’s Spider-Man Remastered
    {
        .exes = { "spider-man.exe" },
        .defaults = { DxgiSpoofing = false, WithUpscalerFGInput(FGDisableHUDFix = true) },
        .quirks = { FSRFGHudlessMismatchFixup },
    },
    // Marvel’s Spider-Man: Miles Morales, Marvel's Spider-Man 2
    {
        .exes = { "milesmorales.exe", "spider-man2.exe" },
        .defaults = { DxgiSpoofing = false, WithUpscalerFGInput(FGDisableHUDFix = true) },
    },
    // DEATH STRANDING 2: ON THE BEACH
    {
        .exes = { "ds2.exe" },
        .defaults = { DxgiSpoofing = false },
        .quirks = { FSRFGHudlessMismatchFixup },
    },
    //
    // Dxgi spoofing disabled, DLSS-FG available, standalone Reflex can be unlocked with -unlockReflexOptions launch
    // option if needed
    //
    // Horizon Zero Dawn Remastered, The Last of Us Part II Remastered
    {
        .exes = { "horizonzerodawnremastered.exe", "tlou-ii.exe", "tlou-ii-l.exe" },
        .defaults = { DxgiSpoofing = false },
    },
    // Horizon Forbidden West Complete Edition
    {
        .exes = { "horizonforbiddenwest.exe" },
        .defaults = { DxgiSpoofing = false, FGAllowedFrameAhead = 2 },
    },
    // Ghost of Tsushima DIRECTOR'S CUT
    {
        .exes = { "ghostoftsushima.exe" },
        .defaults = { DxgiSpoofing = false, WithUpscalerFGInput(FGDisableHUDFix = true) },
    },

    // Dead Space Remake
    // Override Vsync required to avoid crash on boot
    {
        .exes = { "dead space.exe" },
        .defaults = { DxgiSpoofing = false, WithXeFG(OverrideVsync = true), WithXeFG(FGXeFGForceBorderless = true),
                      FGSkipResizeBuffers = false },
    },

    // Metro Exodus Enhanced Edition
    // ForceBorderless required to avoid black screen with XeFG, Manual Input polling for fixing invisible Opti Overlay,
    // Hudfix incompatible
    {
        .exes = { "metroexodus.exe" },
        .defaults = { DxgiSpoofing = false, WithXeFG(FGXeFGForceBorderless = true), AutoExposure = true,
                      WithUpscalerFGInput(FGDisableHUDFix = true) },
    },

    // Star Wars: Outlaws
    // SL spoof enough to unlock everything DLSS, Hudfix incompatible
    {
        .exes = { "outlaws.exe", "outlaws_plus.exe" },
        .defaults = { EnableFsr2Inputs = false, EnableFsr3Inputs = false, DxgiSpoofing = false,
                      WithUpscalerFGInput(FGDisableHUDFix = true) },
    },

    // Lies of P
    // Spoofing disabled as no Streamline and OptiPatcher unlocks DLSS anyway
    {
        .ueExes = { "lop" },
        .defaults = { DxgiSpoofing = false },
    },

    // Crimson Desert
    // Spoofing disabled due to "unsupported GPU" error
    {
        .exes = { "crimsondesert.exe" },
        .defaults = { DxgiSpoofing = false },
    },

    // Assassin's Creed Mirage
    // Game not loading SL plugin even while spoofing, also avoids the "unsupported video driver" notification,
    // Hudfix incompatible
    {
        .exes = { "acmirage.exe", "acmirage_plus.exe" },
        .defaults = { DxgiSpoofing = false, WithUpscalerFGInput(FGDisableHUDFix = true) },
    },

    // DCS World
    // Fakenvapi seems to cause a crash when switching to FSR4 (INT8 only?)
    {
        .exes = { "dcs.exe" },
        .defaults = { UseFakenvapi = false },
    },

    // S.T.A.L.K.E.R.: Legends of the Zone Trilogy - Enhanced Editions
    // Manual input polling for fixing invisible Opti Overlay, no spoof needed for DLSS inputs
    {
        .exes = { "xrengine.exe" },
        .defaults = { DxgiSpoofing = false },
    },

    // Dying Light 2: Reloaded Edition
    // SL spoof enough to unlock everything DLSS, manual input polling for fixing unclickable Opti Overlay,
    // Hudfix incompatible
    {
        .exes = { "dyinglightgame_x64_rwdi.exe" },
        .defaults = { DxgiSpoofing = false, WithUpscalerFGInput(FGDisableHUDFix = true) },
    },

    // Dying Light: The Beast
    // SL spoof enough to unlock everything DLSS, manual input polling for fixing unclickable Opti Overlay
    {
        .exes = { "dyinglightgame_thebeast_x64_rwdi.exe" },
        .defaults = { DxgiSpoofing = false },
    },

    // Assetto Corsa EVO
    // SL spoof enough to unlock everything DLSS, AE required to fix FSR4 ghosting
    {
        .exes = { "assettocorsaevo.exe" },
        .defaults = { DxgiSpoofing = false, AutoExposure = true },
    },

    // Alan Wake 2, Marvel's Guardians of the Galaxy, UNCHARTED: Legacy of Thieves Collection
    // SL spoof enough to unlock everything DLSS, Hudfix incompatible
    {
        .exes = { "alanwake2.exe", "gotg.exe", "u4.exe", "u4-l.exe", "tll.exe", "tll-l.exe" },
        .defaults = { DxgiSpoofing = false, WithUpscalerFGInput(FGDisableHUDFix = true) },
    },

    // The Witcher 3
    // SL spoof enough to unlock everything DLSS/No spoof needed for DLSS inputs,
    // WAR for our SL having to init on the real device that the game will actually be using
    // early in boot it creates a device that it later *needs* to properly destroy
    {
        .exes = { "witcher3.exe" },
        .defaults = { DxgiSpoofing = false },
        .quirks = { CreateSLOnThe2ndDevice },
    },

    // No UE barriers to fix crash on upscaler init
    //
    // Hellblade: Senua's Sacrifice, OUTRIDERS, The Medium - no spoof needed for DLSS inputs
    {
        .ueExes = { "hellbladegame", "outriders", "medium" },
        .defaults = { DxgiSpoofing = false, ColorResourceBarrier = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                      MVResourceBarrier = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE },
    },
    // Sackboy: A Big Adventure (SL spoof enough to unlock everything DLSS), Pumpkin Jack, Mortal Shell - no spoof
    // needed for DLSS inputs
    {
        .ueExes = { "sackboy", "pumpkinjack", "dungeonhaven" },
        .defaults = { DxgiSpoofing = false, AutoExposure = true,
                      ColorResourceBarrier = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                      MVResourceBarrier = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE },
    },
    // Observer: System Redux - no spoof needed for DLSS inputs
    {
        .exes = { "observersystemredux.exe" },
        .defaults = { DxgiSpoofing = false, AutoExposure = true,
                      ColorResourceBarrier = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                      MVResourceBarrier = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE },
    },
    // Sword and Fairy 7
    {
        .ueExes = { "pal7" },
        .defaults = { ColorResourceBarrier = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                      MVResourceBarrier = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE },
    },

    // Watch Dogs: Legion
    // AE required to fix FSR4 ghosting
    {
        .exes = { "watchdogslegion.exe", "watchdogslegion_plus.exe" },
        .defaults = { AutoExposure = true },
    },

    // Assassin’s Creed Shadows, Assassin's Creed Black Flag Resynced, FBC: Firebreak
    // SL spoof enough to unlock everything DLSS
    {
        .exes = { "acshadows.exe", "acshadows_plus.exe", "acblackflag.exe", "acblackflag_plus.exe",
                  "fbcfirebreak.exe" },
        .defaults = { DxgiSpoofing = false },
    },

    // CONTROL Resonant
    // SL spoof enough to unlock everything DLSS, Preserve FG Swapchain just crashes the DLSSG inputs
    {
        .exes = { "controlresonant.exe" },
        .defaults = { DxgiSpoofing = false, FGPreserveSwapChain = false },
    },

    // SL spoof enough to unlock everything DLSS/No spoof needed for DLSS inputs
    //
    // Crysis 3 Remastered, Warhammer 40,000: Darktide, Rise of the Ronin, DYNASTY WARRIORS: ORIGINS, Crysis Remastered,
    // Crysis 2 Remastered, Sekiro: Shadows Die Twice (SekiroTSR mod required for upscalers), God of War (2018),
    // Europa Universalis V, Need for Speed Unbound, Deathloop, FINAL FANTASY VII REMAKE INTERGRADE (Luma mod required
    // for upscalers), Farming Simulator 2025, Nioh 3, FATAL FRAME II: Crimson Butterfly REMAKE, MOUSE: P.I. For Hire,
    // Yet Another Zombie Survivors, Voodoo Fishin', Forza Horizon 6, Over the Hill (demo), SHROT (demo)
    {
        .exes = { "crysis3remastered.exe",
                  "darktide.exe",
                  "ronin.exe",
                  "dworigins.exe",
                  "crysisremastered.exe",
                  "crysis2remastered.exe",
                  "sekiro.exe",
                  "gow.exe",
                  "eu5.exe",
                  "needforspeedunbound.exe",
                  "deathloop.exe",
                  "ff7remake_.exe",
                  "farmingsimulator2025game.exe",
                  "nioh3.exe",
                  "fatalframeii.exe",
                  "mouse.exe",
                  "yet another zombie survivors.exe",
                  "voodoo fishin'.exe",
                  "forzahorizon6.exe",
                  "over the hill.exe",
                  "shrot.exe" },
        .defaults = { DxgiSpoofing = false },
    },
    // Nioh 2 – The Complete Edition, Control Ultimate Edition
    {
        .exes = { "nioh2.exe", "control_dx12.exe" },
        .defaults = { DxgiSpoofing = false, AutoExposure = true },
    },

    // FSR2/3 only, no spoof needed
    //
    // Dead Island 2, The Outer Worlds: Spacer's Choice Edition (indiana)
    {
        .ueExes = { "deadisland", "indiana" },
        .defaults = { DisableReactiveMask = true, DxgiSpoofing = false, AutoExposure = true },
    },
    // Scorn, Thymesia (plagueproject), Company of Heroes 3, Caravan Sandwitch, Asterigos: Curse of the Stars (genesis),
    // Saints Row (2022)
    {
        .exes = { "reliccoh3.exe", "saintsrow_dx12.exe" },
        .ueExes = { "scorn", "plagueproject", "caravansandwitch", "genesis" },
        .defaults = { DxgiSpoofing = false },
    },

    // Tiny Tina's Wonderlands
    // FSR2/3 only, no spoof needed, reactive mask almost good but causes flickering of lampposts and in the distance
    {
        .exes = { "wonderlands.exe" },
        .defaults = { DisableReactiveMask = true, DxgiSpoofing = false },
    },

    // Disable FSR2/3 inputs due to crashing/custom implementations
    //
    // Forgive Me Father 2, Revenge of the Savage Planet (towers), F1 22, Metal Eden, Until Dawn (bates),
    // Bloom and Rage, 171 (bcg), Microsoft Flight Simulator (2020) - MSFS2020, Banishers: Ghosts of New Eden,
    // Rune Factory Guardians of Azuma (game), Supraworld, F1 Manager 2024, Keeper (+ WinGDK PaganIdol version),
    // Assetto Corsa Rally
    {
        .exes = { "f1_22.exe", "bloom&rage.exe", "flightsimulator.exe", "f1manager24.exe", "acr.exe" },
        .ueExes = { "fmf2", "towers", "metaleden", "bates", "bcg", "banishers", "game", "supraworld", "keeper",
                    "paganidol" },
        .defaults = { EnableFsr2Inputs = false, EnableFsr3Inputs = false },
    },

    // XeSS only, no spoof needed
    //
    // Redout 2, Disney Epic Mickey: Rebrushed
    {
        .ueExes = { "redout2", "recolored" },
        .defaults = { DxgiSpoofing = false },
    },

    // Hudfix incompatible
    //
    // Dragon Age: The Veilguard, F1 2020, F1 2021, Grand Theft Auto V Enhanced, Shadow of the Tomb Raider,
    // Stellar Blade
    {
        .exes = { "dragon age the veilguard.exe", "f1_2020_dx12.exe", "f1_2021_dx12.exe", "gta5_enhanced.exe",
                  "sottr.exe" },
        .ueExes = { "sb" },
        .defaults = { WithUpscalerFGInput(FGDisableHUDFix = true) },
    },

    // Rise of the Tomb Raider
    // Hudfix incompatible
    {
        .exes = { "rottr.exe" },
        .quirks = { SkipD3D11FeatureLevelElevation },
    },

    // Self-explanatory
    //
    // The Persistence, Ghostwire: Tokyo, MechWarrior 5: Mercenaries, Ghostrunner, Ghostrunner 2
    {
        .exes = { "gwt.exe" },
        .ueExes = { "persistence", "mechwarrior", "ghostrunner", "ghostrunner2" },
        .quirks = { ForceUnrealEngine },
    },
    // Minecraft Bedrock
    {
        .exes = { "minecraft.windows.exe" },
        .quirks = { KernelBaseHooks },
    },
    // RoadCraft
    {
        .exes = { "roadcraft - retail.exe" },
        .quirks = { FixSlSimulationMarkers },
    },
    // STAR WARS Jedi: Survivor
    {
        .exes = { "jedisurvivor.exe" },
        .defaults = { AutoExposure = true },
    },
    // FINAL FANTASY VII REBIRTH
    {
        .exes = { "ff7rebirth_.exe" },
        .defaults = { WithUpscalerFGInput(FGDisableHUDFix = true) },
        .quirks = { ForceUnrealEngine },
    },
    // Witchfire
    {
        .ueExes = { "witchfire" },
        .defaults = { FsrUseFsrInputValues = false },
    },
    // Soulstice
    {
        .exes = { "soulstice.exe" },
        .defaults = { AutoExposure = true },
        .quirks = { ForceUnrealEngine },
    },

    // VULKAN
    // ------

    // No Man's Sky
    {
        .exes = { "nms.exe" },
        .defaults = { OnNonNvidia(VulkanSpoofing = true) },
        .quirks = { KernelBaseHooks, VulkanDLSSBarrierFixup, FSRFGHudlessMismatchFixup },
    },

    // RTX Remix
    {
        .exes = { "nvremixbridge.exe" },
        .defaults = { DxgiSpoofing = false, OnNonNvidia(VulkanExtensionSpoofing = true) },
        .quirks = { LoadVulkanManually, VulkanDLSSBarrierFixup },
    },

    // Enshrouded
    {
        .exes = { "enshrouded.exe" },
        .defaults = { OnNonNvidia(VulkanSpoofing = true), OnNonNvidia(VulkanExtensionSpoofing = true) },
        .quirks = { LoadVulkanManually },
    },

    // World War Z
    {
        .exes = { "wwzretail.exe" },
        .defaults = { UseFsr2VulkanInputs = true, OnNonNvidia(VulkanExtensionSpoofing = true), DxgiSpoofing = false },
        .quirks = { ForceDepthD32S8 },
    },

    // Baldur's Gate 3
    // VK Ext spoof needed for FSR3
    {
        .exes = { "bg3.exe" },
        .defaults = { OnNonNvidia(VulkanExtensionSpoofing = true) },
    },

    // Indiana Jones and the Great Circle
    // VK Ext spoof needed for unlocking DLSS and DLSS-FG (atleast for AMD)
    {
        .exes = { "thegreatcircle.exe" },
        .defaults = { OnNonNvidia(VulkanExtensionSpoofing = true), DxgiSpoofing = false },
    },

    // DOOM: The Dark Ages
    // Disabled Dxgi spoofing to avoid crash on boot, D3D12 for FSR 4 w/dx12
    {
        .exes = { "doomthedarkages.exe" },
        .defaults = { DxgiSpoofing = false },
        .quirks = { ForceCreateD3D12Device },
    },

};

bool MatchesExe(const GameConfig& game, const std::string& exeName)
{
    if (std::ranges::find(game.exes, exeName) != game.exes.end())
        return true;

    for (const auto& name : game.ueExes)
    {
        if (exeName == name + "-win64-shipping.exe" || exeName == name + "-wingdk-shipping.exe")
            return true;
    }

    return false;
}
} // namespace

flag_set<GameQuirk> ApplyGameConfigs(std::string exeName, const GameConfigContext& context)
{
    to_lower_in_place(exeName);
    flag_set<GameQuirk> quirks;

    for (const auto& game : gameConfigs)
    {
        if (!MatchesExe(game, exeName))
            continue;

        for (auto quirk : game.quirks)
            quirks |= quirk;

        for (const auto& setting : game.defaults)
        {
            if (setting.condition != nullptr && !setting.condition(context))
                continue;

            // User's config takes priority
            if (!setting.apply())
                continue;

            LOG_INFO("Game default: {}", setting.text);
            State::Instance().detectedQuirks.push_back(setting.text);
        }
    }

    return quirks;
}
