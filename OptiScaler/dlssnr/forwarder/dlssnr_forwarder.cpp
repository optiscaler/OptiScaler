// DLSS Neural Rendering calls, isolated in a module the snippet will accept as a caller.
//
// The snippet resolves the module owning its return address and requires that module's path to contain
// "nvngx.dll" (the driver core is _nvngx.dll), rejecting anything else with FAIL_PlatformError before it
// inspects a single argument. Neither a ReShade add-on nor OptiScaler is named anything like that, so the
// calls are made from here instead and reached through the exports below.
//
// The parameter block is the core's capability block rather than a fresh one: it carries the snippet and
// preset callbacks a feature expects at create time. The core exports no Set/Get helpers (they are
// static-library inlines), so it is driven through its vtable. NVSDK_NGX_Parameter declares eight Set
// overloads then eight Get overloads, in this order: ULL, float, double, uint, int, ID3D11Resource*,
// ID3D12Resource*, void*.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

namespace {

// Slot indices confirmed by round-tripping values through the live block: a setter at N is read back by
// the getter at N+8. The unsigned setter is slot 3 (a feature create driven through it succeeds) and the
// resource getter answers at slot 8, so resources are written through slot 0 -- the 64-bit setter, which
// is what a resource handle is. Writing them through the typed D3D12 setter left them unset.
constexpr int VT_SET_ULL = 0;
// Where the float setter actually lives. The public header declares it at slot 1, and this block --
// the driver's own, not the header's implementation -- does not keep a float there: every float written
// to slot 1 reads back as FAIL_UnsupportedParameter while every uint lands. The host discovers the real
// slot by round-tripping a value and sets it here before anything else is written.
int g_floatSlot = 1;
constexpr int VT_SET_UINT = 3;

using PFN_SetULL = void(__thiscall *)(void *, const char *, unsigned long long);
using PFN_SetFloat = void(__thiscall *)(void *, const char *, float);
using PFN_SetUInt = void(__thiscall *)(void *, const char *, unsigned int);

void setUInt(void *params, const char *name, unsigned int v) {
    void **vt = *reinterpret_cast<void ***>(params);
    reinterpret_cast<PFN_SetUInt>(vt[VT_SET_UINT])(params, name, v);
}

void setFloat(void *params, const char *name, float v) {
    void **vt = *reinterpret_cast<void ***>(params);
    reinterpret_cast<PFN_SetFloat>(vt[g_floatSlot])(params, name, v);
}

void setResource(void *params, const char *name, ID3D12Resource *v) {
    void **vt = *reinterpret_cast<void ***>(params);
    reinterpret_cast<PFN_SetULL>(vt[VT_SET_ULL])(params, name, (unsigned long long) v);
}

using PFN_NrInitExt = int(__cdecl *)(unsigned long long, const wchar_t *, ID3D12Device *, int,
                                     const void *);
using PFN_NrCreate = int(__cdecl *)(ID3D12GraphicsCommandList *, int, const void *, void **);
using PFN_NrEvaluate = int(__cdecl *)(ID3D12GraphicsCommandList *, const void *, const void *, void *);
using PFN_NrRelease = int(__cdecl *)(void *);

struct Snippet {
    HMODULE module = nullptr;
    PFN_NrInitExt init = nullptr;
    PFN_NrCreate create = nullptr;
    PFN_NrEvaluate evaluate = nullptr;
    PFN_NrRelease release = nullptr;
    bool initialised = false;
};

Snippet g_snip;

bool loadSnippet(const wchar_t *path) {
    if (g_snip.module) {
        return g_snip.create != nullptr;
    }
    g_snip.module = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_snip.module) {
        return false;
    }
    g_snip.init = (PFN_NrInitExt) GetProcAddress(g_snip.module, "NVSDK_NGX_D3D12_Init_Ext");
    g_snip.create = (PFN_NrCreate) GetProcAddress(g_snip.module, "NVSDK_NGX_D3D12_CreateFeature");
    g_snip.evaluate = (PFN_NrEvaluate) GetProcAddress(g_snip.module, "NVSDK_NGX_D3D12_EvaluateFeature");
    g_snip.release = (PFN_NrRelease) GetProcAddress(g_snip.module, "NVSDK_NGX_D3D12_ReleaseFeature");
    return g_snip.create != nullptr && g_snip.evaluate != nullptr;
}

} // namespace

extern "C" {

// Called once, after the host has worked out which slot this block keeps floats in.
__declspec(dllexport) void dlssnr_call_set_float_slot(int slot) {
    if (slot >= 0 && slot < 8) {
        g_floatSlot = slot;
    }
}

// Writes a float through an arbitrary slot, so the host can find the right one by testing.
__declspec(dllexport) void dlssnr_call_probe_float(void *params, const char *name, float value,
                                                   int slot) {
    if (!params || slot < 0 || slot >= 8) {
        return;
    }
    void **vt = *reinterpret_cast<void ***>(params);
    reinterpret_cast<PFN_SetFloat>(vt[slot])(params, name, value);
}

// Last init and create results, so the add-on can log why a feature never appeared.
__declspec(dllexport) int dlssnr_call_last_init = 0;
__declspec(dllexport) int dlssnr_call_last_create = 0;

// Creates a persistent Neural Rendering feature. The handle records initialisation work into cmd, so it
// must outlive that command list's execution; releasing it early loses the device.
__declspec(dllexport) void *dlssnr_call_create(const wchar_t *snippetPath, const wchar_t *dataPath,
                                               ID3D12Device *device, ID3D12GraphicsCommandList *cmd,
                                               void *capabilityParams, unsigned int width,
                                               unsigned int height, int preset, float intensity,
                                               int style, float localStructure, float localTone,
                                               float skinStructure, int useAutoMask,
                                               int uiCorrection) {
    if (!loadSnippet(snippetPath) || !capabilityParams) {
        return nullptr;
    }
    if (!g_snip.initialised && g_snip.init) {
        // OptiScaler's own generic application id, the one it already hands DLSS when a game's id is
        // not wanted. What was here before was 0x4350324B -- "CP2K" -- so every game that ever loaded
        // this announced itself to the driver as Cyberpunk 2077.
        dlssnr_call_last_init = g_snip.init(0x24480451ull, dataPath, device, 0x0000015, capabilityParams);
        g_snip.initialised = (dlssnr_call_last_init == 1);
        if (!g_snip.initialised) {
            return nullptr;
        }
    }
    setUInt(capabilityParams, "DLSSNR.Enabled", 1);
    setUInt(capabilityParams, "DLSSNR.Width", width);
    setUInt(capabilityParams, "DLSSNR.Height", height);
    setUInt(capabilityParams, "CreationNodeMask", 1);
    setUInt(capabilityParams, "VisibilityNodeMask", 1);
    // Written unconditionally, including zero. This block belongs to the driver and outlives the
    // feature, so skipping the write for "default" left whichever preset was chosen last still sitting
    // in it -- and going back to default did nothing at all.
    setUInt(capabilityParams, "DLSSNR.Hint.Render.Preset", (unsigned int) preset);

    // The tuning has to be here rather than at evaluate. Everything this sets before create takes
    // effect; everything set only at evaluate is ignored, which is why none of these controls did
    // anything for a long time. The model reads them once, when it builds the feature.
    setFloat(capabilityParams, "DLSSNR.Intensity", intensity);
    setUInt(capabilityParams, "DLSSNR.Style", (unsigned int) style);
    setFloat(capabilityParams, "DLSSNR.LocalStructureStrength", localStructure);
    setFloat(capabilityParams, "DLSSNR.LocalToneStrength", localTone);
    setFloat(capabilityParams, "DLSSNR.SkinStructureStrength", skinStructure);
    setUInt(capabilityParams, "DLSSNR.UseAutoMask", (unsigned int) useAutoMask);
    setUInt(capabilityParams, "DLSSNR.UICorrection", (unsigned int) uiCorrection);
    void *handle = nullptr;
    dlssnr_call_last_create = g_snip.create(cmd, 18, capabilityParams, &handle);
    return handle;
}

// Colour and output are display resolution; depth and motion come from the game's own DLSS evaluation and
// may be render resolution, so each resource carries its own subrect and motion scales by the ratio.
__declspec(dllexport) int dlssnr_call_evaluate(ID3D12GraphicsCommandList *cmd, void *feature,
                                               void *capabilityParams, ID3D12Resource *color,
                                               ID3D12Resource *depth, ID3D12Resource *motion,
                                               ID3D12Resource *output, unsigned int width,
                                               unsigned int height, unsigned int guideWidth,
                                               unsigned int guideHeight, int depthInverted, int reset,
                                               float intensity, int style, float localStructure,
                                               float localTone, float skinStructure, int useAutoMask,
                                               float mvScaleX, float mvScaleY) {
    if (!feature || !capabilityParams || !g_snip.evaluate) {
        return 0;
    }
    setResource(capabilityParams, "DLSSNR.Color", color);
    setResource(capabilityParams, "DLSSNR.Depth", depth);
    setResource(capabilityParams, "DLSSNR.MVec", motion);
    setResource(capabilityParams, "DLSSNR.Output", output);

    // The block is shared with the game's own DLSS, which overwrites these between frames, so every
    // value the feature reads is set again here rather than relying on what create left behind.
    setUInt(capabilityParams, "DLSSNR.Enabled", 1);
    setUInt(capabilityParams, "DLSSNR.Width", width);
    setUInt(capabilityParams, "DLSSNR.Height", height);
    setUInt(capabilityParams, "DLSSNR.DepthInverted", (unsigned int) depthInverted);
    setUInt(capabilityParams, "DLSSNR.Reset", (unsigned int) reset);

    setUInt(capabilityParams, "DLSSNR.ColorSubrectBaseX", 0);
    setUInt(capabilityParams, "DLSSNR.ColorSubrectBaseY", 0);
    setUInt(capabilityParams, "DLSSNR.ColorSubrectWidth", width);
    setUInt(capabilityParams, "DLSSNR.ColorSubrectHeight", height);
    setUInt(capabilityParams, "DLSSNR.OutputSubrectBaseX", 0);
    setUInt(capabilityParams, "DLSSNR.OutputSubrectBaseY", 0);
    setUInt(capabilityParams, "DLSSNR.OutputSubrectWidth", width);
    setUInt(capabilityParams, "DLSSNR.OutputSubrectHeight", height);
    setUInt(capabilityParams, "DLSSNR.DepthSubrectBaseX", 0);
    setUInt(capabilityParams, "DLSSNR.DepthSubrectBaseY", 0);
    setUInt(capabilityParams, "DLSSNR.DepthSubrectWidth", guideWidth);
    setUInt(capabilityParams, "DLSSNR.DepthSubrectHeight", guideHeight);
    setUInt(capabilityParams, "DLSSNR.MVecSubrectBaseX", 0);
    setUInt(capabilityParams, "DLSSNR.MVecSubrectBaseY", 0);
    setUInt(capabilityParams, "DLSSNR.MVecSubrectWidth", guideWidth);
    setUInt(capabilityParams, "DLSSNR.MVecSubrectHeight", guideHeight);

    // The game's own encoding, passed through. Deriving this from the resolutions was a guess, and at
    // native resolution it came out as exactly 1.0 -- so a game using normalised vectors was telling
    // the model almost nothing had moved.
    setFloat(capabilityParams, "DLSSNR.MVecScaleX", mvScaleX);
    setFloat(capabilityParams, "DLSSNR.MVecScaleY", mvScaleY);

    setFloat(capabilityParams, "DLSSNR.Intensity", intensity);
    setUInt(capabilityParams, "DLSSNR.Style", (unsigned int) style);
    setFloat(capabilityParams, "DLSSNR.LocalStructureStrength", localStructure);
    setFloat(capabilityParams, "DLSSNR.LocalToneStrength", localTone);
    setFloat(capabilityParams, "DLSSNR.SkinStructureStrength", skinStructure);
    setUInt(capabilityParams, "DLSSNR.UseAutoMask", (unsigned int) useAutoMask);

    // The result must not be returned directly. `return f(...)` is a tail call, and the compiler emits a
    // jmp rather than a call, which leaves this module's frame behind: the snippet then resolves its
    // caller to whoever called us and rejects it. Keeping the value in a volatile forces a real call and
    // a return through this module, which is the whole reason this file exists.
    volatile int result = g_snip.evaluate(cmd, feature, capabilityParams, nullptr);
    return result;
}

// Inputs NVIDIA's own Streamline plugin sets that the positional exports predate: the model's global
// tone strength (read at create), and the interface as the game draws it -- its layer, its alpha, and
// the composited back buffer -- which is what the model's UI correction was designed around. Called
// before create and before every evaluate; absent resources are written as null, because the block
// outlives everything and a stale pointer is a freed resource.
__declspec(dllexport) void dlssnr_call_set_extras(void *capabilityParams, float globalTone,
                                                  ID3D12Resource *ui, ID3D12Resource *uiAlpha,
                                                  ID3D12Resource *backbuffer, unsigned int uiWidth,
                                                  unsigned int uiHeight, unsigned int bbWidth,
                                                  unsigned int bbHeight) {
    if (!capabilityParams) {
        return;
    }
    setFloat(capabilityParams, "DLSSNR.GlobalToneStrength", globalTone);
    setResource(capabilityParams, "DLSSNR.UI", ui);
    setResource(capabilityParams, "DLSSNR.UIAlpha", uiAlpha);
    setResource(capabilityParams, "DLSSNR.Backbuffer", backbuffer);
    setUInt(capabilityParams, "DLSSNR.UISubrectBaseX", 0);
    setUInt(capabilityParams, "DLSSNR.UISubrectBaseY", 0);
    setUInt(capabilityParams, "DLSSNR.UISubrectWidth", uiWidth);
    setUInt(capabilityParams, "DLSSNR.UISubrectHeight", uiHeight);
    setUInt(capabilityParams, "DLSSNR.UIAlphaSubrectBaseX", 0);
    setUInt(capabilityParams, "DLSSNR.UIAlphaSubrectBaseY", 0);
    setUInt(capabilityParams, "DLSSNR.UIAlphaSubrectWidth", uiWidth);
    setUInt(capabilityParams, "DLSSNR.UIAlphaSubrectHeight", uiHeight);
    setUInt(capabilityParams, "DLSSNR.BackbufferSubrectBaseX", 0);
    setUInt(capabilityParams, "DLSSNR.BackbufferSubrectBaseY", 0);
    setUInt(capabilityParams, "DLSSNR.BackbufferSubrectWidth", bbWidth);
    setUInt(capabilityParams, "DLSSNR.BackbufferSubrectHeight", bbHeight);
}

__declspec(dllexport) void dlssnr_call_release(void *feature) {
    if (feature && g_snip.release) {
        volatile int result = g_snip.release(feature); // not a tail call, for the reason above
        (void) result;
    }
}

} // extern "C"
