#pragma once

// Neural Rendering through the driver's own nvngx.dll, instead of through a forwarder.
//
// The forwarder exists for one reason: the model resolves its caller's return address to a module
// and refuses anything whose path does not contain "nvngx.dll". Driving the model directly means
// satisfying that check, which means shipping a small DLL named to satisfy it, which means the
// model's own NGX entry points are called by us rather than by the runtime that was meant to call
// them.
//
// But the model ships inside the driver store, and NVIDIA does not ship a feature DLL that no
// dispatcher can reach. A probe settled it: asking the driver's nvngx.dll to create feature 18
// returns FAIL_UnableToInitializeFeature, while asking for a feature that does not exist returns
// FAIL_OutOfDate. Different answers to different ids means the dispatcher knows this one -- it found
// the feature and failed to start it on the empty parameter block the probe deliberately passed.
//
// OptiScaler already tells the driver where to look. NVNGX_FeatureInfo_Paths carries the executable
// folder and OptiScaler's own folder into Init_Ext, which is exactly the mechanism that finds
// nvngx_dlss.dll and friends. Nothing new is needed to locate the model.
//
// What this path gains, beyond deleting the forwarder:
//
//   * Its own parameter block. The forwarder writes into the block the game's DLSS is using, which
//     the game overwrites between frames -- the reason every value has to be set again at evaluate.
//     A block allocated for this feature belongs to this feature.
//   * The SDK's typed setters. The slot hunting in the forwarder (floats at vtable slot 6, not the
//     header's 1) was a consequence of driving the model's own block by hand. A block from the
//     driver is an ordinary NGX parameter block.
//   * No anti-tail-call trick, because there is no caller check to satisfy.
//
// Off by default until it has been shown to produce the same picture as the forwarder path.

#include "DlssNr_Switch.h"

#if OPTI_DLSSNR

#include <d3d12.h>

namespace DlssNr
{
namespace Proxy
{
// True when the driver's nvngx is initialised and exports what this path needs.
bool Available();

// Creates the feature if it does not exist, or if the resolution changed, and evaluates it.
// Returns the NGX result of the evaluate, or 0 when nothing could be attempted.
unsigned int Run(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device, ID3D12Resource* color,
                 ID3D12Resource* depth, ID3D12Resource* motion, ID3D12Resource* output,
                 unsigned int width, unsigned int height, unsigned int guideWidth,
                 unsigned int guideHeight, bool depthInverted, bool reset, float mvScaleX,
                 float mvScaleY);

// Drops the feature and its parameter block, for a resolution change or shutdown.
void Release();
} // namespace Proxy
} // namespace DlssNr

#endif // OPTI_DLSSNR
