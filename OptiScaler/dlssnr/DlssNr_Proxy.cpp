#include "pch.h"
#include "DlssNr_Proxy.h"

#if OPTI_DLSSNR

#include <Config.h>
#include <Logger.h>
#include <proxies/NVNGX_Proxy.h>
#include <State.h>

namespace
{
// The driver's parameter block does not lay its setters out the way the SDK header declares.
//
// This is the same discovery the forwarder rests on, and it applies here for the same reason: this
// is the driver core's own block, not the header's implementation. A setter at slot N is read back
// by the getter at N+8, which is how the slots below were confirmed. Floats are the awkward one --
// the header says slot 1, and a float written there reads back FAIL_UnsupportedParameter while every
// uint lands, so the real slot is found by round-tripping a value.
//
// Only the setters differ. Typed Get() works normally, which is what made probing possible at all.
//
// None of this needs a forwarder. The caller check lives in the model's own entry points, and those
// are called by the driver on this path -- the block's setters are the driver's and check nothing.
constexpr int VT_SET_ULL = 0;
constexpr int VT_SET_UINT = 3;

int g_floatSlot = -1;

using PFN_SetULL = void(__thiscall*)(void*, const char*, unsigned long long);
using PFN_SetFloat = void(__thiscall*)(void*, const char*, float);
using PFN_SetUInt = void(__thiscall*)(void*, const char*, unsigned int);

void SetUInt(void* params, const char* name, unsigned int v)
{
    void** vt = *reinterpret_cast<void***>(params);
    reinterpret_cast<PFN_SetUInt>(vt[VT_SET_UINT])(params, name, v);
}

void SetResource(void* params, const char* name, ID3D12Resource* v)
{
    void** vt = *reinterpret_cast<void***>(params);
    reinterpret_cast<PFN_SetULL>(vt[VT_SET_ULL])(params, name, (unsigned long long) v);
}

// Find the float slot by writing a known value through each candidate and reading it back with the
// typed getter, which does work. Done once, before anything else is written.
void DiscoverFloatSlot(NVSDK_NGX_Parameter* params)
{
    if (g_floatSlot >= 0)
        return;

    void** vt = *reinterpret_cast<void***>(params);

    for (int slot = 0; slot < 8; ++slot)
    {
        const float probe = 0.3125f; // exact in binary, so a read-back compares cleanly
        float readBack = 0.0f;

        reinterpret_cast<PFN_SetFloat>(vt[slot])(params, "DLSSNR.Probe", probe);

        if (params->Get("DLSSNR.Probe", &readBack) == NVSDK_NGX_Result_Success && readBack == probe)
        {
            g_floatSlot = slot;
            LOG_INFO("DLSS-NR (proxy): float parameters go through vtable slot {}", slot);
            return;
        }
    }

    g_floatSlot = 1; // the header's answer, as a last resort
    LOG_WARN("DLSS-NR (proxy): no float slot round-tripped; falling back to the header's slot 1");
}

void SetFloat(void* params, const char* name, float v)
{
    void** vt = *reinterpret_cast<void***>(params);
    reinterpret_cast<PFN_SetFloat>(vt[g_floatSlot < 0 ? 1 : g_floatSlot])(params, name, v);
}

struct ProxyState
{
    NVSDK_NGX_Handle* feature = nullptr;
    NVSDK_NGX_Parameter* params = nullptr;

    unsigned int width = 0;
    unsigned int height = 0;

    bool failed = false;
};

ProxyState g_proxy;

// Everything the model reads when the feature is built.
//
// These have to be set before create, not at evaluate. The model reads its tuning once, while
// building the feature; values written only at evaluate are ignored, which is why several of these
// controls appeared to do nothing for a long time.
void SetCreationParameters(NVSDK_NGX_Parameter* params, const Config& cfg, unsigned int width,
                           unsigned int height)
{
    SetUInt(params, "DLSSNR.Enabled", 1u);
    SetUInt(params, "DLSSNR.Width", width);
    SetUInt(params, "DLSSNR.Height", height);
    SetUInt(params, "CreationNodeMask", 1u);
    SetUInt(params, "VisibilityNodeMask", 1u);

    // Written unconditionally, zero included. The block outlives the feature, so skipping the write
    // for "default" leaves whichever preset was chosen last still sitting in it, and going back to
    // default does nothing at all.
    SetUInt(params, "DLSSNR.Hint.Render.Preset", (unsigned int) cfg.DlssNrPreset.value_or_default());

    SetFloat(params, "DLSSNR.Intensity", cfg.DlssNrIntensity.value_or_default());
    SetUInt(params, "DLSSNR.Style", (unsigned int) cfg.DlssNrStyle.value_or_default());
    SetFloat(params, "DLSSNR.LocalStructureStrength", cfg.DlssNrLocalStructure.value_or_default());
    SetFloat(params, "DLSSNR.LocalToneStrength", cfg.DlssNrLocalTone.value_or_default());
    SetFloat(params, "DLSSNR.SkinStructureStrength", cfg.DlssNrSkinStructure.value_or_default());
    SetUInt(params, "DLSSNR.UseAutoMask", cfg.DlssNrAutoMask.value_or_default() ? 1u : 0u);

    // UI correction at the model's own default: with no UI layer fed to it there is nothing to
    // correct.
    SetUInt(params, "DLSSNR.UICorrection", 1u);
}
} // namespace

namespace DlssNr
{
namespace Proxy
{
bool Available()
{
    return NVNGXProxy::IsDx12Inited() && NVNGXProxy::D3D12_GetCapabilityParameters() != nullptr &&
           NVNGXProxy::D3D12_CreateFeature() != nullptr && NVNGXProxy::D3D12_EvaluateFeature() != nullptr;
}

void Release()
{
    if (g_proxy.feature != nullptr && NVNGXProxy::D3D12_ReleaseFeature() != nullptr)
        NVNGXProxy::D3D12_ReleaseFeature()(g_proxy.feature);

    // The capability block belongs to the driver core and is shared with the game's own DLSS, so it
    // is dropped here, never destroyed.
    g_proxy.feature = nullptr;
    g_proxy.params = nullptr;
    g_proxy.width = 0;
    g_proxy.height = 0;
}

unsigned int Run(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device, ID3D12Resource* color,
                 ID3D12Resource* depth, ID3D12Resource* motion, ID3D12Resource* output,
                 unsigned int width, unsigned int height, unsigned int guideWidth,
                 unsigned int guideHeight, bool depthInverted, bool reset, float mvScaleX,
                 float mvScaleY)
{
    if (g_proxy.failed || !Available())
        return 0;

    const Config& cfg = *Config::Instance();

    if (g_proxy.feature != nullptr && (g_proxy.width != width || g_proxy.height != height))
        Release();

    if (g_proxy.params == nullptr)
    {
        // The driver core's own capability block, not a freshly allocated one.
        //
        // A fresh block was the first attempt and CreateFeature answered
        // FAIL_UnableToInitializeFeature -- the same code the probe got from an empty block, which
        // should have been the clue. The capability block carries the snippet and preset callbacks
        // a feature expects at create time; an allocated block has none of them, so the dispatcher
        // finds the feature and then has nothing to build it with.
        //
        // The cost is that this block is shared with the game's own DLSS, which overwrites values
        // between frames. That is why everything the feature reads is written again at evaluate
        // below rather than trusted to survive from create.
        if (NVNGXProxy::D3D12_GetCapabilityParameters()(&g_proxy.params) != NVSDK_NGX_Result_Success ||
            g_proxy.params == nullptr)
        {
            g_proxy.failed = true;
            g_proxy.params = nullptr;
            LOG_ERROR("DLSS-NR (proxy): the NGX core refused its capability parameters");
            return 0;
        }
    }

    if (g_proxy.feature == nullptr)
    {
        // Re-initialise the core at the SDK version the model expects, before asking for it.
        //
        // This is the last difference between the two paths. The forwarder calls the snippet's own
        // Init_Ext with SDK 0x15 -- 21 -- while the driver core in a real game is initialised with
        // whatever version the game declares, and Cyberpunk declares 15. A feature that postdates
        // SDK 15, requested from a core initialised at 15, failing with UnableToInitializeFeature is
        // exactly the shape of what we see.
        //
        // The application id is the one the forwarder uses rather than the game's, for the same
        // reason: it is what the working path passes.
        //
        // This is intrusive. It re-initialises the core the game's own DLSS is using, which is why
        // the whole proxy path is off by default and why this happens once, guarded, rather than
        // every frame.
        static bool reinitialised = false;

        if (!reinitialised && NVNGXProxy::D3D12_Init_Ext() != nullptr && device != nullptr)
        {
            reinitialised = true;

            NVSDK_NGX_FeatureCommonInfo fcInfo {};
            NVNGXProxy::GetFeatureCommonInfo(&fcInfo);

            const auto initResult = NVNGXProxy::D3D12_Init_Ext()(
                0x4350324Bull, State::Instance().NVNGX_ApplicationDataPath.c_str(), device,
                (NVSDK_NGX_Version) 0x0000015, &fcInfo);

            // Success here means very little. NGX init is idempotent: a second call on an already
            // initialised core returns success and changes nothing, which the driver's own log
            // confirms -- it reports the app id it was first initialised with, not the one passed
            // here. So this does not test the SDK version or the application id, and cannot without
            // shutting NGX down underneath the game's own DLSS.
            LOG_INFO("DLSS-NR (proxy): re-init at SDK 0x15 returned 0x{:X} (idempotent -- this does "
                     "not change the app id or SDK version the core is running with)",
                     (unsigned int) initResult);
        }

        DiscoverFloatSlot(g_proxy.params);
        SetCreationParameters(g_proxy.params, cfg, width, height);

        const auto created =
            NVNGXProxy::D3D12_CreateFeature()(cmdList, (NVSDK_NGX_Feature) 18, g_proxy.params, &g_proxy.feature);

        if (created != NVSDK_NGX_Result_Success || g_proxy.feature == nullptr)
        {
            g_proxy.failed = true;
            g_proxy.feature = nullptr;
            LOG_ERROR("DLSS-NR (proxy): CreateFeature(18) failed 0x{:X} -- falling back is the "
                      "caller's decision",
                      (unsigned int) created);
            return 0;
        }

        g_proxy.width = width;
        g_proxy.height = height;
        LOG_INFO("DLSS-NR (proxy): feature created at {}x{} through the driver's nvngx -- no "
                 "forwarder in this path",
                 width, height);
    }

    NVSDK_NGX_Parameter* params = g_proxy.params;

    SetResource(params, "DLSSNR.Color", color);
    SetResource(params, "DLSSNR.Depth", depth);
    SetResource(params, "DLSSNR.MVec", motion);
    SetResource(params, "DLSSNR.Output", output);

    SetUInt(params, "DLSSNR.Enabled", 1u);
    SetUInt(params, "DLSSNR.Width", width);
    SetUInt(params, "DLSSNR.Height", height);
    SetUInt(params, "DLSSNR.DepthInverted", depthInverted ? 1u : 0u);
    SetUInt(params, "DLSSNR.Reset", reset ? 1u : 0u);

    // Colour and output are display resolution; depth and motion come from the game's own DLSS
    // evaluation and may be render resolution, so each resource carries its own subrect.
    SetUInt(params, "DLSSNR.ColorSubrectBaseX", 0u);
    SetUInt(params, "DLSSNR.ColorSubrectBaseY", 0u);
    SetUInt(params, "DLSSNR.ColorSubrectWidth", width);
    SetUInt(params, "DLSSNR.ColorSubrectHeight", height);
    SetUInt(params, "DLSSNR.OutputSubrectBaseX", 0u);
    SetUInt(params, "DLSSNR.OutputSubrectBaseY", 0u);
    SetUInt(params, "DLSSNR.OutputSubrectWidth", width);
    SetUInt(params, "DLSSNR.OutputSubrectHeight", height);
    SetUInt(params, "DLSSNR.DepthSubrectBaseX", 0u);
    SetUInt(params, "DLSSNR.DepthSubrectBaseY", 0u);
    SetUInt(params, "DLSSNR.DepthSubrectWidth", guideWidth);
    SetUInt(params, "DLSSNR.DepthSubrectHeight", guideHeight);
    SetUInt(params, "DLSSNR.MVecSubrectBaseX", 0u);
    SetUInt(params, "DLSSNR.MVecSubrectBaseY", 0u);
    SetUInt(params, "DLSSNR.MVecSubrectWidth", guideWidth);
    SetUInt(params, "DLSSNR.MVecSubrectHeight", guideHeight);

    // The game's own encoding, passed through. Deriving this from the resolutions was a guess, and
    // at native resolution it came out as exactly 1.0 -- so a game using normalised vectors was
    // telling the model that almost nothing had moved.
    SetFloat(params, "DLSSNR.MVecScaleX", mvScaleX);
    SetFloat(params, "DLSSNR.MVecScaleY", mvScaleY);

    SetFloat(params, "DLSSNR.Intensity", cfg.DlssNrIntensity.value_or_default());
    SetUInt(params, "DLSSNR.Style", (unsigned int) cfg.DlssNrStyle.value_or_default());
    SetFloat(params, "DLSSNR.LocalStructureStrength", cfg.DlssNrLocalStructure.value_or_default());
    SetFloat(params, "DLSSNR.LocalToneStrength", cfg.DlssNrLocalTone.value_or_default());
    SetFloat(params, "DLSSNR.SkinStructureStrength", cfg.DlssNrSkinStructure.value_or_default());
    SetUInt(params, "DLSSNR.UseAutoMask", cfg.DlssNrAutoMask.value_or_default() ? 1u : 0u);

    const auto result =
        NVNGXProxy::D3D12_EvaluateFeature()(cmdList, g_proxy.feature, params, nullptr);

    return (unsigned int) result;
}
} // namespace Proxy
} // namespace DlssNr

#endif // OPTI_DLSSNR
