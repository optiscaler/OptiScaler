#pragma once

#include "SysUtils.h"

#include <d3d12.h>
#include <xess_d3d12.h>

// Intel's UE XeSS plugin runs XeSS with display resolution motion vectors and without depth. Its velocity flattening
// is hooked to get the scene depth and render resolution motion vectors.
namespace XeSSUnreal
{
// Finds and hooks the plugin's velocity flattening, called whenever the game sets up XeSS
void Hook();

// The plugin was found, its depth is reversed Z
bool Hooked();

// Copy of this frame's scene depth for an execute without depth, filled when commandList is submitted, and the state
// it is in. nullptr when the scene depth isn't known or copies stopped.
ID3D12Resource* SceneDepth(ID3D12GraphicsCommandList* commandList, const xess_d3d12_execute_params_t* params,
                           D3D12_RESOURCE_STATES& state);
} // namespace XeSSUnreal
