#pragma once
#include "SysUtils.h"
#include <map>
#include <unordered_set>

#include <xess_d3d12.h>
#include <xess_d3d11.h>
#include <xess_vk.h>

typedef struct Scale
{
    float x;
    float y;
} scale;

extern std::unordered_map<xess_context_handle_t, NVSDK_NGX_Parameter*> _nvParams;
extern std::unordered_map<xess_context_handle_t, NVSDK_NGX_Handle*> _contexts;
extern std::unordered_map<xess_context_handle_t, Scale> _motionScales;
extern std::unordered_map<xess_context_handle_t, Scale> _jitterScales;
extern std::unordered_map<xess_context_handle_t, xess_d3d12_init_params_t> _d3d12InitParams;
extern std::unordered_map<xess_context_handle_t, xess_vk_init_params_t> _vkInitParams;
extern std::unordered_map<xess_context_handle_t, xess_d3d11_init_params_t> _d3d11InitParams;
extern std::unordered_set<xess_context_handle_t> _vkContexts;
