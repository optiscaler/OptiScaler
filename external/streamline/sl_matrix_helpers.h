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

#include "sl.h"
#include "sl_consts.h"

namespace sl
{

inline void matrixMul(float4x4& result, const float4x4& a, const float4x4& b)
{
    // Alias raw pointers over the input matrices
    const float* pA = &a[0].x;
    const float* pB = &b[0].x;

    result[0].x = (float)((pA[0] * pB[0]) + (pA[1] * pB[4]) + (pA[2] * pB[8]) + (pA[3] * pB[12]));
    result[0].y = (float)((pA[0] * pB[1]) + (pA[1] * pB[5]) + (pA[2] * pB[9]) + (pA[3] * pB[13]));
    result[0].z = (float)((pA[0] * pB[2]) + (pA[1] * pB[6]) + (pA[2] * pB[10]) + (pA[3] * pB[14]));
    result[0].w = (float)((pA[0] * pB[3]) + (pA[1] * pB[7]) + (pA[2] * pB[11]) + (pA[3] * pB[15]));

    result[1].x = (float)((pA[4] * pB[0]) + (pA[5] * pB[4]) + (pA[6] * pB[8]) + (pA[7] * pB[12]));
    result[1].y = (float)((pA[4] * pB[1]) + (pA[5] * pB[5]) + (pA[6] * pB[9]) + (pA[7] * pB[13]));
    result[1].z = (float)((pA[4] * pB[2]) + (pA[5] * pB[6]) + (pA[6] * pB[10]) + (pA[7] * pB[14]));
    result[1].w = (float)((pA[4] * pB[3]) + (pA[5] * pB[7]) + (pA[6] * pB[11]) + (pA[7] * pB[15]));

    result[2].x = (float)((pA[8] * pB[0]) + (pA[9] * pB[4]) + (pA[10] * pB[8]) + (pA[11] * pB[12]));
    result[2].y = (float)((pA[8] * pB[1]) + (pA[9] * pB[5]) + (pA[10] * pB[9]) + (pA[11] * pB[13]));
    result[2].z = (float)((pA[8] * pB[2]) + (pA[9] * pB[6]) + (pA[10] * pB[10]) + (pA[11] * pB[14]));
    result[2].w = (float)((pA[8] * pB[3]) + (pA[9] * pB[7]) + (pA[10] * pB[11]) + (pA[11] * pB[15]));

    result[3].x = (float)((pA[12] * pB[0]) + (pA[13] * pB[4]) + (pA[14] * pB[8]) + (pA[15] * pB[12]));
    result[3].y = (float)((pA[12] * pB[1]) + (pA[13] * pB[5]) + (pA[14] * pB[9]) + (pA[15] * pB[13]));
    result[3].z = (float)((pA[12] * pB[2]) + (pA[13] * pB[6]) + (pA[14] * pB[10]) + (pA[15] * pB[14]));
    result[3].w = (float)((pA[12] * pB[3]) + (pA[13] * pB[7]) + (pA[14] * pB[11]) + (pA[15] * pB[15]));
}

inline void matrixFullInvert(float4x4& result, const float4x4& mat)
{
    // Matrix inversion code from https://stackoverflow.com/questions/1148309/inverting-a-4x4-matrix
    // Alias raw pointers over the input matrix and the result
    const float* pMat = &mat[0].x;
    float* pResult = &result[0].x;

    pResult[0] = pMat[5] * pMat[10] * pMat[15] - pMat[5] * pMat[11] * pMat[14] - pMat[9] * pMat[6] * pMat[15] + pMat[9] * pMat[7] * pMat[14] + pMat[13] * pMat[6] * pMat[11] - pMat[13] * pMat[7] * pMat[10];
    pResult[4] = -pMat[4] * pMat[10] * pMat[15] + pMat[4] * pMat[11] * pMat[14] + pMat[8] * pMat[6] * pMat[15] - pMat[8] * pMat[7] * pMat[14] - pMat[12] * pMat[6] * pMat[11] + pMat[12] * pMat[7] * pMat[10];
    pResult[8] = pMat[4] * pMat[9] * pMat[15] - pMat[4] * pMat[11] * pMat[13] - pMat[8] * pMat[5] * pMat[15] + pMat[8] * pMat[7] * pMat[13] + pMat[12] * pMat[5] * pMat[11] - pMat[12] * pMat[7] * pMat[9];
    pResult[12] = -pMat[4] * pMat[9] * pMat[14] + pMat[4] * pMat[10] * pMat[13] + pMat[8] * pMat[5] * pMat[14] - pMat[8] * pMat[6] * pMat[13] - pMat[12] * pMat[5] * pMat[10] + pMat[12] * pMat[6] * pMat[9];
    pResult[1] = -pMat[1] * pMat[10] * pMat[15] + pMat[1] * pMat[11] * pMat[14] + pMat[9] * pMat[2] * pMat[15] - pMat[9] * pMat[3] * pMat[14] - pMat[13] * pMat[2] * pMat[11] + pMat[13] * pMat[3] * pMat[10];
    pResult[5] = pMat[0] * pMat[10] * pMat[15] - pMat[0] * pMat[11] * pMat[14] - pMat[8] * pMat[2] * pMat[15] + pMat[8] * pMat[3] * pMat[14] + pMat[12] * pMat[2] * pMat[11] - pMat[12] * pMat[3] * pMat[10];
    pResult[9] = -pMat[0] * pMat[9] * pMat[15] + pMat[0] * pMat[11] * pMat[13] + pMat[8] * pMat[1] * pMat[15] - pMat[8] * pMat[3] * pMat[13] - pMat[12] * pMat[1] * pMat[11] + pMat[12] * pMat[3] * pMat[9];
    pResult[13] = pMat[0] * pMat[9] * pMat[14] - pMat[0] * pMat[10] * pMat[13] - pMat[8] * pMat[1] * pMat[14] + pMat[8] * pMat[2] * pMat[13] + pMat[12] * pMat[1] * pMat[10] - pMat[12] * pMat[2] * pMat[9];
    pResult[2] = pMat[1] * pMat[6] * pMat[15] - pMat[1] * pMat[7] * pMat[14] - pMat[5] * pMat[2] * pMat[15] + pMat[5] * pMat[3] * pMat[14] + pMat[13] * pMat[2] * pMat[7] - pMat[13] * pMat[3] * pMat[6];
    pResult[6] = -pMat[0] * pMat[6] * pMat[15] + pMat[0] * pMat[7] * pMat[14] + pMat[4] * pMat[2] * pMat[15] - pMat[4] * pMat[3] * pMat[14] - pMat[12] * pMat[2] * pMat[7] + pMat[12] * pMat[3] * pMat[6];
    pResult[10] = pMat[0] * pMat[5] * pMat[15] - pMat[0] * pMat[7] * pMat[13] - pMat[4] * pMat[1] * pMat[15] + pMat[4] * pMat[3] * pMat[13] + pMat[12] * pMat[1] * pMat[7] - pMat[12] * pMat[3] * pMat[5];
    pResult[14] = -pMat[0] * pMat[5] * pMat[14] + pMat[0] * pMat[6] * pMat[13] + pMat[4] * pMat[1] * pMat[14] - pMat[4] * pMat[2] * pMat[13] - pMat[12] * pMat[1] * pMat[6] + pMat[12] * pMat[2] * pMat[5];
    pResult[3] = -pMat[1] * pMat[6] * pMat[11] + pMat[1] * pMat[7] * pMat[10] + pMat[5] * pMat[2] * pMat[11] - pMat[5] * pMat[3] * pMat[10] - pMat[9] * pMat[2] * pMat[7] + pMat[9] * pMat[3] * pMat[6];
    pResult[7] = pMat[0] * pMat[6] * pMat[11] - pMat[0] * pMat[7] * pMat[10] - pMat[4] * pMat[2] * pMat[11] + pMat[4] * pMat[3] * pMat[10] + pMat[8] * pMat[2] * pMat[7] - pMat[8] * pMat[3] * pMat[6];
    pResult[11] = -pMat[0] * pMat[5] * pMat[11] + pMat[0] * pMat[7] * pMat[9] + pMat[4] * pMat[1] * pMat[11] - pMat[4] * pMat[3] * pMat[9] - pMat[8] * pMat[1] * pMat[7] + pMat[8] * pMat[3] * pMat[5];
    pResult[15] = pMat[0] * pMat[5] * pMat[10] - pMat[0] * pMat[6] * pMat[9] - pMat[4] * pMat[1] * pMat[10] + pMat[4] * pMat[2] * pMat[9] + pMat[8] * pMat[1] * pMat[6] - pMat[8] * pMat[2] * pMat[5];

    float det = pMat[0] * pResult[0] + pMat[1] * pResult[4] + pMat[2] * pResult[8] + pMat[3] * pResult[12];
    if (det != 0.f)
    {
        det = 1.0f / det;

        for (int i = 0; i < 16; ++i)
        {
            pResult[i] *= det;
        }
    }
}

// Specialised lightweight matrix invert when the matrix is known to be orthonormal
inline void matrixOrthoNormalInvert(float4x4& result, const float4x4& mat)
{
    // Transpose the first 3x3
    result[0].x = mat[0].x;
    result[0].y = mat[1].x;
    result[0].z = mat[2].x;
    result[1].x = mat[0].y;
    result[1].y = mat[1].y;
    result[1].z = mat[2].y;
    result[2].x = mat[0].z;
    result[2].y = mat[1].z;
    result[2].z = mat[2].z;

    // Invert the translation
    result[3].x = -((mat[3].x * mat[0].x) + (mat[3].y * mat[0].y) + (mat[3].z * mat[0].z));
    result[3].y = -((mat[3].x * mat[1].x) + (mat[3].y * mat[1].y) + (mat[3].z * mat[1].z));
    result[3].z = -((mat[3].x * mat[2].x) + (mat[3].y * mat[2].y) + (mat[3].z * mat[2].z));

    // Fill in the remaining constants
    result[0].w = 0.0f;
    result[1].w = 0.0f;
    result[2].w = 0.0f;
    result[3].w = 1.0f;
}

inline void vectorNormalize(float3& v)
{
    float k = 1.f / sqrtf((v.x * v.x) + (v.y * v.y) + (v.z * v.z));
    v.x *= k;
    v.y *= k;
    v.z *= k;
}

inline void vectorCrossProduct(float3& result, const float3& a, const float3& b)
{
    result.x = a.y * b.z - a.z * b.y;
    result.y = a.z * b.x - a.x * b.z;
    result.z = a.x * b.y - a.y * b.x;
}

// Calculate a cameraToPrevCamera matrix from cameraToWorld and cameraToWorldPrev matrices
// but do so in such a way as to avoid precision issues.
//
// Traditionally, you might go something like this...
//
//    worldToCameraPrev = invert(cameraToWorldPrev)
//    cameraToPrevCamera = cameraToWorld * worldToCameraPrev
//
// But if you do that, you will subject yourself to fp32 precision issues if the camera is
// any kind of reasonable distance from the origin, because you'll end up adding small
// numbers to large numbers due to the large translations.
//
// But the camera's absolute position in the world doesn't matter at all to the result.
// What we're interested in is the camera's motion.
// So if we add the same thing to the translations of cameraToWorld and cameraToWorldPrev
// then we should get the same result.
// If we choose to subtract the current camera's translation in world space, then we will
// change a potentially very large translation value into a very small one - thereby
// sidestepping the precision issues.
inline void calcCameraToPrevCamera(float4x4& outCameraToPrevCamera, const float4x4& cameraToWorld, const float4x4& cameraToWorldPrev)
{
    // Create translated versions of cameraToWorld and cameraToWorldPrev, translated to
    // so that the current camera is effectively at the world origin.
    // CC == 'Camera-Centred'
    float4x4 cameraToCcWorld = cameraToWorld;
    cameraToCcWorld[3] = float4(0, 0, 0, 1);
    float4x4 cameraToCcWorldPrev = cameraToWorldPrev;
    cameraToCcWorldPrev[3].x -= cameraToWorld[3].x;
    cameraToCcWorldPrev[3].y -= cameraToWorld[3].y;
    cameraToCcWorldPrev[3].z -= cameraToWorld[3].z;

    // We can use an optimised invert if we assume that the camera matrix is orthonormal
    float4x4 ccWorldToCameraPrev;
    matrixOrthoNormalInvert(ccWorldToCameraPrev, cameraToCcWorldPrev);
    matrixMul(outCameraToPrevCamera, cameraToCcWorld, ccWorldToCameraPrev);
}

// Calculate some of the matrix fields in Constants
// This can be used to validate what the app is providing, or tease out precision issues
// The matrices that are recalculated are...
// - clipToCameraView
// - clipToPrevClip
// - prevClipToClip
inline void recalculateCameraMatrices(Constants& values)
{
    // Form a camera-to-world matrix from the camera fields
    vectorNormalize(values.cameraRight);
    vectorNormalize(values.cameraFwd);
    vectorCrossProduct(values.cameraUp, values.cameraFwd, values.cameraRight);
    vectorNormalize(values.cameraUp);
    float4x4 cameraViewToWorld = {
        float4(values.cameraRight.x, values.cameraRight.y, values.cameraRight.z, 0.f),
        float4(values.cameraUp.x,    values.cameraUp.y,    values.cameraUp.z,    0.f),
        float4(values.cameraFwd.x,   values.cameraFwd.y,   values.cameraFwd.z,   0.f),
        float4(values.cameraPos.x,   values.cameraPos.y,   values.cameraPos.z, 1.f)
    };
    // ********* DO NOT USE THIS IN ANYTHING PROPER *********
    // Crap storage of cameraViewToWorldPrev and cameraViewToClipPrev
    // These should be provided by the app, or stored by association with the view index.
    static float4x4 cameraViewToWorldPrev = {
        float4(1, 0, 0, 0),
        float4(0, 1, 0, 0),
        float4(0, 0, 1, 0),
        float4(0, 0, 0, 1),
    };
    static float4x4 cameraViewToClipPrev = {
        float4(1, 0, 0, 0),
        float4(0, 1, 0, 0),
        float4(0, 0, 1, 0),
        float4(0, 0, 0, 1),
    };
    matrixFullInvert(values.clipToCameraView, values.cameraViewToClip);

    float4x4 cameraViewToPrevCameraView;
    calcCameraToPrevCamera(cameraViewToPrevCameraView, cameraViewToWorld, cameraViewToWorldPrev);

    float4x4 clipToPrevCameraView;
    matrixMul(clipToPrevCameraView, values.clipToCameraView, cameraViewToPrevCameraView);
    matrixMul(values.clipToPrevClip, clipToPrevCameraView, cameraViewToClipPrev);
    matrixFullInvert(values.prevClipToClip, values.clipToPrevClip);

    // ********* DO NOT USE THIS IN ANYTHING PROPER *********
    cameraViewToWorldPrev = cameraViewToWorld;
    cameraViewToClipPrev = values.cameraViewToClip;
}

}
