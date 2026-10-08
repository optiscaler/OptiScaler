#pragma once
#include "SysUtils.h"
#include <NVNGX_Parameter.h>
#include <upscalers/IFeature_Dx12.h>

// XeFG input: the game's libxess_fg calls feed the selected FG output
namespace XeFGInputs
{
void Hook(HMODULE libxessFg);

// Depth and motion vectors from the upscaler for frames the game doesn't tag them (Cyberpunk 2077 with DLSS)
void SetUpscalerInputs(ID3D12GraphicsCommandList* cmdList, NVSDK_NGX_Parameter* parameters, IFeature_Dx12* feature);

// XeFG output: the game's own XeFG runs; Override XeFG Ratio sets its count
bool Passthrough();
uint32_t MaxInterpolations();
} // namespace XeFGInputs
