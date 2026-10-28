#pragma once

#include "SysUtils.h"

#include <ffx_api.h>

struct FfxModule
{
    HMODULE dll = nullptr;
    feature_version version { 0, 0, 0 };
    std::wstring filePath;

    bool skipCreateCalls = false;
    bool skipConfigureCalls = false;
    bool skipQueryCalls = false;
    bool skipDispatchCalls = false;

    bool isLoader = false;
    bool hooked = false;

    // Vulkan modules of FFX SDK 2.x and the driver take FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK_V2,
    // the legacy amd_fidelityfx_vk.dll takes FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK
    bool vkBackendV2 = false;

    PfnFfxCreateContext CreateContext = nullptr;
    PfnFfxDestroyContext DestroyContext = nullptr;
    PfnFfxConfigure Configure = nullptr;
    PfnFfxQuery Query = nullptr;
    PfnFfxDispatch Dispatch = nullptr;
};
