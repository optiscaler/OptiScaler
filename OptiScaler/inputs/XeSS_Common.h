#pragma once

#include "Util.h"
#include "Config.h"

#include "xess.h"

xess_result_t hk_xessGetVersion(xess_version_t* pVersion);
xess_result_t hk_xessGetVersionDx11(xess_version_t* pVersion);
xess_result_t hk_xessIsOptimalDriver(xess_context_handle_t hContext);
xess_result_t hk_xessSetLoggingCallback(xess_context_handle_t hContext, xess_logging_level_t loggingLevel,
                                        xess_app_log_callback_t loggingCallback);
xess_result_t hk_xessGetProperties(xess_context_handle_t hContext, const xess_2d_t* pOutputResolution,
                                   xess_properties_t* pBindingProperties);
xess_result_t hk_xessDestroyContext(xess_context_handle_t hContext);
xess_result_t hk_xessDestroyContextDx11(xess_context_handle_t hContext);
xess_result_t hk_xessSetVelocityScale(xess_context_handle_t hContext, float x, float y);
xess_result_t hk_xessForceLegacyScaleFactors(xess_context_handle_t hContext, bool force);
xess_result_t hk_xessGetExposureMultiplier(xess_context_handle_t hContext, float* pScale);
xess_result_t hk_xessGetInputResolution(xess_context_handle_t hContext, const xess_2d_t* pOutputResolution,
                                        xess_quality_settings_t qualitySettings, xess_2d_t* pInputResolution);
xess_result_t hk_xessGetIntelXeFXVersion(xess_context_handle_t hContext, xess_version_t* pVersion);
xess_result_t hk_xessGetJitterScale(xess_context_handle_t hContext, float* pX, float* pY);
xess_result_t hk_xessGetOptimalInputResolution(xess_context_handle_t hContext, const xess_2d_t* pOutputResolution,
                                               xess_quality_settings_t qualitySettings,
                                               xess_2d_t* pInputResolutionOptimal, xess_2d_t* pInputResolutionMin,
                                               xess_2d_t* pInputResolutionMax);
xess_result_t hk_xessGetVelocityScale(xess_context_handle_t hContext, float* pX, float* pY);
xess_result_t hk_xessSetJitterScale(xess_context_handle_t hContext, float x, float y);
xess_result_t hk_xessSetExposureMultiplier(xess_context_handle_t hContext, float scale);
xess_result_t hk_xessSetMaxResponsiveMaskValue(xess_context_handle_t hContext, float value);
xess_result_t hk_xessGetMaxResponsiveMaskValue(xess_context_handle_t hContext, float* pValue);
xess_result_t hk_xessSetContextParameterF();
xess_result_t hk_xessAILGetDecision();
xess_result_t hk_xessAILGetVersion();
xess_result_t hk_xessGetPipelineBuildStatus(xess_context_handle_t hContext);
