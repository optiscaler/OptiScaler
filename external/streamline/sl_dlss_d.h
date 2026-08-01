/*
* Copyright (c) 2023 NVIDIA CORPORATION. All rights reserved
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

#include "sl_dlss.h"

namespace sl
{

enum class DLSSDPreset : uint32_t
{
    //! Default behavior, may or may not change after an OTA
    eDefault,
                    // ePresetA removed, use preset D or E
                    // ePresetB removed, use preset D or E
                    // ePresetC removed, use preset D or E
    ePresetD = 4,   // Default model (transformer)
    ePresetE = 5,   // Latest transformer model (must use if DoF guide is needed)
    ePresetF = 6,   // Reverts to default
    ePresetG = 7,   // Reverts to default
    ePresetH = 8,   // Reverts to default
    ePresetI = 9,   // Reverts to default
    ePresetJ = 10,  // Reverts to default
    ePresetK = 11,  // Reverts to default
    ePresetL = 12,  // Reverts to default
    ePresetM = 13,  // Reverts to default. Not recommended to use
    ePresetN = 14,  // Reverts to default. Not recommended to use
    ePresetO = 15,  // Reverts to default. Not recommended to use

    eCount
};

enum class DLSSDNormalRoughnessMode : uint32_t
{
    eUnpacked,  // App needs to provide Normal resource and Roughness resource separately.
    ePacked,    // App needs to write Roughness to w channel of Normal resource.

    eCount
};

// {0AD87504-774E-4BF3-9633-A44D1F7F9CB8}
SL_STRUCT_BEGIN(DLSSDOptions, StructType({ 0x0ad87504, 0x774e, 0x4bf3, { 0x96, 0x33, 0xa4, 0x4d, 0x1f, 0x7f, 0x9c, 0xb8 } }), kStructVersion3)
    //! Specifies which mode should be used
    DLSSMode mode = DLSSMode::eOff;
    //! Specifies output (final) target width
    uint32_t outputWidth = INVALID_UINT;
    //! Specifies output (final) target height
    uint32_t outputHeight = INVALID_UINT;
    //! Specifies sharpening level in range [0,1]
    float sharpness = 0.0f;
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
    //! Specifies which mode should be used for roughness resource
    DLSSDNormalRoughnessMode normalRoughnessMode = DLSSDNormalRoughnessMode::eUnpacked;
    //! Specifies matrix transformation from the world space to the camera view space.
    float4x4 worldToCameraView;
    //! Specifies matrix transformation from the camera view space to the world space.
    //! cameraViewToWorld = worldToCameraView.inverse()
    float4x4 cameraViewToWorld;
    //! Whether or not the alpha channel should be upscaled (if false, only RGB is upscaled)
    //! Enabling alpha upscaling may impact performance
    Boolean alphaUpscalingEnabled = Boolean::eFalse;

    //! Presets
    DLSSDPreset dlaaPreset = DLSSDPreset::eDefault;
    DLSSDPreset qualityPreset = DLSSDPreset::eDefault;
    DLSSDPreset balancedPreset = DLSSDPreset::eDefault;
    DLSSDPreset performancePreset = DLSSDPreset::eDefault;
    DLSSDPreset ultraPerformancePreset = DLSSDPreset::eDefault;
    DLSSDPreset ultraQualityPreset = DLSSDPreset::eDefault;

    //! IMPORTANT: New members go here or if optional can be chained in a new struct, see sl_struct.h for details
SL_STRUCT_END()

//! Returned by DLSSD plugin
//! 
//! {FBD0C637-A28F-41F2-BC91-B421FAEE8E1E}
SL_STRUCT_BEGIN(DLSSDOptimalSettings, StructType({ 0xfbd0c637, 0xa28f, 0x41f2, { 0xbc, 0x91, 0xb4, 0x21, 0xfa, 0xee, 0x8e, 0x1e } }), kStructVersion1)
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

//! Returned by DLSSD plugin
//! 
//! {71873C14-F8CA-4767-9EAF-3B4393EA98FA}
SL_STRUCT_BEGIN(DLSSDState, StructType({ 0x71873c14, 0xf8ca, 0x4767, { 0x9e, 0xaf, 0x3b, 0x43, 0x93, 0xea, 0x98, 0xfa } }), kStructVersion1)
//! Specified the amount of memory expected to be used
    uint64_t estimatedVRAMUsageInBytes {};

    //! IMPORTANT: New members go here or if optional can be chained in a new struct, see sl_struct.h for details
SL_STRUCT_END()

}

//! Provides optimal DLSSD settings
//!
//! Call this method to obtain optimal render target size and other DLSSD related settings.
//!
//! @param options Specifies DLSSD options to use
//! @param settings Reference to a structure where settings are returned
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! This method is NOT thread safe.
using PFun_slDLSSDGetOptimalSettings = sl::Result(const sl::DLSSDOptions & options, sl::DLSSDOptimalSettings & settings);

//! Provides DLSSD state for the given viewport
//!
//! Call this method to obtain optimal render target size and other DLSSD related settings.
//!
//! @param viewport Specified viewport we are working with
//! @param state Reference to a structure where state is to be returned
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! This method is NOT thread safe.
using PFun_slDLSSDGetState = sl::Result(const sl::ViewportHandle & viewport, sl::DLSSDState & state);

//! Sets DLSSD options
//!
//! Call this method to turn DLSSD on/off, change mode etc.
//!
//! @param viewport Specified viewport we are working with
//! @param options Specifies DLSSD options to use
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! This method is NOT thread safe.
using PFun_slDLSSDSetOptions = sl::Result(const sl::ViewportHandle& viewport, const sl::DLSSDOptions& options);

//! HELPERS
//! 
inline sl::Result slDLSSDGetOptimalSettings(const sl::DLSSDOptions& options, sl::DLSSDOptimalSettings& settings)
{
    SL_FEATURE_FUN_IMPORT_STATIC(sl::kFeatureDLSS_RR, slDLSSDGetOptimalSettings);
    return s_slDLSSDGetOptimalSettings(options, settings);
}

inline sl::Result slDLSSDGetState(const sl::ViewportHandle& viewport, sl::DLSSDState& state)
{
    SL_FEATURE_FUN_IMPORT_STATIC(sl::kFeatureDLSS_RR, slDLSSDGetState);
    return s_slDLSSDGetState(viewport, state);
}

inline sl::Result slDLSSDSetOptions(const sl::ViewportHandle& viewport, const sl::DLSSDOptions& options)
{
    SL_FEATURE_FUN_IMPORT_STATIC(sl::kFeatureDLSS_RR, slDLSSDSetOptions);
    return s_slDLSSDSetOptions(viewport, options);
}
