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

#include <vector>

namespace sl
{

// Parameters used to communicate across plugin borders

namespace param
{
namespace global
{

constexpr const char* kColorBuffersHDR = "sl.param.global.colorBuffersHDR";
constexpr const char* kPFunGetConsts = "sl.param.global.getConstsFunc";
constexpr const char* kPFunAllocateResource = "sl.param.global.allocateResource";
constexpr const char* kPFunReleaseResource = "sl.param.global.releaseResource";
constexpr const char* kPluginPath = "sl.param.global.pluginPath";
constexpr const char* kLogInterface = "sl.param.global.logInterface";
constexpr const char* kOTAInterface = "sl.param.global.otaInterface";
constexpr const char* kNGXContext = "sl.param.global.ngxContext";
constexpr const char* kNGXContextD3D12 = "sl.param.global.ngxContextD3D12";
constexpr const char* kDRSContext = "sl.param.global.drsContext";
constexpr const char* kSwapchainBufferCount = "sl.param.global.swapchainbuffercount";
constexpr const char* kDebugMode = "sl.param.global.dbgMode";
constexpr const char* kPFunGetTag = "sl.param.global.getTag";
constexpr const char* kVulkanTable = "sl.param.global.vulkanTable";
constexpr const char* kPreferenceFlags = "sl.param.global.prefFlags";
}

namespace interposer
{
constexpr const char* kVKValidationActive = "sl.param.interposer.vkValidationActive";
}

namespace common
{

constexpr const char* kSystemCaps = "sl.param.common.gpuInfo";
constexpr const char* kComputeAPI = "sl.param.common.computeAPI";
constexpr const char* kComputeDX11On12API = "sl.param.common.computeDX11On12API";
constexpr const char* kCaptureAPI = "sl.param.common.captureAPI";
constexpr const char* kKeyboardAPI = "sl.param.common.keyboardAPI";
constexpr const char* kPFunRegisterEvaluateCallbacks = "sl.param.common.registerEvaluateCallbacks";
constexpr const char* kPFunGetStringFromModule = "sl.param.common.getStringFromModule";
constexpr const char* kPFunUpdateCommonEmbeddedJSONConfig = "sl.param.common.updateCommonEmbeddedJSONConfig";
constexpr const char* kPFunNGXGetFeatureRequirements = "sl.param.common.NGXGetFeatureRequirements";
constexpr const char* kPFunFindAdapter = "sl.param.common.findAdapter";

}

namespace template_plugin
{

constexpr const char* kCurrentFrame = "sl.param.template_plugin.frame";

}

namespace dlss_g
{

constexpr const char* kCurrentFrame = "sl.param.reserved.frame";

}

namespace dlss
{

constexpr const char* kCurrentFrame = "sl.param.dlss.frame";

}

namespace nis
{

constexpr const char* kCurrentFrame = "sl.param.nis.frame";

}

namespace latency
{

constexpr const char* kCurrentFrame = "sl.param.latency.frame";
constexpr const char* kMarkerPresentFrame = "sl.param.latency.markerPresentFrame";
//! DEPRECATED (reflex-pcl):
constexpr const char* kPFunSetLatencyStatsMarker = "sl.param.latency.setLatencyStatsMarker";

}

namespace pcl
{
constexpr const char* kPFunSetPCLStatsMarker = "sl.param.pcl.setPCLStatsMarker";
}

//! DEPRECATED (reflex-pcl):
//! These should only be used to bridge Reflex and PCL plugins.
namespace _deprecated_reflex_pcl
{
    constexpr const char* kSlSetData = "sl.param._deprecated_reflex_pcl.slSetData";
    constexpr const char* kSlGetData = "sl.param._deprecated_reflex_pcl.slGetData";
}

namespace deepDVC
{
constexpr const char* kCurrentFrame = "sl.param.deepdvc.frame";
}

namespace debug_plugin
{
constexpr const char* kSetConstsFunc = "sl.param.debug_plugin.setConstsFunc";
constexpr const char* kGetSettingsFunc = "sl.param.debug_plugin.getSettingsFunc";
constexpr const char* kStats = "sl.param.debug_plugin.stats";
constexpr const char* kCurrentFrame = "sl.param.debug_plugin.frame";
}

namespace imgui
{
constexpr const char* kInterface = "sl.param.imgui.interface";
}

namespace dlss_d
{
constexpr const char* kCurrentFrame = "sl.param.dlss_d.frame";
}

struct IParameters
{
    virtual void set(const char* key, bool value) = 0;
    virtual void set(const char* key, unsigned long long value) = 0;
    virtual void set(const char* key, float value) = 0;
    virtual void set(const char* key, double value) = 0;
    virtual void set(const char* key, unsigned int value) = 0;
    virtual void set(const char* key, int value) = 0;
    virtual void set(const char* key, void* value) = 0;

    virtual bool get(const char* key, bool* value) const = 0;
    virtual bool get(const char* key, unsigned long long* value) const = 0;
    virtual bool get(const char* key, float* value) const = 0;
    virtual bool get(const char* key, double* value) const = 0;
    virtual bool get(const char* key, unsigned int* value) const = 0;
    virtual bool get(const char* key, int* value) const = 0;
    virtual bool get(const char* key, void** value) const = 0;

    virtual std::vector<std::string> enumerate() const = 0;
};

// Helpers

template<typename T>
inline bool getPointerParam(IParameters* parameters, const char* key, T** res, bool optional = false, uint32_t id = 0)
{
    void* p = nullptr;
    if (!parameters->get(id ? (std::string(key) + "." + std::to_string(id)).c_str() : key, &p))
    {
        if (!optional)
        {
            return false;
        }
    }
    *res = (T*)p;
    return true;
};

template<typename T>
inline bool getParam(IParameters* parameters, const char* key, T* res, bool optional = false)
{
    if (!parameters->get(key, res))
    {
        if (!optional)
        {
            return false;
        }
        *res = {};
    }
    return true;
};

} // namespace param
} // namespace sl
