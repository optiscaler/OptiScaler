#pragma once

#include "Util.h"
#include "Config.h"
#include "resource.h"

#include "XeSS_Base.h"
#include "xess_d3d12_debug.h"

xess_result_t hk_xessD3D12CreateContext(ID3D12Device* pDevice, xess_context_handle_t* phContext);
void XeSSDx12DeviceDestroyed(IUnknown* device);
xess_result_t hk_xessD3D12BuildPipelines(xess_context_handle_t hContext, ID3D12PipelineLibrary* pPipelineLibrary,
                                         bool blocking, uint32_t initFlags);
xess_result_t hk_xessD3D12Init(xess_context_handle_t hContext, const xess_d3d12_init_params_t* pInitParams);
xess_result_t hk_xessD3D12Execute(xess_context_handle_t hContext, ID3D12GraphicsCommandList* pCommandList,
                                  const xess_d3d12_execute_params_t* pExecParams);
xess_result_t hk_xessD3D12GetInitParams(xess_context_handle_t hContext, xess_d3d12_init_params_t* pInitParams);
xess_result_t hk_xessD3D12GetResourcesToDump(xess_context_handle_t hContext,
                                             xess_resources_to_dump_t** pResourcesToDump);
xess_result_t hk_xessD3D12GetProfilingData(xess_context_handle_t hContext, xess_profiling_data_t** pProfilingData);
