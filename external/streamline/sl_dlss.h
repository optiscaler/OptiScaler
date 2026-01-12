/*
* Copyright (c) 2022-2023 NVIDIA CORPORATION. All rights reserved
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/ 

#pragma once

#include <stdint.h>
#include "sl.h"

#if __cplusplus >= 201402L
#define SR_DEPRECATED_SHARPENING [[deprecated("Sharpness is not supported")]]
#else
#define SR_DEPRECATED_SHARPENING
#endif

namespace sl
{

enum class DLSSMode : uint32_t
{
    eOff,
    eMaxPerformance,
    eBalanced,
    eMaxQuality,
    eUltraPerformance,
    eUltraQuality,
    eDLAA,
    eCount,
};

enum class DLSSPreset : uint32_t
{
    //! Default behavior, may or may not change after an OTA
    eDefault,
    //! Fixed DL models
                    // ePresetA removed, use presets J or K
                    // ePresetB removed, use presets J or K
                    // ePresetC removed, use presets J or K
                    // ePresetD removed, use presets J or K
                    // ePresetE removed, use presets J or K
    ePresetF = 6,   // Intended for Ultra Perf/DLAA modes. The default preset for Ultra Perf
    ePresetG = 7,   // Reverts to default, not recommended to use
    ePresetH = 8,   // Reverts to default, not recommended to use
    ePresetI = 9,   // Reverts to default, not recommended to use
    ePresetJ = 10,  // Similar to preset K. Preset J might exhibit slightly less ghosting at the cost of extra flickering. Preset K is generally recommended over preset J
    ePresetK = 11,  // Default preset for DLAA/Perf/Balanced/Quality modes that is transformer based. Best image quality preset at a higher performance cost
    ePresetL = 12,  // Reverts to default, not recommended to use
    ePresetM = 13,  // Reverts to default, not recommended to use
    ePresetN = 14,  // Reverts to default, not recommended to use
    ePresetO = 15,  // Reverts to default, not recommended to use

    eCount
};

// {6AC826E4-4C61-4101-A92D-638D421057B8}
SL_STRUCT_BEGIN(DLSSOptions, StructType({ 0x6ac826e4, 0x4c61, 0x4101, { 0xa9, 0x2d, 0x63, 0x8d, 0x42, 0x10, 0x57, 0xb8 } }), kStructVersion3)
    //! Specifies which mode should be used
    DLSSMode mode = DLSSMode::eOff;
    //! Specifies output (final) target width
    uint32_t outputWidth = INVALID_UINT;
    //! Specifies output (final) target height
    uint32_t outputHeight = INVALID_UINT;
    //! Specifies sharpening level in range [0,1] this is a deprecated field
    float sharpness SR_DEPRECATED_SHARPENING = 0.0f;
    //! Specifies pre-exposure value
    float preExposure = 1.0f;
    //! Specifies exposure scale value
    float exposureScale = 1.0f;
    //! Specifies if tagged color buffers are full HDR or not (DLSS in HDR pipeline or not)
    Boolean colorBuffersHDR = Boolean::eTrue;
    //! Specifies if indicator on screen should invert axis
    Boolean indicatorInvertAxisX = Boolean::eFalse;
    //! Specifies if indicator on screen should invert axis
    Boolean indicatorInvertAxisY = Boolean::eFalse;
    //! Presets
    DLSSPreset dlaaPreset = DLSSPreset::eDefault;
    DLSSPreset qualityPreset = DLSSPreset::eDefault;
    DLSSPreset balancedPreset = DLSSPreset::eDefault;
    DLSSPreset performancePreset = DLSSPreset::eDefault;
    DLSSPreset ultraPerformancePreset = DLSSPreset::eDefault;
    DLSSPreset ultraQualityPreset = DLSSPreset::eDefault;

    //! Specifies if the setting for AutoExposure is used
    Boolean useAutoExposure = Boolean::eFalse;

    //! Whether or not the alpha channel should be upscaled (if false, only RGB is upscaled)
    //! Enabling alpha upscaling may impact performance
    Boolean alphaUpscalingEnabled = Boolean::eFalse;

    //! IMPORTANT: New members go here or if optional can be chained in a new struct, see sl_struct.h for details
SL_STRUCT_END()

//! Returned by DLSS plugin
//! 
//! {EF1D0957-FD58-4DF7-B504-8B69D8AA6B76}
SL_STRUCT_BEGIN(DLSSOptimalSettings, StructType({ 0xef1d0957, 0xfd58, 0x4df7, { 0xb5, 0x4, 0x8b, 0x69, 0xd8, 0xaa, 0x6b, 0x76 } }), kStructVersion1)
    //! Specifies render area width
    uint32_t optimalRenderWidth{};
    //! Specifies render area height
    uint32_t optimalRenderHeight{};
    //! Specifies the optimal sharpness value
    float optimalSharpness{};
    //! Specifies minimal render area width
    uint32_t renderWidthMin{};
    //! Specifies minimal render area height
    uint32_t renderHeightMin{};
    //! Specifies maximal render area width
    uint32_t renderWidthMax{};
    //! Specifies maximal render area height
    uint32_t renderHeightMax{};

    //! IMPORTANT: New members go here or if optional can be chained in a new struct, see sl_struct.h for details
SL_STRUCT_END()

//! Returned by DLSS plugin
//! 
//! {9366B056-8C01-463C-BB91-E68782636CE9}
SL_STRUCT_BEGIN(DLSSState, StructType({ 0x9366b056, 0x8c01, 0x463c, { 0xbb, 0x91, 0xe6, 0x87, 0x82, 0x63, 0x6c, 0xe9 } }), kStructVersion1)
    //! Specified the amount of memory expected to be used
    uint64_t estimatedVRAMUsageInBytes{};

    //! IMPORTANT: New members go here or if optional can be chained in a new struct, see sl_struct.h for details
SL_STRUCT_END()

}

//! Provides optimal DLSS settings
//!
//! Call this method to obtain optimal render target size and other DLSS related settings.
//!
//! @param options Specifies DLSS options to use
//! @param settings Reference to a structure where settings are returned
//! @return sl::Result::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! This method is NOT thread safe.
using PFun_slDLSSGetOptimalSettings = sl::Result(const sl::DLSSOptions & options, sl::DLSSOptimalSettings & settings);

//! Provides DLSS state for the given viewport
//!
//! Call this method to obtain optimal render target size and other DLSS related settings.
//!
//! @param viewport Specified viewport we are working with
//! @param state Reference to a structure where state is to be returned
//! @return sl::Result::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! This method is NOT thread safe.
using PFun_slDLSSGetState = sl::Result(const sl::ViewportHandle & viewport, sl::DLSSState & state);

//! Sets DLSS options
//!
//! Call this method to turn DLSS on/off, change mode etc.
//!
//! @param viewport Specified viewport we are working with
//! @param options Specifies DLSS options to use
//! @return sl::Result::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! This method is NOT thread safe.
using PFun_slDLSSSetOptions = sl::Result(const sl::ViewportHandle& viewport, const sl::DLSSOptions& options);

//! HELPERS
//! 
inline sl::Result slDLSSGetOptimalSettings(const sl::DLSSOptions& options, sl::DLSSOptimalSettings& settings)
{
    SL_FEATURE_FUN_IMPORT_STATIC(sl::kFeatureDLSS, slDLSSGetOptimalSettings);
    return s_slDLSSGetOptimalSettings(options, settings);
}

inline sl::Result slDLSSGetState(const sl::ViewportHandle& viewport, sl::DLSSState& state)
{
    SL_FEATURE_FUN_IMPORT_STATIC(sl::kFeatureDLSS, slDLSSGetState);
    return s_slDLSSGetState(viewport, state);
}

inline sl::Result slDLSSSetOptions(const sl::ViewportHandle& viewport, const sl::DLSSOptions& options)
{
    SL_FEATURE_FUN_IMPORT_STATIC(sl::kFeatureDLSS, slDLSSSetOptions);
    return s_slDLSSSetOptions(viewport, options);
}
