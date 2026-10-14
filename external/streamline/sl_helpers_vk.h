/*
* Copyright (c) 2022-2023 NVIDIA CORPORATION. All rights reserved
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

#pragma once

#include "sl.h"
#include <string.h>

namespace sl
{

#define SL_VK_FEATURE(n) if(strcmp(featureNames[i], #n) == 0) features.n = VK_TRUE;

inline VkPhysicalDeviceVulkan12Features getVkPhysicalDeviceVulkan12Features(uint32_t featureCount, const char** featureNames)
{
    VkPhysicalDeviceVulkan12Features features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    for (uint32_t i = 0; i < featureCount; i++)
    {
        SL_VK_FEATURE(samplerMirrorClampToEdge);
        SL_VK_FEATURE(drawIndirectCount);
        SL_VK_FEATURE(storageBuffer8BitAccess);
        SL_VK_FEATURE(uniformAndStorageBuffer8BitAccess);
        SL_VK_FEATURE(storagePushConstant8);
        SL_VK_FEATURE(shaderBufferInt64Atomics);
        SL_VK_FEATURE(shaderSharedInt64Atomics);
        SL_VK_FEATURE(shaderFloat16);
        SL_VK_FEATURE(shaderInt8);
        SL_VK_FEATURE(descriptorIndexing);
        SL_VK_FEATURE(shaderInputAttachmentArrayDynamicIndexing);
        SL_VK_FEATURE(shaderUniformTexelBufferArrayDynamicIndexing);
        SL_VK_FEATURE(shaderStorageTexelBufferArrayDynamicIndexing);
        SL_VK_FEATURE(shaderUniformBufferArrayNonUniformIndexing);
        SL_VK_FEATURE(shaderSampledImageArrayNonUniformIndexing);
        SL_VK_FEATURE(shaderStorageBufferArrayNonUniformIndexing);
        SL_VK_FEATURE(shaderStorageImageArrayNonUniformIndexing);
        SL_VK_FEATURE(shaderInputAttachmentArrayNonUniformIndexing);
        SL_VK_FEATURE(shaderUniformTexelBufferArrayNonUniformIndexing);
        SL_VK_FEATURE(shaderStorageTexelBufferArrayNonUniformIndexing);
        SL_VK_FEATURE(descriptorBindingUniformBufferUpdateAfterBind);
        SL_VK_FEATURE(descriptorBindingSampledImageUpdateAfterBind);
        SL_VK_FEATURE(descriptorBindingStorageImageUpdateAfterBind);
        SL_VK_FEATURE(descriptorBindingStorageBufferUpdateAfterBind);
        SL_VK_FEATURE(descriptorBindingUniformTexelBufferUpdateAfterBind);
        SL_VK_FEATURE(descriptorBindingStorageTexelBufferUpdateAfterBind);
        SL_VK_FEATURE(descriptorBindingUpdateUnusedWhilePending);
        SL_VK_FEATURE(descriptorBindingPartiallyBound);
        SL_VK_FEATURE(descriptorBindingVariableDescriptorCount);
        SL_VK_FEATURE(runtimeDescriptorArray);
        SL_VK_FEATURE(samplerFilterMinmax);
        SL_VK_FEATURE(scalarBlockLayout);
        SL_VK_FEATURE(imagelessFramebuffer);
        SL_VK_FEATURE(uniformBufferStandardLayout);
        SL_VK_FEATURE(shaderSubgroupExtendedTypes);
        SL_VK_FEATURE(separateDepthStencilLayouts);
        SL_VK_FEATURE(hostQueryReset);
        SL_VK_FEATURE(timelineSemaphore);
        SL_VK_FEATURE(bufferDeviceAddress);
        SL_VK_FEATURE(bufferDeviceAddressCaptureReplay);
        SL_VK_FEATURE(bufferDeviceAddressMultiDevice);
        SL_VK_FEATURE(vulkanMemoryModel);
        SL_VK_FEATURE(vulkanMemoryModelDeviceScope);
        SL_VK_FEATURE(vulkanMemoryModelAvailabilityVisibilityChains);
        SL_VK_FEATURE(shaderOutputViewportIndex);
        SL_VK_FEATURE(shaderOutputLayer);
        SL_VK_FEATURE(subgroupBroadcastDynamicId);
    }
    return features;
}

inline VkPhysicalDeviceVulkan13Features getVkPhysicalDeviceVulkan13Features(uint32_t featureCount, const char** featureNames)
{
    VkPhysicalDeviceVulkan13Features features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    for (uint32_t i = 0; i < featureCount; i++)
    {
        SL_VK_FEATURE(robustImageAccess);
        SL_VK_FEATURE(robustImageAccess);
        SL_VK_FEATURE(inlineUniformBlock);
        SL_VK_FEATURE(descriptorBindingInlineUniformBlockUpdateAfterBind);
        SL_VK_FEATURE(pipelineCreationCacheControl);
        SL_VK_FEATURE(privateData);
        SL_VK_FEATURE(shaderDemoteToHelperInvocation);
        SL_VK_FEATURE(shaderTerminateInvocation);
        SL_VK_FEATURE(subgroupSizeControl);
        SL_VK_FEATURE(computeFullSubgroups);
        SL_VK_FEATURE(synchronization2);
        SL_VK_FEATURE(textureCompressionASTC_HDR);
        SL_VK_FEATURE(shaderZeroInitializeWorkgroupMemory);
        SL_VK_FEATURE(dynamicRendering);
        SL_VK_FEATURE(shaderIntegerDotProduct);
        SL_VK_FEATURE(maintenance4);
    }
    return features;
}

inline VkPhysicalDeviceOpticalFlowFeaturesNV getVkPhysicalDeviceOpticalFlowNVFeatures(uint32_t featureCount, const char** featureNames)
{
    VkPhysicalDeviceOpticalFlowFeaturesNV features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV };
    for (uint32_t i = 0; i < featureCount; i++)
    {
        SL_VK_FEATURE(opticalFlow);
    }
    return features;
}

#define SL_VK_FEATURE_SUPPORT(T, feature, n) ((T*)pPhysicalDeviceFeatures)->n = ((feature) && (((T*)pSupportedFeatures)->n))
#define SL_VK_FEATURE_MERGE_SUPPORT(T, n) (pFeaturesToMerge == NULL) ? \
    SL_VK_FEATURE_SUPPORT(T, ((T*)pPhysicalDeviceFeatures)->n, n) : SL_VK_FEATURE_SUPPORT(T, ((((T*)pPhysicalDeviceFeatures)->n) || (((T*)pFeaturesToMerge)->n)), n)

inline void getMergedSupportedVkPhysicalDeviceVulkanFeatures(VkBaseOutStructure* pPhysicalDeviceFeatures, const VkBaseOutStructure* pFeaturesToMerge, const VkBaseOutStructure* pSupportedFeatures)
{
    if (pPhysicalDeviceFeatures == NULL || pSupportedFeatures == NULL)
    {
        return;
    }

    if (pFeaturesToMerge != NULL)
    {
        assert(pFeaturesToMerge->sType == pPhysicalDeviceFeatures->sType);
    }
    assert(pSupportedFeatures->sType == pPhysicalDeviceFeatures->sType);

    switch (pPhysicalDeviceFeatures->sType)
    {
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES:
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, samplerMirrorClampToEdge);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, samplerMirrorClampToEdge);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, drawIndirectCount);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, storageBuffer8BitAccess);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, uniformAndStorageBuffer8BitAccess);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, storagePushConstant8);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, shaderBufferInt64Atomics);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, shaderSharedInt64Atomics);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, shaderFloat16);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, shaderInt8);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, descriptorIndexing);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, shaderInputAttachmentArrayDynamicIndexing);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, shaderUniformTexelBufferArrayDynamicIndexing);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, shaderStorageTexelBufferArrayDynamicIndexing);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, shaderUniformBufferArrayNonUniformIndexing);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, shaderSampledImageArrayNonUniformIndexing);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, shaderStorageBufferArrayNonUniformIndexing);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, shaderStorageImageArrayNonUniformIndexing);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, shaderInputAttachmentArrayNonUniformIndexing);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, shaderUniformTexelBufferArrayNonUniformIndexing);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, shaderStorageTexelBufferArrayNonUniformIndexing);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, descriptorBindingUniformBufferUpdateAfterBind);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, descriptorBindingSampledImageUpdateAfterBind);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, descriptorBindingStorageImageUpdateAfterBind);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, descriptorBindingStorageBufferUpdateAfterBind);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, descriptorBindingUniformTexelBufferUpdateAfterBind);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, descriptorBindingStorageTexelBufferUpdateAfterBind);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, descriptorBindingUpdateUnusedWhilePending);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, descriptorBindingPartiallyBound);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, descriptorBindingVariableDescriptorCount);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, runtimeDescriptorArray);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, samplerFilterMinmax);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, scalarBlockLayout);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, imagelessFramebuffer);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, uniformBufferStandardLayout);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, shaderSubgroupExtendedTypes);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, separateDepthStencilLayouts);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, hostQueryReset);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, timelineSemaphore);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, bufferDeviceAddress);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, bufferDeviceAddressCaptureReplay);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, bufferDeviceAddressMultiDevice);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, vulkanMemoryModel);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, vulkanMemoryModelDeviceScope);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, vulkanMemoryModelAvailabilityVisibilityChains);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, shaderOutputViewportIndex);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, shaderOutputLayer);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan12Features, subgroupBroadcastDynamicId);
        break;

    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES:
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan13Features, robustImageAccess);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan13Features, robustImageAccess);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan13Features, inlineUniformBlock);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan13Features, descriptorBindingInlineUniformBlockUpdateAfterBind);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan13Features, pipelineCreationCacheControl);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan13Features, privateData);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan13Features, shaderDemoteToHelperInvocation);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan13Features, shaderTerminateInvocation);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan13Features, subgroupSizeControl);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan13Features, computeFullSubgroups);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan13Features, synchronization2);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan13Features, textureCompressionASTC_HDR);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan13Features, shaderZeroInitializeWorkgroupMemory);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan13Features, dynamicRendering);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan13Features, shaderIntegerDotProduct);
        SL_VK_FEATURE_MERGE_SUPPORT(VkPhysicalDeviceVulkan13Features, maintenance4);
        break;

    default:
        break;
    }
}

//! Interface to provide to slSetVulkanInfo when manually hooking Vulkan API and NOT
//! leveraging vkCreateDevice and vkCreateInstance proxies provided by SL.
//!
//! {0EED6FD5-82CD-43A9-BDB5-47A5BA2F45D6}
SL_STRUCT_BEGIN(VulkanInfo, StructType({ 0xeed6fd5, 0x82cd, 0x43a9, { 0xbd, 0xb5, 0x47, 0xa5, 0xba, 0x2f, 0x45, 0xd6 } }), kStructVersion3)
VkDevice device {};
VkInstance instance{};
VkPhysicalDevice physicalDevice{};
//! IMPORTANT: 
//! 
//! SL features can request additional graphics or compute queues.
//! The below values provide information about the queue families and
//! starting index at which SL queues are created.
uint32_t computeQueueIndex{};
uint32_t computeQueueFamily{};
uint32_t graphicsQueueIndex{};
uint32_t graphicsQueueFamily{};
uint32_t opticalFlowQueueIndex{};
uint32_t opticalFlowQueueFamily{};
bool useNativeOpticalFlowMode = false;
uint32_t computeQueueCreateFlags{};
uint32_t graphicsQueueCreateFlags{};
uint32_t opticalFlowQueueCreateFlags{};
SL_STRUCT_END()

}

using PFun_slSetVulkanInfo = sl::Result(const sl::VulkanInfo& info);

//! Specify Vulkan specific information
//! 
//! Use this method to provide Vulkan device, instance information to SL.
//! 
//! IMPORTANT: Only call this API if NOT using vkCreateDevice and vkCreateInstance proxies provided by SL.
//
//! @param info Reference to the structure providing the information
//! 
//! This method is NOT thread safe and should be called IMMEDIATELY after base interface is created.
SL_API sl::Result slSetVulkanInfo(const sl::VulkanInfo& info);
