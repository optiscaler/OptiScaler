#pragma once

#include "Util.h"
#include "Config.h"
#include "resource.h"

#include "XeSS_Base.h"
#include "xess_vk_debug.h"

xess_result_t hk_xessVKCreateContext(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                                     xess_context_handle_t* phContext);
void XeSSVkDeviceDestroyed(VkDevice device);
xess_result_t hk_xessVKBuildPipelines(xess_context_handle_t hContext, VkPipelineCache pipelineCache, bool blocking,
                                      uint32_t initFlags);
xess_result_t hk_xessVKInit(xess_context_handle_t hContext, const xess_vk_init_params_t* pInitParams);
xess_result_t hk_xessVKExecute(xess_context_handle_t hContext, VkCommandBuffer commandBuffer,
                               const xess_vk_execute_params_t* pExecParams);
xess_result_t hk_xessVKGetInitParams(xess_context_handle_t hContext, xess_vk_init_params_t* pInitParams);
xess_result_t hk_xessVKGetResourcesToDump(xess_context_handle_t hContext,
                                          xess_vk_resources_to_dump_t** pResourcesToDump);
xess_result_t hk_xessVKGetRequiredInstanceExtensions(uint32_t* instanceExtensionsCount,
                                                     const char* const** instanceExtensions, uint32_t* minVkApiVersion);
xess_result_t hk_xessVKGetRequiredDeviceExtensions(VkInstance instance, VkPhysicalDevice physicalDevice,
                                                   uint32_t* deviceExtensionsCount,
                                                   const char* const** deviceExtensions);
xess_result_t hk_xessVKGetRequiredDeviceFeatures(VkInstance instance, VkPhysicalDevice physicalDevice, void** features);
