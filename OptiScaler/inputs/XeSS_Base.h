#pragma once

#include "pch.h"
#include <map>

#include <xess_d3d12.h>
#include <xess_vk.h>

typedef struct Scale
{
    float x;
    float y;
} scale;

extern std::map<xess_context_handle_t, NVSDK_NGX_Parameter*> _nvParams;
extern std::map<xess_context_handle_t, NVSDK_NGX_Handle*> _contexts;
extern std::map<xess_context_handle_t, Scale> _motionScales;
extern std::map<xess_context_handle_t, Scale> _jitterScales;
extern std::map<xess_context_handle_t, xess_d3d12_init_params_t> _d3d12InitParams;
extern std::map<xess_context_handle_t, xess_vk_init_params_t> _vkInitParams;
