#pragma once

// The one switch that turns DLSS 5 Neural Rendering off.
//
// Set it to 0 -- here, or as a project-wide preprocessor definition -- and the feature compiles out
// completely: the eight guarded call sites in OptiScaler's own code vanish, the three module sources
// become empty translation units, the menu section disappears and the [DlssNr] config block is not
// even declared. The resulting build carries no trace of it.
//
// This header holds nothing but the macro, so Config.h can test it without pulling in D3D12.

#ifndef OPTI_DLSSNR
#define OPTI_DLSSNR 1
#endif
