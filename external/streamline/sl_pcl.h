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

#include <cassert>

namespace sl
{

//! Hot-key which should be used instead of custom message for PC latency marker
enum class PCLHotKey: int16_t
{
    eUsePingMessage = 0,
    eVK_F13 = 0x7C,
    eVK_F14 = 0x7D,
    eVK_F15 = 0x7E,
};

// {cfa32f9b-023c-420e-9056-6832b74f89b4}
SL_STRUCT_BEGIN(PCLOptions, StructType({ 0xcfa32f9b, 0x023c, 0x420e, { 0x90, 0x56, 0x68, 0x32, 0xb7, 0x4f, 0x89, 0xb4 } }), kStructVersion1)
    //! Specifies the hot-key which should be used instead of custom message for PC latency marker
    //! Possible values: VK_F13, VK_F14, VK_F15
    PCLHotKey virtualKey = PCLHotKey::eUsePingMessage;
    //! ThreadID for PCL messages
    uint32_t idThread = 0;

    //! IMPORTANT: New members go here or if optional can be chained in a new struct, see sl_struct.h for details
SL_STRUCT_END()

// {cfa32f9b-023c-420e-9056-6832b74f89b5}
SL_STRUCT_BEGIN(PCLState, StructType({ 0xcfa32f9b, 0x023c, 0x420e, { 0x90, 0x56, 0x68, 0x32, 0xb7, 0x4f, 0x89, 0xb5 } }), kStructVersion1)
    //! Specifies PCL Windows message id (if PCLOptions::virtualKey is 0)
    uint32_t statsWindowMessage;

    //! IMPORTANT: New members go here or if optional can be chained in a new struct, see sl_struct.h for details
SL_STRUCT_END()

enum class PCLMarker: uint32_t
{
    eSimulationStart = 0,
    eSimulationEnd = 1,
    eRenderSubmitStart = 2,
    eRenderSubmitEnd = 3,
    ePresentStart = 4,
    ePresentEnd = 5,
    //eInputSample = 6, // Deprecated
    eTriggerFlash = 7,
    ePCLatencyPing = 8,
    eOutOfBandRenderSubmitStart = 9,
    eOutOfBandRenderSubmitEnd = 10,
    eOutOfBandPresentStart = 11,
    eOutOfBandPresentEnd = 12,
    eControllerInputSample = 13,
    eDeltaTCalculation = 14,
    eLateWarpPresentStart = 15,
    eLateWarpPresentEnd = 16,
    eCameraConstructed = 17,
    eLateWarpRenderSubmitStart = 18,
    eLateWarpRenderSubmitEnd = 19,

    eMaximum
};

// c++23 has to_underlying implementation
#if __cplusplus == 202302L
using to_underlying = std::to_underlying;
#else
// Return `enum class` member as value of underlying type (i.e. an int).  Basically same as:
// static_cast<std::underlying_type_t<decltype(value)>>(value);
// See c++23s std::to_underlying()
template<class T>
inline constexpr auto to_underlying(T value)
{
    return std::underlying_type_t<T>(value);
}
#endif

// {cfa32f9b-023c-420e-9056-6832b74f89b6}
SL_STRUCT_BEGIN(PCLHelper, StructType({ 0xcfa32f9b, 0x023c, 0x420e, { 0x90, 0x56, 0x68, 0x32, 0xb7, 0x4f, 0x89, 0xb6 } }), kStructVersion1)
    PCLHelper(PCLMarker m) : BaseStructure(PCLHelper::s_structType, kStructVersion1), marker(m) {};
    PCLMarker get() const { return marker; };
private:
    PCLMarker marker;
SL_STRUCT_END()


}

//! Provides PCL settings
//!
//! Call this method to get stats etc.
//!
//! @param state Reference to a structure where states are returned
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! This method is NOT thread safe.
using PFun_slPCLGetState = sl::Result(sl::PCLState& state);

//! Sets PCL marker
//!
//! Call this method to set specific PCL marker
//!
//! @param marker Specifies which marker to use
//! @param frame Specifies current frame
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! This method is thread safe.
using PFun_slPCLSetMarker = sl::Result(sl::PCLMarker marker, const sl::FrameToken& frame);

//! Sets PCL options
//!
//! Call this method to set PCL options.
//!
//! @param options Specifies options to use
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! This method is NOT thread safe.
using PFun_slPCLSetOptions = sl::Result(const sl::PCLOptions& options);

//! HELPERS
//!
inline sl::Result slPCLGetState(sl::PCLState& state)
{
    SL_FEATURE_FUN_IMPORT_STATIC(sl::kFeaturePCL, slPCLGetState);
    return s_slPCLGetState(state);
}

inline sl::Result slPCLSetMarker(sl::PCLMarker marker, const sl::FrameToken& frame)
{
    SL_FEATURE_FUN_IMPORT_STATIC(sl::kFeaturePCL, slPCLSetMarker);
    return s_slPCLSetMarker(marker, frame);
}

inline sl::Result slPCLSetOptions(const sl::PCLOptions& options)
{
    SL_FEATURE_FUN_IMPORT_STATIC(sl::kFeaturePCL, slPCLSetOptions);
    return s_slPCLSetOptions(options);
}