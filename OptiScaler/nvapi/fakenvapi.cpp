#include "pch.h"

#include <unordered_map>

#include "fakenvapi.h"
#include "NvApiTypes.h"
#include "fakenvapi/nvapi_calls.h"

#define nvapi_interface_table nvapi_interface_table_extern
#include <nvapi_interface.h>
#undef nvapi_interface_table

std::unordered_map<NvU32, void*> fakenvapi::idToFuncMapping;

void fakenvapi::init(bool onlyContext)
{
    LowLatencyCtx::init();

    if (onlyContext)
        return;
}

void fakenvapi::deinit()
{
    LowLatencyCtx::get()->deinit_current_tech();
    LowLatencyCtx::shutdown();
}

// names from: https://github.com/SveSop/nvapi_standalone/blob/master/dlls/nvapi/nvapi.c
static NVAPI_INTERFACE_TABLE additional_interface_table[] = { { "NvAPI_Diag_ReportCallStart", 0x33c7358c },
                                                              { "NvAPI_Diag_ReportCallReturn", 0x593e8644 },
                                                              { "NvAPI_Unknown_1", 0xe9b009b9 },
                                                              { "NvAPI_SK_1", 0x57f7caac },
                                                              { "NvAPI_SK_2", 0x11104158 },
                                                              { "NvAPI_SK_3", 0xe3795199 },
                                                              { "NvAPI_SK_4", 0xdf0dfcdd },
                                                              { "NvAPI_SK_5", 0x932ac8fb } };

extern "C" __declspec(dllexport) void* nvapi_QueryInterface(NvU32 id) { return fakenvapi::queryInterface(id); }

void* __cdecl fakenvapi::queryInterface(NvU32 id)
{
    auto entry = idToFuncMapping.find(id);
    if (entry != idToFuncMapping.end())
        return entry->second;

    constexpr auto original_size = sizeof(nvapi_interface_table_extern) / sizeof(nvapi_interface_table_extern[0]);
    constexpr auto additional_size = sizeof(additional_interface_table) / sizeof(additional_interface_table[0]);

    constexpr auto total_size = original_size + additional_size;

    struct NVAPI_INTERFACE_TABLE extended_interface_table[total_size] {};
    memcpy(extended_interface_table, nvapi_interface_table_extern,
           sizeof(nvapi_interface_table_extern)); // copy original table

    for (unsigned int i = 0; i < additional_size; i++)
    {
        extended_interface_table[original_size + i] = additional_interface_table[i];
    }

    auto it = std::find_if(std::begin(extended_interface_table), std::end(extended_interface_table),
                           [id](const auto& item) { return item.id == id; });

    if (it == std::end(extended_interface_table))
    {
        LOG_DEBUG("NvAPI_QueryInterface (0x{:x}): Unknown interface ID", id);
        return idToFuncMapping.insert({ id, nullptr }).first->second;
    }

    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Initialize)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GetInterfaceVersionString)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_EnumNvidiaDisplayHandle)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GetLogicalGPUFromPhysicalGPU)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_EnumPhysicalGPUs)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_EnumLogicalGPUs)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GetGPUIDfromPhysicalGPU)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GetPhysicalGPUFromGPUID)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GetPhysicalGPUsFromDisplay)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GetPhysicalGPUsFromLogicalGPU)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GetErrorMessage)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GetDisplayDriverVersion)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GPU_GetLogicalGpuInfo)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GPU_GetConnectedDisplayIds)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GPU_CudaEnumComputeCapableGpus)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GPU_GetArchInfo)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GPU_GetPCIIdentifiers)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GPU_GetFullName)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GPU_GetGpuCoreCount)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GPU_GetAllClockFrequencies)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GPU_GetAdapterIdFromPhysicalGpu)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GPU_GetPstates20)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_DISP_GetDisplayIdByDisplayName)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_DISP_GetGDIPrimaryDisplayId)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Disp_SetOutputMode)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Disp_GetOutputMode)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Disp_GetHdrCapabilities)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Disp_HdrColorControl)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Mosaic_GetDisplayViewportsByResolution)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_SYS_GetDisplayDriverInfo)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_SYS_GetDriverAndBranchVersion)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_SYS_GetDisplayIdFromGpuAndOutputId)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_SYS_GetGpuAndOutputIdFromDisplayId)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D_SetResourceHint)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D_GetObjectHandleForResource)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D_GetSleepStatus)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D_GetLatency)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D_SetSleepMode)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D_SetLatencyMarker)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D_Sleep)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D_SetReflexSync)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D11_IsNvShaderExtnOpCodeSupported)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D11_BeginUAVOverlap)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D11_EndUAVOverlap)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D11_SetDepthBoundsTest)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D12_GetRaytracingCaps)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D12_IsNvShaderExtnOpCodeSupported)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D12_SetNvShaderExtnSlotSpaceLocalThread)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D12_GetRaytracingAccelerationStructurePrebuildInfoEx)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D12_BuildRaytracingAccelerationStructureEx)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D12_NotifyOutOfBandCommandQueue)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D12_SetAsyncFrameMarker)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Vulkan_InitLowLatencyDevice)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Vulkan_DestroyLowLatencyDevice)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Vulkan_GetSleepStatus)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Vulkan_SetSleepMode)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Vulkan_Sleep)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Vulkan_GetLatency)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Vulkan_SetLatencyMarker)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Vulkan_NotifyOutOfBandVkQueue)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_DRS_CreateSession)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_DRS_LoadSettings)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_DRS_SaveSettings)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_DRS_GetBaseProfile)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_DRS_GetSetting)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_DRS_SetSetting)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_DRS_DestroySession)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_NGX_GetDriverFeatureSupport)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Unknown_1)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_SK_1)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_SK_2)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_SK_3)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_SK_4)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_SK_5)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Unload)

    LOG_DEBUG("{}: not implemented, placeholder given", it->func);
    return idToFuncMapping.insert({ id, (void*) placeholder }).first->second;
    // return registry.insert({ id, nullptr }).first->second;
}

bool fakenvapi::updateModeAndContext()
{
    LOG_FUNC();

    auto lowLatencyCtx = LowLatencyCtx::get();

    if (!lowLatencyCtx)
    {
        // Real reflex should fall here and not log
        if (_usingFakenvapiAsMainNvapi)
            LOG_ERROR("Couldn't get low latency context");

        return false;
    }

    auto result = lowLatencyCtx->get_low_latency_tech_context(&_lowLatencyTechContext, &_lowLatencyMode);

    if (!result)
        LOG_TRACE_FAKENVAPI("Can't get Low Latency context from fakenvapi");

    return result;
}

bool fakenvapi::isLowLatencyActive()
{
    auto lowLatencyCtx = LowLatencyCtx::get();

    if (!lowLatencyCtx)
    {
        if (_usingFakenvapiAsMainNvapi)
            LOG_ERROR("Couldn't get low latency context");

        return false;
    }

    return lowLatencyCtx->is_low_latency_enabled();
}

LowLatencyMode fakenvapi::getCurrentMode()
{
    if (updateModeAndContext())
        return _lowLatencyMode;
    else
        return LowLatencyMode::None;
}

bool fakenvapi::isUsingAsMainNvapi() { return _usingFakenvapiAsMainNvapi; }

void fakenvapi::setUsingAsMainNvapi(bool usingAsMain) { _usingFakenvapiAsMainNvapi = usingAsMain; }
