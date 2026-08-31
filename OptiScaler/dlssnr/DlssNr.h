#pragma once

// DLSS 5 Neural Rendering for OptiScaler.
//
// Two halves, and only one of them is a shader.
//
// The model is an NGX feature -- created and evaluated rather than dispatched -- and it lives here,
// under OptiScaler/dlssnr/. The composition pass, which builds the proxy the model is shown and
// transfers its answer back onto the frame, is an ordinary compute shader and lives with the others
// under OptiScaler/shaders/dlssnr/.
//
// Call sites, for the record:
//   inputs/NVNGX_DLSS_Dx12.cpp        the pass after an upscale
//   upscalers/IFeature_Dx11wDx12.cpp  the pass inside the D3D11-on-D3D12 bridge
//   menu/menu_common.cpp              the settings panel

#include "DlssNr_Dx12.h"
