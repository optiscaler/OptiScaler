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
#include <assert.h>
#include <string>
#include "sl_struct.h"

#define SL_ENUM_OPERATORS_64(T)                                                         \
inline bool operator&(T a, T b)                                                         \
{                                                                                       \
    return ((uint64_t)a & (uint64_t)b) != 0;                                            \
}                                                                                       \
                                                                                        \
inline T& operator&=(T& a, T b)                                                         \
{                                                                                       \
    a = (T)((uint64_t)a & (uint64_t)b);                                                 \
    return a;                                                                           \
}                                                                                       \
                                                                                        \
inline T operator|(T a, T b)                                                            \
{                                                                                       \
    return (T)((uint64_t)a | (uint64_t)b);                                              \
}                                                                                       \
                                                                                        \
inline T& operator |= (T& lhs, T rhs)                                                   \
{                                                                                       \
    lhs = (T)((uint64_t)lhs | (uint64_t)rhs);                                           \
    return lhs;                                                                         \
}                                                                                       \
                                                                                        \
inline T operator~(T a)                                                                 \
{                                                                                       \
    return (T)~((uint64_t)a);                                                           \
}

#define SL_ENUM_OPERATORS_32(T)                                                         \
inline bool operator&(T a, T b)                                                         \
{                                                                                       \
    return ((uint32_t)a & (uint32_t)b) != 0;                                            \
}                                                                                       \
                                                                                        \
inline T& operator&=(T& a, T b)                                                         \
{                                                                                       \
    a = (T)((uint32_t)a & (uint32_t)b);                                                 \
    return a;                                                                           \
}                                                                                       \
                                                                                        \
inline T operator|(T a, T b)                                                            \
{                                                                                       \
    return (T)((uint32_t)a | (uint32_t)b);                                              \
}                                                                                       \
                                                                                        \
inline T& operator |= (T& lhs, T rhs)                                                   \
{                                                                                       \
    lhs = (T)((uint32_t)lhs | (uint32_t)rhs);                                           \
    return lhs;                                                                         \
}                                                                                       \
                                                                                        \
inline T operator~(T a)                                                                 \
{                                                                                       \
    return (T)~((uint32_t)a);                                                           \
}

namespace sl
{

//! For cases when value has to be provided and we don't have good default
constexpr float INVALID_FLOAT = 3.40282346638528859811704183484516925440e38f;
constexpr uint32_t INVALID_UINT = 0xffffffff;

//! Normally host would work with no more than 2 frames at the same time but sl.reflex sometimes 
//! needs to send markers for previous and next frame so the total number of in-flight frames can be higher
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 6;

struct uint3
{
    uint32_t x;
    uint32_t y;
    uint32_t z;
};

struct float2
{
    float2() : x(INVALID_FLOAT), y(INVALID_FLOAT) {}
    float2(float _x, float _y) : x(_x), y(_y) {}
    float x, y;
};

struct float3
{
    float3() : x(INVALID_FLOAT), y(INVALID_FLOAT), z(INVALID_FLOAT) {}
    float3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
    float x, y, z;
};

struct float4
{
    float4() : x(INVALID_FLOAT), y(INVALID_FLOAT), z(INVALID_FLOAT), w(INVALID_FLOAT) {}
    float4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
    float x, y, z, w;
};

struct float4x4
{
    //! All access points take row index as a parameter
    inline float4& operator[](uint32_t i) { return row[i]; }
    inline const float4& operator[](uint32_t i) const { return row[i]; }
    inline void setRow(uint32_t i, const float4& v) { row[i] = v; }
    inline const float4& getRow(uint32_t i) { return row[i]; }

    //! Row major matrix
    float4 row[4];
};

struct Extent
{
    uint32_t top{};
    uint32_t left{};
    uint32_t width{};
    uint32_t height{};

    inline operator bool() const { return width != 0 && height != 0; }
    inline bool operator==(const Extent& rhs) const 
    { 
        return top == rhs.top && left == rhs.left &&
        width == rhs.width && height == rhs.height;
    }
    inline bool operator!=(const Extent& rhs) const
    {
        return !operator==(rhs);
    }
    inline bool isSameRes(const Extent& rhs) const
    {
        return width == rhs.width && height == rhs.height;
    }

#if defined(_WINDEF_)
    // Cast helper for sl::Extent->RECT when windef.h has been included
    inline operator RECT() const { return RECT { (LONG)left, (LONG)top, (LONG)(left + width), (LONG)(top + height) }; }
#endif
};

//! For cases when value has to be provided and we don't have good default
enum Boolean : char
{
    eFalse,
    eTrue,
    eInvalid
};

//! Common constants, all parameters must be provided unless they are marked as optional
//! 
//! {DCD35AD7-4E4A-4BAD-A90C-E0C49EB23AFE}
SL_STRUCT_BEGIN(Constants, StructType({ 0xdcd35ad7, 0x4e4a, 0x4bad, { 0xa9, 0xc, 0xe0, 0xc4, 0x9e, 0xb2, 0x3a, 0xfe } }), kStructVersion2)
    //! IMPORTANT: All matrices are row major (see float4x4 definition) and
    //! must NOT contain temporal AA jitter offset (if any). Any jitter offset
    //! should be provided as the additional parameter Constants::jitterOffset (see below)
            
    //! Specifies matrix transformation from the camera view to the clip space.
    float4x4 cameraViewToClip;
    //! Specifies matrix transformation from the clip space to the camera view space.
    float4x4 clipToCameraView;
    //! Optional - Specifies matrix transformation describing lens distortion in clip space.
    float4x4 clipToLensClip;
    //! Specifies matrix transformation from the current clip to the previous clip space.
    //! clipToPrevClip = clipToView * viewToViewPrev * viewToClipPrev
    //! Sample code can be found in sl_matrix_helpers.h
    float4x4 clipToPrevClip;
    //! Specifies matrix transformation from the previous clip to the current clip space.
    //! prevClipToClip = clipToPrevClip.inverse()
    float4x4 prevClipToClip;
        
    //! Specifies pixel space jitter offset
    float2 jitterOffset;
    //! Specifies scale factors used to normalize motion vectors (so the values are in [-1,1] range)
    float2 mvecScale;
    //! Optional - Specifies camera pinhole offset if used.
    float2 cameraPinholeOffset;
    //! Specifies camera position in world space.
    float3 cameraPos;
    //! Specifies camera up vector in world space.
    float3 cameraUp;
    //! Specifies camera right vector in world space.
    float3 cameraRight;
    //! Specifies camera forward vector in world space.
    float3 cameraFwd;
        
    //! Specifies camera near view plane distance.
    float cameraNear = INVALID_FLOAT;
    //! Specifies camera far view plane distance.
    float cameraFar = INVALID_FLOAT;
    //! Specifies camera field of view in radians.
    float cameraFOV = INVALID_FLOAT;
    //! Specifies camera aspect ratio defined as view space width divided by height.
    float cameraAspectRatio = INVALID_FLOAT;
    //! Specifies which value represents an invalid (un-initialized) value in the motion vectors buffer
    //! NOTE: This is only required if `cameraMotionIncluded` is set to false and SL needs to compute it.
    float motionVectorsInvalidValue = INVALID_FLOAT;

    //! Specifies if depth values are inverted (value closer to the camera is higher) or not.
    Boolean depthInverted = Boolean::eInvalid;
    //! Specifies if camera motion is included in the MVec buffer.
    Boolean cameraMotionIncluded = Boolean::eInvalid;
    //! Specifies if motion vectors are 3D or not.
    Boolean motionVectors3D = Boolean::eInvalid;
    //! Specifies if previous frame has no connection to the current one (i.e. motion vectors are invalid)
    Boolean reset = Boolean::eInvalid;
    //! Specifies if orthographic projection is used or not.
    Boolean orthographicProjection = Boolean::eFalse;
    //! Specifies if motion vectors are already dilated or not.
    Boolean motionVectorsDilated = Boolean::eFalse;
    //! Specifies if motion vectors are jittered or not.
    Boolean motionVectorsJittered = Boolean::eFalse;

    //! Version 2 members:
    //! 
    //! Optional heuristic that specifies the minimum depth difference between two objects in screen-space.
    //! The units of the value are in linear depth units.
    //! Linear depth is computed as:
    //!     if depthInverted is false:  `lin_depth = 1 / (1 - depth)` 
    //!     if depthInverted is true:   `lin_depth = 1 / depth`
    //! 
    //! Although unlikely to need to be modified, smaller thresholds are useful when depth units are
    //! unusually compressed into a small dynamic range near 1.
    //! 
    //! If not specified, the default value is 40.0f.
    float minRelativeLinearDepthObjectSeparation = 40.0f;

    //! IMPORTANT: New members go here or if optional can be chained in a new struct, see sl_struct.h for details
SL_STRUCT_END()

}
