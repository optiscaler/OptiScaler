/*
* Copyright (c) 2022 NVIDIA CORPORATION. All rights reserved
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

namespace sl1
{

enum ReflexMode
{
    eReflexModeOff,
    eReflexModeLowLatency,
    eReflexModeLowLatencyWithBoost,
};

struct ReflexConstants
{
    //! Specifies which mode should be used
    ReflexMode mode = eReflexModeOff;
    //! Specifies if frame limiting is enabled (0 to disable, microseconds otherwise)
    uint32_t frameLimitUs = 0;
    //! Specifies if markers are used or not (this should always be true and markers should be placed correctly)
    bool useMarkersToOptimize = false;
    //! Specifies the hot-key which should be used instead of custom message for PC latency marker
    //! Possible values: VK_F13, VK_F14, VK_F15
    uint16_t virtualKey = 0;
    //! Reserved for future expansion, must be set to null
    void* ext = {};
};

struct ReflexReport
{
    //! Various latency related stats
    uint64_t frameID{};
    uint64_t inputSampleTime{};
    uint64_t simStartTime{};
    uint64_t simEndTime{};
    uint64_t renderSubmitStartTime{};
    uint64_t renderSubmitEndTime{};
    uint64_t presentStartTime{};
    uint64_t presentEndTime{};
    uint64_t driverStartTime{};
    uint64_t driverEndTime{};
    uint64_t osRenderQueueStartTime{};
    uint64_t osRenderQueueEndTime{};
    uint64_t gpuRenderStartTime{};
    uint64_t gpuRenderEndTime{};
    uint32_t gpuActiveRenderTimeUs{};
    uint32_t gpuFrameTimeUs{};
};

struct ReflexSettings
{
    //! Specifies if low-latency mode is available or not
    bool lowLatencyAvailable = false;
    //! Specifies if latency report is available or not
    bool latencyReportAvailable = false;
    //! Specifies low latency Windows message id (if ReflexConstants::virtualKey is 0)
    uint32_t statsWindowMessage;
    //! Reflex report per frame
    ReflexReport frameReport[64];
    //! Specifies ownership of flash indicator toggle (true = driver, false = application)
    bool flashIndicatorDriverControlled = false;    
    //! Reserved for future expansion, must be set to null
    void* ext = {};
};

enum ReflexMarker
{
    eReflexMarkerSimulationStart,
    eReflexMarkerSimulationEnd,
    eReflexMarkerRenderSubmitStart,
    eReflexMarkerRenderSubmitEnd,
    eReflexMarkerPresentStart,
    eReflexMarkerPresentEnd,
    eReflexMarkerInputSample,
    eReflexMarkerTriggerFlash,    
    eReflexMarkerPCLatencyPing,
    //! Special marker
    eReflexMarkerSleep = 0x1000,
};

}
