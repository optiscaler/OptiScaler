#pragma once

#include "Util.h"
#include "Config.h"
#include "resource.h"

#include "XeSS_Base.h"

xess_result_t hk_xessD3D11CreateContext(ID3D11Device* device, xess_context_handle_t* phContext);
void XeSSDx11DeviceDestroyed(IUnknown* device);
xess_result_t hk_xessD3D11Init(xess_context_handle_t hContext, const xess_d3d11_init_params_t* pInitParams);
xess_result_t hk_xessD3D11Execute(xess_context_handle_t hContext, const xess_d3d11_execute_params_t* pExecParams);
xess_result_t hk_xessD3D11GetInitParams(xess_context_handle_t hContext, xess_d3d11_init_params_t* pInitParams);
