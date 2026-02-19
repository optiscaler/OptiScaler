/*
* Copyright (c) 2022-2024 NVIDIA CORPORATION. All rights reserved
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

#define SL_CHECK(f) {auto _r = f; if(_r != sl::Result::eOk) return _r;}
#define SL_FAILED(r, f) sl::Result r = f; r != sl::Result::eOk
#define SL_SUCCEEDED(r, f) sl::Result r = f; r == sl::Result::eOk

namespace sl
{

enum class Result
{
    eOk,
    eErrorIO,
    eErrorDriverOutOfDate,
    eErrorOSOutOfDate,
    eErrorOSDisabledHWS,
    eErrorDeviceNotCreated,
    eErrorNoSupportedAdapterFound,
    eErrorAdapterNotSupported,
    eErrorNoPlugins,
    eErrorVulkanAPI,
    eErrorDXGIAPI,
    eErrorD3DAPI,
    // NRD was removed
    eErrorNRDAPI,
    eErrorNVAPI,
    eErrorReflexAPI,
    eErrorNGXFailed,
    eErrorJSONParsing,
    eErrorMissingProxy,
    eErrorMissingResourceState,
    eErrorInvalidIntegration,
    eErrorMissingInputParameter,
    eErrorNotInitialized,
    eErrorComputeFailed,
    eErrorInitNotCalled,
    eErrorExceptionHandler,
    eErrorInvalidParameter,
    eErrorMissingConstants,
    eErrorDuplicatedConstants,
    eErrorMissingOrInvalidAPI,
    eErrorCommonConstantsMissing,
    eErrorUnsupportedInterface,
    eErrorFeatureMissing,
    eErrorFeatureNotSupported,
    eErrorFeatureMissingHooks,
    eErrorFeatureFailedToLoad,
    eErrorFeatureWrongPriority,
    eErrorFeatureMissingDependency,
    eErrorFeatureManagerInvalidState,
    eErrorInvalidState,
    eWarnOutOfVRAM
};

}
