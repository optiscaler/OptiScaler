#include "pch.h"
#include "DlssNr_Proxy.h"

#if OPTI_DLSSNR

#include <Config.h>
#include <Logger.h>
#include <proxies/NVNGX_Proxy.h>

namespace
{
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
    params->Set("DLSSNR.Enabled", 1u);
    params->Set("DLSSNR.Width", width);
    params->Set("DLSSNR.Height", height);
    params->Set("CreationNodeMask", 1u);
    params->Set("VisibilityNodeMask", 1u);

    // Written unconditionally, zero included. The block outlives the feature, so skipping the write
    // for "default" leaves whichever preset was chosen last still sitting in it, and going back to
    // default does nothing at all.
    params->Set("DLSSNR.Hint.Render.Preset", (unsigned int) cfg.DlssNrPreset.value_or_default());

    params->Set("DLSSNR.Intensity", cfg.DlssNrIntensity.value_or_default());
    params->Set("DLSSNR.Style", (unsigned int) cfg.DlssNrStyle.value_or_default());
    params->Set("DLSSNR.LocalStructureStrength", cfg.DlssNrLocalStructure.value_or_default());
    params->Set("DLSSNR.LocalToneStrength", cfg.DlssNrLocalTone.value_or_default());
    params->Set("DLSSNR.SkinStructureStrength", cfg.DlssNrSkinStructure.value_or_default());
    params->Set("DLSSNR.UseAutoMask", cfg.DlssNrAutoMask.value_or_default() ? 1u : 0u);

    // UI correction at the model's own default: with no UI layer fed to it there is nothing to
    // correct.
    params->Set("DLSSNR.UICorrection", 1u);
}
} // namespace

namespace DlssNr
{
namespace Proxy
{
bool Available()
{
    return NVNGXProxy::IsDx12Inited() && NVNGXProxy::D3D12_AllocateParameters() != nullptr &&
           NVNGXProxy::D3D12_CreateFeature() != nullptr && NVNGXProxy::D3D12_EvaluateFeature() != nullptr;
}

void Release()
{
    if (g_proxy.feature != nullptr && NVNGXProxy::D3D12_ReleaseFeature() != nullptr)
        NVNGXProxy::D3D12_ReleaseFeature()(g_proxy.feature);

    if (g_proxy.params != nullptr && NVNGXProxy::D3D12_DestroyParameters() != nullptr)
        NVNGXProxy::D3D12_DestroyParameters()(g_proxy.params);

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
    (void) device;

    if (g_proxy.failed || !Available())
        return 0;

    const Config& cfg = *Config::Instance();

    if (g_proxy.feature != nullptr && (g_proxy.width != width || g_proxy.height != height))
        Release();

    if (g_proxy.params == nullptr)
    {
        // Our own block, not the one the game's DLSS is using. That is the point: the forwarder
        // writes into a shared block which the game overwrites between frames, and every value has
        // to be rewritten each evaluate to survive it. A block allocated here belongs to this
        // feature and nothing else touches it.
        if (NVNGXProxy::D3D12_AllocateParameters()(&g_proxy.params) != NVSDK_NGX_Result_Success ||
            g_proxy.params == nullptr)
        {
            g_proxy.failed = true;
            LOG_ERROR("DLSS-NR (proxy): could not allocate a parameter block");
            return 0;
        }
    }

    if (g_proxy.feature == nullptr)
    {
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

    params->Set("DLSSNR.Color", color);
    params->Set("DLSSNR.Depth", depth);
    params->Set("DLSSNR.MVec", motion);
    params->Set("DLSSNR.Output", output);

    params->Set("DLSSNR.Enabled", 1u);
    params->Set("DLSSNR.Width", width);
    params->Set("DLSSNR.Height", height);
    params->Set("DLSSNR.DepthInverted", depthInverted ? 1u : 0u);
    params->Set("DLSSNR.Reset", reset ? 1u : 0u);

    // Colour and output are display resolution; depth and motion come from the game's own DLSS
    // evaluation and may be render resolution, so each resource carries its own subrect.
    params->Set("DLSSNR.ColorSubrectBaseX", 0u);
    params->Set("DLSSNR.ColorSubrectBaseY", 0u);
    params->Set("DLSSNR.ColorSubrectWidth", width);
    params->Set("DLSSNR.ColorSubrectHeight", height);
    params->Set("DLSSNR.OutputSubrectBaseX", 0u);
    params->Set("DLSSNR.OutputSubrectBaseY", 0u);
    params->Set("DLSSNR.OutputSubrectWidth", width);
    params->Set("DLSSNR.OutputSubrectHeight", height);
    params->Set("DLSSNR.DepthSubrectBaseX", 0u);
    params->Set("DLSSNR.DepthSubrectBaseY", 0u);
    params->Set("DLSSNR.DepthSubrectWidth", guideWidth);
    params->Set("DLSSNR.DepthSubrectHeight", guideHeight);
    params->Set("DLSSNR.MVecSubrectBaseX", 0u);
    params->Set("DLSSNR.MVecSubrectBaseY", 0u);
    params->Set("DLSSNR.MVecSubrectWidth", guideWidth);
    params->Set("DLSSNR.MVecSubrectHeight", guideHeight);

    // The game's own encoding, passed through. Deriving this from the resolutions was a guess, and
    // at native resolution it came out as exactly 1.0 -- so a game using normalised vectors was
    // telling the model that almost nothing had moved.
    params->Set("DLSSNR.MVecScaleX", mvScaleX);
    params->Set("DLSSNR.MVecScaleY", mvScaleY);

    const auto result =
        NVNGXProxy::D3D12_EvaluateFeature()(cmdList, g_proxy.feature, params, nullptr);

    return (unsigned int) result;
}
} // namespace Proxy
} // namespace DlssNr

#endif // OPTI_DLSSNR
