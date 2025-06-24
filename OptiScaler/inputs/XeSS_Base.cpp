#include "XeSS_Base.h"

std::map<xess_context_handle_t, NVSDK_NGX_Parameter*> _nvParams;
std::map<xess_context_handle_t, NVSDK_NGX_Handle*> _contexts;
std::map<xess_context_handle_t, Scale> _motionScales;
std::map<xess_context_handle_t, Scale> _jitterScales;
std::map<xess_context_handle_t, xess_d3d12_init_params_t> _d3d12InitParams;
std::map<xess_context_handle_t, xess_vk_init_params_t> _vkInitParams;
