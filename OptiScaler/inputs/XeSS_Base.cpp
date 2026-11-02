#include "pch.h"
#include "XeSS_Base.h"

std::unordered_map<xess_context_handle_t, NVSDK_NGX_Parameter*> _nvParams;
std::unordered_map<xess_context_handle_t, NVSDK_NGX_Handle*> _contexts;
std::unordered_map<xess_context_handle_t, Scale> _motionScales;
std::unordered_map<xess_context_handle_t, Scale> _jitterScales;
std::unordered_map<xess_context_handle_t, xess_d3d12_init_params_t> _d3d12InitParams;
std::unordered_map<xess_context_handle_t, xess_vk_init_params_t> _vkInitParams;
std::unordered_map<xess_context_handle_t, xess_d3d11_init_params_t> _d3d11InitParams;
std::unordered_set<xess_context_handle_t> _vkContexts;
