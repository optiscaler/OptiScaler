#include <pch.h>

#include "VulkanwDx12_Hooks.h"

#include <State.h>
#include <Config.h>

#include <magic_enum.hpp>

#include <detours/detours.h>

#include <vulkan/vulkan_core.h>
#include <misc/IdentifyGpu.h>

static PFN_vkQueueSubmit o_vkQueueSubmit = nullptr;
static PFN_vkQueueSubmit2 o_vkQueueSubmit2 = nullptr;
static PFN_vkQueueSubmit2KHR o_vkQueueSubmit2KHR = nullptr;
static PFN_vkBeginCommandBuffer o_vkBeginCommandBuffer = nullptr;
static PFN_vkEndCommandBuffer o_vkEndCommandBuffer = nullptr;
static PFN_vkResetCommandBuffer o_vkResetCommandBuffer = nullptr;
static PFN_vkCmdExecuteCommands o_vkCmdExecuteCommands = nullptr;
static PFN_vkFreeCommandBuffers o_vkFreeCommandBuffers = nullptr;
static PFN_vkResetCommandPool o_vkResetCommandPool = nullptr;
static PFN_vkAllocateCommandBuffers o_vkAllocateCommandBuffers = nullptr;
static PFN_vkDestroyCommandPool o_vkDestroyCommandPool = nullptr;
static PFN_vkCreateCommandPool o_vkCreateCommandPool = nullptr;

static std::mutex mutexCommandPoolToQueueFamilyMap;
static std::unordered_map<VkCommandPool, uint32_t> commandPoolToQueueFamilyMap;

// #define LOG_ALL_RECORDS

#ifndef LOG_ALL_RECORDS
// #define LOG_VIRTUAL_RECORDS
#endif // !LOG_ALL_RECORDS

#pragma region vkCmd function pointers

static PFN_vkCmdBindPipeline o_vkCmdBindPipeline = nullptr;
static PFN_vkCmdSetViewport o_vkCmdSetViewport = nullptr;
static PFN_vkCmdSetScissor o_vkCmdSetScissor = nullptr;
static PFN_vkCmdSetLineWidth o_vkCmdSetLineWidth = nullptr;
static PFN_vkCmdSetDepthBias o_vkCmdSetDepthBias = nullptr;
static PFN_vkCmdSetBlendConstants o_vkCmdSetBlendConstants = nullptr;
static PFN_vkCmdSetDepthBounds o_vkCmdSetDepthBounds = nullptr;
static PFN_vkCmdSetStencilCompareMask o_vkCmdSetStencilCompareMask = nullptr;
static PFN_vkCmdSetStencilWriteMask o_vkCmdSetStencilWriteMask = nullptr;
static PFN_vkCmdSetStencilReference o_vkCmdSetStencilReference = nullptr;
static PFN_vkCmdBindDescriptorSets o_vkCmdBindDescriptorSets = nullptr;
static PFN_vkCmdBindIndexBuffer o_vkCmdBindIndexBuffer = nullptr;
static PFN_vkCmdBindVertexBuffers o_vkCmdBindVertexBuffers = nullptr;
static PFN_vkCmdDraw o_vkCmdDraw = nullptr;
static PFN_vkCmdDrawIndexed o_vkCmdDrawIndexed = nullptr;
static PFN_vkCmdDrawIndirect o_vkCmdDrawIndirect = nullptr;
static PFN_vkCmdDrawIndexedIndirect o_vkCmdDrawIndexedIndirect = nullptr;
static PFN_vkCmdDispatch o_vkCmdDispatch = nullptr;
static PFN_vkCmdDispatchIndirect o_vkCmdDispatchIndirect = nullptr;
static PFN_vkCmdCopyBuffer o_vkCmdCopyBuffer = nullptr;
static PFN_vkCmdCopyImage o_vkCmdCopyImage = nullptr;
static PFN_vkCmdBlitImage o_vkCmdBlitImage = nullptr;
static PFN_vkCmdCopyBufferToImage o_vkCmdCopyBufferToImage = nullptr;
static PFN_vkCmdCopyImageToBuffer o_vkCmdCopyImageToBuffer = nullptr;
static PFN_vkCmdUpdateBuffer o_vkCmdUpdateBuffer = nullptr;
static PFN_vkCmdFillBuffer o_vkCmdFillBuffer = nullptr;
static PFN_vkCmdClearColorImage o_vkCmdClearColorImage = nullptr;
static PFN_vkCmdClearDepthStencilImage o_vkCmdClearDepthStencilImage = nullptr;
static PFN_vkCmdClearAttachments o_vkCmdClearAttachments = nullptr;
static PFN_vkCmdResolveImage o_vkCmdResolveImage = nullptr;
static PFN_vkCmdSetEvent o_vkCmdSetEvent = nullptr;
static PFN_vkCmdResetEvent o_vkCmdResetEvent = nullptr;
static PFN_vkCmdWaitEvents o_vkCmdWaitEvents = nullptr;
static PFN_vkCmdPipelineBarrier o_vkCmdPipelineBarrier = nullptr;
static PFN_vkCmdBeginQuery o_vkCmdBeginQuery = nullptr;
static PFN_vkCmdEndQuery o_vkCmdEndQuery = nullptr;
static PFN_vkCmdResetQueryPool o_vkCmdResetQueryPool = nullptr;
static PFN_vkCmdWriteTimestamp o_vkCmdWriteTimestamp = nullptr;
static PFN_vkCmdCopyQueryPoolResults o_vkCmdCopyQueryPoolResults = nullptr;
static PFN_vkCmdPushConstants o_vkCmdPushConstants = nullptr;
static PFN_vkCmdBeginRenderPass o_vkCmdBeginRenderPass = nullptr;
static PFN_vkCmdNextSubpass o_vkCmdNextSubpass = nullptr;
static PFN_vkCmdEndRenderPass o_vkCmdEndRenderPass = nullptr;
static PFN_vkCmdSetDeviceMask o_vkCmdSetDeviceMask = nullptr;
static PFN_vkCmdDispatchBase o_vkCmdDispatchBase = nullptr;
static PFN_vkCmdDrawIndirectCount o_vkCmdDrawIndirectCount = nullptr;
static PFN_vkCmdDrawIndexedIndirectCount o_vkCmdDrawIndexedIndirectCount = nullptr;
static PFN_vkCmdBeginRenderPass2 o_vkCmdBeginRenderPass2 = nullptr;
static PFN_vkCmdNextSubpass2 o_vkCmdNextSubpass2 = nullptr;
static PFN_vkCmdEndRenderPass2 o_vkCmdEndRenderPass2 = nullptr;
static PFN_vkCmdSetEvent2 o_vkCmdSetEvent2 = nullptr;
static PFN_vkCmdResetEvent2 o_vkCmdResetEvent2 = nullptr;
static PFN_vkCmdWaitEvents2 o_vkCmdWaitEvents2 = nullptr;
static PFN_vkCmdPipelineBarrier2 o_vkCmdPipelineBarrier2 = nullptr;
static PFN_vkCmdWriteTimestamp2 o_vkCmdWriteTimestamp2 = nullptr;
static PFN_vkCmdCopyBuffer2 o_vkCmdCopyBuffer2 = nullptr;
static PFN_vkCmdCopyImage2 o_vkCmdCopyImage2 = nullptr;
static PFN_vkCmdCopyBufferToImage2 o_vkCmdCopyBufferToImage2 = nullptr;
static PFN_vkCmdCopyImageToBuffer2 o_vkCmdCopyImageToBuffer2 = nullptr;
static PFN_vkCmdBlitImage2 o_vkCmdBlitImage2 = nullptr;
static PFN_vkCmdResolveImage2 o_vkCmdResolveImage2 = nullptr;
static PFN_vkCmdBeginRendering o_vkCmdBeginRendering = nullptr;
static PFN_vkCmdEndRendering o_vkCmdEndRendering = nullptr;
static PFN_vkCmdSetCullMode o_vkCmdSetCullMode = nullptr;
static PFN_vkCmdSetFrontFace o_vkCmdSetFrontFace = nullptr;
static PFN_vkCmdSetPrimitiveTopology o_vkCmdSetPrimitiveTopology = nullptr;
static PFN_vkCmdSetViewportWithCount o_vkCmdSetViewportWithCount = nullptr;
static PFN_vkCmdSetScissorWithCount o_vkCmdSetScissorWithCount = nullptr;
static PFN_vkCmdBindVertexBuffers2 o_vkCmdBindVertexBuffers2 = nullptr;
static PFN_vkCmdSetDepthTestEnable o_vkCmdSetDepthTestEnable = nullptr;
static PFN_vkCmdSetDepthWriteEnable o_vkCmdSetDepthWriteEnable = nullptr;
static PFN_vkCmdSetDepthCompareOp o_vkCmdSetDepthCompareOp = nullptr;
static PFN_vkCmdSetDepthBoundsTestEnable o_vkCmdSetDepthBoundsTestEnable = nullptr;
static PFN_vkCmdSetStencilTestEnable o_vkCmdSetStencilTestEnable = nullptr;
static PFN_vkCmdSetStencilOp o_vkCmdSetStencilOp = nullptr;
static PFN_vkCmdSetRasterizerDiscardEnable o_vkCmdSetRasterizerDiscardEnable = nullptr;
static PFN_vkCmdSetDepthBiasEnable o_vkCmdSetDepthBiasEnable = nullptr;
static PFN_vkCmdSetPrimitiveRestartEnable o_vkCmdSetPrimitiveRestartEnable = nullptr;
static PFN_vkCmdSetLineStipple o_vkCmdSetLineStipple = nullptr;
static PFN_vkCmdBindIndexBuffer2 o_vkCmdBindIndexBuffer2 = nullptr;
static PFN_vkCmdPushDescriptorSet o_vkCmdPushDescriptorSet = nullptr;
static PFN_vkCmdPushDescriptorSetWithTemplate o_vkCmdPushDescriptorSetWithTemplate = nullptr;
static PFN_vkCmdSetRenderingAttachmentLocations o_vkCmdSetRenderingAttachmentLocations = nullptr;
static PFN_vkCmdSetRenderingInputAttachmentIndices o_vkCmdSetRenderingInputAttachmentIndices = nullptr;
static PFN_vkCmdBindDescriptorSets2 o_vkCmdBindDescriptorSets2 = nullptr;
static PFN_vkCmdPushConstants2 o_vkCmdPushConstants2 = nullptr;
static PFN_vkCmdPushDescriptorSet2 o_vkCmdPushDescriptorSet2 = nullptr;
static PFN_vkCmdPushDescriptorSetWithTemplate2 o_vkCmdPushDescriptorSetWithTemplate2 = nullptr;
static PFN_vkCmdBeginVideoCodingKHR o_vkCmdBeginVideoCodingKHR = nullptr;
static PFN_vkCmdEndVideoCodingKHR o_vkCmdEndVideoCodingKHR = nullptr;
static PFN_vkCmdControlVideoCodingKHR o_vkCmdControlVideoCodingKHR = nullptr;
static PFN_vkCmdDecodeVideoKHR o_vkCmdDecodeVideoKHR = nullptr;
static PFN_vkCmdBeginRenderingKHR o_vkCmdBeginRenderingKHR = nullptr;
static PFN_vkCmdEndRenderingKHR o_vkCmdEndRenderingKHR = nullptr;
static PFN_vkCmdSetDeviceMaskKHR o_vkCmdSetDeviceMaskKHR = nullptr;
static PFN_vkCmdDispatchBaseKHR o_vkCmdDispatchBaseKHR = nullptr;
static PFN_vkCmdPushDescriptorSetKHR o_vkCmdPushDescriptorSetKHR = nullptr;
static PFN_vkCmdPushDescriptorSetWithTemplateKHR o_vkCmdPushDescriptorSetWithTemplateKHR = nullptr;
static PFN_vkCmdBeginRenderPass2KHR o_vkCmdBeginRenderPass2KHR = nullptr;
static PFN_vkCmdNextSubpass2KHR o_vkCmdNextSubpass2KHR = nullptr;
static PFN_vkCmdEndRenderPass2KHR o_vkCmdEndRenderPass2KHR = nullptr;
static PFN_vkCmdDrawIndirectCountKHR o_vkCmdDrawIndirectCountKHR = nullptr;
static PFN_vkCmdDrawIndexedIndirectCountKHR o_vkCmdDrawIndexedIndirectCountKHR = nullptr;
static PFN_vkCmdSetFragmentShadingRateKHR o_vkCmdSetFragmentShadingRateKHR = nullptr;
static PFN_vkCmdSetRenderingAttachmentLocationsKHR o_vkCmdSetRenderingAttachmentLocationsKHR = nullptr;
static PFN_vkCmdSetRenderingInputAttachmentIndicesKHR o_vkCmdSetRenderingInputAttachmentIndicesKHR = nullptr;
static PFN_vkCmdEncodeVideoKHR o_vkCmdEncodeVideoKHR = nullptr;
static PFN_vkCmdSetEvent2KHR o_vkCmdSetEvent2KHR = nullptr;
static PFN_vkCmdResetEvent2KHR o_vkCmdResetEvent2KHR = nullptr;
static PFN_vkCmdWaitEvents2KHR o_vkCmdWaitEvents2KHR = nullptr;
static PFN_vkCmdPipelineBarrier2KHR o_vkCmdPipelineBarrier2KHR = nullptr;
static PFN_vkCmdWriteTimestamp2KHR o_vkCmdWriteTimestamp2KHR = nullptr;
static PFN_vkCmdCopyBuffer2KHR o_vkCmdCopyBuffer2KHR = nullptr;
static PFN_vkCmdCopyImage2KHR o_vkCmdCopyImage2KHR = nullptr;
static PFN_vkCmdCopyBufferToImage2KHR o_vkCmdCopyBufferToImage2KHR = nullptr;
static PFN_vkCmdCopyImageToBuffer2KHR o_vkCmdCopyImageToBuffer2KHR = nullptr;
static PFN_vkCmdBlitImage2KHR o_vkCmdBlitImage2KHR = nullptr;
static PFN_vkCmdResolveImage2KHR o_vkCmdResolveImage2KHR = nullptr;
static PFN_vkCmdTraceRaysIndirect2KHR o_vkCmdTraceRaysIndirect2KHR = nullptr;
static PFN_vkCmdBindIndexBuffer2KHR o_vkCmdBindIndexBuffer2KHR = nullptr;
static PFN_vkCmdSetLineStippleKHR o_vkCmdSetLineStippleKHR = nullptr;
static PFN_vkCmdBindDescriptorSets2KHR o_vkCmdBindDescriptorSets2KHR = nullptr;
static PFN_vkCmdPushConstants2KHR o_vkCmdPushConstants2KHR = nullptr;
static PFN_vkCmdPushDescriptorSet2KHR o_vkCmdPushDescriptorSet2KHR = nullptr;
static PFN_vkCmdPushDescriptorSetWithTemplate2KHR o_vkCmdPushDescriptorSetWithTemplate2KHR = nullptr;
static PFN_vkCmdSetDescriptorBufferOffsets2EXT o_vkCmdSetDescriptorBufferOffsets2EXT = nullptr;
static PFN_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT o_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT = nullptr;
static PFN_vkCmdDebugMarkerBeginEXT o_vkCmdDebugMarkerBeginEXT = nullptr;
static PFN_vkCmdDebugMarkerEndEXT o_vkCmdDebugMarkerEndEXT = nullptr;
static PFN_vkCmdDebugMarkerInsertEXT o_vkCmdDebugMarkerInsertEXT = nullptr;
static PFN_vkCmdBindTransformFeedbackBuffersEXT o_vkCmdBindTransformFeedbackBuffersEXT = nullptr;
static PFN_vkCmdBeginTransformFeedbackEXT o_vkCmdBeginTransformFeedbackEXT = nullptr;
static PFN_vkCmdEndTransformFeedbackEXT o_vkCmdEndTransformFeedbackEXT = nullptr;
static PFN_vkCmdBeginQueryIndexedEXT o_vkCmdBeginQueryIndexedEXT = nullptr;
static PFN_vkCmdEndQueryIndexedEXT o_vkCmdEndQueryIndexedEXT = nullptr;
static PFN_vkCmdDrawIndirectByteCountEXT o_vkCmdDrawIndirectByteCountEXT = nullptr;
static PFN_vkCmdCuLaunchKernelNVX o_vkCmdCuLaunchKernelNVX = nullptr;
static PFN_vkCmdDrawIndirectCountAMD o_vkCmdDrawIndirectCountAMD = nullptr;
static PFN_vkCmdDrawIndexedIndirectCountAMD o_vkCmdDrawIndexedIndirectCountAMD = nullptr;
static PFN_vkCmdBeginConditionalRenderingEXT o_vkCmdBeginConditionalRenderingEXT = nullptr;
static PFN_vkCmdEndConditionalRenderingEXT o_vkCmdEndConditionalRenderingEXT = nullptr;
static PFN_vkCmdSetViewportWScalingNV o_vkCmdSetViewportWScalingNV = nullptr;
static PFN_vkCmdSetDiscardRectangleEXT o_vkCmdSetDiscardRectangleEXT = nullptr;
static PFN_vkCmdSetDiscardRectangleEnableEXT o_vkCmdSetDiscardRectangleEnableEXT = nullptr;
static PFN_vkCmdSetDiscardRectangleModeEXT o_vkCmdSetDiscardRectangleModeEXT = nullptr;
static PFN_vkCmdBeginDebugUtilsLabelEXT o_vkCmdBeginDebugUtilsLabelEXT = nullptr;
static PFN_vkCmdEndDebugUtilsLabelEXT o_vkCmdEndDebugUtilsLabelEXT = nullptr;
static PFN_vkCmdInsertDebugUtilsLabelEXT o_vkCmdInsertDebugUtilsLabelEXT = nullptr;
static PFN_vkCmdSetSampleLocationsEXT o_vkCmdSetSampleLocationsEXT = nullptr;
static PFN_vkCmdBindShadingRateImageNV o_vkCmdBindShadingRateImageNV = nullptr;
static PFN_vkCmdSetViewportShadingRatePaletteNV o_vkCmdSetViewportShadingRatePaletteNV = nullptr;
static PFN_vkCmdSetCoarseSampleOrderNV o_vkCmdSetCoarseSampleOrderNV = nullptr;
static PFN_vkCmdBuildAccelerationStructureNV o_vkCmdBuildAccelerationStructureNV = nullptr;
static PFN_vkCmdCopyAccelerationStructureNV o_vkCmdCopyAccelerationStructureNV = nullptr;
static PFN_vkCmdTraceRaysNV o_vkCmdTraceRaysNV = nullptr;
static PFN_vkCmdWriteAccelerationStructuresPropertiesNV o_vkCmdWriteAccelerationStructuresPropertiesNV = nullptr;
static PFN_vkCmdWriteBufferMarkerAMD o_vkCmdWriteBufferMarkerAMD = nullptr;
static PFN_vkCmdWriteBufferMarker2AMD o_vkCmdWriteBufferMarker2AMD = nullptr;
static PFN_vkCmdDrawMeshTasksNV o_vkCmdDrawMeshTasksNV = nullptr;
static PFN_vkCmdDrawMeshTasksIndirectNV o_vkCmdDrawMeshTasksIndirectNV = nullptr;
static PFN_vkCmdDrawMeshTasksIndirectCountNV o_vkCmdDrawMeshTasksIndirectCountNV = nullptr;
static PFN_vkCmdSetExclusiveScissorEnableNV o_vkCmdSetExclusiveScissorEnableNV = nullptr;
static PFN_vkCmdSetExclusiveScissorNV o_vkCmdSetExclusiveScissorNV = nullptr;
static PFN_vkCmdSetCheckpointNV o_vkCmdSetCheckpointNV = nullptr;
static PFN_vkCmdSetPerformanceMarkerINTEL o_vkCmdSetPerformanceMarkerINTEL = nullptr;
static PFN_vkCmdSetPerformanceStreamMarkerINTEL o_vkCmdSetPerformanceStreamMarkerINTEL = nullptr;
static PFN_vkCmdSetPerformanceOverrideINTEL o_vkCmdSetPerformanceOverrideINTEL = nullptr;
static PFN_vkCmdSetLineStippleEXT o_vkCmdSetLineStippleEXT = nullptr;
static PFN_vkCmdSetCullModeEXT o_vkCmdSetCullModeEXT = nullptr;
static PFN_vkCmdSetFrontFaceEXT o_vkCmdSetFrontFaceEXT = nullptr;
static PFN_vkCmdSetPrimitiveTopologyEXT o_vkCmdSetPrimitiveTopologyEXT = nullptr;
static PFN_vkCmdSetViewportWithCountEXT o_vkCmdSetViewportWithCountEXT = nullptr;
static PFN_vkCmdSetScissorWithCountEXT o_vkCmdSetScissorWithCountEXT = nullptr;
static PFN_vkCmdBindVertexBuffers2EXT o_vkCmdBindVertexBuffers2EXT = nullptr;
static PFN_vkCmdSetDepthTestEnableEXT o_vkCmdSetDepthTestEnableEXT = nullptr;
static PFN_vkCmdSetDepthWriteEnableEXT o_vkCmdSetDepthWriteEnableEXT = nullptr;
static PFN_vkCmdSetDepthCompareOpEXT o_vkCmdSetDepthCompareOpEXT = nullptr;
static PFN_vkCmdSetDepthBoundsTestEnableEXT o_vkCmdSetDepthBoundsTestEnableEXT = nullptr;
static PFN_vkCmdSetStencilTestEnableEXT o_vkCmdSetStencilTestEnableEXT = nullptr;
static PFN_vkCmdSetStencilOpEXT o_vkCmdSetStencilOpEXT = nullptr;
static PFN_vkCmdPreprocessGeneratedCommandsNV o_vkCmdPreprocessGeneratedCommandsNV = nullptr;
static PFN_vkCmdExecuteGeneratedCommandsNV o_vkCmdExecuteGeneratedCommandsNV = nullptr;
static PFN_vkCmdBindPipelineShaderGroupNV o_vkCmdBindPipelineShaderGroupNV = nullptr;
static PFN_vkCmdSetDepthBias2EXT o_vkCmdSetDepthBias2EXT = nullptr;
static PFN_vkCmdCudaLaunchKernelNV o_vkCmdCudaLaunchKernelNV = nullptr;
static PFN_vkCmdBindDescriptorBuffersEXT o_vkCmdBindDescriptorBuffersEXT = nullptr;
static PFN_vkCmdSetDescriptorBufferOffsetsEXT o_vkCmdSetDescriptorBufferOffsetsEXT = nullptr;
static PFN_vkCmdBindDescriptorBufferEmbeddedSamplersEXT o_vkCmdBindDescriptorBufferEmbeddedSamplersEXT = nullptr;
static PFN_vkCmdSetFragmentShadingRateEnumNV o_vkCmdSetFragmentShadingRateEnumNV = nullptr;
static PFN_vkCmdSetVertexInputEXT o_vkCmdSetVertexInputEXT = nullptr;
static PFN_vkCmdSubpassShadingHUAWEI o_vkCmdSubpassShadingHUAWEI = nullptr;
static PFN_vkCmdBindInvocationMaskHUAWEI o_vkCmdBindInvocationMaskHUAWEI = nullptr;
static PFN_vkCmdSetPatchControlPointsEXT o_vkCmdSetPatchControlPointsEXT = nullptr;
static PFN_vkCmdSetRasterizerDiscardEnableEXT o_vkCmdSetRasterizerDiscardEnableEXT = nullptr;
static PFN_vkCmdSetDepthBiasEnableEXT o_vkCmdSetDepthBiasEnableEXT = nullptr;
static PFN_vkCmdSetLogicOpEXT o_vkCmdSetLogicOpEXT = nullptr;
static PFN_vkCmdSetPrimitiveRestartEnableEXT o_vkCmdSetPrimitiveRestartEnableEXT = nullptr;
static PFN_vkCmdSetColorWriteEnableEXT o_vkCmdSetColorWriteEnableEXT = nullptr;
static PFN_vkCmdDrawMultiEXT o_vkCmdDrawMultiEXT = nullptr;
static PFN_vkCmdDrawMultiIndexedEXT o_vkCmdDrawMultiIndexedEXT = nullptr;
static PFN_vkCmdBuildMicromapsEXT o_vkCmdBuildMicromapsEXT = nullptr;
static PFN_vkCmdCopyMicromapEXT o_vkCmdCopyMicromapEXT = nullptr;
static PFN_vkCmdCopyMicromapToMemoryEXT o_vkCmdCopyMicromapToMemoryEXT = nullptr;
static PFN_vkCmdCopyMemoryToMicromapEXT o_vkCmdCopyMemoryToMicromapEXT = nullptr;
static PFN_vkCmdWriteMicromapsPropertiesEXT o_vkCmdWriteMicromapsPropertiesEXT = nullptr;
static PFN_vkCmdDrawClusterHUAWEI o_vkCmdDrawClusterHUAWEI = nullptr;
static PFN_vkCmdDrawClusterIndirectHUAWEI o_vkCmdDrawClusterIndirectHUAWEI = nullptr;
static PFN_vkCmdCopyMemoryIndirectNV o_vkCmdCopyMemoryIndirectNV = nullptr;
static PFN_vkCmdCopyMemoryToImageIndirectNV o_vkCmdCopyMemoryToImageIndirectNV = nullptr;
static PFN_vkCmdDecompressMemoryNV o_vkCmdDecompressMemoryNV = nullptr;
static PFN_vkCmdDecompressMemoryIndirectCountNV o_vkCmdDecompressMemoryIndirectCountNV = nullptr;
static PFN_vkCmdUpdatePipelineIndirectBufferNV o_vkCmdUpdatePipelineIndirectBufferNV = nullptr;
static PFN_vkCmdSetDepthClampEnableEXT o_vkCmdSetDepthClampEnableEXT = nullptr;
static PFN_vkCmdSetPolygonModeEXT o_vkCmdSetPolygonModeEXT = nullptr;
static PFN_vkCmdSetRasterizationSamplesEXT o_vkCmdSetRasterizationSamplesEXT = nullptr;
static PFN_vkCmdSetSampleMaskEXT o_vkCmdSetSampleMaskEXT = nullptr;
static PFN_vkCmdSetAlphaToCoverageEnableEXT o_vkCmdSetAlphaToCoverageEnableEXT = nullptr;
static PFN_vkCmdSetAlphaToOneEnableEXT o_vkCmdSetAlphaToOneEnableEXT = nullptr;
static PFN_vkCmdSetLogicOpEnableEXT o_vkCmdSetLogicOpEnableEXT = nullptr;
static PFN_vkCmdSetColorBlendEnableEXT o_vkCmdSetColorBlendEnableEXT = nullptr;
static PFN_vkCmdSetColorBlendEquationEXT o_vkCmdSetColorBlendEquationEXT = nullptr;
static PFN_vkCmdSetColorWriteMaskEXT o_vkCmdSetColorWriteMaskEXT = nullptr;
static PFN_vkCmdSetTessellationDomainOriginEXT o_vkCmdSetTessellationDomainOriginEXT = nullptr;
static PFN_vkCmdSetRasterizationStreamEXT o_vkCmdSetRasterizationStreamEXT = nullptr;
static PFN_vkCmdSetConservativeRasterizationModeEXT o_vkCmdSetConservativeRasterizationModeEXT = nullptr;
static PFN_vkCmdSetExtraPrimitiveOverestimationSizeEXT o_vkCmdSetExtraPrimitiveOverestimationSizeEXT = nullptr;
static PFN_vkCmdSetDepthClipEnableEXT o_vkCmdSetDepthClipEnableEXT = nullptr;
static PFN_vkCmdSetSampleLocationsEnableEXT o_vkCmdSetSampleLocationsEnableEXT = nullptr;
static PFN_vkCmdSetColorBlendAdvancedEXT o_vkCmdSetColorBlendAdvancedEXT = nullptr;
static PFN_vkCmdSetProvokingVertexModeEXT o_vkCmdSetProvokingVertexModeEXT = nullptr;
static PFN_vkCmdSetLineRasterizationModeEXT o_vkCmdSetLineRasterizationModeEXT = nullptr;
static PFN_vkCmdSetLineStippleEnableEXT o_vkCmdSetLineStippleEnableEXT = nullptr;
static PFN_vkCmdSetDepthClipNegativeOneToOneEXT o_vkCmdSetDepthClipNegativeOneToOneEXT = nullptr;
static PFN_vkCmdSetViewportWScalingEnableNV o_vkCmdSetViewportWScalingEnableNV = nullptr;
static PFN_vkCmdSetViewportSwizzleNV o_vkCmdSetViewportSwizzleNV = nullptr;
static PFN_vkCmdSetCoverageToColorEnableNV o_vkCmdSetCoverageToColorEnableNV = nullptr;
static PFN_vkCmdSetCoverageToColorLocationNV o_vkCmdSetCoverageToColorLocationNV = nullptr;
static PFN_vkCmdSetCoverageModulationModeNV o_vkCmdSetCoverageModulationModeNV = nullptr;
static PFN_vkCmdSetCoverageModulationTableEnableNV o_vkCmdSetCoverageModulationTableEnableNV = nullptr;
static PFN_vkCmdSetCoverageModulationTableNV o_vkCmdSetCoverageModulationTableNV = nullptr;
static PFN_vkCmdSetShadingRateImageEnableNV o_vkCmdSetShadingRateImageEnableNV = nullptr;
static PFN_vkCmdSetRepresentativeFragmentTestEnableNV o_vkCmdSetRepresentativeFragmentTestEnableNV = nullptr;
static PFN_vkCmdSetCoverageReductionModeNV o_vkCmdSetCoverageReductionModeNV = nullptr;
static PFN_vkCmdOpticalFlowExecuteNV o_vkCmdOpticalFlowExecuteNV = nullptr;
static PFN_vkCmdBindShadersEXT o_vkCmdBindShadersEXT = nullptr;
static PFN_vkCmdSetDepthClampRangeEXT o_vkCmdSetDepthClampRangeEXT = nullptr;
static PFN_vkCmdConvertCooperativeVectorMatrixNV o_vkCmdConvertCooperativeVectorMatrixNV = nullptr;
static PFN_vkCmdSetAttachmentFeedbackLoopEnableEXT o_vkCmdSetAttachmentFeedbackLoopEnableEXT = nullptr;
static PFN_vkCmdBuildClusterAccelerationStructureIndirectNV o_vkCmdBuildClusterAccelerationStructureIndirectNV =
    nullptr;
static PFN_vkCmdBuildPartitionedAccelerationStructuresNV o_vkCmdBuildPartitionedAccelerationStructuresNV = nullptr;
static PFN_vkCmdPreprocessGeneratedCommandsEXT o_vkCmdPreprocessGeneratedCommandsEXT = nullptr;
static PFN_vkCmdExecuteGeneratedCommandsEXT o_vkCmdExecuteGeneratedCommandsEXT = nullptr;
static PFN_vkCmdBuildAccelerationStructuresKHR o_vkCmdBuildAccelerationStructuresKHR = nullptr;
static PFN_vkCmdBuildAccelerationStructuresIndirectKHR o_vkCmdBuildAccelerationStructuresIndirectKHR = nullptr;
static PFN_vkCmdCopyAccelerationStructureKHR o_vkCmdCopyAccelerationStructureKHR = nullptr;
static PFN_vkCmdCopyAccelerationStructureToMemoryKHR o_vkCmdCopyAccelerationStructureToMemoryKHR = nullptr;
static PFN_vkCmdCopyMemoryToAccelerationStructureKHR o_vkCmdCopyMemoryToAccelerationStructureKHR = nullptr;
static PFN_vkCmdWriteAccelerationStructuresPropertiesKHR o_vkCmdWriteAccelerationStructuresPropertiesKHR = nullptr;
static PFN_vkCmdTraceRaysKHR o_vkCmdTraceRaysKHR = nullptr;
static PFN_vkCmdTraceRaysIndirectKHR o_vkCmdTraceRaysIndirectKHR = nullptr;
static PFN_vkCmdSetRayTracingPipelineStackSizeKHR o_vkCmdSetRayTracingPipelineStackSizeKHR = nullptr;
static PFN_vkCmdDrawMeshTasksEXT o_vkCmdDrawMeshTasksEXT = nullptr;
static PFN_vkCmdDrawMeshTasksIndirectEXT o_vkCmdDrawMeshTasksIndirectEXT = nullptr;
static PFN_vkCmdDrawMeshTasksIndirectCountEXT o_vkCmdDrawMeshTasksIndirectCountEXT = nullptr;

#pragma endregion

#pragma region vkCmd hook implementations

// Add after hk_vkEndCommandBuffer implementation

void Vulkan_wDx12::hk_vkCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                                        VkPipeline pipeline)
{
    VkCommandBuffer cmdBuffer = commandBuffer;
    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(commandBuffer);

    if (mappedVirtualCmdBuffer == VK_NULL_HANDLE)
    {
        cmdBufferStateTracker.OnBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
    }
    else
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBindPipeline(cmdBuffer, pipelineBindPoint, pipeline);
}

void Vulkan_wDx12::hk_vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount,
                                       const VkViewport* pViewports)
{
    VkCommandBuffer cmdBuffer = commandBuffer;
    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(commandBuffer);

    if (mappedVirtualCmdBuffer == VK_NULL_HANDLE)
    {
        cmdBufferStateTracker.OnSetViewport(commandBuffer, firstViewport, viewportCount, pViewports);
    }
    else
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetViewport(cmdBuffer, firstViewport, viewportCount, pViewports);
}

void Vulkan_wDx12::hk_vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount,
                                      const VkRect2D* pScissors)
{
    VkCommandBuffer cmdBuffer = commandBuffer;
    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(commandBuffer);

    if (mappedVirtualCmdBuffer == VK_NULL_HANDLE)
    {
        cmdBufferStateTracker.OnSetScissor(commandBuffer, firstScissor, scissorCount, pScissors);
    }
    else
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetScissor(cmdBuffer, firstScissor, scissorCount, pScissors);
}

void Vulkan_wDx12::hk_vkCmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetLineWidth(cmdBuffer, lineWidth);
}

void Vulkan_wDx12::hk_vkCmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor,
                                        float depthBiasClamp, float depthBiasSlopeFactor)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDepthBias(cmdBuffer, depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor);
}

void Vulkan_wDx12::hk_vkCmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4])
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetBlendConstants(cmdBuffer, blendConstants);
}

void Vulkan_wDx12::hk_vkCmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDepthBounds(cmdBuffer, minDepthBounds, maxDepthBounds);
}

void Vulkan_wDx12::hk_vkCmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
                                                 uint32_t compareMask)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetStencilCompareMask(cmdBuffer, faceMask, compareMask);
}

void Vulkan_wDx12::hk_vkCmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
                                               uint32_t writeMask)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetStencilWriteMask(cmdBuffer, faceMask, writeMask);
}

void Vulkan_wDx12::hk_vkCmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
                                               uint32_t reference)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetStencilReference(cmdBuffer, faceMask, reference);
}

void Vulkan_wDx12::hk_vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                                              VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount,
                                              const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount,
                                              const uint32_t* pDynamicOffsets)
{
    VkCommandBuffer cmdBuffer = commandBuffer;
    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(commandBuffer);

    if (mappedVirtualCmdBuffer == VK_NULL_HANDLE)
    {
        cmdBufferStateTracker.OnBindDescriptorSets(commandBuffer, pipelineBindPoint, layout, firstSet,
                                                   descriptorSetCount, pDescriptorSets, dynamicOffsetCount,
                                                   pDynamicOffsets);
    }
    else
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}, pipelineBindPoint: {}, layout: {:X}, firstSet: {}, descriptorSetCount: {}, "
              "pDescriptorSets: {:X}, dynamicOffsetCount: {}, pDynamicOffsets: {:X}",
              (size_t) cmdBuffer, magic_enum::enum_name(pipelineBindPoint), (size_t) layout, firstSet,
              descriptorSetCount, (size_t) pDescriptorSets, dynamicOffsetCount, (size_t) pDynamicOffsets);
#endif

    o_vkCmdBindDescriptorSets(cmdBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount, pDescriptorSets,
                              dynamicOffsetCount, pDynamicOffsets);
}

void Vulkan_wDx12::hk_vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                           VkIndexType indexType)
{
    VkCommandBuffer cmdBuffer = commandBuffer;
    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(commandBuffer);

    if (mappedVirtualCmdBuffer == VK_NULL_HANDLE)
    {
        cmdBufferStateTracker.OnBindIndexBuffer(commandBuffer, buffer, offset, indexType);
    }
    else
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBindIndexBuffer(cmdBuffer, buffer, offset, indexType);
}

void Vulkan_wDx12::hk_vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding,
                                             uint32_t bindingCount, const VkBuffer* pBuffers,
                                             const VkDeviceSize* pOffsets)
{
    VkCommandBuffer cmdBuffer = commandBuffer;
    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(commandBuffer);

    if (mappedVirtualCmdBuffer == VK_NULL_HANDLE)
    {
        cmdBufferStateTracker.OnBindVertexBuffers(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets);
    }
    else
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBindVertexBuffers(cmdBuffer, firstBinding, bindingCount, pBuffers, pOffsets);
}

void Vulkan_wDx12::hk_vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount,
                                uint32_t firstVertex, uint32_t firstInstance)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDraw(cmdBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void Vulkan_wDx12::hk_vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount,
                                       uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDrawIndexed(cmdBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void Vulkan_wDx12::hk_vkCmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                        uint32_t drawCount, uint32_t stride)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDrawIndirect(cmdBuffer, buffer, offset, drawCount, stride);
}

void Vulkan_wDx12::hk_vkCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                               uint32_t drawCount, uint32_t stride)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDrawIndexedIndirect(cmdBuffer, buffer, offset, drawCount, stride);
}

void Vulkan_wDx12::hk_vkCmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY,
                                    uint32_t groupCountZ)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDispatch(cmdBuffer, groupCountX, groupCountY, groupCountZ);
}

void Vulkan_wDx12::hk_vkCmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDispatchIndirect(cmdBuffer, buffer, offset);
}

void Vulkan_wDx12::hk_vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer,
                                      uint32_t regionCount, const VkBufferCopy* pRegions)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyBuffer(cmdBuffer, srcBuffer, dstBuffer, regionCount, pRegions);
}

void Vulkan_wDx12::hk_vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                                     VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
                                     const VkImageCopy* pRegions)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyImage(cmdBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
}

void Vulkan_wDx12::hk_vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                                     VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
                                     const VkImageBlit* pRegions, VkFilter filter)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBlitImage(cmdBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions, filter);
}

void Vulkan_wDx12::hk_vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage,
                                             VkImageLayout dstImageLayout, uint32_t regionCount,
                                             const VkBufferImageCopy* pRegions)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyBufferToImage(cmdBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
}

void Vulkan_wDx12::hk_vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage,
                                             VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount,
                                             const VkBufferImageCopy* pRegions)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyImageToBuffer(cmdBuffer, srcImage, srcImageLayout, dstBuffer, regionCount, pRegions);
}

void Vulkan_wDx12::hk_vkCmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset,
                                        VkDeviceSize dataSize, const void* pData)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdUpdateBuffer(cmdBuffer, dstBuffer, dstOffset, dataSize, pData);
}

void Vulkan_wDx12::hk_vkCmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset,
                                      VkDeviceSize size, uint32_t data)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdFillBuffer(cmdBuffer, dstBuffer, dstOffset, size, data);
}

void Vulkan_wDx12::hk_vkCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
                                           const VkClearColorValue* pColor, uint32_t rangeCount,
                                           const VkImageSubresourceRange* pRanges)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdClearColorImage(cmdBuffer, image, imageLayout, pColor, rangeCount, pRanges);
}

void Vulkan_wDx12::hk_vkCmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image,
                                                  VkImageLayout imageLayout,
                                                  const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount,
                                                  const VkImageSubresourceRange* pRanges)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdClearDepthStencilImage(cmdBuffer, image, imageLayout, pDepthStencil, rangeCount, pRanges);
}

void Vulkan_wDx12::hk_vkCmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                                            const VkClearAttachment* pAttachments, uint32_t rectCount,
                                            const VkClearRect* pRects)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdClearAttachments(cmdBuffer, attachmentCount, pAttachments, rectCount, pRects);
}

void Vulkan_wDx12::hk_vkCmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                                        VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
                                        const VkImageResolve* pRegions)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdResolveImage(cmdBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
}

void Vulkan_wDx12::hk_vkCmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetEvent(cmdBuffer, event, stageMask);
}

void Vulkan_wDx12::hk_vkCmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdResetEvent(cmdBuffer, event, stageMask);
}

void Vulkan_wDx12::hk_vkCmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents,
                                      VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
                                      uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers,
                                      uint32_t bufferMemoryBarrierCount,
                                      const VkBufferMemoryBarrier* pBufferMemoryBarriers,
                                      uint32_t imageMemoryBarrierCount,
                                      const VkImageMemoryBarrier* pImageMemoryBarriers)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdWaitEvents(cmdBuffer, eventCount, pEvents, srcStageMask, dstStageMask, memoryBarrierCount, pMemoryBarriers,
                      bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
}

void Vulkan_wDx12::hk_vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
                                           VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
                                           uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers,
                                           uint32_t bufferMemoryBarrierCount,
                                           const VkBufferMemoryBarrier* pBufferMemoryBarriers,
                                           uint32_t imageMemoryBarrierCount,
                                           const VkImageMemoryBarrier* pImageMemoryBarriers)
{
    auto primaryGpu = IdentifyGpu::getPrimaryGpu();
    if (State::Instance().gameQuirks & GameQuirk::VulkanDLSSBarrierFixup &&
        (primaryGpu.vendorId != VendorId::Nvidia || !primaryGpu.dlssCapable))
    {
        // AMD drivers on the cards around RDNA2 didn't treat VK_IMAGE_LAYOUT_UNDEFINED in the same way Nvidia does.
        // Doesn't seem like a bug, just a different way of handling an UB but we need to adjust.

        for (size_t i = 0; i < imageMemoryBarrierCount; i++)
        {
            if (pImageMemoryBarriers[i].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
                pImageMemoryBarriers[i].newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
            {
                LOG_TRACE("Changing an UNDEFINED barrier in DLSSG Present, imageMemoryBarrierCount: {}",
                          imageMemoryBarrierCount);

                VkImageMemoryBarrier* newImageBarriers = new VkImageMemoryBarrier[imageMemoryBarrierCount];

                std::memcpy(newImageBarriers, pImageMemoryBarriers,
                            sizeof(*newImageBarriers) * imageMemoryBarrierCount);

                newImageBarriers[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

                o_vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount,
                                       pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers,
                                       imageMemoryBarrierCount, newImageBarriers);

                delete[] newImageBarriers;

                return;
            }
        }

        // DLSS
        // Those are already in the correct layouts
        if (imageMemoryBarrierCount == 4)
        {
            // In the Voyagers update, the 2nd oldLayout has changed
            if (pImageMemoryBarriers[0].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
                // pImageMemoryBarriers[1].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
                pImageMemoryBarriers[2].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
                pImageMemoryBarriers[3].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
            {
                LOG_TRACE("Removing an UNDEFINED barrier in DLSS");
                return;
            }
        }
    }

    VkCommandBuffer cmdBuffer = commandBuffer;
    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(commandBuffer);

    if (mappedVirtualCmdBuffer == VK_NULL_HANDLE)
    {
        cmdBufferStateTracker.OnPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags,
                                                memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount,
                                                pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
    }
    else
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdPipelineBarrier(cmdBuffer, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount, pMemoryBarriers,
                           bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount,
                           pImageMemoryBarriers);
}

void Vulkan_wDx12::hk_vkCmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query,
                                      VkQueryControlFlags flags)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBeginQuery(cmdBuffer, queryPool, query, flags);
}

void Vulkan_wDx12::hk_vkCmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdEndQuery(cmdBuffer, queryPool, query);
}

void Vulkan_wDx12::hk_vkCmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery,
                                          uint32_t queryCount)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdResetQueryPool(cmdBuffer, queryPool, firstQuery, queryCount);
}

void Vulkan_wDx12::hk_vkCmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage,
                                          VkQueryPool queryPool, uint32_t query)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdWriteTimestamp(cmdBuffer, pipelineStage, queryPool, query);
}

void Vulkan_wDx12::hk_vkCmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                                                uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer,
                                                VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyQueryPoolResults(cmdBuffer, queryPool, firstQuery, queryCount, dstBuffer, dstOffset, stride, flags);
}

void Vulkan_wDx12::hk_vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout,
                                         VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size,
                                         const void* pValues)
{
    VkCommandBuffer cmdBuffer = commandBuffer;
    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(commandBuffer);

    if (mappedVirtualCmdBuffer == VK_NULL_HANDLE)
    {
        // Infer bind point from stage flags
        VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

        // If only compute stage is set, it's compute
        if (stageFlags == VK_SHADER_STAGE_COMPUTE_BIT)
        {
            bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
        }
        // If any graphics stages are set, it's graphics
        else if (stageFlags &
                 (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                  VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_GEOMETRY_BIT |
                  VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT))
        {
            bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        }

        cmdBufferStateTracker.OnPushConstants(commandBuffer, bindPoint, layout, stageFlags, offset, size, pValues);
    }
    else
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdPushConstants(cmdBuffer, layout, stageFlags, offset, size, pValues);
}

void Vulkan_wDx12::hk_vkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin,
                                           VkSubpassContents contents)
{
    VkCommandBuffer cmdBuffer = commandBuffer;
    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(commandBuffer);

    if (mappedVirtualCmdBuffer == VK_NULL_HANDLE)
    {
        cmdBufferStateTracker.OnBeginRenderPass(commandBuffer, pRenderPassBegin);
    }
    else
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBeginRenderPass(cmdBuffer, pRenderPassBegin, contents);
}

void Vulkan_wDx12::hk_vkCmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdNextSubpass(cmdBuffer, contents);
}

void Vulkan_wDx12::hk_vkCmdEndRenderPass(VkCommandBuffer commandBuffer)
{
    VkCommandBuffer cmdBuffer = commandBuffer;
    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(commandBuffer);

    if (mappedVirtualCmdBuffer == VK_NULL_HANDLE)
    {
        cmdBufferStateTracker.OnEndRenderPass(commandBuffer);
    }
    else
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdEndRenderPass(cmdBuffer);
}

void Vulkan_wDx12::hk_vkCmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDeviceMask(cmdBuffer, deviceMask);
}

void Vulkan_wDx12::hk_vkCmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY,
                                        uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY,
                                        uint32_t groupCountZ)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDispatchBase(cmdBuffer, baseGroupX, baseGroupY, baseGroupZ, groupCountX, groupCountY, groupCountZ);
}

void Vulkan_wDx12::hk_vkCmdDrawIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                             VkBuffer countBuffer, VkDeviceSize countBufferOffset,
                                             uint32_t maxDrawCount, uint32_t stride)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDrawIndirectCount(cmdBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}

void Vulkan_wDx12::hk_vkCmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                    VkBuffer countBuffer, VkDeviceSize countBufferOffset,
                                                    uint32_t maxDrawCount, uint32_t stride)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDrawIndexedIndirectCount(cmdBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}

void Vulkan_wDx12::hk_vkCmdBeginRenderPass2(VkCommandBuffer commandBuffer,
                                            const VkRenderPassBeginInfo* pRenderPassBegin,
                                            const VkSubpassBeginInfo* pSubpassBeginInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBeginRenderPass2(cmdBuffer, pRenderPassBegin, pSubpassBeginInfo);
}

void Vulkan_wDx12::hk_vkCmdNextSubpass2(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo* pSubpassBeginInfo,
                                        const VkSubpassEndInfo* pSubpassEndInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdNextSubpass2(cmdBuffer, pSubpassBeginInfo, pSubpassEndInfo);
}

void Vulkan_wDx12::hk_vkCmdEndRenderPass2(VkCommandBuffer commandBuffer, const VkSubpassEndInfo* pSubpassEndInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdEndRenderPass2(cmdBuffer, pSubpassEndInfo);
}

void Vulkan_wDx12::hk_vkCmdSetEvent2(VkCommandBuffer commandBuffer, VkEvent event,
                                     const VkDependencyInfo* pDependencyInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetEvent2(cmdBuffer, event, pDependencyInfo);
}

void Vulkan_wDx12::hk_vkCmdResetEvent2(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags2 stageMask)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdResetEvent2(cmdBuffer, event, stageMask);
}

void Vulkan_wDx12::hk_vkCmdWaitEvents2(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents,
                                       const VkDependencyInfo* pDependencyInfos)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdWaitEvents2(cmdBuffer, eventCount, pEvents, pDependencyInfos);
}

void Vulkan_wDx12::hk_vkCmdPipelineBarrier2(VkCommandBuffer commandBuffer, const VkDependencyInfo* pDependencyInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdPipelineBarrier2(cmdBuffer, pDependencyInfo);
}

void Vulkan_wDx12::hk_vkCmdWriteTimestamp2(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage,
                                           VkQueryPool queryPool, uint32_t query)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdWriteTimestamp2(cmdBuffer, stage, queryPool, query);
}

void Vulkan_wDx12::hk_vkCmdCopyBuffer2(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2* pCopyBufferInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyBuffer2(cmdBuffer, pCopyBufferInfo);
}

void Vulkan_wDx12::hk_vkCmdCopyImage2(VkCommandBuffer commandBuffer, const VkCopyImageInfo2* pCopyImageInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyImage2(cmdBuffer, pCopyImageInfo);
}

void Vulkan_wDx12::hk_vkCmdCopyBufferToImage2(VkCommandBuffer commandBuffer,
                                              const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyBufferToImage2(cmdBuffer, pCopyBufferToImageInfo);
}

void Vulkan_wDx12::hk_vkCmdCopyImageToBuffer2(VkCommandBuffer commandBuffer,
                                              const VkCopyImageToBufferInfo2* pCopyImageToBufferInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyImageToBuffer2(cmdBuffer, pCopyImageToBufferInfo);
}

void Vulkan_wDx12::hk_vkCmdBlitImage2(VkCommandBuffer commandBuffer, const VkBlitImageInfo2* pBlitImageInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBlitImage2(cmdBuffer, pBlitImageInfo);
}

void Vulkan_wDx12::hk_vkCmdResolveImage2(VkCommandBuffer commandBuffer, const VkResolveImageInfo2* pResolveImageInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdResolveImage2(cmdBuffer, pResolveImageInfo);
}

void Vulkan_wDx12::hk_vkCmdBeginRendering(VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBeginRendering(cmdBuffer, pRenderingInfo);
}

void Vulkan_wDx12::hk_vkCmdEndRendering(VkCommandBuffer commandBuffer)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdEndRendering(cmdBuffer);
}

void Vulkan_wDx12::hk_vkCmdSetCullMode(VkCommandBuffer commandBuffer, VkCullModeFlags cullMode)
{
    VkCommandBuffer cmdBuffer = commandBuffer;
    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(commandBuffer);

    if (mappedVirtualCmdBuffer == VK_NULL_HANDLE)
    {
        cmdBufferStateTracker.OnSetCullMode(commandBuffer, cullMode);
    }
    else
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetCullMode(cmdBuffer, cullMode);
}

void Vulkan_wDx12::hk_vkCmdSetFrontFace(VkCommandBuffer commandBuffer, VkFrontFace frontFace)
{
    VkCommandBuffer cmdBuffer = commandBuffer;
    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(commandBuffer);

    if (mappedVirtualCmdBuffer == VK_NULL_HANDLE)
    {
        cmdBufferStateTracker.OnSetFrontFace(commandBuffer, frontFace);
    }
    else
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetFrontFace(cmdBuffer, frontFace);
}

void Vulkan_wDx12::hk_vkCmdSetPrimitiveTopology(VkCommandBuffer commandBuffer, VkPrimitiveTopology primitiveTopology)
{
    VkCommandBuffer cmdBuffer = commandBuffer;
    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(commandBuffer);

    if (mappedVirtualCmdBuffer == VK_NULL_HANDLE)
    {
        cmdBufferStateTracker.OnSetPrimitiveTopology(commandBuffer, primitiveTopology);
    }
    else
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetPrimitiveTopology(cmdBuffer, primitiveTopology);
}

void Vulkan_wDx12::hk_vkCmdSetViewportWithCount(VkCommandBuffer commandBuffer, uint32_t viewportCount,
                                                const VkViewport* pViewports)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetViewportWithCount(cmdBuffer, viewportCount, pViewports);
}

void Vulkan_wDx12::hk_vkCmdSetScissorWithCount(VkCommandBuffer commandBuffer, uint32_t scissorCount,
                                               const VkRect2D* pScissors)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetScissorWithCount(cmdBuffer, scissorCount, pScissors);
}

void Vulkan_wDx12::hk_vkCmdBindVertexBuffers2(VkCommandBuffer commandBuffer, uint32_t firstBinding,
                                              uint32_t bindingCount, const VkBuffer* pBuffers,
                                              const VkDeviceSize* pOffsets, const VkDeviceSize* pSizes,
                                              const VkDeviceSize* pStrides)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBindVertexBuffers2(cmdBuffer, firstBinding, bindingCount, pBuffers, pOffsets, pSizes, pStrides);
}

void Vulkan_wDx12::hk_vkCmdSetDepthTestEnable(VkCommandBuffer commandBuffer, VkBool32 depthTestEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;
    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(commandBuffer);

    if (mappedVirtualCmdBuffer == VK_NULL_HANDLE)
    {
        cmdBufferStateTracker.OnSetDepthTestEnable(commandBuffer, depthTestEnable);
    }
    else
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDepthTestEnable(cmdBuffer, depthTestEnable);
}

void Vulkan_wDx12::hk_vkCmdSetDepthWriteEnable(VkCommandBuffer commandBuffer, VkBool32 depthWriteEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;
    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(commandBuffer);

    if (mappedVirtualCmdBuffer == VK_NULL_HANDLE)
    {
        cmdBufferStateTracker.OnSetDepthWriteEnable(commandBuffer, depthWriteEnable);
    }
    else
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDepthWriteEnable(cmdBuffer, depthWriteEnable);
}

void Vulkan_wDx12::hk_vkCmdSetDepthCompareOp(VkCommandBuffer commandBuffer, VkCompareOp depthCompareOp)
{
    VkCommandBuffer cmdBuffer = commandBuffer;
    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(commandBuffer);

    if (mappedVirtualCmdBuffer == VK_NULL_HANDLE)
    {
        cmdBufferStateTracker.OnSetDepthCompareOp(commandBuffer, depthCompareOp);
    }
    else
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDepthCompareOp(cmdBuffer, depthCompareOp);
}

void Vulkan_wDx12::hk_vkCmdSetDepthBoundsTestEnable(VkCommandBuffer commandBuffer, VkBool32 depthBoundsTestEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;
    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(commandBuffer);

    if (mappedVirtualCmdBuffer == VK_NULL_HANDLE)
    {
        cmdBufferStateTracker.OnSetDepthBoundsTestEnable(commandBuffer, depthBoundsTestEnable);
    }
    else
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDepthBoundsTestEnable(cmdBuffer, depthBoundsTestEnable);
}

void Vulkan_wDx12::hk_vkCmdSetStencilTestEnable(VkCommandBuffer commandBuffer, VkBool32 stencilTestEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;
    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(commandBuffer);

    if (mappedVirtualCmdBuffer == VK_NULL_HANDLE)
    {
        cmdBufferStateTracker.OnSetStencilTestEnable(commandBuffer, stencilTestEnable);
    }
    else
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetStencilTestEnable(cmdBuffer, stencilTestEnable);
}

void Vulkan_wDx12::hk_vkCmdSetStencilOp(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, VkStencilOp failOp,
                                        VkStencilOp passOp, VkStencilOp depthFailOp, VkCompareOp compareOp)
{
    VkCommandBuffer cmdBuffer = commandBuffer;
    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(commandBuffer);

    if (mappedVirtualCmdBuffer == VK_NULL_HANDLE)
    {
        cmdBufferStateTracker.OnSetStencilOp(commandBuffer, faceMask, failOp, passOp, depthFailOp, compareOp);
    }
    else
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetStencilOp(cmdBuffer, faceMask, failOp, passOp, depthFailOp, compareOp);
}

void Vulkan_wDx12::hk_vkCmdSetRasterizerDiscardEnable(VkCommandBuffer commandBuffer, VkBool32 rasterizerDiscardEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetRasterizerDiscardEnable(cmdBuffer, rasterizerDiscardEnable);
}

void Vulkan_wDx12::hk_vkCmdSetDepthBiasEnable(VkCommandBuffer commandBuffer, VkBool32 depthBiasEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDepthBiasEnable(cmdBuffer, depthBiasEnable);
}

void Vulkan_wDx12::hk_vkCmdSetPrimitiveRestartEnable(VkCommandBuffer commandBuffer, VkBool32 primitiveRestartEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetPrimitiveRestartEnable(cmdBuffer, primitiveRestartEnable);
}

void Vulkan_wDx12::hk_vkCmdSetLineStipple(VkCommandBuffer commandBuffer, uint32_t lineStippleFactor,
                                          uint16_t lineStipplePattern)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetLineStipple(cmdBuffer, lineStippleFactor, lineStipplePattern);
}

void Vulkan_wDx12::hk_vkCmdBindIndexBuffer2(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                            VkDeviceSize size, VkIndexType indexType)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBindIndexBuffer2(cmdBuffer, buffer, offset, size, indexType);
}

void Vulkan_wDx12::hk_vkCmdPushDescriptorSet(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                                             VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount,
                                             const VkWriteDescriptorSet* pDescriptorWrites)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdPushDescriptorSet(cmdBuffer, pipelineBindPoint, layout, set, descriptorWriteCount, pDescriptorWrites);
}

void Vulkan_wDx12::hk_vkCmdPushDescriptorSetWithTemplate(VkCommandBuffer commandBuffer,
                                                         VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                                         VkPipelineLayout layout, uint32_t set, const void* pData)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdPushDescriptorSetWithTemplate(cmdBuffer, descriptorUpdateTemplate, layout, set, pData);
}

void Vulkan_wDx12::hk_vkCmdSetRenderingAttachmentLocations(VkCommandBuffer commandBuffer,
                                                           const VkRenderingAttachmentLocationInfo* pLocationInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetRenderingAttachmentLocations(cmdBuffer, pLocationInfo);
}

void Vulkan_wDx12::hk_vkCmdSetRenderingInputAttachmentIndices(
    VkCommandBuffer commandBuffer, const VkRenderingInputAttachmentIndexInfo* pInputAttachmentIndexInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetRenderingInputAttachmentIndices(cmdBuffer, pInputAttachmentIndexInfo);
}

void Vulkan_wDx12::hk_vkCmdBindDescriptorSets2(VkCommandBuffer commandBuffer,
                                               const VkBindDescriptorSetsInfo* pBindDescriptorSetsInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBindDescriptorSets2(cmdBuffer, pBindDescriptorSetsInfo);
}

void Vulkan_wDx12::hk_vkCmdPushConstants2(VkCommandBuffer commandBuffer, const VkPushConstantsInfo* pPushConstantsInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdPushConstants2(cmdBuffer, pPushConstantsInfo);
}

void Vulkan_wDx12::hk_vkCmdPushDescriptorSet2(VkCommandBuffer commandBuffer,
                                              const VkPushDescriptorSetInfo* pPushDescriptorSetInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdPushDescriptorSet2(cmdBuffer, pPushDescriptorSetInfo);
}

void Vulkan_wDx12::hk_vkCmdPushDescriptorSetWithTemplate2(
    VkCommandBuffer commandBuffer, const VkPushDescriptorSetWithTemplateInfo* pPushDescriptorSetWithTemplateInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdPushDescriptorSetWithTemplate2(cmdBuffer, pPushDescriptorSetWithTemplateInfo);
}

void Vulkan_wDx12::hk_vkCmdBeginVideoCodingKHR(VkCommandBuffer commandBuffer,
                                               const VkVideoBeginCodingInfoKHR* pBeginInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBeginVideoCodingKHR(cmdBuffer, pBeginInfo);
}

void Vulkan_wDx12::hk_vkCmdEndVideoCodingKHR(VkCommandBuffer commandBuffer,
                                             const VkVideoEndCodingInfoKHR* pEndCodingInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdEndVideoCodingKHR(cmdBuffer, pEndCodingInfo);
}

void Vulkan_wDx12::hk_vkCmdControlVideoCodingKHR(VkCommandBuffer commandBuffer,
                                                 const VkVideoCodingControlInfoKHR* pCodingControlInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdControlVideoCodingKHR(cmdBuffer, pCodingControlInfo);
}

void Vulkan_wDx12::hk_vkCmdDecodeVideoKHR(VkCommandBuffer commandBuffer, const VkVideoDecodeInfoKHR* pDecodeInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDecodeVideoKHR(cmdBuffer, pDecodeInfo);
}

void Vulkan_wDx12::hk_vkCmdBeginRenderingKHR(VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBeginRenderingKHR(cmdBuffer, pRenderingInfo);
}

void Vulkan_wDx12::hk_vkCmdEndRenderingKHR(VkCommandBuffer commandBuffer)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdEndRenderingKHR(cmdBuffer);
}

void Vulkan_wDx12::hk_vkCmdSetDeviceMaskKHR(VkCommandBuffer commandBuffer, uint32_t deviceMask)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDeviceMaskKHR(cmdBuffer, deviceMask);
}

void Vulkan_wDx12::hk_vkCmdDispatchBaseKHR(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY,
                                           uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY,
                                           uint32_t groupCountZ)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDispatchBaseKHR(cmdBuffer, baseGroupX, baseGroupY, baseGroupZ, groupCountX, groupCountY, groupCountZ);
}

void Vulkan_wDx12::hk_vkCmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                                                VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount,
                                                const VkWriteDescriptorSet* pDescriptorWrites)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdPushDescriptorSetKHR(cmdBuffer, pipelineBindPoint, layout, set, descriptorWriteCount, pDescriptorWrites);
}

void Vulkan_wDx12::hk_vkCmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer,
                                                            VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                                            VkPipelineLayout layout, uint32_t set, const void* pData)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdPushDescriptorSetWithTemplateKHR(cmdBuffer, descriptorUpdateTemplate, layout, set, pData);
}

void Vulkan_wDx12::hk_vkCmdBeginRenderPass2KHR(VkCommandBuffer commandBuffer,
                                               const VkRenderPassBeginInfo* pRenderPassBegin,
                                               const VkSubpassBeginInfo* pSubpassBeginInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBeginRenderPass2KHR(cmdBuffer, pRenderPassBegin, pSubpassBeginInfo);
}

void Vulkan_wDx12::hk_vkCmdNextSubpass2KHR(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo* pSubpassBeginInfo,
                                           const VkSubpassEndInfo* pSubpassEndInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdNextSubpass2KHR(cmdBuffer, pSubpassBeginInfo, pSubpassEndInfo);
}

void Vulkan_wDx12::hk_vkCmdEndRenderPass2KHR(VkCommandBuffer commandBuffer, const VkSubpassEndInfo* pSubpassEndInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdEndRenderPass2KHR(cmdBuffer, pSubpassEndInfo);
}

void Vulkan_wDx12::hk_vkCmdDrawIndirectCountKHR(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                VkBuffer countBuffer, VkDeviceSize countBufferOffset,
                                                uint32_t maxDrawCount, uint32_t stride)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDrawIndirectCountKHR(cmdBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}

void Vulkan_wDx12::hk_vkCmdDrawIndexedIndirectCountKHR(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                                       VkDeviceSize offset, VkBuffer countBuffer,
                                                       VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                                       uint32_t stride)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDrawIndexedIndirectCountKHR(cmdBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}

void Vulkan_wDx12::hk_vkCmdSetFragmentShadingRateKHR(VkCommandBuffer commandBuffer, const VkExtent2D* pFragmentSize,
                                                     const VkFragmentShadingRateCombinerOpKHR combinerOps[2])
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetFragmentShadingRateKHR(cmdBuffer, pFragmentSize, combinerOps);
}

void Vulkan_wDx12::hk_vkCmdSetRenderingAttachmentLocationsKHR(VkCommandBuffer commandBuffer,
                                                              const VkRenderingAttachmentLocationInfo* pLocationInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetRenderingAttachmentLocationsKHR(cmdBuffer, pLocationInfo);
}

void Vulkan_wDx12::hk_vkCmdSetRenderingInputAttachmentIndicesKHR(
    VkCommandBuffer commandBuffer, const VkRenderingInputAttachmentIndexInfo* pInputAttachmentIndexInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetRenderingInputAttachmentIndicesKHR(cmdBuffer, pInputAttachmentIndexInfo);
}

void Vulkan_wDx12::hk_vkCmdEncodeVideoKHR(VkCommandBuffer commandBuffer, const VkVideoEncodeInfoKHR* pEncodeInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdEncodeVideoKHR(cmdBuffer, pEncodeInfo);
}

void Vulkan_wDx12::hk_vkCmdSetEvent2KHR(VkCommandBuffer commandBuffer, VkEvent event,
                                        const VkDependencyInfo* pDependencyInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetEvent2KHR(cmdBuffer, event, pDependencyInfo);
}

void Vulkan_wDx12::hk_vkCmdResetEvent2KHR(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags2 stageMask)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdResetEvent2KHR(cmdBuffer, event, stageMask);
}

void Vulkan_wDx12::hk_vkCmdWaitEvents2KHR(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents,
                                          const VkDependencyInfo* pDependencyInfos)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdWaitEvents2KHR(cmdBuffer, eventCount, pEvents, pDependencyInfos);
}

void Vulkan_wDx12::hk_vkCmdPipelineBarrier2KHR(VkCommandBuffer commandBuffer, const VkDependencyInfo* pDependencyInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdPipelineBarrier2KHR(cmdBuffer, pDependencyInfo);
}

void Vulkan_wDx12::hk_vkCmdWriteTimestamp2KHR(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage,
                                              VkQueryPool queryPool, uint32_t query)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdWriteTimestamp2KHR(cmdBuffer, stage, queryPool, query);
}

void Vulkan_wDx12::hk_vkCmdCopyBuffer2KHR(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2* pCopyBufferInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyBuffer2KHR(cmdBuffer, pCopyBufferInfo);
}

void Vulkan_wDx12::hk_vkCmdCopyImage2KHR(VkCommandBuffer commandBuffer, const VkCopyImageInfo2* pCopyImageInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyImage2KHR(cmdBuffer, pCopyImageInfo);
}

void Vulkan_wDx12::hk_vkCmdCopyBufferToImage2KHR(VkCommandBuffer commandBuffer,
                                                 const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyBufferToImage2KHR(cmdBuffer, pCopyBufferToImageInfo);
}

void Vulkan_wDx12::hk_vkCmdCopyImageToBuffer2KHR(VkCommandBuffer commandBuffer,
                                                 const VkCopyImageToBufferInfo2* pCopyImageToBufferInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyImageToBuffer2KHR(cmdBuffer, pCopyImageToBufferInfo);
}

void Vulkan_wDx12::hk_vkCmdBlitImage2KHR(VkCommandBuffer commandBuffer, const VkBlitImageInfo2* pBlitImageInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBlitImage2KHR(cmdBuffer, pBlitImageInfo);
}

void Vulkan_wDx12::hk_vkCmdResolveImage2KHR(VkCommandBuffer commandBuffer, const VkResolveImageInfo2* pResolveImageInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdResolveImage2KHR(cmdBuffer, pResolveImageInfo);
}

void Vulkan_wDx12::hk_vkCmdTraceRaysIndirect2KHR(VkCommandBuffer commandBuffer, VkDeviceAddress indirectDeviceAddress)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdTraceRaysIndirect2KHR(cmdBuffer, indirectDeviceAddress);
}

void Vulkan_wDx12::hk_vkCmdBindIndexBuffer2KHR(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                               VkDeviceSize size, VkIndexType indexType)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBindIndexBuffer2KHR(cmdBuffer, buffer, offset, size, indexType);
}

void Vulkan_wDx12::hk_vkCmdSetLineStippleKHR(VkCommandBuffer commandBuffer, uint32_t lineStippleFactor,
                                             uint16_t lineStipplePattern)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetLineStippleKHR(cmdBuffer, lineStippleFactor, lineStipplePattern);
}

void Vulkan_wDx12::hk_vkCmdBindDescriptorSets2KHR(VkCommandBuffer commandBuffer,
                                                  const VkBindDescriptorSetsInfo* pBindDescriptorSetsInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBindDescriptorSets2KHR(cmdBuffer, pBindDescriptorSetsInfo);
}

void Vulkan_wDx12::hk_vkCmdPushConstants2KHR(VkCommandBuffer commandBuffer,
                                             const VkPushConstantsInfo* pPushConstantsInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdPushConstants2KHR(cmdBuffer, pPushConstantsInfo);
}

void Vulkan_wDx12::hk_vkCmdPushDescriptorSet2KHR(VkCommandBuffer commandBuffer,
                                                 const VkPushDescriptorSetInfo* pPushDescriptorSetInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdPushDescriptorSet2KHR(cmdBuffer, pPushDescriptorSetInfo);
}

void Vulkan_wDx12::hk_vkCmdPushDescriptorSetWithTemplate2KHR(
    VkCommandBuffer commandBuffer, const VkPushDescriptorSetWithTemplateInfo* pPushDescriptorSetWithTemplateInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdPushDescriptorSetWithTemplate2KHR(cmdBuffer, pPushDescriptorSetWithTemplateInfo);
}

void Vulkan_wDx12::hk_vkCmdSetDescriptorBufferOffsets2EXT(
    VkCommandBuffer commandBuffer, const VkSetDescriptorBufferOffsetsInfoEXT* pSetDescriptorBufferOffsetsInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDescriptorBufferOffsets2EXT(cmdBuffer, pSetDescriptorBufferOffsetsInfo);
}

void Vulkan_wDx12::hk_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT(
    VkCommandBuffer commandBuffer,
    const VkBindDescriptorBufferEmbeddedSamplersInfoEXT* pBindDescriptorBufferEmbeddedSamplersInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT(cmdBuffer, pBindDescriptorBufferEmbeddedSamplersInfo);
}

void Vulkan_wDx12::hk_vkCmdDebugMarkerBeginEXT(VkCommandBuffer commandBuffer,
                                               const VkDebugMarkerMarkerInfoEXT* pMarkerInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDebugMarkerBeginEXT(cmdBuffer, pMarkerInfo);
}

void Vulkan_wDx12::hk_vkCmdDebugMarkerEndEXT(VkCommandBuffer commandBuffer)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDebugMarkerEndEXT(cmdBuffer);
}

void Vulkan_wDx12::hk_vkCmdDebugMarkerInsertEXT(VkCommandBuffer commandBuffer,
                                                const VkDebugMarkerMarkerInfoEXT* pMarkerInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDebugMarkerInsertEXT(cmdBuffer, pMarkerInfo);
}

void Vulkan_wDx12::hk_vkCmdBindTransformFeedbackBuffersEXT(VkCommandBuffer commandBuffer, uint32_t firstBinding,
                                                           uint32_t bindingCount, const VkBuffer* pBuffers,
                                                           const VkDeviceSize* pOffsets, const VkDeviceSize* pSizes)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBindTransformFeedbackBuffersEXT(cmdBuffer, firstBinding, bindingCount, pBuffers, pOffsets, pSizes);
}

void Vulkan_wDx12::hk_vkCmdBeginTransformFeedbackEXT(VkCommandBuffer commandBuffer, uint32_t firstCounterBuffer,
                                                     uint32_t counterBufferCount, const VkBuffer* pCounterBuffers,
                                                     const VkDeviceSize* pCounterBufferOffsets)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBeginTransformFeedbackEXT(cmdBuffer, firstCounterBuffer, counterBufferCount, pCounterBuffers,
                                     pCounterBufferOffsets);
}

void Vulkan_wDx12::hk_vkCmdEndTransformFeedbackEXT(VkCommandBuffer commandBuffer, uint32_t firstCounterBuffer,
                                                   uint32_t counterBufferCount, const VkBuffer* pCounterBuffers,
                                                   const VkDeviceSize* pCounterBufferOffsets)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdEndTransformFeedbackEXT(cmdBuffer, firstCounterBuffer, counterBufferCount, pCounterBuffers,
                                   pCounterBufferOffsets);
}

void Vulkan_wDx12::hk_vkCmdBeginQueryIndexedEXT(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query,
                                                VkQueryControlFlags flags, uint32_t index)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBeginQueryIndexedEXT(cmdBuffer, queryPool, query, flags, index);
}

void Vulkan_wDx12::hk_vkCmdEndQueryIndexedEXT(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query,
                                              uint32_t index)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdEndQueryIndexedEXT(cmdBuffer, queryPool, query, index);
}

void Vulkan_wDx12::hk_vkCmdDrawIndirectByteCountEXT(VkCommandBuffer commandBuffer, uint32_t instanceCount,
                                                    uint32_t firstInstance, VkBuffer counterBuffer,
                                                    VkDeviceSize counterBufferOffset, uint32_t counterOffset,
                                                    uint32_t vertexStride)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDrawIndirectByteCountEXT(cmdBuffer, instanceCount, firstInstance, counterBuffer, counterBufferOffset,
                                    counterOffset, vertexStride);
}

void Vulkan_wDx12::hk_vkCmdCuLaunchKernelNVX(VkCommandBuffer commandBuffer, const VkCuLaunchInfoNVX* pLaunchInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCuLaunchKernelNVX(cmdBuffer, pLaunchInfo);
}

void Vulkan_wDx12::hk_vkCmdDrawIndirectCountAMD(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                VkBuffer countBuffer, VkDeviceSize countBufferOffset,
                                                uint32_t maxDrawCount, uint32_t stride)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDrawIndirectCountAMD(cmdBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}

void Vulkan_wDx12::hk_vkCmdDrawIndexedIndirectCountAMD(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                                       VkDeviceSize offset, VkBuffer countBuffer,
                                                       VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                                       uint32_t stride)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDrawIndexedIndirectCountAMD(cmdBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}

void Vulkan_wDx12::hk_vkCmdBeginConditionalRenderingEXT(
    VkCommandBuffer commandBuffer, const VkConditionalRenderingBeginInfoEXT* pConditionalRenderingBegin)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBeginConditionalRenderingEXT(cmdBuffer, pConditionalRenderingBegin);
}

void Vulkan_wDx12::hk_vkCmdEndConditionalRenderingEXT(VkCommandBuffer commandBuffer)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdEndConditionalRenderingEXT(cmdBuffer);
}

void Vulkan_wDx12::hk_vkCmdSetViewportWScalingNV(VkCommandBuffer commandBuffer, uint32_t firstViewport,
                                                 uint32_t viewportCount, const VkViewportWScalingNV* pViewportWScalings)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetViewportWScalingNV(cmdBuffer, firstViewport, viewportCount, pViewportWScalings);
}

void Vulkan_wDx12::hk_vkCmdSetDiscardRectangleEXT(VkCommandBuffer commandBuffer, uint32_t firstDiscardRectangle,
                                                  uint32_t discardRectangleCount, const VkRect2D* pDiscardRectangles)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDiscardRectangleEXT(cmdBuffer, firstDiscardRectangle, discardRectangleCount, pDiscardRectangles);
}

void Vulkan_wDx12::hk_vkCmdSetDiscardRectangleEnableEXT(VkCommandBuffer commandBuffer, VkBool32 discardRectangleEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDiscardRectangleEnableEXT(cmdBuffer, discardRectangleEnable);
}

void Vulkan_wDx12::hk_vkCmdSetDiscardRectangleModeEXT(VkCommandBuffer commandBuffer,
                                                      VkDiscardRectangleModeEXT discardRectangleMode)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDiscardRectangleModeEXT(cmdBuffer, discardRectangleMode);
}

void Vulkan_wDx12::hk_vkCmdBeginDebugUtilsLabelEXT(VkCommandBuffer commandBuffer,
                                                   const VkDebugUtilsLabelEXT* pLabelInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBeginDebugUtilsLabelEXT(cmdBuffer, pLabelInfo);
}

void Vulkan_wDx12::hk_vkCmdEndDebugUtilsLabelEXT(VkCommandBuffer commandBuffer)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdEndDebugUtilsLabelEXT(cmdBuffer);
}

void Vulkan_wDx12::hk_vkCmdInsertDebugUtilsLabelEXT(VkCommandBuffer commandBuffer,
                                                    const VkDebugUtilsLabelEXT* pLabelInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdInsertDebugUtilsLabelEXT(cmdBuffer, pLabelInfo);
}

void Vulkan_wDx12::hk_vkCmdSetSampleLocationsEXT(VkCommandBuffer commandBuffer,
                                                 const VkSampleLocationsInfoEXT* pSampleLocationsInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetSampleLocationsEXT(cmdBuffer, pSampleLocationsInfo);
}

void Vulkan_wDx12::hk_vkCmdBindShadingRateImageNV(VkCommandBuffer commandBuffer, VkImageView imageView,
                                                  VkImageLayout imageLayout)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBindShadingRateImageNV(cmdBuffer, imageView, imageLayout);
}

void Vulkan_wDx12::hk_vkCmdSetViewportShadingRatePaletteNV(VkCommandBuffer commandBuffer, uint32_t firstViewport,
                                                           uint32_t viewportCount,
                                                           const VkShadingRatePaletteNV* pShadingRatePalettes)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetViewportShadingRatePaletteNV(cmdBuffer, firstViewport, viewportCount, pShadingRatePalettes);
}

void Vulkan_wDx12::hk_vkCmdSetCoarseSampleOrderNV(VkCommandBuffer commandBuffer,
                                                  VkCoarseSampleOrderTypeNV sampleOrderType,
                                                  uint32_t customSampleOrderCount,
                                                  const VkCoarseSampleOrderCustomNV* pCustomSampleOrders)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetCoarseSampleOrderNV(cmdBuffer, sampleOrderType, customSampleOrderCount, pCustomSampleOrders);
}

void Vulkan_wDx12::hk_vkCmdBuildAccelerationStructureNV(VkCommandBuffer commandBuffer,
                                                        const VkAccelerationStructureInfoNV* pInfo,
                                                        VkBuffer instanceData, VkDeviceSize instanceOffset,
                                                        VkBool32 update, VkAccelerationStructureNV dst,
                                                        VkAccelerationStructureNV src, VkBuffer scratch,
                                                        VkDeviceSize scratchOffset)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBuildAccelerationStructureNV(cmdBuffer, pInfo, instanceData, instanceOffset, update, dst, src, scratch,
                                        scratchOffset);
}

void Vulkan_wDx12::hk_vkCmdCopyAccelerationStructureNV(VkCommandBuffer commandBuffer, VkAccelerationStructureNV dst,
                                                       VkAccelerationStructureNV src,
                                                       VkCopyAccelerationStructureModeKHR mode)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyAccelerationStructureNV(cmdBuffer, dst, src, mode);
}

void Vulkan_wDx12::hk_vkCmdTraceRaysNV(VkCommandBuffer commandBuffer, VkBuffer raygenShaderBindingTableBuffer,
                                       VkDeviceSize raygenShaderBindingOffset, VkBuffer missShaderBindingTableBuffer,
                                       VkDeviceSize missShaderBindingOffset, VkDeviceSize missShaderBindingStride,
                                       VkBuffer hitShaderBindingTableBuffer, VkDeviceSize hitShaderBindingOffset,
                                       VkDeviceSize hitShaderBindingStride, VkBuffer callableShaderBindingTableBuffer,
                                       VkDeviceSize callableShaderBindingOffset,
                                       VkDeviceSize callableShaderBindingStride, uint32_t width, uint32_t height,
                                       uint32_t depth)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdTraceRaysNV(cmdBuffer, raygenShaderBindingTableBuffer, raygenShaderBindingOffset,
                       missShaderBindingTableBuffer, missShaderBindingOffset, missShaderBindingStride,
                       hitShaderBindingTableBuffer, hitShaderBindingOffset, hitShaderBindingStride,
                       callableShaderBindingTableBuffer, callableShaderBindingOffset, callableShaderBindingStride,
                       width, height, depth);
}

void Vulkan_wDx12::hk_vkCmdWriteAccelerationStructuresPropertiesNV(
    VkCommandBuffer commandBuffer, uint32_t accelerationStructureCount,
    const VkAccelerationStructureNV* pAccelerationStructures, VkQueryType queryType, VkQueryPool queryPool,
    uint32_t firstQuery)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdWriteAccelerationStructuresPropertiesNV(cmdBuffer, accelerationStructureCount, pAccelerationStructures,
                                                   queryType, queryPool, firstQuery);
}

void Vulkan_wDx12::hk_vkCmdWriteBufferMarkerAMD(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage,
                                                VkBuffer dstBuffer, VkDeviceSize dstOffset, uint32_t marker)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdWriteBufferMarkerAMD(cmdBuffer, pipelineStage, dstBuffer, dstOffset, marker);
}

void Vulkan_wDx12::hk_vkCmdWriteBufferMarker2AMD(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage,
                                                 VkBuffer dstBuffer, VkDeviceSize dstOffset, uint32_t marker)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdWriteBufferMarker2AMD(cmdBuffer, stage, dstBuffer, dstOffset, marker);
}

void Vulkan_wDx12::hk_vkCmdDrawMeshTasksNV(VkCommandBuffer commandBuffer, uint32_t taskCount, uint32_t firstTask)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDrawMeshTasksNV(cmdBuffer, taskCount, firstTask);
}

void Vulkan_wDx12::hk_vkCmdDrawMeshTasksIndirectNV(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                   uint32_t drawCount, uint32_t stride)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDrawMeshTasksIndirectNV(cmdBuffer, buffer, offset, drawCount, stride);
}

void Vulkan_wDx12::hk_vkCmdDrawMeshTasksIndirectCountNV(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                                        VkDeviceSize offset, VkBuffer countBuffer,
                                                        VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                                        uint32_t stride)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDrawMeshTasksIndirectCountNV(cmdBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount,
                                        stride);
}

void Vulkan_wDx12::hk_vkCmdSetExclusiveScissorEnableNV(VkCommandBuffer commandBuffer, uint32_t firstExclusiveScissor,
                                                       uint32_t exclusiveScissorCount,
                                                       const VkBool32* pExclusiveScissorEnables)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetExclusiveScissorEnableNV(cmdBuffer, firstExclusiveScissor, exclusiveScissorCount,
                                       pExclusiveScissorEnables);
}

void Vulkan_wDx12::hk_vkCmdSetExclusiveScissorNV(VkCommandBuffer commandBuffer, uint32_t firstExclusiveScissor,
                                                 uint32_t exclusiveScissorCount, const VkRect2D* pExclusiveScissors)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetExclusiveScissorNV(cmdBuffer, firstExclusiveScissor, exclusiveScissorCount, pExclusiveScissors);
}

void Vulkan_wDx12::hk_vkCmdSetCheckpointNV(VkCommandBuffer commandBuffer, const void* pCheckpointMarker)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetCheckpointNV(cmdBuffer, pCheckpointMarker);
}

VkResult Vulkan_wDx12::hk_vkCmdSetPerformanceMarkerINTEL(VkCommandBuffer commandBuffer,
                                                         const VkPerformanceMarkerInfoINTEL* pMarkerInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    return o_vkCmdSetPerformanceMarkerINTEL(cmdBuffer, pMarkerInfo);
}

VkResult Vulkan_wDx12::hk_vkCmdSetPerformanceStreamMarkerINTEL(VkCommandBuffer commandBuffer,
                                                               const VkPerformanceStreamMarkerInfoINTEL* pMarkerInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    return o_vkCmdSetPerformanceStreamMarkerINTEL(cmdBuffer, pMarkerInfo);
}

VkResult Vulkan_wDx12::hk_vkCmdSetPerformanceOverrideINTEL(VkCommandBuffer commandBuffer,
                                                           const VkPerformanceOverrideInfoINTEL* pOverrideInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    return o_vkCmdSetPerformanceOverrideINTEL(cmdBuffer, pOverrideInfo);
}

void Vulkan_wDx12::hk_vkCmdSetLineStippleEXT(VkCommandBuffer commandBuffer, uint32_t lineStippleFactor,
                                             uint16_t lineStipplePattern)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetLineStippleEXT(cmdBuffer, lineStippleFactor, lineStipplePattern);
}

void Vulkan_wDx12::hk_vkCmdSetCullModeEXT(VkCommandBuffer commandBuffer, VkCullModeFlags cullMode)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetCullModeEXT(cmdBuffer, cullMode);
}

void Vulkan_wDx12::hk_vkCmdSetFrontFaceEXT(VkCommandBuffer commandBuffer, VkFrontFace frontFace)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetFrontFaceEXT(cmdBuffer, frontFace);
}

void Vulkan_wDx12::hk_vkCmdSetPrimitiveTopologyEXT(VkCommandBuffer commandBuffer, VkPrimitiveTopology primitiveTopology)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetPrimitiveTopologyEXT(cmdBuffer, primitiveTopology);
}

void Vulkan_wDx12::hk_vkCmdSetViewportWithCountEXT(VkCommandBuffer commandBuffer, uint32_t viewportCount,
                                                   const VkViewport* pViewports)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetViewportWithCountEXT(cmdBuffer, viewportCount, pViewports);
}

void Vulkan_wDx12::hk_vkCmdSetScissorWithCountEXT(VkCommandBuffer commandBuffer, uint32_t scissorCount,
                                                  const VkRect2D* pScissors)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetScissorWithCountEXT(cmdBuffer, scissorCount, pScissors);
}

void Vulkan_wDx12::hk_vkCmdBindVertexBuffers2EXT(VkCommandBuffer commandBuffer, uint32_t firstBinding,
                                                 uint32_t bindingCount, const VkBuffer* pBuffers,
                                                 const VkDeviceSize* pOffsets, const VkDeviceSize* pSizes,
                                                 const VkDeviceSize* pStrides)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBindVertexBuffers2EXT(cmdBuffer, firstBinding, bindingCount, pBuffers, pOffsets, pSizes, pStrides);
}

void Vulkan_wDx12::hk_vkCmdSetDepthTestEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthTestEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDepthTestEnableEXT(cmdBuffer, depthTestEnable);
}

void Vulkan_wDx12::hk_vkCmdSetDepthWriteEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthWriteEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDepthWriteEnableEXT(cmdBuffer, depthWriteEnable);
}

void Vulkan_wDx12::hk_vkCmdSetDepthCompareOpEXT(VkCommandBuffer commandBuffer, VkCompareOp depthCompareOp)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDepthCompareOpEXT(cmdBuffer, depthCompareOp);
}

void Vulkan_wDx12::hk_vkCmdSetDepthBoundsTestEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthBoundsTestEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDepthBoundsTestEnableEXT(cmdBuffer, depthBoundsTestEnable);
}

void Vulkan_wDx12::hk_vkCmdSetStencilTestEnableEXT(VkCommandBuffer commandBuffer, VkBool32 stencilTestEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetStencilTestEnableEXT(cmdBuffer, stencilTestEnable);
}

void Vulkan_wDx12::hk_vkCmdSetStencilOpEXT(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
                                           VkStencilOp failOp, VkStencilOp passOp, VkStencilOp depthFailOp,
                                           VkCompareOp compareOp)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetStencilOpEXT(cmdBuffer, faceMask, failOp, passOp, depthFailOp, compareOp);
}

void Vulkan_wDx12::hk_vkCmdPreprocessGeneratedCommandsNV(VkCommandBuffer commandBuffer,
                                                         const VkGeneratedCommandsInfoNV* pGeneratedCommandsInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdPreprocessGeneratedCommandsNV(cmdBuffer, pGeneratedCommandsInfo);
}

void Vulkan_wDx12::hk_vkCmdExecuteGeneratedCommandsNV(VkCommandBuffer commandBuffer, VkBool32 isPreprocessed,
                                                      const VkGeneratedCommandsInfoNV* pGeneratedCommandsInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdExecuteGeneratedCommandsNV(cmdBuffer, isPreprocessed, pGeneratedCommandsInfo);
}

void Vulkan_wDx12::hk_vkCmdBindPipelineShaderGroupNV(VkCommandBuffer commandBuffer,
                                                     VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline,
                                                     uint32_t groupIndex)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBindPipelineShaderGroupNV(cmdBuffer, pipelineBindPoint, pipeline, groupIndex);
}

void Vulkan_wDx12::hk_vkCmdSetDepthBias2EXT(VkCommandBuffer commandBuffer, const VkDepthBiasInfoEXT* pDepthBiasInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDepthBias2EXT(cmdBuffer, pDepthBiasInfo);
}

void Vulkan_wDx12::hk_vkCmdCudaLaunchKernelNV(VkCommandBuffer commandBuffer, const VkCudaLaunchInfoNV* pLaunchInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCudaLaunchKernelNV(cmdBuffer, pLaunchInfo);
}

void Vulkan_wDx12::hk_vkCmdBindDescriptorBuffersEXT(VkCommandBuffer commandBuffer, uint32_t bufferCount,
                                                    const VkDescriptorBufferBindingInfoEXT* pBindingInfos)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBindDescriptorBuffersEXT(cmdBuffer, bufferCount, pBindingInfos);
}

void Vulkan_wDx12::hk_vkCmdSetDescriptorBufferOffsetsEXT(VkCommandBuffer commandBuffer,
                                                         VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout,
                                                         uint32_t firstSet, uint32_t setCount,
                                                         const uint32_t* pBufferIndices, const VkDeviceSize* pOffsets)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDescriptorBufferOffsetsEXT(cmdBuffer, pipelineBindPoint, layout, firstSet, setCount, pBufferIndices,
                                         pOffsets);
}

void Vulkan_wDx12::hk_vkCmdBindDescriptorBufferEmbeddedSamplersEXT(VkCommandBuffer commandBuffer,
                                                                   VkPipelineBindPoint pipelineBindPoint,
                                                                   VkPipelineLayout layout, uint32_t set)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBindDescriptorBufferEmbeddedSamplersEXT(cmdBuffer, pipelineBindPoint, layout, set);
}

void Vulkan_wDx12::hk_vkCmdSetFragmentShadingRateEnumNV(VkCommandBuffer commandBuffer,
                                                        VkFragmentShadingRateNV shadingRate,
                                                        const VkFragmentShadingRateCombinerOpKHR combinerOps[2])
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetFragmentShadingRateEnumNV(cmdBuffer, shadingRate, combinerOps);
}

void Vulkan_wDx12::hk_vkCmdSetVertexInputEXT(VkCommandBuffer commandBuffer, uint32_t vertexBindingDescriptionCount,
                                             const VkVertexInputBindingDescription2EXT* pVertexBindingDescriptions,
                                             uint32_t vertexAttributeDescriptionCount,
                                             const VkVertexInputAttributeDescription2EXT* pVertexAttributeDescriptions)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetVertexInputEXT(cmdBuffer, vertexBindingDescriptionCount, pVertexBindingDescriptions,
                             vertexAttributeDescriptionCount, pVertexAttributeDescriptions);
}

void Vulkan_wDx12::hk_vkCmdSubpassShadingHUAWEI(VkCommandBuffer commandBuffer)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSubpassShadingHUAWEI(cmdBuffer);
}

void Vulkan_wDx12::hk_vkCmdBindInvocationMaskHUAWEI(VkCommandBuffer commandBuffer, VkImageView imageView,
                                                    VkImageLayout imageLayout)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBindInvocationMaskHUAWEI(cmdBuffer, imageView, imageLayout);
}

void Vulkan_wDx12::hk_vkCmdSetPatchControlPointsEXT(VkCommandBuffer commandBuffer, uint32_t patchControlPoints)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetPatchControlPointsEXT(cmdBuffer, patchControlPoints);
}

void Vulkan_wDx12::hk_vkCmdSetRasterizerDiscardEnableEXT(VkCommandBuffer commandBuffer,
                                                         VkBool32 rasterizerDiscardEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetRasterizerDiscardEnableEXT(cmdBuffer, rasterizerDiscardEnable);
}

void Vulkan_wDx12::hk_vkCmdSetDepthBiasEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthBiasEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDepthBiasEnableEXT(cmdBuffer, depthBiasEnable);
}

void Vulkan_wDx12::hk_vkCmdSetLogicOpEXT(VkCommandBuffer commandBuffer, VkLogicOp logicOp)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetLogicOpEXT(cmdBuffer, logicOp);
}

void Vulkan_wDx12::hk_vkCmdSetPrimitiveRestartEnableEXT(VkCommandBuffer commandBuffer, VkBool32 primitiveRestartEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetPrimitiveRestartEnableEXT(cmdBuffer, primitiveRestartEnable);
}

void Vulkan_wDx12::hk_vkCmdSetColorWriteEnableEXT(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                                                  const VkBool32* pColorWriteEnables)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetColorWriteEnableEXT(cmdBuffer, attachmentCount, pColorWriteEnables);
}

void Vulkan_wDx12::hk_vkCmdDrawMultiEXT(VkCommandBuffer commandBuffer, uint32_t drawCount,
                                        const VkMultiDrawInfoEXT* pVertexInfo, uint32_t instanceCount,
                                        uint32_t firstInstance, uint32_t stride)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDrawMultiEXT(cmdBuffer, drawCount, pVertexInfo, instanceCount, firstInstance, stride);
}

void Vulkan_wDx12::hk_vkCmdDrawMultiIndexedEXT(VkCommandBuffer commandBuffer, uint32_t drawCount,
                                               const VkMultiDrawIndexedInfoEXT* pIndexInfo, uint32_t instanceCount,
                                               uint32_t firstInstance, uint32_t stride, const int32_t* pVertexOffset)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDrawMultiIndexedEXT(cmdBuffer, drawCount, pIndexInfo, instanceCount, firstInstance, stride, pVertexOffset);
}

void Vulkan_wDx12::hk_vkCmdBuildMicromapsEXT(VkCommandBuffer commandBuffer, uint32_t infoCount,
                                             const VkMicromapBuildInfoEXT* pInfos)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBuildMicromapsEXT(cmdBuffer, infoCount, pInfos);
}

void Vulkan_wDx12::hk_vkCmdCopyMicromapEXT(VkCommandBuffer commandBuffer, const VkCopyMicromapInfoEXT* pInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyMicromapEXT(cmdBuffer, pInfo);
}

void Vulkan_wDx12::hk_vkCmdCopyMicromapToMemoryEXT(VkCommandBuffer commandBuffer,
                                                   const VkCopyMicromapToMemoryInfoEXT* pInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyMicromapToMemoryEXT(cmdBuffer, pInfo);
}

void Vulkan_wDx12::hk_vkCmdCopyMemoryToMicromapEXT(VkCommandBuffer commandBuffer,
                                                   const VkCopyMemoryToMicromapInfoEXT* pInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyMemoryToMicromapEXT(cmdBuffer, pInfo);
}

void Vulkan_wDx12::hk_vkCmdWriteMicromapsPropertiesEXT(VkCommandBuffer commandBuffer, uint32_t micromapCount,
                                                       const VkMicromapEXT* pMicromaps, VkQueryType queryType,
                                                       VkQueryPool queryPool, uint32_t firstQuery)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdWriteMicromapsPropertiesEXT(cmdBuffer, micromapCount, pMicromaps, queryType, queryPool, firstQuery);
}

void Vulkan_wDx12::hk_vkCmdDrawClusterHUAWEI(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY,
                                             uint32_t groupCountZ)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDrawClusterHUAWEI(cmdBuffer, groupCountX, groupCountY, groupCountZ);
}

void Vulkan_wDx12::hk_vkCmdDrawClusterIndirectHUAWEI(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                                     VkDeviceSize offset)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDrawClusterIndirectHUAWEI(cmdBuffer, buffer, offset);
}

void Vulkan_wDx12::hk_vkCmdCopyMemoryIndirectNV(VkCommandBuffer commandBuffer, VkDeviceAddress copyBufferAddress,
                                                uint32_t copyCount, uint32_t stride)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyMemoryIndirectNV(cmdBuffer, copyBufferAddress, copyCount, stride);
}

void Vulkan_wDx12::hk_vkCmdCopyMemoryToImageIndirectNV(VkCommandBuffer commandBuffer, VkDeviceAddress copyBufferAddress,
                                                       uint32_t copyCount, uint32_t stride, VkImage dstImage,
                                                       VkImageLayout dstImageLayout,
                                                       const VkImageSubresourceLayers* pImageSubresources)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyMemoryToImageIndirectNV(cmdBuffer, copyBufferAddress, copyCount, stride, dstImage, dstImageLayout,
                                       pImageSubresources);
}

void Vulkan_wDx12::hk_vkCmdDecompressMemoryNV(VkCommandBuffer commandBuffer, uint32_t decompressRegionCount,
                                              const VkDecompressMemoryRegionNV* pDecompressMemoryRegions)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDecompressMemoryNV(cmdBuffer, decompressRegionCount, pDecompressMemoryRegions);
}

void Vulkan_wDx12::hk_vkCmdDecompressMemoryIndirectCountNV(VkCommandBuffer commandBuffer,
                                                           VkDeviceAddress indirectCommandsAddress,
                                                           VkDeviceAddress indirectCommandsCountAddress,
                                                           uint32_t stride)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDecompressMemoryIndirectCountNV(cmdBuffer, indirectCommandsAddress, indirectCommandsCountAddress, stride);
}

void Vulkan_wDx12::hk_vkCmdUpdatePipelineIndirectBufferNV(VkCommandBuffer commandBuffer,
                                                          VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdUpdatePipelineIndirectBufferNV(cmdBuffer, pipelineBindPoint, pipeline);
}

void Vulkan_wDx12::hk_vkCmdSetDepthClampEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthClampEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDepthClampEnableEXT(cmdBuffer, depthClampEnable);
}

void Vulkan_wDx12::hk_vkCmdSetPolygonModeEXT(VkCommandBuffer commandBuffer, VkPolygonMode polygonMode)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetPolygonModeEXT(cmdBuffer, polygonMode);
}

void Vulkan_wDx12::hk_vkCmdSetRasterizationSamplesEXT(VkCommandBuffer commandBuffer,
                                                      VkSampleCountFlagBits rasterizationSamples)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetRasterizationSamplesEXT(cmdBuffer, rasterizationSamples);
}

void Vulkan_wDx12::hk_vkCmdSetSampleMaskEXT(VkCommandBuffer commandBuffer, VkSampleCountFlagBits samples,
                                            const VkSampleMask* pSampleMask)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetSampleMaskEXT(cmdBuffer, samples, pSampleMask);
}

void Vulkan_wDx12::hk_vkCmdSetAlphaToCoverageEnableEXT(VkCommandBuffer commandBuffer, VkBool32 alphaToCoverageEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetAlphaToCoverageEnableEXT(cmdBuffer, alphaToCoverageEnable);
}

void Vulkan_wDx12::hk_vkCmdSetAlphaToOneEnableEXT(VkCommandBuffer commandBuffer, VkBool32 alphaToOneEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetAlphaToOneEnableEXT(cmdBuffer, alphaToOneEnable);
}

void Vulkan_wDx12::hk_vkCmdSetLogicOpEnableEXT(VkCommandBuffer commandBuffer, VkBool32 logicOpEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetLogicOpEnableEXT(cmdBuffer, logicOpEnable);
}

void Vulkan_wDx12::hk_vkCmdSetColorBlendEnableEXT(VkCommandBuffer commandBuffer, uint32_t firstAttachment,
                                                  uint32_t attachmentCount, const VkBool32* pColorBlendEnables)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetColorBlendEnableEXT(cmdBuffer, firstAttachment, attachmentCount, pColorBlendEnables);
}

void Vulkan_wDx12::hk_vkCmdSetColorBlendEquationEXT(VkCommandBuffer commandBuffer, uint32_t firstAttachment,
                                                    uint32_t attachmentCount,
                                                    const VkColorBlendEquationEXT* pColorBlendEquations)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetColorBlendEquationEXT(cmdBuffer, firstAttachment, attachmentCount, pColorBlendEquations);
}

void Vulkan_wDx12::hk_vkCmdSetColorWriteMaskEXT(VkCommandBuffer commandBuffer, uint32_t firstAttachment,
                                                uint32_t attachmentCount, const VkColorComponentFlags* pColorWriteMasks)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetColorWriteMaskEXT(cmdBuffer, firstAttachment, attachmentCount, pColorWriteMasks);
}

void Vulkan_wDx12::hk_vkCmdSetTessellationDomainOriginEXT(VkCommandBuffer commandBuffer,
                                                          VkTessellationDomainOrigin domainOrigin)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetTessellationDomainOriginEXT(cmdBuffer, domainOrigin);
}

void Vulkan_wDx12::hk_vkCmdSetRasterizationStreamEXT(VkCommandBuffer commandBuffer, uint32_t rasterizationStream)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetRasterizationStreamEXT(cmdBuffer, rasterizationStream);
}

void Vulkan_wDx12::hk_vkCmdSetConservativeRasterizationModeEXT(
    VkCommandBuffer commandBuffer, VkConservativeRasterizationModeEXT conservativeRasterizationMode)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetConservativeRasterizationModeEXT(cmdBuffer, conservativeRasterizationMode);
}

void Vulkan_wDx12::hk_vkCmdSetExtraPrimitiveOverestimationSizeEXT(VkCommandBuffer commandBuffer,
                                                                  float extraPrimitiveOverestimationSize)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetExtraPrimitiveOverestimationSizeEXT(cmdBuffer, extraPrimitiveOverestimationSize);
}

void Vulkan_wDx12::hk_vkCmdSetDepthClipEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthClipEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDepthClipEnableEXT(cmdBuffer, depthClipEnable);
}

void Vulkan_wDx12::hk_vkCmdSetSampleLocationsEnableEXT(VkCommandBuffer commandBuffer, VkBool32 sampleLocationsEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetSampleLocationsEnableEXT(cmdBuffer, sampleLocationsEnable);
}

void Vulkan_wDx12::hk_vkCmdSetColorBlendAdvancedEXT(VkCommandBuffer commandBuffer, uint32_t firstAttachment,
                                                    uint32_t attachmentCount,
                                                    const VkColorBlendAdvancedEXT* pColorBlendAdvanced)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetColorBlendAdvancedEXT(cmdBuffer, firstAttachment, attachmentCount, pColorBlendAdvanced);
}

void Vulkan_wDx12::hk_vkCmdSetProvokingVertexModeEXT(VkCommandBuffer commandBuffer,
                                                     VkProvokingVertexModeEXT provokingVertexMode)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetProvokingVertexModeEXT(cmdBuffer, provokingVertexMode);
}

void Vulkan_wDx12::hk_vkCmdSetLineRasterizationModeEXT(VkCommandBuffer commandBuffer,
                                                       VkLineRasterizationModeEXT lineRasterizationMode)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetLineRasterizationModeEXT(cmdBuffer, lineRasterizationMode);
}

void Vulkan_wDx12::hk_vkCmdSetLineStippleEnableEXT(VkCommandBuffer commandBuffer, VkBool32 stippledLineEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetLineStippleEnableEXT(cmdBuffer, stippledLineEnable);
}

void Vulkan_wDx12::hk_vkCmdSetDepthClipNegativeOneToOneEXT(VkCommandBuffer commandBuffer, VkBool32 negativeOneToOne)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDepthClipNegativeOneToOneEXT(cmdBuffer, negativeOneToOne);
}

void Vulkan_wDx12::hk_vkCmdSetViewportWScalingEnableNV(VkCommandBuffer commandBuffer, VkBool32 viewportWScalingEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetViewportWScalingEnableNV(cmdBuffer, viewportWScalingEnable);
}

void Vulkan_wDx12::hk_vkCmdSetViewportSwizzleNV(VkCommandBuffer commandBuffer, uint32_t firstViewport,
                                                uint32_t viewportCount, const VkViewportSwizzleNV* pViewportSwizzles)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetViewportSwizzleNV(cmdBuffer, firstViewport, viewportCount, pViewportSwizzles);
}

void Vulkan_wDx12::hk_vkCmdSetCoverageToColorEnableNV(VkCommandBuffer commandBuffer, VkBool32 coverageToColorEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetCoverageToColorEnableNV(cmdBuffer, coverageToColorEnable);
}

void Vulkan_wDx12::hk_vkCmdSetCoverageToColorLocationNV(VkCommandBuffer commandBuffer, uint32_t coverageToColorLocation)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetCoverageToColorLocationNV(cmdBuffer, coverageToColorLocation);
}

void Vulkan_wDx12::hk_vkCmdSetCoverageModulationModeNV(VkCommandBuffer commandBuffer,
                                                       VkCoverageModulationModeNV coverageModulationMode)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetCoverageModulationModeNV(cmdBuffer, coverageModulationMode);
}

void Vulkan_wDx12::hk_vkCmdSetCoverageModulationTableEnableNV(VkCommandBuffer commandBuffer,
                                                              VkBool32 coverageModulationTableEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetCoverageModulationTableEnableNV(cmdBuffer, coverageModulationTableEnable);
}

void Vulkan_wDx12::hk_vkCmdSetCoverageModulationTableNV(VkCommandBuffer commandBuffer,
                                                        uint32_t coverageModulationTableCount,
                                                        const float* pCoverageModulationTable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetCoverageModulationTableNV(cmdBuffer, coverageModulationTableCount, pCoverageModulationTable);
}

void Vulkan_wDx12::hk_vkCmdSetShadingRateImageEnableNV(VkCommandBuffer commandBuffer, VkBool32 shadingRateImageEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetShadingRateImageEnableNV(cmdBuffer, shadingRateImageEnable);
}

void Vulkan_wDx12::hk_vkCmdSetRepresentativeFragmentTestEnableNV(VkCommandBuffer commandBuffer,
                                                                 VkBool32 representativeFragmentTestEnable)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetRepresentativeFragmentTestEnableNV(cmdBuffer, representativeFragmentTestEnable);
}

void Vulkan_wDx12::hk_vkCmdSetCoverageReductionModeNV(VkCommandBuffer commandBuffer,
                                                      VkCoverageReductionModeNV coverageReductionMode)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetCoverageReductionModeNV(cmdBuffer, coverageReductionMode);
}

void Vulkan_wDx12::hk_vkCmdOpticalFlowExecuteNV(VkCommandBuffer commandBuffer, VkOpticalFlowSessionNV session,
                                                const VkOpticalFlowExecuteInfoNV* pExecuteInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdOpticalFlowExecuteNV(cmdBuffer, session, pExecuteInfo);
}

void Vulkan_wDx12::hk_vkCmdBindShadersEXT(VkCommandBuffer commandBuffer, uint32_t stageCount,
                                          const VkShaderStageFlagBits* pStages, const VkShaderEXT* pShaders)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBindShadersEXT(cmdBuffer, stageCount, pStages, pShaders);
}

void Vulkan_wDx12::hk_vkCmdSetDepthClampRangeEXT(VkCommandBuffer commandBuffer, VkDepthClampModeEXT depthClampMode,
                                                 const VkDepthClampRangeEXT* pDepthClampRange)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetDepthClampRangeEXT(cmdBuffer, depthClampMode, pDepthClampRange);
}

void Vulkan_wDx12::hk_vkCmdConvertCooperativeVectorMatrixNV(VkCommandBuffer commandBuffer, uint32_t infoCount,
                                                            const VkConvertCooperativeVectorMatrixInfoNV* pInfos)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdConvertCooperativeVectorMatrixNV(cmdBuffer, infoCount, pInfos);
}

void Vulkan_wDx12::hk_vkCmdSetAttachmentFeedbackLoopEnableEXT(VkCommandBuffer commandBuffer,
                                                              VkImageAspectFlags aspectMask)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetAttachmentFeedbackLoopEnableEXT(cmdBuffer, aspectMask);
}

void Vulkan_wDx12::hk_vkCmdBuildClusterAccelerationStructureIndirectNV(
    VkCommandBuffer commandBuffer, const VkClusterAccelerationStructureCommandsInfoNV* pCommandInfos)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBuildClusterAccelerationStructureIndirectNV(cmdBuffer, pCommandInfos);
}

void Vulkan_wDx12::hk_vkCmdBuildPartitionedAccelerationStructuresNV(
    VkCommandBuffer commandBuffer, const VkBuildPartitionedAccelerationStructureInfoNV* pBuildInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBuildPartitionedAccelerationStructuresNV(cmdBuffer, pBuildInfo);
}

void Vulkan_wDx12::hk_vkCmdPreprocessGeneratedCommandsEXT(VkCommandBuffer commandBuffer,
                                                          const VkGeneratedCommandsInfoEXT* pGeneratedCommandsInfo,
                                                          VkCommandBuffer stateCommandBuffer)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdPreprocessGeneratedCommandsEXT(cmdBuffer, pGeneratedCommandsInfo, stateCommandBuffer);
}

void Vulkan_wDx12::hk_vkCmdExecuteGeneratedCommandsEXT(VkCommandBuffer commandBuffer, VkBool32 isPreprocessed,
                                                       const VkGeneratedCommandsInfoEXT* pGeneratedCommandsInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdExecuteGeneratedCommandsEXT(cmdBuffer, isPreprocessed, pGeneratedCommandsInfo);
}

void Vulkan_wDx12::hk_vkCmdBuildAccelerationStructuresKHR(
    VkCommandBuffer commandBuffer, uint32_t infoCount, const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
    const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBuildAccelerationStructuresKHR(cmdBuffer, infoCount, pInfos, ppBuildRangeInfos);
}

void Vulkan_wDx12::hk_vkCmdBuildAccelerationStructuresIndirectKHR(
    VkCommandBuffer commandBuffer, uint32_t infoCount, const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
    const VkDeviceAddress* pIndirectDeviceAddresses, const uint32_t* pIndirectStrides,
    const uint32_t* const* ppMaxPrimitiveCounts)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdBuildAccelerationStructuresIndirectKHR(cmdBuffer, infoCount, pInfos, pIndirectDeviceAddresses,
                                                  pIndirectStrides, ppMaxPrimitiveCounts);
}

void Vulkan_wDx12::hk_vkCmdCopyAccelerationStructureKHR(VkCommandBuffer commandBuffer,
                                                        const VkCopyAccelerationStructureInfoKHR* pInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyAccelerationStructureKHR(cmdBuffer, pInfo);
}

void Vulkan_wDx12::hk_vkCmdCopyAccelerationStructureToMemoryKHR(VkCommandBuffer commandBuffer,
                                                                const VkCopyAccelerationStructureToMemoryInfoKHR* pInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyAccelerationStructureToMemoryKHR(cmdBuffer, pInfo);
}

void Vulkan_wDx12::hk_vkCmdCopyMemoryToAccelerationStructureKHR(VkCommandBuffer commandBuffer,
                                                                const VkCopyMemoryToAccelerationStructureInfoKHR* pInfo)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdCopyMemoryToAccelerationStructureKHR(cmdBuffer, pInfo);
}

void Vulkan_wDx12::hk_vkCmdWriteAccelerationStructuresPropertiesKHR(
    VkCommandBuffer commandBuffer, uint32_t accelerationStructureCount,
    const VkAccelerationStructureKHR* pAccelerationStructures, VkQueryType queryType, VkQueryPool queryPool,
    uint32_t firstQuery)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdWriteAccelerationStructuresPropertiesKHR(cmdBuffer, accelerationStructureCount, pAccelerationStructures,
                                                    queryType, queryPool, firstQuery);
}

void Vulkan_wDx12::hk_vkCmdTraceRaysKHR(VkCommandBuffer commandBuffer,
                                        const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable,
                                        const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable,
                                        const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable,
                                        const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable,
                                        uint32_t width, uint32_t height, uint32_t depth)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdTraceRaysKHR(cmdBuffer, pRaygenShaderBindingTable, pMissShaderBindingTable, pHitShaderBindingTable,
                        pCallableShaderBindingTable, width, height, depth);
}

void Vulkan_wDx12::hk_vkCmdTraceRaysIndirectKHR(VkCommandBuffer commandBuffer,
                                                const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable,
                                                const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable,
                                                const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable,
                                                const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable,
                                                VkDeviceAddress indirectDeviceAddress)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdTraceRaysIndirectKHR(cmdBuffer, pRaygenShaderBindingTable, pMissShaderBindingTable, pHitShaderBindingTable,
                                pCallableShaderBindingTable, indirectDeviceAddress);
}

void Vulkan_wDx12::hk_vkCmdSetRayTracingPipelineStackSizeKHR(VkCommandBuffer commandBuffer, uint32_t pipelineStackSize)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdSetRayTracingPipelineStackSizeKHR(cmdBuffer, pipelineStackSize);
}

void Vulkan_wDx12::hk_vkCmdDrawMeshTasksEXT(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY,
                                            uint32_t groupCountZ)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDrawMeshTasksEXT(cmdBuffer, groupCountX, groupCountY, groupCountZ);
}

void Vulkan_wDx12::hk_vkCmdDrawMeshTasksIndirectEXT(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                    uint32_t drawCount, uint32_t stride)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDrawMeshTasksIndirectEXT(cmdBuffer, buffer, offset, drawCount, stride);
}

void Vulkan_wDx12::hk_vkCmdDrawMeshTasksIndirectCountEXT(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                                         VkDeviceSize offset, VkBuffer countBuffer,
                                                         VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                                         uint32_t stride)
{
    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("cmdBuffer: {:X}", (size_t) cmdBuffer);
#endif

    o_vkCmdDrawMeshTasksIndirectCountEXT(cmdBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount,
                                         stride);
}

#pragma endregion

VkResult Vulkan_wDx12::hk_vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool)
{
    VkResult result = o_vkCreateCommandPool(device, pCreateInfo, pAllocator, pCommandPool);

    if (result == VK_SUCCESS && pCommandPool && *pCommandPool != VK_NULL_HANDLE && pCreateInfo)
    {
        {
            std::scoped_lock lock(mutexCommandPoolToQueueFamilyMap);
            commandPoolToQueueFamilyMap[*pCommandPool] = pCreateInfo->queueFamilyIndex;
        }

        LOG_DEBUG("Command pool {:X} created for queue family {}", (size_t) *pCommandPool,
                  pCreateInfo->queueFamilyIndex);
    }

    return result;
}

void Vulkan_wDx12::hk_vkCmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount,
                                           const VkCommandBuffer* pCommandBuffers)
{
#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("commandBuffer: {:X}, commandBufferCount: {}, pCommandBuffers: {:X}", (size_t) commandBuffer,
              commandBufferCount, (size_t) pCommandBuffers);
#endif

    VkCommandBuffer cmdBuffer = commandBuffer;

    auto mappedVirtualCmdBuffer = GetVirtualCommandBuffer(cmdBuffer);
    if (mappedVirtualCmdBuffer != VK_NULL_HANDLE)
    {
#ifdef LOG_VIRTUAL_RECORDS
        LOG_DEBUG("cmdBuffer: {:X}, virtualCmdBuffer: {:X}", (size_t) cmdBuffer, (size_t) mappedVirtualCmdBuffer);
#endif
        cmdBuffer = mappedVirtualCmdBuffer;
    }

    o_vkCmdExecuteCommands(cmdBuffer, commandBufferCount, pCommandBuffers);
}

bool Vulkan_wDx12::RegisterVirtualCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBuffer virtualCommandBuffer)
{
    if (!cmdBufferStateTracker.RegisterVirtualCommandBuffer(commandBuffer, virtualCommandBuffer))
    {
        LOG_ERROR("A virtual Vulkan command buffer is already registered for {:X}", (size_t) commandBuffer);
        return false;
    }

    return true;
}

VkCommandBuffer Vulkan_wDx12::RemoveVirtualCommandBuffer(VkCommandBuffer commandBuffer)
{
    return cmdBufferStateTracker.RemoveVirtualCommandBuffer(commandBuffer);
}

VkCommandBuffer Vulkan_wDx12::GetVirtualCommandBuffer(VkCommandBuffer commandBuffer)
{
    return cmdBufferStateTracker.GetVirtualCommandBuffer(commandBuffer);
}

bool Vulkan_wDx12::RegisterPendingSubmission(const PendingSubmission& submission)
{
    if (submission.submitCommandBuffer == VK_NULL_HANDLE || submission.resourceCopySemaphore == VK_NULL_HANDLE ||
        submission.resourceCopyFence == nullptr || submission.copyBackSemaphore == VK_NULL_HANDLE ||
        submission.copyBackCommandBuffer == VK_NULL_HANDLE || submission.barrierCommandBuffer == VK_NULL_HANDLE)
    {
        LOG_ERROR("Invalid Vulkan w/Dx12 pending submission");
        return false;
    }

    std::scoped_lock lock(pendingSubmissionMutex);
    if (pendingSubmissions.contains(submission.submitCommandBuffer))
    {
        LOG_ERROR("A Vulkan w/Dx12 submission is already pending for command buffer {:X}",
                  (size_t) submission.submitCommandBuffer);
        return false;
    }

    pendingSubmissions.emplace(submission.submitCommandBuffer, submission);
    return true;
}

void Vulkan_wDx12::CancelPendingSubmission(VkCommandBuffer commandBuffer)
{
    if (commandBuffer == VK_NULL_HANDLE)
        return;

    std::scoped_lock lock(pendingSubmissionMutex);
    pendingSubmissions.erase(commandBuffer);
}

bool Vulkan_wDx12::TakePendingSubmission(VkCommandBuffer commandBuffer, PendingSubmission& submission)
{
    std::scoped_lock lock(pendingSubmissionMutex);
    auto it = pendingSubmissions.find(commandBuffer);
    if (it == pendingSubmissions.end())
        return false;

    submission = it->second;
    pendingSubmissions.erase(it);
    return true;
}

static void AbortPendingD3D12Wait(const Vulkan_wDx12::PendingSubmission& pending, const char* reason)
{
    LOG_WARN("Aborting Vulkan w/Dx12 submit transaction: {}", reason ? reason : "unknown reason");

    if (pending.resourceCopyFence == nullptr)
        return;

    // The private D3D12 interop queue is already waiting for this value. Host-signaling it drains only OptiScaler's
    // private shared-copy work; because the Vulkan injection is being abandoned, no copy-back will consume it.
    HRESULT result = pending.resourceCopyFence->Signal(pending.resourceCopyValue);
    if (result != S_OK)
        LOG_ERROR("Failed to release aborted Vulkan w/Dx12 D3D12 wait: {0:x}", result);
}

static bool LegacySubmitPNextSupported(const void* pNext)
{
    if (pNext == nullptr)
        return true;

    const auto* first = reinterpret_cast<const VkBaseInStructure*>(pNext);
    return first->sType == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO && first->pNext == nullptr;
}

static const VkTimelineSemaphoreSubmitInfo* FindTimelineSubmitInfo(const void* pNext)
{
    auto next = reinterpret_cast<const VkBaseInStructure*>(pNext);
    while (next != nullptr)
    {
        if (next->sType == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO)
            return reinterpret_cast<const VkTimelineSemaphoreSubmitInfo*>(next);

        next = next->pNext;
    }

    return nullptr;
}

VkResult Vulkan_wDx12::hk_vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits,
                                        VkFence fence)
{
    if (pSubmits == nullptr || o_vkQueueSubmit == nullptr)
    {
        LOG_ERROR("Invalid parameters to hk_vkQueueSubmit");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("queue: {:X}, submitCount: {}, fence: {:X}", (size_t) queue, submitCount, (size_t) fence);
#endif

    PendingSubmission pending {};
    uint32_t submitIndex = UINT32_MAX;
    uint32_t commandBufferIndex = UINT32_MAX;

    for (uint32_t i = 0; i < submitCount && submitIndex == UINT32_MAX; i++)
    {
        for (uint32_t j = 0; j < pSubmits[i].commandBufferCount; j++)
        {
            if (TakePendingSubmission(pSubmits[i].pCommandBuffers[j], pending))
            {
                submitIndex = i;
                commandBufferIndex = j;
                break;
            }
        }
    }

    if (submitIndex == UINT32_MAX)
        return o_vkQueueSubmit(queue, submitCount, pSubmits, fence);

    const auto& original = pSubmits[submitIndex];

    // Splitting a legacy VkSubmitInfo changes command/signal counts. Submit-level extensions such as device-group or
    // protected-submit structures can carry count-coupled semantics, so only the plain submit and the single
    // VkTimelineSemaphoreSubmitInfo case are rewritten. Everything else is passed through unchanged.
    if (!LegacySubmitPNextSupported(original.pNext))
    {
        AbortPendingD3D12Wait(pending, "unsupported legacy VkSubmitInfo pNext chain");
        return o_vkQueueSubmit(queue, submitCount, pSubmits, fence);
    }

    const auto* originalTimeline = FindTimelineSubmitInfo(original.pNext);

    // Copy timeline values instead of modifying the application's pNext chain. Binary semaphore slots use value 0.
    std::vector<uint64_t> originalWaitValues(original.waitSemaphoreCount, 0);
    std::vector<uint64_t> originalSignalValues(original.signalSemaphoreCount, 0);
    if (originalTimeline != nullptr)
    {
        if (originalTimeline->pWaitSemaphoreValues != nullptr)
        {
            auto count = std::min(original.waitSemaphoreCount, originalTimeline->waitSemaphoreValueCount);
            std::copy_n(originalTimeline->pWaitSemaphoreValues, count, originalWaitValues.data());
        }

        if (originalTimeline->pSignalSemaphoreValues != nullptr)
        {
            auto count = std::min(original.signalSemaphoreCount, originalTimeline->signalSemaphoreValueCount);
            std::copy_n(originalTimeline->pSignalSemaphoreValues, count, originalSignalValues.data());
        }
    }

    std::vector<VkCommandBuffer> originalCommandBuffers(original.pCommandBuffers,
                                                        original.pCommandBuffers + commandBufferIndex + 1);

    VkSemaphore resourceCopySemaphore = pending.resourceCopySemaphore;
    uint64_t resourceCopyValue = pending.resourceCopyValue;

    VkTimelineSemaphoreSubmitInfo originalTimelineCopy {};
    originalTimelineCopy.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    originalTimelineCopy.waitSemaphoreValueCount = original.waitSemaphoreCount;
    originalTimelineCopy.pWaitSemaphoreValues = originalWaitValues.empty() ? nullptr : originalWaitValues.data();
    originalTimelineCopy.signalSemaphoreValueCount = 1;
    originalTimelineCopy.pSignalSemaphoreValues = &resourceCopyValue;

    // LegacySubmitPNextSupported guarantees either no extension or one standalone timeline node.
    originalTimelineCopy.pNext = nullptr;

    VkSubmitInfo modifiedOriginal = original;
    modifiedOriginal.pNext = &originalTimelineCopy;
    modifiedOriginal.commandBufferCount = static_cast<uint32_t>(originalCommandBuffers.size());
    modifiedOriginal.pCommandBuffers = originalCommandBuffers.data();
    modifiedOriginal.signalSemaphoreCount = 1;
    modifiedOriginal.pSignalSemaphores = &resourceCopySemaphore;

    VkSemaphore copyBackWaitSemaphore = pending.resourceCopySemaphore;
    VkSemaphore copyBackSignalSemaphore = pending.copyBackSemaphore;
    VkPipelineStageFlags copyBackWaitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    uint64_t d3d12CompleteValue = pending.d3d12CompleteValue;
    uint64_t copyBackValue = pending.copyBackValue;

    VkTimelineSemaphoreSubmitInfo copyBackTimeline {};
    copyBackTimeline.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    copyBackTimeline.waitSemaphoreValueCount = 1;
    copyBackTimeline.pWaitSemaphoreValues = &d3d12CompleteValue;
    copyBackTimeline.signalSemaphoreValueCount = 1;
    copyBackTimeline.pSignalSemaphoreValues = &copyBackValue;

    VkSubmitInfo copyBackSubmit {};
    copyBackSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    copyBackSubmit.pNext = &copyBackTimeline;
    copyBackSubmit.waitSemaphoreCount = 1;
    copyBackSubmit.pWaitSemaphores = &copyBackWaitSemaphore;
    copyBackSubmit.pWaitDstStageMask = &copyBackWaitStage;
    copyBackSubmit.commandBufferCount = 1;
    copyBackSubmit.pCommandBuffers = &pending.copyBackCommandBuffer;
    copyBackSubmit.signalSemaphoreCount = 1;
    copyBackSubmit.pSignalSemaphores = &copyBackSignalSemaphore;

    std::vector<VkCommandBuffer> syncCommandBuffers;
    syncCommandBuffers.reserve(1 + original.commandBufferCount - commandBufferIndex - 1);
    syncCommandBuffers.push_back(pending.barrierCommandBuffer);
    for (uint32_t i = commandBufferIndex + 1; i < original.commandBufferCount; i++)
        syncCommandBuffers.push_back(original.pCommandBuffers[i]);

    VkSemaphore syncWaitSemaphore = pending.copyBackSemaphore;
    VkPipelineStageFlags syncWaitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    VkTimelineSemaphoreSubmitInfo syncTimeline {};
    syncTimeline.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    syncTimeline.waitSemaphoreValueCount = 1;
    syncTimeline.pWaitSemaphoreValues = &copyBackValue;
    syncTimeline.signalSemaphoreValueCount = original.signalSemaphoreCount;
    syncTimeline.pSignalSemaphoreValues = originalSignalValues.empty() ? nullptr : originalSignalValues.data();

    VkSubmitInfo syncSubmit {};
    syncSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    syncSubmit.pNext = &syncTimeline;
    syncSubmit.waitSemaphoreCount = 1;
    syncSubmit.pWaitSemaphores = &syncWaitSemaphore;
    syncSubmit.pWaitDstStageMask = &syncWaitStage;
    syncSubmit.commandBufferCount = static_cast<uint32_t>(syncCommandBuffers.size());
    syncSubmit.pCommandBuffers = syncCommandBuffers.data();
    syncSubmit.signalSemaphoreCount = original.signalSemaphoreCount;
    syncSubmit.pSignalSemaphores = original.pSignalSemaphores;

    std::vector<VkSubmitInfo> finalSubmits;
    finalSubmits.reserve(submitCount + 2);
    for (uint32_t i = 0; i < submitCount; i++)
    {
        if (i == submitIndex)
        {
            finalSubmits.push_back(modifiedOriginal);
            finalSubmits.push_back(copyBackSubmit);
            finalSubmits.push_back(syncSubmit);
        }
        else
        {
            finalSubmits.push_back(pSubmits[i]);
        }
    }

    LOG_DEBUG("Injected Vulkan w/Dx12 legacy submit for command buffer {:X}", (size_t) pending.submitCommandBuffer);
    auto result = o_vkQueueSubmit(queue, static_cast<uint32_t>(finalSubmits.size()), finalSubmits.data(), fence);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR("vkQueueSubmit failed after Vulkan w/Dx12 injection: {}", magic_enum::enum_name(result));
        AbortPendingD3D12Wait(pending, "vkQueueSubmit failed");
    }

    return result;
}

template <typename SubmitFn>
static VkResult Submit2WithDx12Interop(SubmitFn originalSubmit, VkQueue queue, uint32_t submitCount,
                                       const VkSubmitInfo2* pSubmits, VkFence fence,
                                       const Vulkan_wDx12::PendingSubmission& pending, uint32_t submitIndex,
                                       uint32_t commandBufferIndex)
{
    const auto& original = pSubmits[submitIndex];

    if (original.pNext != nullptr || original.flags != 0)
    {
        AbortPendingD3D12Wait(pending, original.pNext != nullptr ? "unsupported VkSubmitInfo2 pNext chain"
                                                                 : "non-zero VkSubmitInfo2 flags are unsupported");
        return originalSubmit(queue, submitCount, pSubmits, fence);
    }

    std::vector<VkCommandBufferSubmitInfo> originalCommandBuffers;
    originalCommandBuffers.reserve(commandBufferIndex + 1);
    for (uint32_t i = 0; i <= commandBufferIndex; i++)
        originalCommandBuffers.push_back(original.pCommandBufferInfos[i]);

    VkSemaphoreSubmitInfo resourceCopySignal {};
    resourceCopySignal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    resourceCopySignal.semaphore = pending.resourceCopySemaphore;
    resourceCopySignal.value = pending.resourceCopyValue;
    resourceCopySignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkSubmitInfo2 modifiedOriginal = original;
    modifiedOriginal.commandBufferInfoCount = static_cast<uint32_t>(originalCommandBuffers.size());
    modifiedOriginal.pCommandBufferInfos = originalCommandBuffers.data();
    modifiedOriginal.signalSemaphoreInfoCount = 1;
    modifiedOriginal.pSignalSemaphoreInfos = &resourceCopySignal;

    VkSemaphoreSubmitInfo copyBackWait {};
    copyBackWait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    copyBackWait.semaphore = pending.resourceCopySemaphore;
    copyBackWait.value = pending.d3d12CompleteValue;
    copyBackWait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkCommandBufferSubmitInfo copyBackCommand {};
    copyBackCommand.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    copyBackCommand.commandBuffer = pending.copyBackCommandBuffer;

    VkSemaphoreSubmitInfo copyBackSignal {};
    copyBackSignal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    copyBackSignal.semaphore = pending.copyBackSemaphore;
    copyBackSignal.value = pending.copyBackValue;
    copyBackSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkSubmitInfo2 copyBackSubmit {};
    copyBackSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    copyBackSubmit.flags = original.flags;
    copyBackSubmit.waitSemaphoreInfoCount = 1;
    copyBackSubmit.pWaitSemaphoreInfos = &copyBackWait;
    copyBackSubmit.commandBufferInfoCount = 1;
    copyBackSubmit.pCommandBufferInfos = &copyBackCommand;
    copyBackSubmit.signalSemaphoreInfoCount = 1;
    copyBackSubmit.pSignalSemaphoreInfos = &copyBackSignal;

    std::vector<VkCommandBufferSubmitInfo> syncCommandBuffers;
    syncCommandBuffers.reserve(1 + original.commandBufferInfoCount - commandBufferIndex - 1);

    VkCommandBufferSubmitInfo barrierCommand {};
    barrierCommand.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    barrierCommand.commandBuffer = pending.barrierCommandBuffer;
    syncCommandBuffers.push_back(barrierCommand);

    for (uint32_t i = commandBufferIndex + 1; i < original.commandBufferInfoCount; i++)
        syncCommandBuffers.push_back(original.pCommandBufferInfos[i]);

    VkSemaphoreSubmitInfo syncWait {};
    syncWait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    syncWait.semaphore = pending.copyBackSemaphore;
    syncWait.value = pending.copyBackValue;
    syncWait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    std::vector<VkSemaphoreSubmitInfo> originalSignals;
    originalSignals.reserve(original.signalSemaphoreInfoCount);
    for (uint32_t i = 0; i < original.signalSemaphoreInfoCount; i++)
        originalSignals.push_back(original.pSignalSemaphoreInfos[i]);

    VkSubmitInfo2 syncSubmit {};
    syncSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    syncSubmit.flags = original.flags;
    syncSubmit.waitSemaphoreInfoCount = 1;
    syncSubmit.pWaitSemaphoreInfos = &syncWait;
    syncSubmit.commandBufferInfoCount = static_cast<uint32_t>(syncCommandBuffers.size());
    syncSubmit.pCommandBufferInfos = syncCommandBuffers.data();
    syncSubmit.signalSemaphoreInfoCount = static_cast<uint32_t>(originalSignals.size());
    syncSubmit.pSignalSemaphoreInfos = originalSignals.empty() ? nullptr : originalSignals.data();

    std::vector<VkSubmitInfo2> finalSubmits;
    finalSubmits.reserve(submitCount + 2);
    for (uint32_t i = 0; i < submitCount; i++)
    {
        if (i == submitIndex)
        {
            finalSubmits.push_back(modifiedOriginal);
            finalSubmits.push_back(copyBackSubmit);
            finalSubmits.push_back(syncSubmit);
        }
        else
        {
            finalSubmits.push_back(pSubmits[i]);
        }
    }

    return originalSubmit(queue, static_cast<uint32_t>(finalSubmits.size()), finalSubmits.data(), fence);
}

VkResult Vulkan_wDx12::hk_vkQueueSubmit2(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2* pSubmits,
                                         VkFence fence)
{
    if (pSubmits == nullptr || o_vkQueueSubmit2 == nullptr)
    {
        LOG_ERROR("Invalid parameters to hk_vkQueueSubmit2");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PendingSubmission pending {};
    uint32_t submitIndex = UINT32_MAX;
    uint32_t commandBufferIndex = UINT32_MAX;

    for (uint32_t i = 0; i < submitCount && submitIndex == UINT32_MAX; i++)
    {
        for (uint32_t j = 0; j < pSubmits[i].commandBufferInfoCount; j++)
        {
            if (TakePendingSubmission(pSubmits[i].pCommandBufferInfos[j].commandBuffer, pending))
            {
                submitIndex = i;
                commandBufferIndex = j;
                break;
            }
        }
    }

    if (submitIndex == UINT32_MAX)
        return o_vkQueueSubmit2(queue, submitCount, pSubmits, fence);

    LOG_DEBUG("Injected Vulkan w/Dx12 submit2 for command buffer {:X}", (size_t) pending.submitCommandBuffer);
    auto result = Submit2WithDx12Interop(o_vkQueueSubmit2, queue, submitCount, pSubmits, fence, pending, submitIndex,
                                         commandBufferIndex);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR("vkQueueSubmit2 failed after Vulkan w/Dx12 injection: {}", magic_enum::enum_name(result));
        AbortPendingD3D12Wait(pending, "vkQueueSubmit2 failed");
    }

    return result;
}

VkResult Vulkan_wDx12::hk_vkQueueSubmit2KHR(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2* pSubmits,
                                            VkFence fence)
{
    if (pSubmits == nullptr || o_vkQueueSubmit2KHR == nullptr)
    {
        LOG_ERROR("Invalid parameters to hk_vkQueueSubmit2KHR");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PendingSubmission pending {};
    uint32_t submitIndex = UINT32_MAX;
    uint32_t commandBufferIndex = UINT32_MAX;

    for (uint32_t i = 0; i < submitCount && submitIndex == UINT32_MAX; i++)
    {
        for (uint32_t j = 0; j < pSubmits[i].commandBufferInfoCount; j++)
        {
            if (TakePendingSubmission(pSubmits[i].pCommandBufferInfos[j].commandBuffer, pending))
            {
                submitIndex = i;
                commandBufferIndex = j;
                break;
            }
        }
    }

    if (submitIndex == UINT32_MAX)
        return o_vkQueueSubmit2KHR(queue, submitCount, pSubmits, fence);

    LOG_DEBUG("Injected Vulkan w/Dx12 submit2KHR for command buffer {:X}", (size_t) pending.submitCommandBuffer);
    auto result = Submit2WithDx12Interop(o_vkQueueSubmit2KHR, queue, submitCount, pSubmits, fence, pending, submitIndex,
                                         commandBufferIndex);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR("vkQueueSubmit2KHR failed after Vulkan w/Dx12 injection: {}", magic_enum::enum_name(result));
        AbortPendingD3D12Wait(pending, "vkQueueSubmit2KHR failed");
    }

    return result;
}

VkResult Vulkan_wDx12::hk_vkBeginCommandBuffer(VkCommandBuffer commandBuffer,
                                               const VkCommandBufferBeginInfo* pBeginInfo)
{
    if (GetVirtualCommandBuffer(commandBuffer) == VK_NULL_HANDLE)
        cmdBufferStateTracker.OnBegin(commandBuffer, pBeginInfo);

#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("commandBuffer: {:X}", (size_t) commandBuffer);
#endif

    return o_vkBeginCommandBuffer(commandBuffer, pBeginInfo);
}

VkResult Vulkan_wDx12::hk_vkEndCommandBuffer(VkCommandBuffer commandBuffer)
{
#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("commandBuffer: {:X}", (size_t) commandBuffer);
#endif

    auto virtualCommandBuffer = RemoveVirtualCommandBuffer(commandBuffer);
    if (virtualCommandBuffer == VK_NULL_HANDLE)
    {
        cmdBufferStateTracker.OnEnd(commandBuffer);
    }
    else
    {
        auto result = o_vkEndCommandBuffer(virtualCommandBuffer);
        LOG_DEBUG("Ending virtual command buffer: {:X}, result: {}", (size_t) virtualCommandBuffer,
                  magic_enum::enum_name(result));
    }

    return o_vkEndCommandBuffer(commandBuffer);
}

VkResult Vulkan_wDx12::hk_vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags)
{
#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("commandBuffer: {:X}", (size_t) commandBuffer);
#endif

    if (GetVirtualCommandBuffer(commandBuffer) == VK_NULL_HANDLE)
        cmdBufferStateTracker.OnReset(commandBuffer);

    return o_vkResetCommandBuffer(commandBuffer, flags);
}

void Vulkan_wDx12::hk_vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount,
                                           const VkCommandBuffer* pCommandBuffers)
{
#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("device: {:X}, commandPool: {:X}, commandBufferCount: {}", (size_t) device, (size_t) commandPool,
              commandBufferCount);
#endif

    // Notify state tracker before freeing
    cmdBufferStateTracker.OnFreeCommandBuffers(commandPool, commandBufferCount, pCommandBuffers);

    // Call original function
    o_vkFreeCommandBuffers(device, commandPool, commandBufferCount, pCommandBuffers);
}

VkResult Vulkan_wDx12::hk_vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo,
                                                   VkCommandBuffer* pCommandBuffers)
{
#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("device: {:X}, pCommandBuffers: {:X}", (size_t) device, (size_t) pCommandBuffers);
#endif

    auto result = o_vkAllocateCommandBuffers(device, pAllocateInfo, pCommandBuffers);

    if (result == VK_SUCCESS && pAllocateInfo != nullptr && pCommandBuffers != nullptr)
    {
        std::optional<uint32_t> queueFamily;

        {
            std::scoped_lock lock(mutexCommandPoolToQueueFamilyMap);
            auto it = commandPoolToQueueFamilyMap.find(pAllocateInfo->commandPool);
            if (it != commandPoolToQueueFamilyMap.end())
                queueFamily = it->second;
        }

        if (!queueFamily.has_value())
        {
            LOG_WARN("Queue family is unknown for command pool {:X}; command buffers will remain fail-closed for "
                     "Vulkan w/Dx12 interop",
                     (size_t) pAllocateInfo->commandPool);
        }

        // Track both queue family (when known) and primary/secondary level.
        cmdBufferStateTracker.OnAllocateCommandBuffers(pAllocateInfo->commandPool, pAllocateInfo->commandBufferCount,
                                                       pCommandBuffers, queueFamily, pAllocateInfo->level);
    }

    return result;
}

void Vulkan_wDx12::hk_vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool,
                                           const VkAllocationCallbacks* pAllocator)
{
#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("device: {:X}, commandPool: {:X}", (size_t) device, (size_t) commandPool);
#endif

    // Notify state tracker about pool destruction
    cmdBufferStateTracker.OnDestroyPool(commandPool);

    {
        std::scoped_lock lock(mutexCommandPoolToQueueFamilyMap);
        commandPoolToQueueFamilyMap.erase(commandPool);
    }

    o_vkDestroyCommandPool(device, commandPool, pAllocator);
}

VkResult Vulkan_wDx12::hk_vkResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags)
{
#ifdef LOG_ALL_RECORDS
    LOG_DEBUG("device: {:X}, commandPool: {:X}, flags: {:X}", (size_t) device, (size_t) commandPool, (uint32_t) flags);
#endif

    // Notify state tracker before reset
    cmdBufferStateTracker.OnResetPool(commandPool);

    // Call original function
    return o_vkResetCommandPool(device, commandPool, flags);
}

PFN_vkVoidFunction Vulkan_wDx12::GetDeviceProcAddr(const PFN_vkVoidFunction original, const char* pName)
{
    return GetAddress(original, pName);
}

PFN_vkVoidFunction Vulkan_wDx12::GetInstanceProcAddr(const PFN_vkVoidFunction original, const char* pName)
{
    return GetAddress(original, pName);
}

void Vulkan_wDx12::EndCmdBuffer(VkCommandBuffer commandBuffer) { o_vkEndCommandBuffer(commandBuffer); }

PFN_vkVoidFunction Vulkan_wDx12::GetAddress(const PFN_vkVoidFunction original, const char* pName)
{
    if (original == nullptr || pName == nullptr)
        return VK_NULL_HANDLE;

    auto procName = std::string(pName);

    if (procName == std::string("vkQueueSubmit"))
    {
        // LOG_DEBUG("vkQueueSubmit");

        if (o_vkQueueSubmit == nullptr)
            o_vkQueueSubmit = (PFN_vkQueueSubmit) original;

        return (PFN_vkVoidFunction) hk_vkQueueSubmit;
    }
    if (procName == std::string("vkQueueSubmit2"))
    {
        // LOG_DEBUG("vkQueueSubmit2");

        if (o_vkQueueSubmit2 == nullptr)
            o_vkQueueSubmit2 = (PFN_vkQueueSubmit2) original;

        return (PFN_vkVoidFunction) hk_vkQueueSubmit2;
    }
    if (procName == std::string("vkQueueSubmit2KHR"))
    {
        // LOG_DEBUG("vkQueueSubmit2KHR");

        if (o_vkQueueSubmit2KHR == nullptr)
            o_vkQueueSubmit2KHR = (PFN_vkQueueSubmit2KHR) original;

        return (PFN_vkVoidFunction) hk_vkQueueSubmit2KHR;
    }
    if (procName == std::string("vkBeginCommandBuffer"))
    {
        // LOG_DEBUG("vkBeginCommandBuffer");

        if (o_vkBeginCommandBuffer == nullptr)
            o_vkBeginCommandBuffer = (PFN_vkBeginCommandBuffer) original;

        return (PFN_vkVoidFunction) hk_vkBeginCommandBuffer;
    }
    if (procName == std::string("vkEndCommandBuffer"))
    {
        // LOG_DEBUG("vkEndCommandBuffer");

        if (o_vkEndCommandBuffer == nullptr)
            o_vkEndCommandBuffer = (PFN_vkEndCommandBuffer) original;

        return (PFN_vkVoidFunction) hk_vkEndCommandBuffer;
    }
    if (procName == std::string("vkResetCommandBuffer"))
    {
        // LOG_DEBUG("vkResetCommandBuffer");

        if (o_vkResetCommandBuffer == nullptr)
            o_vkResetCommandBuffer = (PFN_vkResetCommandBuffer) original;

        return (PFN_vkVoidFunction) hk_vkResetCommandBuffer;
    }
    if (procName == std::string("vkCmdExecuteCommands"))
    {
        // LOG_DEBUG("vkCmdExecuteCommands");

        if (o_vkCmdExecuteCommands == nullptr)
            o_vkCmdExecuteCommands = (PFN_vkCmdExecuteCommands) original;

        return (PFN_vkVoidFunction) hk_vkCmdExecuteCommands;
    }
    if (procName == std::string("vkCreateCommandPool"))
    {
        // LOG_DEBUG("vkCreateCommandPool");

        if (o_vkCreateCommandPool == nullptr)
            o_vkCreateCommandPool = (PFN_vkCreateCommandPool) original;

        return (PFN_vkVoidFunction) hk_vkCreateCommandPool;
    }
    if (procName == std::string("vkFreeCommandBuffers"))
    {
        // LOG_DEBUG("vkFreeCommandBuffers");

        if (o_vkFreeCommandBuffers == nullptr)
            o_vkFreeCommandBuffers = (PFN_vkFreeCommandBuffers) original;

        return (PFN_vkVoidFunction) hk_vkFreeCommandBuffers;
    }
    if (procName == std::string("vkResetCommandPool"))
    {
        // LOG_DEBUG("vkResetCommandPool");

        if (o_vkResetCommandPool == nullptr)
            o_vkResetCommandPool = (PFN_vkResetCommandPool) original;

        return (PFN_vkVoidFunction) hk_vkResetCommandPool;
    }
    if (procName == std::string("vkAllocateCommandBuffers"))
    {
        // LOG_DEBUG("vkAllocateCommandBuffers");

        if (o_vkAllocateCommandBuffers == nullptr)
            o_vkAllocateCommandBuffers = (PFN_vkAllocateCommandBuffers) original;

        return (PFN_vkVoidFunction) hk_vkAllocateCommandBuffers;
    }
    if (procName == std::string("vkDestroyCommandPool"))
    {
        // LOG_DEBUG("vkDestroyCommandPool");

        if (o_vkDestroyCommandPool == nullptr)
            o_vkDestroyCommandPool = (PFN_vkDestroyCommandPool) original;

        return (PFN_vkVoidFunction) hk_vkDestroyCommandPool;
    }
    if (procName == std::string("vkCmdBindPipeline"))
    {
        // LOG_DEBUG("vkCmdBindPipeline");

        if (o_vkCmdBindPipeline == nullptr)
            o_vkCmdBindPipeline = (PFN_vkCmdBindPipeline) original;

        return (PFN_vkVoidFunction) hk_vkCmdBindPipeline;
    }
    if (procName == std::string("vkCmdSetViewport"))
    {
        // LOG_DEBUG("vkCmdSetViewport");

        if (o_vkCmdSetViewport == nullptr)
            o_vkCmdSetViewport = (PFN_vkCmdSetViewport) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetViewport;
    }
    if (procName == std::string("vkCmdSetScissor"))
    {
        // LOG_DEBUG("vkCmdSetScissor");

        if (o_vkCmdSetScissor == nullptr)
            o_vkCmdSetScissor = (PFN_vkCmdSetScissor) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetScissor;
    }
    if (procName == std::string("vkCmdSetLineWidth"))
    {
        // LOG_DEBUG("vkCmdSetLineWidth");

        if (o_vkCmdSetLineWidth == nullptr)
            o_vkCmdSetLineWidth = (PFN_vkCmdSetLineWidth) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetLineWidth;
    }
    if (procName == std::string("vkCmdSetDepthBias"))
    {
        // LOG_DEBUG("vkCmdSetDepthBias");

        if (o_vkCmdSetDepthBias == nullptr)
            o_vkCmdSetDepthBias = (PFN_vkCmdSetDepthBias) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDepthBias;
    }
    if (procName == std::string("vkCmdSetBlendConstants"))
    {
        // LOG_DEBUG("vkCmdSetBlendConstants");

        if (o_vkCmdSetBlendConstants == nullptr)
            o_vkCmdSetBlendConstants = (PFN_vkCmdSetBlendConstants) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetBlendConstants;
    }
    if (procName == std::string("vkCmdSetDepthBounds"))
    {
        // LOG_DEBUG("vkCmdSetDepthBounds");

        if (o_vkCmdSetDepthBounds == nullptr)
            o_vkCmdSetDepthBounds = (PFN_vkCmdSetDepthBounds) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDepthBounds;
    }
    if (procName == std::string("vkCmdSetStencilCompareMask"))
    {
        // LOG_DEBUG("vkCmdSetStencilCompareMask");

        if (o_vkCmdSetStencilCompareMask == nullptr)
            o_vkCmdSetStencilCompareMask = (PFN_vkCmdSetStencilCompareMask) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetStencilCompareMask;
    }
    if (procName == std::string("vkCmdSetStencilWriteMask"))
    {
        // LOG_DEBUG("vkCmdSetStencilWriteMask");

        if (o_vkCmdSetStencilWriteMask == nullptr)
            o_vkCmdSetStencilWriteMask = (PFN_vkCmdSetStencilWriteMask) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetStencilWriteMask;
    }
    if (procName == std::string("vkCmdSetStencilReference"))
    {
        // LOG_DEBUG("vkCmdSetStencilReference");

        if (o_vkCmdSetStencilReference == nullptr)
            o_vkCmdSetStencilReference = (PFN_vkCmdSetStencilReference) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetStencilReference;
    }
    if (procName == std::string("vkCmdBindDescriptorSets"))
    {
        // LOG_DEBUG("vkCmdBindDescriptorSets");

        if (o_vkCmdBindDescriptorSets == nullptr)
            o_vkCmdBindDescriptorSets = (PFN_vkCmdBindDescriptorSets) original;

        return (PFN_vkVoidFunction) hk_vkCmdBindDescriptorSets;
    }
    if (procName == std::string("vkCmdBindIndexBuffer"))
    {
        // LOG_DEBUG("vkCmdBindIndexBuffer");

        if (o_vkCmdBindIndexBuffer == nullptr)
            o_vkCmdBindIndexBuffer = (PFN_vkCmdBindIndexBuffer) original;

        return (PFN_vkVoidFunction) hk_vkCmdBindIndexBuffer;
    }
    if (procName == std::string("vkCmdBindVertexBuffers"))
    {
        // LOG_DEBUG("vkCmdBindVertexBuffers");

        if (o_vkCmdBindVertexBuffers == nullptr)
            o_vkCmdBindVertexBuffers = (PFN_vkCmdBindVertexBuffers) original;

        return (PFN_vkVoidFunction) hk_vkCmdBindVertexBuffers;
    }
    if (procName == std::string("vkCmdDraw"))
    {
        // LOG_DEBUG("vkCmdDraw");

        if (o_vkCmdDraw == nullptr)
            o_vkCmdDraw = (PFN_vkCmdDraw) original;

        return (PFN_vkVoidFunction) hk_vkCmdDraw;
    }
    if (procName == std::string("vkCmdDrawIndexed"))
    {
        // LOG_DEBUG("vkCmdDrawIndexed");

        if (o_vkCmdDrawIndexed == nullptr)
            o_vkCmdDrawIndexed = (PFN_vkCmdDrawIndexed) original;

        return (PFN_vkVoidFunction) hk_vkCmdDrawIndexed;
    }
    if (procName == std::string("vkCmdDrawIndirect"))
    {
        // LOG_DEBUG("vkCmdDrawIndirect");

        if (o_vkCmdDrawIndirect == nullptr)
            o_vkCmdDrawIndirect = (PFN_vkCmdDrawIndirect) original;

        return (PFN_vkVoidFunction) hk_vkCmdDrawIndirect;
    }
    if (procName == std::string("vkCmdDrawIndexedIndirect"))
    {
        // LOG_DEBUG("vkCmdDrawIndexedIndirect");

        if (o_vkCmdDrawIndexedIndirect == nullptr)
            o_vkCmdDrawIndexedIndirect = (PFN_vkCmdDrawIndexedIndirect) original;

        return (PFN_vkVoidFunction) hk_vkCmdDrawIndexedIndirect;
    }
    if (procName == std::string("vkCmdDispatch"))
    {
        // LOG_DEBUG("vkCmdDispatch");

        if (o_vkCmdDispatch == nullptr)
            o_vkCmdDispatch = (PFN_vkCmdDispatch) original;

        return (PFN_vkVoidFunction) hk_vkCmdDispatch;
    }
    if (procName == std::string("vkCmdDispatchIndirect"))
    {
        // LOG_DEBUG("vkCmdDispatchIndirect");

        if (o_vkCmdDispatchIndirect == nullptr)
            o_vkCmdDispatchIndirect = (PFN_vkCmdDispatchIndirect) original;

        return (PFN_vkVoidFunction) hk_vkCmdDispatchIndirect;
    }
    if (procName == std::string("vkCmdCopyBuffer"))
    {
        // LOG_DEBUG("vkCmdCopyBuffer");

        if (o_vkCmdCopyBuffer == nullptr)
            o_vkCmdCopyBuffer = (PFN_vkCmdCopyBuffer) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyBuffer;
    }
    if (procName == std::string("vkCmdCopyImage"))
    {
        // LOG_DEBUG("vkCmdCopyImage");

        if (o_vkCmdCopyImage == nullptr)
            o_vkCmdCopyImage = (PFN_vkCmdCopyImage) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyImage;
    }
    if (procName == std::string("vkCmdBlitImage"))
    {
        // LOG_DEBUG("vkCmdBlitImage");

        if (o_vkCmdBlitImage == nullptr)
            o_vkCmdBlitImage = (PFN_vkCmdBlitImage) original;

        return (PFN_vkVoidFunction) hk_vkCmdBlitImage;
    }
    if (procName == std::string("vkCmdCopyBufferToImage"))
    {
        // LOG_DEBUG("vkCmdCopyBufferToImage");

        if (o_vkCmdCopyBufferToImage == nullptr)
            o_vkCmdCopyBufferToImage = (PFN_vkCmdCopyBufferToImage) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyBufferToImage;
    }
    if (procName == std::string("vkCmdCopyImageToBuffer"))
    {
        // LOG_DEBUG("vkCmdCopyImageToBuffer");

        if (o_vkCmdCopyImageToBuffer == nullptr)
            o_vkCmdCopyImageToBuffer = (PFN_vkCmdCopyImageToBuffer) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyImageToBuffer;
    }
    if (procName == std::string("vkCmdUpdateBuffer"))
    {
        // LOG_DEBUG("vkCmdUpdateBuffer");

        if (o_vkCmdUpdateBuffer == nullptr)
            o_vkCmdUpdateBuffer = (PFN_vkCmdUpdateBuffer) original;

        return (PFN_vkVoidFunction) hk_vkCmdUpdateBuffer;
    }
    if (procName == std::string("vkCmdFillBuffer"))
    {
        // LOG_DEBUG("vkCmdFillBuffer");

        if (o_vkCmdFillBuffer == nullptr)
            o_vkCmdFillBuffer = (PFN_vkCmdFillBuffer) original;

        return (PFN_vkVoidFunction) hk_vkCmdFillBuffer;
    }
    if (procName == std::string("vkCmdClearColorImage"))
    {
        // LOG_DEBUG("vkCmdClearColorImage");

        if (o_vkCmdClearColorImage == nullptr)
            o_vkCmdClearColorImage = (PFN_vkCmdClearColorImage) original;

        return (PFN_vkVoidFunction) hk_vkCmdClearColorImage;
    }
    if (procName == std::string("vkCmdClearDepthStencilImage"))
    {
        // LOG_DEBUG("vkCmdClearDepthStencilImage");

        if (o_vkCmdClearDepthStencilImage == nullptr)
            o_vkCmdClearDepthStencilImage = (PFN_vkCmdClearDepthStencilImage) original;

        return (PFN_vkVoidFunction) hk_vkCmdClearDepthStencilImage;
    }
    if (procName == std::string("vkCmdClearAttachments"))
    {
        // LOG_DEBUG("vkCmdClearAttachments");

        if (o_vkCmdClearAttachments == nullptr)
            o_vkCmdClearAttachments = (PFN_vkCmdClearAttachments) original;

        return (PFN_vkVoidFunction) hk_vkCmdClearAttachments;
    }
    if (procName == std::string("vkCmdResolveImage"))
    {
        // LOG_DEBUG("vkCmdResolveImage");

        if (o_vkCmdResolveImage == nullptr)
            o_vkCmdResolveImage = (PFN_vkCmdResolveImage) original;

        return (PFN_vkVoidFunction) hk_vkCmdResolveImage;
    }
    if (procName == std::string("vkCmdSetEvent"))
    {
        // LOG_DEBUG("vkCmdSetEvent");

        if (o_vkCmdSetEvent == nullptr)
            o_vkCmdSetEvent = (PFN_vkCmdSetEvent) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetEvent;
    }
    if (procName == std::string("vkCmdResetEvent"))
    {
        // LOG_DEBUG("vkCmdResetEvent");

        if (o_vkCmdResetEvent == nullptr)
            o_vkCmdResetEvent = (PFN_vkCmdResetEvent) original;

        return (PFN_vkVoidFunction) hk_vkCmdResetEvent;
    }
    if (procName == std::string("vkCmdWaitEvents"))
    {
        // LOG_DEBUG("vkCmdWaitEvents");

        if (o_vkCmdWaitEvents == nullptr)
            o_vkCmdWaitEvents = (PFN_vkCmdWaitEvents) original;

        return (PFN_vkVoidFunction) hk_vkCmdWaitEvents;
    }
    if (procName == std::string("vkCmdPipelineBarrier"))
    {
        // LOG_DEBUG("vkCmdPipelineBarrier");

        if (o_vkCmdPipelineBarrier == nullptr)
            o_vkCmdPipelineBarrier = (PFN_vkCmdPipelineBarrier) original;

        return (PFN_vkVoidFunction) hk_vkCmdPipelineBarrier;
    }
    if (procName == std::string("vkCmdBeginQuery"))
    {
        // LOG_DEBUG("vkCmdBeginQuery");

        if (o_vkCmdBeginQuery == nullptr)
            o_vkCmdBeginQuery = (PFN_vkCmdBeginQuery) original;

        return (PFN_vkVoidFunction) hk_vkCmdBeginQuery;
    }
    if (procName == std::string("vkCmdEndQuery"))
    {
        // LOG_DEBUG("vkCmdEndQuery");

        if (o_vkCmdEndQuery == nullptr)
            o_vkCmdEndQuery = (PFN_vkCmdEndQuery) original;

        return (PFN_vkVoidFunction) hk_vkCmdEndQuery;
    }
    if (procName == std::string("vkCmdResetQueryPool"))
    {
        // LOG_DEBUG("vkCmdResetQueryPool");

        if (o_vkCmdResetQueryPool == nullptr)
            o_vkCmdResetQueryPool = (PFN_vkCmdResetQueryPool) original;

        return (PFN_vkVoidFunction) hk_vkCmdResetQueryPool;
    }
    if (procName == std::string("vkCmdWriteTimestamp"))
    {
        // LOG_DEBUG("vkCmdWriteTimestamp");

        if (o_vkCmdWriteTimestamp == nullptr)
            o_vkCmdWriteTimestamp = (PFN_vkCmdWriteTimestamp) original;

        return (PFN_vkVoidFunction) hk_vkCmdWriteTimestamp;
    }
    if (procName == std::string("vkCmdCopyQueryPoolResults"))
    {
        // LOG_DEBUG("vkCmdCopyQueryPoolResults");

        if (o_vkCmdCopyQueryPoolResults == nullptr)
            o_vkCmdCopyQueryPoolResults = (PFN_vkCmdCopyQueryPoolResults) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyQueryPoolResults;
    }
    if (procName == std::string("vkCmdPushConstants"))
    {
        // LOG_DEBUG("vkCmdPushConstants");

        if (o_vkCmdPushConstants == nullptr)
            o_vkCmdPushConstants = (PFN_vkCmdPushConstants) original;

        return (PFN_vkVoidFunction) hk_vkCmdPushConstants;
    }
    if (procName == std::string("vkCmdBeginRenderPass"))
    {
        // LOG_DEBUG("vkCmdBeginRenderPass");

        if (o_vkCmdBeginRenderPass == nullptr)
            o_vkCmdBeginRenderPass = (PFN_vkCmdBeginRenderPass) original;

        return (PFN_vkVoidFunction) hk_vkCmdBeginRenderPass;
    }
    if (procName == std::string("vkCmdNextSubpass"))
    {
        // LOG_DEBUG("vkCmdNextSubpass");

        if (o_vkCmdNextSubpass == nullptr)
            o_vkCmdNextSubpass = (PFN_vkCmdNextSubpass) original;

        return (PFN_vkVoidFunction) hk_vkCmdNextSubpass;
    }
    if (procName == std::string("vkCmdEndRenderPass"))
    {
        // LOG_DEBUG("vkCmdEndRenderPass");

        if (o_vkCmdEndRenderPass == nullptr)
            o_vkCmdEndRenderPass = (PFN_vkCmdEndRenderPass) original;

        return (PFN_vkVoidFunction) hk_vkCmdEndRenderPass;
    }
    if (procName == std::string("vkCmdSetDeviceMask"))
    {
        // LOG_DEBUG("vkCmdSetDeviceMask");

        if (o_vkCmdSetDeviceMask == nullptr)
            o_vkCmdSetDeviceMask = (PFN_vkCmdSetDeviceMask) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDeviceMask;
    }
    if (procName == std::string("vkCmdDispatchBase"))
    {
        // LOG_DEBUG("vkCmdDispatchBase");

        if (o_vkCmdDispatchBase == nullptr)
            o_vkCmdDispatchBase = (PFN_vkCmdDispatchBase) original;

        return (PFN_vkVoidFunction) hk_vkCmdDispatchBase;
    }
    if (procName == std::string("vkCmdDrawIndirectCount"))
    {
        // LOG_DEBUG("vkCmdDrawIndirectCount");

        if (o_vkCmdDrawIndirectCount == nullptr)
            o_vkCmdDrawIndirectCount = (PFN_vkCmdDrawIndirectCount) original;

        return (PFN_vkVoidFunction) hk_vkCmdDrawIndirectCount;
    }
    if (procName == std::string("vkCmdDrawIndexedIndirectCount"))
    {
        // LOG_DEBUG("vkCmdDrawIndexedIndirectCount");

        if (o_vkCmdDrawIndexedIndirectCount == nullptr)
            o_vkCmdDrawIndexedIndirectCount = (PFN_vkCmdDrawIndexedIndirectCount) original;

        return (PFN_vkVoidFunction) hk_vkCmdDrawIndexedIndirectCount;
    }
    if (procName == std::string("vkCmdBeginRenderPass2"))
    {
        // LOG_DEBUG("vkCmdBeginRenderPass2");

        if (o_vkCmdBeginRenderPass2 == nullptr)
            o_vkCmdBeginRenderPass2 = (PFN_vkCmdBeginRenderPass2) original;

        return (PFN_vkVoidFunction) hk_vkCmdBeginRenderPass2;
    }
    if (procName == std::string("vkCmdNextSubpass2"))
    {
        // LOG_DEBUG("vkCmdNextSubpass2");

        if (o_vkCmdNextSubpass2 == nullptr)
            o_vkCmdNextSubpass2 = (PFN_vkCmdNextSubpass2) original;

        return (PFN_vkVoidFunction) hk_vkCmdNextSubpass2;
    }
    if (procName == std::string("vkCmdEndRenderPass2"))
    {
        // LOG_DEBUG("vkCmdEndRenderPass2");

        if (o_vkCmdEndRenderPass2 == nullptr)
            o_vkCmdEndRenderPass2 = (PFN_vkCmdEndRenderPass2) original;

        return (PFN_vkVoidFunction) hk_vkCmdEndRenderPass2;
    }
    if (procName == std::string("vkCmdSetEvent2"))
    {
        // LOG_DEBUG("vkCmdSetEvent2");

        if (o_vkCmdSetEvent2 == nullptr)
            o_vkCmdSetEvent2 = (PFN_vkCmdSetEvent2) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetEvent2;
    }
    if (procName == std::string("vkCmdResetEvent2"))
    {
        // LOG_DEBUG("vkCmdResetEvent2");

        if (o_vkCmdResetEvent2 == nullptr)
            o_vkCmdResetEvent2 = (PFN_vkCmdResetEvent2) original;

        return (PFN_vkVoidFunction) hk_vkCmdResetEvent2;
    }
    if (procName == std::string("vkCmdWaitEvents2"))
    {
        // LOG_DEBUG("vkCmdWaitEvents2");

        if (o_vkCmdWaitEvents2 == nullptr)
            o_vkCmdWaitEvents2 = (PFN_vkCmdWaitEvents2) original;

        return (PFN_vkVoidFunction) hk_vkCmdWaitEvents2;
    }
    if (procName == std::string("vkCmdPipelineBarrier2"))
    {
        // LOG_DEBUG("vkCmdPipelineBarrier2");

        if (o_vkCmdPipelineBarrier2 == nullptr)
            o_vkCmdPipelineBarrier2 = (PFN_vkCmdPipelineBarrier2) original;

        return (PFN_vkVoidFunction) hk_vkCmdPipelineBarrier2;
    }
    if (procName == std::string("vkCmdWriteTimestamp2"))
    {
        // LOG_DEBUG("vkCmdWriteTimestamp2");

        if (o_vkCmdWriteTimestamp2 == nullptr)
            o_vkCmdWriteTimestamp2 = (PFN_vkCmdWriteTimestamp2) original;

        return (PFN_vkVoidFunction) hk_vkCmdWriteTimestamp2;
    }
    if (procName == std::string("vkCmdCopyBuffer2"))
    {
        // LOG_DEBUG("vkCmdCopyBuffer2");

        if (o_vkCmdCopyBuffer2 == nullptr)
            o_vkCmdCopyBuffer2 = (PFN_vkCmdCopyBuffer2) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyBuffer2;
    }
    if (procName == std::string("vkCmdCopyImage2"))
    {
        // LOG_DEBUG("vkCmdCopyImage2");

        if (o_vkCmdCopyImage2 == nullptr)
            o_vkCmdCopyImage2 = (PFN_vkCmdCopyImage2) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyImage2;
    }
    if (procName == std::string("vkCmdCopyBufferToImage2"))
    {
        // LOG_DEBUG("vkCmdCopyBufferToImage2");

        if (o_vkCmdCopyBufferToImage2 == nullptr)
            o_vkCmdCopyBufferToImage2 = (PFN_vkCmdCopyBufferToImage2) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyBufferToImage2;
    }
    if (procName == std::string("vkCmdCopyImageToBuffer2"))
    {
        // LOG_DEBUG("vkCmdCopyImageToBuffer2");

        if (o_vkCmdCopyImageToBuffer2 == nullptr)
            o_vkCmdCopyImageToBuffer2 = (PFN_vkCmdCopyImageToBuffer2) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyImageToBuffer2;
    }
    if (procName == std::string("vkCmdBlitImage2"))
    {
        // LOG_DEBUG("vkCmdBlitImage2");

        if (o_vkCmdBlitImage2 == nullptr)
            o_vkCmdBlitImage2 = (PFN_vkCmdBlitImage2) original;

        return (PFN_vkVoidFunction) hk_vkCmdBlitImage2;
    }
    if (procName == std::string("vkCmdResolveImage2"))
    {
        // LOG_DEBUG("vkCmdResolveImage2");

        if (o_vkCmdResolveImage2 == nullptr)
            o_vkCmdResolveImage2 = (PFN_vkCmdResolveImage2) original;

        return (PFN_vkVoidFunction) hk_vkCmdResolveImage2;
    }
    if (procName == std::string("vkCmdBeginRendering"))
    {
        // LOG_DEBUG("vkCmdBeginRendering");

        if (o_vkCmdBeginRendering == nullptr)
            o_vkCmdBeginRendering = (PFN_vkCmdBeginRendering) original;

        return (PFN_vkVoidFunction) hk_vkCmdBeginRendering;
    }
    if (procName == std::string("vkCmdEndRendering"))
    {
        // LOG_DEBUG("vkCmdEndRendering");

        if (o_vkCmdEndRendering == nullptr)
            o_vkCmdEndRendering = (PFN_vkCmdEndRendering) original;

        return (PFN_vkVoidFunction) hk_vkCmdEndRendering;
    }
    if (procName == std::string("vkCmdSetCullMode"))
    {
        // LOG_DEBUG("vkCmdSetCullMode");

        if (o_vkCmdSetCullMode == nullptr)
            o_vkCmdSetCullMode = (PFN_vkCmdSetCullMode) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetCullMode;
    }
    if (procName == std::string("vkCmdSetFrontFace"))
    {
        // LOG_DEBUG("vkCmdSetFrontFace");

        if (o_vkCmdSetFrontFace == nullptr)
            o_vkCmdSetFrontFace = (PFN_vkCmdSetFrontFace) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetFrontFace;
    }
    if (procName == std::string("vkCmdSetPrimitiveTopology"))
    {
        // LOG_DEBUG("vkCmdSetPrimitiveTopology");

        if (o_vkCmdSetPrimitiveTopology == nullptr)
            o_vkCmdSetPrimitiveTopology = (PFN_vkCmdSetPrimitiveTopology) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetPrimitiveTopology;
    }
    if (procName == std::string("vkCmdSetViewportWithCount"))
    {
        // LOG_DEBUG("vkCmdSetViewportWithCount");

        if (o_vkCmdSetViewportWithCount == nullptr)
            o_vkCmdSetViewportWithCount = (PFN_vkCmdSetViewportWithCount) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetViewportWithCount;
    }
    if (procName == std::string("vkCmdSetScissorWithCount"))
    {
        // LOG_DEBUG("vkCmdSetScissorWithCount");

        if (o_vkCmdSetScissorWithCount == nullptr)
            o_vkCmdSetScissorWithCount = (PFN_vkCmdSetScissorWithCount) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetScissorWithCount;
    }
    if (procName == std::string("vkCmdBindVertexBuffers2"))
    {
        // LOG_DEBUG("vkCmdBindVertexBuffers2");

        if (o_vkCmdBindVertexBuffers2 == nullptr)
            o_vkCmdBindVertexBuffers2 = (PFN_vkCmdBindVertexBuffers2) original;

        return (PFN_vkVoidFunction) hk_vkCmdBindVertexBuffers2;
    }
    if (procName == std::string("vkCmdSetDepthTestEnable"))
    {
        // LOG_DEBUG("vkCmdSetDepthTestEnable");

        if (o_vkCmdSetDepthTestEnable == nullptr)
            o_vkCmdSetDepthTestEnable = (PFN_vkCmdSetDepthTestEnable) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDepthTestEnable;
    }
    if (procName == std::string("vkCmdSetDepthWriteEnable"))
    {
        // LOG_DEBUG("vkCmdSetDepthWriteEnable");

        if (o_vkCmdSetDepthWriteEnable == nullptr)
            o_vkCmdSetDepthWriteEnable = (PFN_vkCmdSetDepthWriteEnable) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDepthWriteEnable;
    }
    if (procName == std::string("vkCmdSetDepthCompareOp"))
    {
        // LOG_DEBUG("vkCmdSetDepthCompareOp");

        if (o_vkCmdSetDepthCompareOp == nullptr)
            o_vkCmdSetDepthCompareOp = (PFN_vkCmdSetDepthCompareOp) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDepthCompareOp;
    }
    if (procName == std::string("vkCmdSetDepthBoundsTestEnable"))
    {
        // LOG_DEBUG("vkCmdSetDepthBoundsTestEnable");

        if (o_vkCmdSetDepthBoundsTestEnable == nullptr)
            o_vkCmdSetDepthBoundsTestEnable = (PFN_vkCmdSetDepthBoundsTestEnable) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDepthBoundsTestEnable;
    }
    if (procName == std::string("vkCmdSetStencilTestEnable"))
    {
        // LOG_DEBUG("vkCmdSetStencilTestEnable");

        if (o_vkCmdSetStencilTestEnable == nullptr)
            o_vkCmdSetStencilTestEnable = (PFN_vkCmdSetStencilTestEnable) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetStencilTestEnable;
    }
    if (procName == std::string("vkCmdSetStencilOp"))
    {
        // LOG_DEBUG("vkCmdSetStencilOp");

        if (o_vkCmdSetStencilOp == nullptr)
            o_vkCmdSetStencilOp = (PFN_vkCmdSetStencilOp) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetStencilOp;
    }
    if (procName == std::string("vkCmdSetRasterizerDiscardEnable"))
    {
        // LOG_DEBUG("vkCmdSetRasterizerDiscardEnable");

        if (o_vkCmdSetRasterizerDiscardEnable == nullptr)
            o_vkCmdSetRasterizerDiscardEnable = (PFN_vkCmdSetRasterizerDiscardEnable) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetRasterizerDiscardEnable;
    }
    if (procName == std::string("vkCmdSetDepthBiasEnable"))
    {
        // LOG_DEBUG("vkCmdSetDepthBiasEnable");

        if (o_vkCmdSetDepthBiasEnable == nullptr)
            o_vkCmdSetDepthBiasEnable = (PFN_vkCmdSetDepthBiasEnable) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDepthBiasEnable;
    }
    if (procName == std::string("vkCmdSetPrimitiveRestartEnable"))
    {
        // LOG_DEBUG("vkCmdSetPrimitiveRestartEnable");

        if (o_vkCmdSetPrimitiveRestartEnable == nullptr)
            o_vkCmdSetPrimitiveRestartEnable = (PFN_vkCmdSetPrimitiveRestartEnable) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetPrimitiveRestartEnable;
    }
    if (procName == std::string("vkCmdSetLineStipple"))
    {
        // LOG_DEBUG("vkCmdSetLineStipple");

        if (o_vkCmdSetLineStipple == nullptr)
            o_vkCmdSetLineStipple = (PFN_vkCmdSetLineStipple) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetLineStipple;
    }
    if (procName == std::string("vkCmdBindIndexBuffer2"))
    {
        // LOG_DEBUG("vkCmdBindIndexBuffer2");

        if (o_vkCmdBindIndexBuffer2 == nullptr)
            o_vkCmdBindIndexBuffer2 = (PFN_vkCmdBindIndexBuffer2) original;

        return (PFN_vkVoidFunction) hk_vkCmdBindIndexBuffer2;
    }
    if (procName == std::string("vkCmdPushDescriptorSet"))
    {
        // LOG_DEBUG("vkCmdPushDescriptorSet");

        if (o_vkCmdPushDescriptorSet == nullptr)
            o_vkCmdPushDescriptorSet = (PFN_vkCmdPushDescriptorSet) original;

        return (PFN_vkVoidFunction) hk_vkCmdPushDescriptorSet;
    }
    if (procName == std::string("vkCmdPushDescriptorSetWithTemplate"))
    {
        // LOG_DEBUG("vkCmdPushDescriptorSetWithTemplate");

        if (o_vkCmdPushDescriptorSetWithTemplate == nullptr)
            o_vkCmdPushDescriptorSetWithTemplate = (PFN_vkCmdPushDescriptorSetWithTemplate) original;

        return (PFN_vkVoidFunction) hk_vkCmdPushDescriptorSetWithTemplate;
    }
    if (procName == std::string("vkCmdSetRenderingAttachmentLocations"))
    {
        // LOG_DEBUG("vkCmdSetRenderingAttachmentLocations");

        if (o_vkCmdSetRenderingAttachmentLocations == nullptr)
            o_vkCmdSetRenderingAttachmentLocations = (PFN_vkCmdSetRenderingAttachmentLocations) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetRenderingAttachmentLocations;
    }
    if (procName == std::string("vkCmdSetRenderingInputAttachmentIndices"))
    {
        // LOG_DEBUG("vkCmdSetRenderingInputAttachmentIndices");

        if (o_vkCmdSetRenderingInputAttachmentIndices == nullptr)
            o_vkCmdSetRenderingInputAttachmentIndices = (PFN_vkCmdSetRenderingInputAttachmentIndices) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetRenderingInputAttachmentIndices;
    }
    if (procName == std::string("vkCmdBindDescriptorSets2"))
    {
        // LOG_DEBUG("vkCmdBindDescriptorSets2");

        if (o_vkCmdBindDescriptorSets2 == nullptr)
            o_vkCmdBindDescriptorSets2 = (PFN_vkCmdBindDescriptorSets2) original;

        return (PFN_vkVoidFunction) hk_vkCmdBindDescriptorSets2;
    }
    if (procName == std::string("vkCmdPushConstants2"))
    {
        // LOG_DEBUG("vkCmdPushConstants2");

        if (o_vkCmdPushConstants2 == nullptr)
            o_vkCmdPushConstants2 = (PFN_vkCmdPushConstants2) original;

        return (PFN_vkVoidFunction) hk_vkCmdPushConstants2;
    }
    if (procName == std::string("vkCmdPushDescriptorSet2"))
    {
        // LOG_DEBUG("vkCmdPushDescriptorSet2");

        if (o_vkCmdPushDescriptorSet2 == nullptr)
            o_vkCmdPushDescriptorSet2 = (PFN_vkCmdPushDescriptorSet2) original;

        return (PFN_vkVoidFunction) hk_vkCmdPushDescriptorSet2;
    }
    if (procName == std::string("vkCmdPushDescriptorSetWithTemplate2"))
    {
        // LOG_DEBUG("vkCmdPushDescriptorSetWithTemplate2");

        if (o_vkCmdPushDescriptorSetWithTemplate2 == nullptr)
            o_vkCmdPushDescriptorSetWithTemplate2 = (PFN_vkCmdPushDescriptorSetWithTemplate2) original;

        return (PFN_vkVoidFunction) hk_vkCmdPushDescriptorSetWithTemplate2;
    }
    if (procName == std::string("vkCmdBeginVideoCodingKHR"))
    {
        // LOG_DEBUG("vkCmdBeginVideoCodingKHR");

        if (o_vkCmdBeginVideoCodingKHR == nullptr)
            o_vkCmdBeginVideoCodingKHR = (PFN_vkCmdBeginVideoCodingKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdBeginVideoCodingKHR;
    }
    if (procName == std::string("vkCmdEndVideoCodingKHR"))
    {
        // LOG_DEBUG("vkCmdEndVideoCodingKHR");

        if (o_vkCmdEndVideoCodingKHR == nullptr)
            o_vkCmdEndVideoCodingKHR = (PFN_vkCmdEndVideoCodingKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdEndVideoCodingKHR;
    }
    if (procName == std::string("vkCmdControlVideoCodingKHR"))
    {
        // LOG_DEBUG("vkCmdControlVideoCodingKHR");

        if (o_vkCmdControlVideoCodingKHR == nullptr)
            o_vkCmdControlVideoCodingKHR = (PFN_vkCmdControlVideoCodingKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdControlVideoCodingKHR;
    }
    if (procName == std::string("vkCmdDecodeVideoKHR"))
    {
        // LOG_DEBUG("vkCmdDecodeVideoKHR");

        if (o_vkCmdDecodeVideoKHR == nullptr)
            o_vkCmdDecodeVideoKHR = (PFN_vkCmdDecodeVideoKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdDecodeVideoKHR;
    }
    if (procName == std::string("vkCmdBeginRenderingKHR"))
    {
        // LOG_DEBUG("vkCmdBeginRenderingKHR");

        if (o_vkCmdBeginRenderingKHR == nullptr)
            o_vkCmdBeginRenderingKHR = (PFN_vkCmdBeginRenderingKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdBeginRenderingKHR;
    }
    if (procName == std::string("vkCmdEndRenderingKHR"))
    {
        // LOG_DEBUG("vkCmdEndRenderingKHR");

        if (o_vkCmdEndRenderingKHR == nullptr)
            o_vkCmdEndRenderingKHR = (PFN_vkCmdEndRenderingKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdEndRenderingKHR;
    }
    if (procName == std::string("vkCmdSetDeviceMaskKHR"))
    {
        // LOG_DEBUG("vkCmdSetDeviceMaskKHR");

        if (o_vkCmdSetDeviceMaskKHR == nullptr)
            o_vkCmdSetDeviceMaskKHR = (PFN_vkCmdSetDeviceMaskKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDeviceMaskKHR;
    }
    if (procName == std::string("vkCmdDispatchBaseKHR"))
    {
        // LOG_DEBUG("vkCmdDispatchBaseKHR");

        if (o_vkCmdDispatchBaseKHR == nullptr)
            o_vkCmdDispatchBaseKHR = (PFN_vkCmdDispatchBaseKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdDispatchBaseKHR;
    }
    if (procName == std::string("vkCmdPushDescriptorSetKHR"))
    {
        // LOG_DEBUG("vkCmdPushDescriptorSetKHR");

        if (o_vkCmdPushDescriptorSetKHR == nullptr)
            o_vkCmdPushDescriptorSetKHR = (PFN_vkCmdPushDescriptorSetKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdPushDescriptorSetKHR;
    }
    if (procName == std::string("vkCmdPushDescriptorSetWithTemplateKHR"))
    {
        // LOG_DEBUG("vkCmdPushDescriptorSetWithTemplateKHR");

        if (o_vkCmdPushDescriptorSetWithTemplateKHR == nullptr)
            o_vkCmdPushDescriptorSetWithTemplateKHR = (PFN_vkCmdPushDescriptorSetWithTemplateKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdPushDescriptorSetWithTemplateKHR;
    }
    if (procName == std::string("vkCmdBeginRenderPass2KHR"))
    {
        // LOG_DEBUG("vkCmdBeginRenderPass2KHR");

        if (o_vkCmdBeginRenderPass2KHR == nullptr)
            o_vkCmdBeginRenderPass2KHR = (PFN_vkCmdBeginRenderPass2KHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdBeginRenderPass2KHR;
    }
    if (procName == std::string("vkCmdNextSubpass2KHR"))
    {
        // LOG_DEBUG("vkCmdNextSubpass2KHR");

        if (o_vkCmdNextSubpass2KHR == nullptr)
            o_vkCmdNextSubpass2KHR = (PFN_vkCmdNextSubpass2KHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdNextSubpass2KHR;
    }
    if (procName == std::string("vkCmdEndRenderPass2KHR"))
    {
        // LOG_DEBUG("vkCmdEndRenderPass2KHR");

        if (o_vkCmdEndRenderPass2KHR == nullptr)
            o_vkCmdEndRenderPass2KHR = (PFN_vkCmdEndRenderPass2KHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdEndRenderPass2KHR;
    }
    if (procName == std::string("vkCmdDrawIndirectCountKHR"))
    {
        // LOG_DEBUG("vkCmdDrawIndirectCountKHR");

        if (o_vkCmdDrawIndirectCountKHR == nullptr)
            o_vkCmdDrawIndirectCountKHR = (PFN_vkCmdDrawIndirectCountKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdDrawIndirectCountKHR;
    }
    if (procName == std::string("vkCmdDrawIndexedIndirectCountKHR"))
    {
        // LOG_DEBUG("vkCmdDrawIndexedIndirectCountKHR");

        if (o_vkCmdDrawIndexedIndirectCountKHR == nullptr)
            o_vkCmdDrawIndexedIndirectCountKHR = (PFN_vkCmdDrawIndexedIndirectCountKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdDrawIndexedIndirectCountKHR;
    }
    if (procName == std::string("vkCmdSetFragmentShadingRateKHR"))
    {
        // LOG_DEBUG("vkCmdSetFragmentShadingRateKHR");

        if (o_vkCmdSetFragmentShadingRateKHR == nullptr)
            o_vkCmdSetFragmentShadingRateKHR = (PFN_vkCmdSetFragmentShadingRateKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetFragmentShadingRateKHR;
    }
    if (procName == std::string("vkCmdSetRenderingAttachmentLocationsKHR"))
    {
        // LOG_DEBUG("vkCmdSetRenderingAttachmentLocationsKHR");

        if (o_vkCmdSetRenderingAttachmentLocationsKHR == nullptr)
            o_vkCmdSetRenderingAttachmentLocationsKHR = (PFN_vkCmdSetRenderingAttachmentLocationsKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetRenderingAttachmentLocationsKHR;
    }
    if (procName == std::string("vkCmdSetRenderingInputAttachmentIndicesKHR"))
    {
        // LOG_DEBUG("vkCmdSetRenderingInputAttachmentIndicesKHR");

        if (o_vkCmdSetRenderingInputAttachmentIndicesKHR == nullptr)
            o_vkCmdSetRenderingInputAttachmentIndicesKHR = (PFN_vkCmdSetRenderingInputAttachmentIndicesKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetRenderingInputAttachmentIndicesKHR;
    }
    if (procName == std::string("vkCmdEncodeVideoKHR"))
    {
        // LOG_DEBUG("vkCmdEncodeVideoKHR");

        if (o_vkCmdEncodeVideoKHR == nullptr)
            o_vkCmdEncodeVideoKHR = (PFN_vkCmdEncodeVideoKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdEncodeVideoKHR;
    }
    if (procName == std::string("vkCmdSetEvent2KHR"))
    {
        // LOG_DEBUG("vkCmdSetEvent2KHR");

        if (o_vkCmdSetEvent2KHR == nullptr)
            o_vkCmdSetEvent2KHR = (PFN_vkCmdSetEvent2KHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetEvent2KHR;
    }
    if (procName == std::string("vkCmdResetEvent2KHR"))
    {
        // LOG_DEBUG("vkCmdResetEvent2KHR");

        if (o_vkCmdResetEvent2KHR == nullptr)
            o_vkCmdResetEvent2KHR = (PFN_vkCmdResetEvent2KHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdResetEvent2KHR;
    }
    if (procName == std::string("vkCmdWaitEvents2KHR"))
    {
        // LOG_DEBUG("vkCmdWaitEvents2KHR");

        if (o_vkCmdWaitEvents2KHR == nullptr)
            o_vkCmdWaitEvents2KHR = (PFN_vkCmdWaitEvents2KHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdWaitEvents2KHR;
    }
    if (procName == std::string("vkCmdPipelineBarrier2KHR"))
    {
        // LOG_DEBUG("vkCmdPipelineBarrier2KHR");

        if (o_vkCmdPipelineBarrier2KHR == nullptr)
            o_vkCmdPipelineBarrier2KHR = (PFN_vkCmdPipelineBarrier2KHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdPipelineBarrier2KHR;
    }
    if (procName == std::string("vkCmdWriteTimestamp2KHR"))
    {
        // LOG_DEBUG("vkCmdWriteTimestamp2KHR");

        if (o_vkCmdWriteTimestamp2KHR == nullptr)
            o_vkCmdWriteTimestamp2KHR = (PFN_vkCmdWriteTimestamp2KHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdWriteTimestamp2KHR;
    }
    if (procName == std::string("vkCmdCopyBuffer2KHR"))
    {
        // LOG_DEBUG("vkCmdCopyBuffer2KHR");

        if (o_vkCmdCopyBuffer2KHR == nullptr)
            o_vkCmdCopyBuffer2KHR = (PFN_vkCmdCopyBuffer2KHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyBuffer2KHR;
    }
    if (procName == std::string("vkCmdCopyImage2KHR"))
    {
        // LOG_DEBUG("vkCmdCopyImage2KHR");

        if (o_vkCmdCopyImage2KHR == nullptr)
            o_vkCmdCopyImage2KHR = (PFN_vkCmdCopyImage2KHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyImage2KHR;
    }
    if (procName == std::string("vkCmdCopyBufferToImage2KHR"))
    {
        // LOG_DEBUG("vkCmdCopyBufferToImage2KHR");

        if (o_vkCmdCopyBufferToImage2KHR == nullptr)
            o_vkCmdCopyBufferToImage2KHR = (PFN_vkCmdCopyBufferToImage2KHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyBufferToImage2KHR;
    }
    if (procName == std::string("vkCmdCopyImageToBuffer2KHR"))
    {
        // LOG_DEBUG("vkCmdCopyImageToBuffer2KHR");

        if (o_vkCmdCopyImageToBuffer2KHR == nullptr)
            o_vkCmdCopyImageToBuffer2KHR = (PFN_vkCmdCopyImageToBuffer2KHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyImageToBuffer2KHR;
    }
    if (procName == std::string("vkCmdBlitImage2KHR"))
    {
        // LOG_DEBUG("vkCmdBlitImage2KHR");

        if (o_vkCmdBlitImage2KHR == nullptr)
            o_vkCmdBlitImage2KHR = (PFN_vkCmdBlitImage2KHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdBlitImage2KHR;
    }
    if (procName == std::string("vkCmdResolveImage2KHR"))
    {
        // LOG_DEBUG("vkCmdResolveImage2KHR");

        if (o_vkCmdResolveImage2KHR == nullptr)
            o_vkCmdResolveImage2KHR = (PFN_vkCmdResolveImage2KHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdResolveImage2KHR;
    }
    if (procName == std::string("vkCmdTraceRaysIndirect2KHR"))
    {
        // LOG_DEBUG("vkCmdTraceRaysIndirect2KHR");

        if (o_vkCmdTraceRaysIndirect2KHR == nullptr)
            o_vkCmdTraceRaysIndirect2KHR = (PFN_vkCmdTraceRaysIndirect2KHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdTraceRaysIndirect2KHR;
    }
    if (procName == std::string("vkCmdBindIndexBuffer2KHR"))
    {
        // LOG_DEBUG("vkCmdBindIndexBuffer2KHR");

        if (o_vkCmdBindIndexBuffer2KHR == nullptr)
            o_vkCmdBindIndexBuffer2KHR = (PFN_vkCmdBindIndexBuffer2KHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdBindIndexBuffer2KHR;
    }
    if (procName == std::string("vkCmdSetLineStippleKHR"))
    {
        // LOG_DEBUG("vkCmdSetLineStippleKHR");

        if (o_vkCmdSetLineStippleKHR == nullptr)
            o_vkCmdSetLineStippleKHR = (PFN_vkCmdSetLineStippleKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetLineStippleKHR;
    }
    if (procName == std::string("vkCmdBindDescriptorSets2KHR"))
    {
        // LOG_DEBUG("vkCmdBindDescriptorSets2KHR");

        if (o_vkCmdBindDescriptorSets2KHR == nullptr)
            o_vkCmdBindDescriptorSets2KHR = (PFN_vkCmdBindDescriptorSets2KHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdBindDescriptorSets2KHR;
    }
    if (procName == std::string("vkCmdPushConstants2KHR"))
    {
        // LOG_DEBUG("vkCmdPushConstants2KHR");

        if (o_vkCmdPushConstants2KHR == nullptr)
            o_vkCmdPushConstants2KHR = (PFN_vkCmdPushConstants2KHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdPushConstants2KHR;
    }
    if (procName == std::string("vkCmdPushDescriptorSet2KHR"))
    {
        // LOG_DEBUG("vkCmdPushDescriptorSet2KHR");

        if (o_vkCmdPushDescriptorSet2KHR == nullptr)
            o_vkCmdPushDescriptorSet2KHR = (PFN_vkCmdPushDescriptorSet2KHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdPushDescriptorSet2KHR;
    }
    if (procName == std::string("vkCmdPushDescriptorSetWithTemplate2KHR"))
    {
        // LOG_DEBUG("vkCmdPushDescriptorSetWithTemplate2KHR");

        if (o_vkCmdPushDescriptorSetWithTemplate2KHR == nullptr)
            o_vkCmdPushDescriptorSetWithTemplate2KHR = (PFN_vkCmdPushDescriptorSetWithTemplate2KHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdPushDescriptorSetWithTemplate2KHR;
    }
    if (procName == std::string("vkCmdSetDescriptorBufferOffsets2EXT"))
    {
        // LOG_DEBUG("vkCmdSetDescriptorBufferOffsets2EXT");

        if (o_vkCmdSetDescriptorBufferOffsets2EXT == nullptr)
            o_vkCmdSetDescriptorBufferOffsets2EXT = (PFN_vkCmdSetDescriptorBufferOffsets2EXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDescriptorBufferOffsets2EXT;
    }
    if (procName == std::string("vkCmdBindDescriptorBufferEmbeddedSamplers2EXT"))
    {
        // LOG_DEBUG("vkCmdBindDescriptorBufferEmbeddedSamplers2EXT");

        if (o_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT == nullptr)
            o_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT =
                (PFN_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT;
    }
    if (procName == std::string("vkCmdDebugMarkerBeginEXT"))
    {
        // LOG_DEBUG("vkCmdDebugMarkerBeginEXT");

        if (o_vkCmdDebugMarkerBeginEXT == nullptr)
            o_vkCmdDebugMarkerBeginEXT = (PFN_vkCmdDebugMarkerBeginEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdDebugMarkerBeginEXT;
    }
    if (procName == std::string("vkCmdDebugMarkerEndEXT"))
    {
        // LOG_DEBUG("vkCmdDebugMarkerEndEXT");

        if (o_vkCmdDebugMarkerEndEXT == nullptr)
            o_vkCmdDebugMarkerEndEXT = (PFN_vkCmdDebugMarkerEndEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdDebugMarkerEndEXT;
    }
    if (procName == std::string("vkCmdDebugMarkerInsertEXT"))
    {
        // LOG_DEBUG("vkCmdDebugMarkerInsertEXT");

        if (o_vkCmdDebugMarkerInsertEXT == nullptr)
            o_vkCmdDebugMarkerInsertEXT = (PFN_vkCmdDebugMarkerInsertEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdDebugMarkerInsertEXT;
    }
    if (procName == std::string("vkCmdBindTransformFeedbackBuffersEXT"))
    {
        // LOG_DEBUG("vkCmdBindTransformFeedbackBuffersEXT");

        if (o_vkCmdBindTransformFeedbackBuffersEXT == nullptr)
            o_vkCmdBindTransformFeedbackBuffersEXT = (PFN_vkCmdBindTransformFeedbackBuffersEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdBindTransformFeedbackBuffersEXT;
    }
    if (procName == std::string("vkCmdBeginTransformFeedbackEXT"))
    {
        // LOG_DEBUG("vkCmdBeginTransformFeedbackEXT");

        if (o_vkCmdBeginTransformFeedbackEXT == nullptr)
            o_vkCmdBeginTransformFeedbackEXT = (PFN_vkCmdBeginTransformFeedbackEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdBeginTransformFeedbackEXT;
    }
    if (procName == std::string("vkCmdEndTransformFeedbackEXT"))
    {
        // LOG_DEBUG("vkCmdEndTransformFeedbackEXT");

        if (o_vkCmdEndTransformFeedbackEXT == nullptr)
            o_vkCmdEndTransformFeedbackEXT = (PFN_vkCmdEndTransformFeedbackEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdEndTransformFeedbackEXT;
    }
    if (procName == std::string("vkCmdBeginQueryIndexedEXT"))
    {
        // LOG_DEBUG("vkCmdBeginQueryIndexedEXT");

        if (o_vkCmdBeginQueryIndexedEXT == nullptr)
            o_vkCmdBeginQueryIndexedEXT = (PFN_vkCmdBeginQueryIndexedEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdBeginQueryIndexedEXT;
    }
    if (procName == std::string("vkCmdEndQueryIndexedEXT"))
    {
        // LOG_DEBUG("vkCmdEndQueryIndexedEXT");

        if (o_vkCmdEndQueryIndexedEXT == nullptr)
            o_vkCmdEndQueryIndexedEXT = (PFN_vkCmdEndQueryIndexedEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdEndQueryIndexedEXT;
    }
    if (procName == std::string("vkCmdDrawIndirectByteCountEXT"))
    {
        // LOG_DEBUG("vkCmdDrawIndirectByteCountEXT");

        if (o_vkCmdDrawIndirectByteCountEXT == nullptr)
            o_vkCmdDrawIndirectByteCountEXT = (PFN_vkCmdDrawIndirectByteCountEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdDrawIndirectByteCountEXT;
    }
    if (procName == std::string("vkCmdCuLaunchKernelNVX"))
    {
        // LOG_DEBUG("vkCmdCuLaunchKernelNVX");

        if (o_vkCmdCuLaunchKernelNVX == nullptr)
            o_vkCmdCuLaunchKernelNVX = (PFN_vkCmdCuLaunchKernelNVX) original;

        return (PFN_vkVoidFunction) hk_vkCmdCuLaunchKernelNVX;
    }
    if (procName == std::string("vkCmdDrawIndirectCountAMD"))
    {
        // LOG_DEBUG("vkCmdDrawIndirectCountAMD");

        if (o_vkCmdDrawIndirectCountAMD == nullptr)
            o_vkCmdDrawIndirectCountAMD = (PFN_vkCmdDrawIndirectCountAMD) original;

        return (PFN_vkVoidFunction) hk_vkCmdDrawIndirectCountAMD;
    }
    if (procName == std::string("vkCmdDrawIndexedIndirectCountAMD"))
    {
        // LOG_DEBUG("vkCmdDrawIndexedIndirectCountAMD");

        if (o_vkCmdDrawIndexedIndirectCountAMD == nullptr)
            o_vkCmdDrawIndexedIndirectCountAMD = (PFN_vkCmdDrawIndexedIndirectCountAMD) original;

        return (PFN_vkVoidFunction) hk_vkCmdDrawIndexedIndirectCountAMD;
    }
    if (procName == std::string("vkCmdBeginConditionalRenderingEXT"))
    {
        // LOG_DEBUG("vkCmdBeginConditionalRenderingEXT");

        if (o_vkCmdBeginConditionalRenderingEXT == nullptr)
            o_vkCmdBeginConditionalRenderingEXT = (PFN_vkCmdBeginConditionalRenderingEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdBeginConditionalRenderingEXT;
    }
    if (procName == std::string("vkCmdEndConditionalRenderingEXT"))
    {
        // LOG_DEBUG("vkCmdEndConditionalRenderingEXT");

        if (o_vkCmdEndConditionalRenderingEXT == nullptr)
            o_vkCmdEndConditionalRenderingEXT = (PFN_vkCmdEndConditionalRenderingEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdEndConditionalRenderingEXT;
    }
    if (procName == std::string("vkCmdSetViewportWScalingNV"))
    {
        // LOG_DEBUG("vkCmdSetViewportWScalingNV");

        if (o_vkCmdSetViewportWScalingNV == nullptr)
            o_vkCmdSetViewportWScalingNV = (PFN_vkCmdSetViewportWScalingNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetViewportWScalingNV;
    }
    if (procName == std::string("vkCmdSetDiscardRectangleEXT"))
    {
        // LOG_DEBUG("vkCmdSetDiscardRectangleEXT");

        if (o_vkCmdSetDiscardRectangleEXT == nullptr)
            o_vkCmdSetDiscardRectangleEXT = (PFN_vkCmdSetDiscardRectangleEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDiscardRectangleEXT;
    }
    if (procName == std::string("vkCmdSetDiscardRectangleEnableEXT"))
    {
        // LOG_DEBUG("vkCmdSetDiscardRectangleEnableEXT");

        if (o_vkCmdSetDiscardRectangleEnableEXT == nullptr)
            o_vkCmdSetDiscardRectangleEnableEXT = (PFN_vkCmdSetDiscardRectangleEnableEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDiscardRectangleEnableEXT;
    }
    if (procName == std::string("vkCmdSetDiscardRectangleModeEXT"))
    {
        // LOG_DEBUG("vkCmdSetDiscardRectangleModeEXT");

        if (o_vkCmdSetDiscardRectangleModeEXT == nullptr)
            o_vkCmdSetDiscardRectangleModeEXT = (PFN_vkCmdSetDiscardRectangleModeEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDiscardRectangleModeEXT;
    }
    if (procName == std::string("vkCmdBeginDebugUtilsLabelEXT"))
    {
        // LOG_DEBUG("vkCmdBeginDebugUtilsLabelEXT");

        if (o_vkCmdBeginDebugUtilsLabelEXT == nullptr)
            o_vkCmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdBeginDebugUtilsLabelEXT;
    }
    if (procName == std::string("vkCmdEndDebugUtilsLabelEXT"))
    {
        // LOG_DEBUG("vkCmdEndDebugUtilsLabelEXT");

        if (o_vkCmdEndDebugUtilsLabelEXT == nullptr)
            o_vkCmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdEndDebugUtilsLabelEXT;
    }
    if (procName == std::string("vkCmdInsertDebugUtilsLabelEXT"))
    {
        // LOG_DEBUG("vkCmdInsertDebugUtilsLabelEXT");

        if (o_vkCmdInsertDebugUtilsLabelEXT == nullptr)
            o_vkCmdInsertDebugUtilsLabelEXT = (PFN_vkCmdInsertDebugUtilsLabelEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdInsertDebugUtilsLabelEXT;
    }
    if (procName == std::string("vkCmdSetSampleLocationsEXT"))
    {
        // LOG_DEBUG("vkCmdSetSampleLocationsEXT");

        if (o_vkCmdSetSampleLocationsEXT == nullptr)
            o_vkCmdSetSampleLocationsEXT = (PFN_vkCmdSetSampleLocationsEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetSampleLocationsEXT;
    }
    if (procName == std::string("vkCmdBindShadingRateImageNV"))
    {
        // LOG_DEBUG("vkCmdBindShadingRateImageNV");

        if (o_vkCmdBindShadingRateImageNV == nullptr)
            o_vkCmdBindShadingRateImageNV = (PFN_vkCmdBindShadingRateImageNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdBindShadingRateImageNV;
    }
    if (procName == std::string("vkCmdSetViewportShadingRatePaletteNV"))
    {
        // LOG_DEBUG("vkCmdSetViewportShadingRatePaletteNV");

        if (o_vkCmdSetViewportShadingRatePaletteNV == nullptr)
            o_vkCmdSetViewportShadingRatePaletteNV = (PFN_vkCmdSetViewportShadingRatePaletteNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetViewportShadingRatePaletteNV;
    }
    if (procName == std::string("vkCmdSetCoarseSampleOrderNV"))
    {
        // LOG_DEBUG("vkCmdSetCoarseSampleOrderNV");

        if (o_vkCmdSetCoarseSampleOrderNV == nullptr)
            o_vkCmdSetCoarseSampleOrderNV = (PFN_vkCmdSetCoarseSampleOrderNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetCoarseSampleOrderNV;
    }
    if (procName == std::string("vkCmdBuildAccelerationStructureNV"))
    {
        // LOG_DEBUG("vkCmdBuildAccelerationStructureNV");

        if (o_vkCmdBuildAccelerationStructureNV == nullptr)
            o_vkCmdBuildAccelerationStructureNV = (PFN_vkCmdBuildAccelerationStructureNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdBuildAccelerationStructureNV;
    }
    if (procName == std::string("vkCmdCopyAccelerationStructureNV"))
    {
        // LOG_DEBUG("vkCmdCopyAccelerationStructureNV");

        if (o_vkCmdCopyAccelerationStructureNV == nullptr)
            o_vkCmdCopyAccelerationStructureNV = (PFN_vkCmdCopyAccelerationStructureNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyAccelerationStructureNV;
    }
    if (procName == std::string("vkCmdTraceRaysNV"))
    {
        // LOG_DEBUG("vkCmdTraceRaysNV");

        if (o_vkCmdTraceRaysNV == nullptr)
            o_vkCmdTraceRaysNV = (PFN_vkCmdTraceRaysNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdTraceRaysNV;
    }
    if (procName == std::string("vkCmdWriteAccelerationStructuresPropertiesNV"))
    {
        // LOG_DEBUG("vkCmdWriteAccelerationStructuresPropertiesNV");

        if (o_vkCmdWriteAccelerationStructuresPropertiesNV == nullptr)
            o_vkCmdWriteAccelerationStructuresPropertiesNV =
                (PFN_vkCmdWriteAccelerationStructuresPropertiesNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdWriteAccelerationStructuresPropertiesNV;
    }
    if (procName == std::string("vkCmdWriteBufferMarkerAMD"))
    {
        // LOG_DEBUG("vkCmdWriteBufferMarkerAMD");

        if (o_vkCmdWriteBufferMarkerAMD == nullptr)
            o_vkCmdWriteBufferMarkerAMD = (PFN_vkCmdWriteBufferMarkerAMD) original;

        return (PFN_vkVoidFunction) hk_vkCmdWriteBufferMarkerAMD;
    }
    if (procName == std::string("vkCmdWriteBufferMarker2AMD"))
    {
        // LOG_DEBUG("vkCmdWriteBufferMarker2AMD");

        if (o_vkCmdWriteBufferMarker2AMD == nullptr)
            o_vkCmdWriteBufferMarker2AMD = (PFN_vkCmdWriteBufferMarker2AMD) original;

        return (PFN_vkVoidFunction) hk_vkCmdWriteBufferMarker2AMD;
    }
    if (procName == std::string("vkCmdDrawMeshTasksNV"))
    {
        // LOG_DEBUG("vkCmdDrawMeshTasksNV");

        if (o_vkCmdDrawMeshTasksNV == nullptr)
            o_vkCmdDrawMeshTasksNV = (PFN_vkCmdDrawMeshTasksNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdDrawMeshTasksNV;
    }
    if (procName == std::string("vkCmdDrawMeshTasksIndirectNV"))
    {
        // LOG_DEBUG("vkCmdDrawMeshTasksIndirectNV");

        if (o_vkCmdDrawMeshTasksIndirectNV == nullptr)
            o_vkCmdDrawMeshTasksIndirectNV = (PFN_vkCmdDrawMeshTasksIndirectNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdDrawMeshTasksIndirectNV;
    }
    if (procName == std::string("vkCmdDrawMeshTasksIndirectCountNV"))
    {
        // LOG_DEBUG("vkCmdDrawMeshTasksIndirectCountNV");

        if (o_vkCmdDrawMeshTasksIndirectCountNV == nullptr)
            o_vkCmdDrawMeshTasksIndirectCountNV = (PFN_vkCmdDrawMeshTasksIndirectCountNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdDrawMeshTasksIndirectCountNV;
    }
    if (procName == std::string("vkCmdSetExclusiveScissorEnableNV"))
    {
        // LOG_DEBUG("vkCmdSetExclusiveScissorEnableNV");

        if (o_vkCmdSetExclusiveScissorEnableNV == nullptr)
            o_vkCmdSetExclusiveScissorEnableNV = (PFN_vkCmdSetExclusiveScissorEnableNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetExclusiveScissorEnableNV;
    }
    if (procName == std::string("vkCmdSetExclusiveScissorNV"))
    {
        // LOG_DEBUG("vkCmdSetExclusiveScissorNV");

        if (o_vkCmdSetExclusiveScissorNV == nullptr)
            o_vkCmdSetExclusiveScissorNV = (PFN_vkCmdSetExclusiveScissorNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetExclusiveScissorNV;
    }
    if (procName == std::string("vkCmdSetCheckpointNV"))
    {
        // LOG_DEBUG("vkCmdSetCheckpointNV");

        if (o_vkCmdSetCheckpointNV == nullptr)
            o_vkCmdSetCheckpointNV = (PFN_vkCmdSetCheckpointNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetCheckpointNV;
    }
    if (procName == std::string("vkCmdSetPerformanceMarkerINTEL"))
    {
        // LOG_DEBUG("vkCmdSetPerformanceMarkerINTEL");

        if (o_vkCmdSetPerformanceMarkerINTEL == nullptr)
            o_vkCmdSetPerformanceMarkerINTEL = (PFN_vkCmdSetPerformanceMarkerINTEL) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetPerformanceMarkerINTEL;
    }
    if (procName == std::string("vkCmdSetPerformanceStreamMarkerINTEL"))
    {
        // LOG_DEBUG("vkCmdSetPerformanceStreamMarkerINTEL");

        if (o_vkCmdSetPerformanceStreamMarkerINTEL == nullptr)
            o_vkCmdSetPerformanceStreamMarkerINTEL = (PFN_vkCmdSetPerformanceStreamMarkerINTEL) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetPerformanceStreamMarkerINTEL;
    }
    if (procName == std::string("vkCmdSetPerformanceOverrideINTEL"))
    {
        // LOG_DEBUG("vkCmdSetPerformanceOverrideINTEL");

        if (o_vkCmdSetPerformanceOverrideINTEL == nullptr)
            o_vkCmdSetPerformanceOverrideINTEL = (PFN_vkCmdSetPerformanceOverrideINTEL) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetPerformanceOverrideINTEL;
    }
    if (procName == std::string("vkCmdSetLineStippleEXT"))
    {
        // LOG_DEBUG("vkCmdSetLineStippleEXT");

        if (o_vkCmdSetLineStippleEXT == nullptr)
            o_vkCmdSetLineStippleEXT = (PFN_vkCmdSetLineStippleEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetLineStippleEXT;
    }
    if (procName == std::string("vkCmdSetCullModeEXT"))
    {
        // LOG_DEBUG("vkCmdSetCullModeEXT");

        if (o_vkCmdSetCullModeEXT == nullptr)
            o_vkCmdSetCullModeEXT = (PFN_vkCmdSetCullModeEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetCullModeEXT;
    }
    if (procName == std::string("vkCmdSetFrontFaceEXT"))
    {
        // LOG_DEBUG("vkCmdSetFrontFaceEXT");

        if (o_vkCmdSetFrontFaceEXT == nullptr)
            o_vkCmdSetFrontFaceEXT = (PFN_vkCmdSetFrontFaceEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetFrontFaceEXT;
    }
    if (procName == std::string("vkCmdSetPrimitiveTopologyEXT"))
    {
        // LOG_DEBUG("vkCmdSetPrimitiveTopologyEXT");

        if (o_vkCmdSetPrimitiveTopologyEXT == nullptr)
            o_vkCmdSetPrimitiveTopologyEXT = (PFN_vkCmdSetPrimitiveTopologyEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetPrimitiveTopologyEXT;
    }
    if (procName == std::string("vkCmdSetViewportWithCountEXT"))
    {
        // LOG_DEBUG("vkCmdSetViewportWithCountEXT");

        if (o_vkCmdSetViewportWithCountEXT == nullptr)
            o_vkCmdSetViewportWithCountEXT = (PFN_vkCmdSetViewportWithCountEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetViewportWithCountEXT;
    }
    if (procName == std::string("vkCmdSetScissorWithCountEXT"))
    {
        // LOG_DEBUG("vkCmdSetScissorWithCountEXT");

        if (o_vkCmdSetScissorWithCountEXT == nullptr)
            o_vkCmdSetScissorWithCountEXT = (PFN_vkCmdSetScissorWithCountEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetScissorWithCountEXT;
    }
    if (procName == std::string("vkCmdBindVertexBuffers2EXT"))
    {
        // LOG_DEBUG("vkCmdBindVertexBuffers2EXT");

        if (o_vkCmdBindVertexBuffers2EXT == nullptr)
            o_vkCmdBindVertexBuffers2EXT = (PFN_vkCmdBindVertexBuffers2EXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdBindVertexBuffers2EXT;
    }
    if (procName == std::string("vkCmdSetDepthTestEnableEXT"))
    {
        // LOG_DEBUG("vkCmdSetDepthTestEnableEXT");

        if (o_vkCmdSetDepthTestEnableEXT == nullptr)
            o_vkCmdSetDepthTestEnableEXT = (PFN_vkCmdSetDepthTestEnableEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDepthTestEnableEXT;
    }
    if (procName == std::string("vkCmdSetDepthWriteEnableEXT"))
    {
        // LOG_DEBUG("vkCmdSetDepthWriteEnableEXT");

        if (o_vkCmdSetDepthWriteEnableEXT == nullptr)
            o_vkCmdSetDepthWriteEnableEXT = (PFN_vkCmdSetDepthWriteEnableEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDepthWriteEnableEXT;
    }
    if (procName == std::string("vkCmdSetDepthCompareOpEXT"))
    {
        // LOG_DEBUG("vkCmdSetDepthCompareOpEXT");

        if (o_vkCmdSetDepthCompareOpEXT == nullptr)
            o_vkCmdSetDepthCompareOpEXT = (PFN_vkCmdSetDepthCompareOpEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDepthCompareOpEXT;
    }
    if (procName == std::string("vkCmdSetDepthBoundsTestEnableEXT"))
    {
        // LOG_DEBUG("vkCmdSetDepthBoundsTestEnableEXT");

        if (o_vkCmdSetDepthBoundsTestEnableEXT == nullptr)
            o_vkCmdSetDepthBoundsTestEnableEXT = (PFN_vkCmdSetDepthBoundsTestEnableEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDepthBoundsTestEnableEXT;
    }
    if (procName == std::string("vkCmdSetStencilTestEnableEXT"))
    {
        // LOG_DEBUG("vkCmdSetStencilTestEnableEXT");

        if (o_vkCmdSetStencilTestEnableEXT == nullptr)
            o_vkCmdSetStencilTestEnableEXT = (PFN_vkCmdSetStencilTestEnableEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetStencilTestEnableEXT;
    }
    if (procName == std::string("vkCmdSetStencilOpEXT"))
    {
        // LOG_DEBUG("vkCmdSetStencilOpEXT");

        if (o_vkCmdSetStencilOpEXT == nullptr)
            o_vkCmdSetStencilOpEXT = (PFN_vkCmdSetStencilOpEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetStencilOpEXT;
    }
    if (procName == std::string("vkCmdPreprocessGeneratedCommandsNV"))
    {
        // LOG_DEBUG("vkCmdPreprocessGeneratedCommandsNV");

        if (o_vkCmdPreprocessGeneratedCommandsNV == nullptr)
            o_vkCmdPreprocessGeneratedCommandsNV = (PFN_vkCmdPreprocessGeneratedCommandsNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdPreprocessGeneratedCommandsNV;
    }
    if (procName == std::string("vkCmdExecuteGeneratedCommandsNV"))
    {
        // LOG_DEBUG("vkCmdExecuteGeneratedCommandsNV");

        if (o_vkCmdExecuteGeneratedCommandsNV == nullptr)
            o_vkCmdExecuteGeneratedCommandsNV = (PFN_vkCmdExecuteGeneratedCommandsNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdExecuteGeneratedCommandsNV;
    }
    if (procName == std::string("vkCmdBindPipelineShaderGroupNV"))
    {
        // LOG_DEBUG("vkCmdBindPipelineShaderGroupNV");

        if (o_vkCmdBindPipelineShaderGroupNV == nullptr)
            o_vkCmdBindPipelineShaderGroupNV = (PFN_vkCmdBindPipelineShaderGroupNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdBindPipelineShaderGroupNV;
    }
    if (procName == std::string("vkCmdSetDepthBias2EXT"))
    {
        // LOG_DEBUG("vkCmdSetDepthBias2EXT");

        if (o_vkCmdSetDepthBias2EXT == nullptr)
            o_vkCmdSetDepthBias2EXT = (PFN_vkCmdSetDepthBias2EXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDepthBias2EXT;
    }
    if (procName == std::string("vkCmdCudaLaunchKernelNV"))
    {
        // LOG_DEBUG("vkCmdCudaLaunchKernelNV");

        if (o_vkCmdCudaLaunchKernelNV == nullptr)
            o_vkCmdCudaLaunchKernelNV = (PFN_vkCmdCudaLaunchKernelNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdCudaLaunchKernelNV;
    }
    if (procName == std::string("vkCmdBindDescriptorBuffersEXT"))
    {
        // LOG_DEBUG("vkCmdBindDescriptorBuffersEXT");

        if (o_vkCmdBindDescriptorBuffersEXT == nullptr)
            o_vkCmdBindDescriptorBuffersEXT = (PFN_vkCmdBindDescriptorBuffersEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdBindDescriptorBuffersEXT;
    }
    if (procName == std::string("vkCmdSetDescriptorBufferOffsetsEXT"))
    {
        // LOG_DEBUG("vkCmdSetDescriptorBufferOffsetsEXT");

        if (o_vkCmdSetDescriptorBufferOffsetsEXT == nullptr)
            o_vkCmdSetDescriptorBufferOffsetsEXT = (PFN_vkCmdSetDescriptorBufferOffsetsEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDescriptorBufferOffsetsEXT;
    }
    if (procName == std::string("vkCmdBindDescriptorBufferEmbeddedSamplersEXT"))
    {
        // LOG_DEBUG("vkCmdBindDescriptorBufferEmbeddedSamplersEXT");

        if (o_vkCmdBindDescriptorBufferEmbeddedSamplersEXT == nullptr)
            o_vkCmdBindDescriptorBufferEmbeddedSamplersEXT =
                (PFN_vkCmdBindDescriptorBufferEmbeddedSamplersEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdBindDescriptorBufferEmbeddedSamplersEXT;
    }
    if (procName == std::string("vkCmdSetFragmentShadingRateEnumNV"))
    {
        // LOG_DEBUG("vkCmdSetFragmentShadingRateEnumNV");

        if (o_vkCmdSetFragmentShadingRateEnumNV == nullptr)
            o_vkCmdSetFragmentShadingRateEnumNV = (PFN_vkCmdSetFragmentShadingRateEnumNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetFragmentShadingRateEnumNV;
    }
    if (procName == std::string("vkCmdSetVertexInputEXT"))
    {
        // LOG_DEBUG("vkCmdSetVertexInputEXT");

        if (o_vkCmdSetVertexInputEXT == nullptr)
            o_vkCmdSetVertexInputEXT = (PFN_vkCmdSetVertexInputEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetVertexInputEXT;
    }
    if (procName == std::string("vkCmdSubpassShadingHUAWEI"))
    {
        // LOG_DEBUG("vkCmdSubpassShadingHUAWEI");

        if (o_vkCmdSubpassShadingHUAWEI == nullptr)
            o_vkCmdSubpassShadingHUAWEI = (PFN_vkCmdSubpassShadingHUAWEI) original;

        return (PFN_vkVoidFunction) hk_vkCmdSubpassShadingHUAWEI;
    }
    if (procName == std::string("vkCmdBindInvocationMaskHUAWEI"))
    {
        // LOG_DEBUG("vkCmdBindInvocationMaskHUAWEI");

        if (o_vkCmdBindInvocationMaskHUAWEI == nullptr)
            o_vkCmdBindInvocationMaskHUAWEI = (PFN_vkCmdBindInvocationMaskHUAWEI) original;

        return (PFN_vkVoidFunction) hk_vkCmdBindInvocationMaskHUAWEI;
    }
    if (procName == std::string("vkCmdSetPatchControlPointsEXT"))
    {
        // LOG_DEBUG("vkCmdSetPatchControlPointsEXT");

        if (o_vkCmdSetPatchControlPointsEXT == nullptr)
            o_vkCmdSetPatchControlPointsEXT = (PFN_vkCmdSetPatchControlPointsEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetPatchControlPointsEXT;
    }
    if (procName == std::string("vkCmdSetRasterizerDiscardEnableEXT"))
    {
        // LOG_DEBUG("vkCmdSetRasterizerDiscardEnableEXT");

        if (o_vkCmdSetRasterizerDiscardEnableEXT == nullptr)
            o_vkCmdSetRasterizerDiscardEnableEXT = (PFN_vkCmdSetRasterizerDiscardEnableEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetRasterizerDiscardEnableEXT;
    }
    if (procName == std::string("vkCmdSetDepthBiasEnableEXT"))
    {
        // LOG_DEBUG("vkCmdSetDepthBiasEnableEXT");

        if (o_vkCmdSetDepthBiasEnableEXT == nullptr)
            o_vkCmdSetDepthBiasEnableEXT = (PFN_vkCmdSetDepthBiasEnableEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDepthBiasEnableEXT;
    }
    if (procName == std::string("vkCmdSetLogicOpEXT"))
    {
        // LOG_DEBUG("vkCmdSetLogicOpEXT");

        if (o_vkCmdSetLogicOpEXT == nullptr)
            o_vkCmdSetLogicOpEXT = (PFN_vkCmdSetLogicOpEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetLogicOpEXT;
    }
    if (procName == std::string("vkCmdSetPrimitiveRestartEnableEXT"))
    {
        // LOG_DEBUG("vkCmdSetPrimitiveRestartEnableEXT");

        if (o_vkCmdSetPrimitiveRestartEnableEXT == nullptr)
            o_vkCmdSetPrimitiveRestartEnableEXT = (PFN_vkCmdSetPrimitiveRestartEnableEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetPrimitiveRestartEnableEXT;
    }
    if (procName == std::string("vkCmdSetColorWriteEnableEXT"))
    {
        // LOG_DEBUG("vkCmdSetColorWriteEnableEXT");

        if (o_vkCmdSetColorWriteEnableEXT == nullptr)
            o_vkCmdSetColorWriteEnableEXT = (PFN_vkCmdSetColorWriteEnableEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetColorWriteEnableEXT;
    }
    if (procName == std::string("vkCmdDrawMultiEXT"))
    {
        // LOG_DEBUG("vkCmdDrawMultiEXT");

        if (o_vkCmdDrawMultiEXT == nullptr)
            o_vkCmdDrawMultiEXT = (PFN_vkCmdDrawMultiEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdDrawMultiEXT;
    }
    if (procName == std::string("vkCmdDrawMultiIndexedEXT"))
    {
        // LOG_DEBUG("vkCmdDrawMultiIndexedEXT");

        if (o_vkCmdDrawMultiIndexedEXT == nullptr)
            o_vkCmdDrawMultiIndexedEXT = (PFN_vkCmdDrawMultiIndexedEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdDrawMultiIndexedEXT;
    }
    if (procName == std::string("vkCmdBuildMicromapsEXT"))
    {
        // LOG_DEBUG("vkCmdBuildMicromapsEXT");

        if (o_vkCmdBuildMicromapsEXT == nullptr)
            o_vkCmdBuildMicromapsEXT = (PFN_vkCmdBuildMicromapsEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdBuildMicromapsEXT;
    }
    if (procName == std::string("vkCmdCopyMicromapEXT"))
    {
        // LOG_DEBUG("vkCmdCopyMicromapEXT");

        if (o_vkCmdCopyMicromapEXT == nullptr)
            o_vkCmdCopyMicromapEXT = (PFN_vkCmdCopyMicromapEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyMicromapEXT;
    }
    if (procName == std::string("vkCmdCopyMicromapToMemoryEXT"))
    {
        // LOG_DEBUG("vkCmdCopyMicromapToMemoryEXT");

        if (o_vkCmdCopyMicromapToMemoryEXT == nullptr)
            o_vkCmdCopyMicromapToMemoryEXT = (PFN_vkCmdCopyMicromapToMemoryEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyMicromapToMemoryEXT;
    }
    if (procName == std::string("vkCmdCopyMemoryToMicromapEXT"))
    {
        // LOG_DEBUG("vkCmdCopyMemoryToMicromapEXT");

        if (o_vkCmdCopyMemoryToMicromapEXT == nullptr)
            o_vkCmdCopyMemoryToMicromapEXT = (PFN_vkCmdCopyMemoryToMicromapEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyMemoryToMicromapEXT;
    }
    if (procName == std::string("vkCmdWriteMicromapsPropertiesEXT"))
    {
        // LOG_DEBUG("vkCmdWriteMicromapsPropertiesEXT");

        if (o_vkCmdWriteMicromapsPropertiesEXT == nullptr)
            o_vkCmdWriteMicromapsPropertiesEXT = (PFN_vkCmdWriteMicromapsPropertiesEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdWriteMicromapsPropertiesEXT;
    }
    if (procName == std::string("vkCmdDrawClusterHUAWEI"))
    {
        // LOG_DEBUG("vkCmdDrawClusterHUAWEI");

        if (o_vkCmdDrawClusterHUAWEI == nullptr)
            o_vkCmdDrawClusterHUAWEI = (PFN_vkCmdDrawClusterHUAWEI) original;

        return (PFN_vkVoidFunction) hk_vkCmdDrawClusterHUAWEI;
    }
    if (procName == std::string("vkCmdDrawClusterIndirectHUAWEI"))
    {
        // LOG_DEBUG("vkCmdDrawClusterIndirectHUAWEI");

        if (o_vkCmdDrawClusterIndirectHUAWEI == nullptr)
            o_vkCmdDrawClusterIndirectHUAWEI = (PFN_vkCmdDrawClusterIndirectHUAWEI) original;

        return (PFN_vkVoidFunction) hk_vkCmdDrawClusterIndirectHUAWEI;
    }
    if (procName == std::string("vkCmdCopyMemoryIndirectNV"))
    {
        // LOG_DEBUG("vkCmdCopyMemoryIndirectNV");

        if (o_vkCmdCopyMemoryIndirectNV == nullptr)
            o_vkCmdCopyMemoryIndirectNV = (PFN_vkCmdCopyMemoryIndirectNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyMemoryIndirectNV;
    }
    if (procName == std::string("vkCmdCopyMemoryToImageIndirectNV"))
    {
        // LOG_DEBUG("vkCmdCopyMemoryToImageIndirectNV");

        if (o_vkCmdCopyMemoryToImageIndirectNV == nullptr)
            o_vkCmdCopyMemoryToImageIndirectNV = (PFN_vkCmdCopyMemoryToImageIndirectNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyMemoryToImageIndirectNV;
    }
    if (procName == std::string("vkCmdDecompressMemoryNV"))
    {
        // LOG_DEBUG("vkCmdDecompressMemoryNV");

        if (o_vkCmdDecompressMemoryNV == nullptr)
            o_vkCmdDecompressMemoryNV = (PFN_vkCmdDecompressMemoryNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdDecompressMemoryNV;
    }
    if (procName == std::string("vkCmdDecompressMemoryIndirectCountNV"))
    {
        // LOG_DEBUG("vkCmdDecompressMemoryIndirectCountNV");

        if (o_vkCmdDecompressMemoryIndirectCountNV == nullptr)
            o_vkCmdDecompressMemoryIndirectCountNV = (PFN_vkCmdDecompressMemoryIndirectCountNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdDecompressMemoryIndirectCountNV;
    }
    if (procName == std::string("vkCmdUpdatePipelineIndirectBufferNV"))
    {
        // LOG_DEBUG("vkCmdUpdatePipelineIndirectBufferNV");

        if (o_vkCmdUpdatePipelineIndirectBufferNV == nullptr)
            o_vkCmdUpdatePipelineIndirectBufferNV = (PFN_vkCmdUpdatePipelineIndirectBufferNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdUpdatePipelineIndirectBufferNV;
    }
    if (procName == std::string("vkCmdSetDepthClampEnableEXT"))
    {
        // LOG_DEBUG("vkCmdSetDepthClampEnableEXT");

        if (o_vkCmdSetDepthClampEnableEXT == nullptr)
            o_vkCmdSetDepthClampEnableEXT = (PFN_vkCmdSetDepthClampEnableEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDepthClampEnableEXT;
    }
    if (procName == std::string("vkCmdSetPolygonModeEXT"))
    {
        // LOG_DEBUG("vkCmdSetPolygonModeEXT");

        if (o_vkCmdSetPolygonModeEXT == nullptr)
            o_vkCmdSetPolygonModeEXT = (PFN_vkCmdSetPolygonModeEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetPolygonModeEXT;
    }
    if (procName == std::string("vkCmdSetRasterizationSamplesEXT"))
    {
        // LOG_DEBUG("vkCmdSetRasterizationSamplesEXT");

        if (o_vkCmdSetRasterizationSamplesEXT == nullptr)
            o_vkCmdSetRasterizationSamplesEXT = (PFN_vkCmdSetRasterizationSamplesEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetRasterizationSamplesEXT;
    }
    if (procName == std::string("vkCmdSetSampleMaskEXT"))
    {
        // LOG_DEBUG("vkCmdSetSampleMaskEXT");

        if (o_vkCmdSetSampleMaskEXT == nullptr)
            o_vkCmdSetSampleMaskEXT = (PFN_vkCmdSetSampleMaskEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetSampleMaskEXT;
    }
    if (procName == std::string("vkCmdSetAlphaToCoverageEnableEXT"))
    {
        // LOG_DEBUG("vkCmdSetAlphaToCoverageEnableEXT");

        if (o_vkCmdSetAlphaToCoverageEnableEXT == nullptr)
            o_vkCmdSetAlphaToCoverageEnableEXT = (PFN_vkCmdSetAlphaToCoverageEnableEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetAlphaToCoverageEnableEXT;
    }
    if (procName == std::string("vkCmdSetAlphaToOneEnableEXT"))
    {
        // LOG_DEBUG("vkCmdSetAlphaToOneEnableEXT");

        if (o_vkCmdSetAlphaToOneEnableEXT == nullptr)
            o_vkCmdSetAlphaToOneEnableEXT = (PFN_vkCmdSetAlphaToOneEnableEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetAlphaToOneEnableEXT;
    }
    if (procName == std::string("vkCmdSetLogicOpEnableEXT"))
    {
        // LOG_DEBUG("vkCmdSetLogicOpEnableEXT");

        if (o_vkCmdSetLogicOpEnableEXT == nullptr)
            o_vkCmdSetLogicOpEnableEXT = (PFN_vkCmdSetLogicOpEnableEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetLogicOpEnableEXT;
    }
    if (procName == std::string("vkCmdSetColorBlendEnableEXT"))
    {
        // LOG_DEBUG("vkCmdSetColorBlendEnableEXT");

        if (o_vkCmdSetColorBlendEnableEXT == nullptr)
            o_vkCmdSetColorBlendEnableEXT = (PFN_vkCmdSetColorBlendEnableEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetColorBlendEnableEXT;
    }
    if (procName == std::string("vkCmdSetColorBlendEquationEXT"))
    {
        // LOG_DEBUG("vkCmdSetColorBlendEquationEXT");

        if (o_vkCmdSetColorBlendEquationEXT == nullptr)
            o_vkCmdSetColorBlendEquationEXT = (PFN_vkCmdSetColorBlendEquationEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetColorBlendEquationEXT;
    }
    if (procName == std::string("vkCmdSetColorWriteMaskEXT"))
    {
        // LOG_DEBUG("vkCmdSetColorWriteMaskEXT");

        if (o_vkCmdSetColorWriteMaskEXT == nullptr)
            o_vkCmdSetColorWriteMaskEXT = (PFN_vkCmdSetColorWriteMaskEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetColorWriteMaskEXT;
    }
    if (procName == std::string("vkCmdSetTessellationDomainOriginEXT"))
    {
        // LOG_DEBUG("vkCmdSetTessellationDomainOriginEXT");

        if (o_vkCmdSetTessellationDomainOriginEXT == nullptr)
            o_vkCmdSetTessellationDomainOriginEXT = (PFN_vkCmdSetTessellationDomainOriginEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetTessellationDomainOriginEXT;
    }
    if (procName == std::string("vkCmdSetRasterizationStreamEXT"))
    {
        // LOG_DEBUG("vkCmdSetRasterizationStreamEXT");

        if (o_vkCmdSetRasterizationStreamEXT == nullptr)
            o_vkCmdSetRasterizationStreamEXT = (PFN_vkCmdSetRasterizationStreamEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetRasterizationStreamEXT;
    }
    if (procName == std::string("vkCmdSetConservativeRasterizationModeEXT"))
    {
        // LOG_DEBUG("vkCmdSetConservativeRasterizationModeEXT");

        if (o_vkCmdSetConservativeRasterizationModeEXT == nullptr)
            o_vkCmdSetConservativeRasterizationModeEXT = (PFN_vkCmdSetConservativeRasterizationModeEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetConservativeRasterizationModeEXT;
    }
    if (procName == std::string("vkCmdSetExtraPrimitiveOverestimationSizeEXT"))
    {
        // LOG_DEBUG("vkCmdSetExtraPrimitiveOverestimationSizeEXT");

        if (o_vkCmdSetExtraPrimitiveOverestimationSizeEXT == nullptr)
            o_vkCmdSetExtraPrimitiveOverestimationSizeEXT = (PFN_vkCmdSetExtraPrimitiveOverestimationSizeEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetExtraPrimitiveOverestimationSizeEXT;
    }
    if (procName == std::string("vkCmdSetDepthClipEnableEXT"))
    {
        // LOG_DEBUG("vkCmdSetDepthClipEnableEXT");

        if (o_vkCmdSetDepthClipEnableEXT == nullptr)
            o_vkCmdSetDepthClipEnableEXT = (PFN_vkCmdSetDepthClipEnableEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDepthClipEnableEXT;
    }
    if (procName == std::string("vkCmdSetSampleLocationsEnableEXT"))
    {
        // LOG_DEBUG("vkCmdSetSampleLocationsEnableEXT");

        if (o_vkCmdSetSampleLocationsEnableEXT == nullptr)
            o_vkCmdSetSampleLocationsEnableEXT = (PFN_vkCmdSetSampleLocationsEnableEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetSampleLocationsEnableEXT;
    }
    if (procName == std::string("vkCmdSetColorBlendAdvancedEXT"))
    {
        // LOG_DEBUG("vkCmdSetColorBlendAdvancedEXT");

        if (o_vkCmdSetColorBlendAdvancedEXT == nullptr)
            o_vkCmdSetColorBlendAdvancedEXT = (PFN_vkCmdSetColorBlendAdvancedEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetColorBlendAdvancedEXT;
    }
    if (procName == std::string("vkCmdSetProvokingVertexModeEXT"))
    {
        // LOG_DEBUG("vkCmdSetProvokingVertexModeEXT");

        if (o_vkCmdSetProvokingVertexModeEXT == nullptr)
            o_vkCmdSetProvokingVertexModeEXT = (PFN_vkCmdSetProvokingVertexModeEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetProvokingVertexModeEXT;
    }
    if (procName == std::string("vkCmdSetLineRasterizationModeEXT"))
    {
        // LOG_DEBUG("vkCmdSetLineRasterizationModeEXT");

        if (o_vkCmdSetLineRasterizationModeEXT == nullptr)
            o_vkCmdSetLineRasterizationModeEXT = (PFN_vkCmdSetLineRasterizationModeEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetLineRasterizationModeEXT;
    }
    if (procName == std::string("vkCmdSetLineStippleEnableEXT"))
    {
        // LOG_DEBUG("vkCmdSetLineStippleEnableEXT");

        if (o_vkCmdSetLineStippleEnableEXT == nullptr)
            o_vkCmdSetLineStippleEnableEXT = (PFN_vkCmdSetLineStippleEnableEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetLineStippleEnableEXT;
    }
    if (procName == std::string("vkCmdSetDepthClipNegativeOneToOneEXT"))
    {
        // LOG_DEBUG("vkCmdSetDepthClipNegativeOneToOneEXT");

        if (o_vkCmdSetDepthClipNegativeOneToOneEXT == nullptr)
            o_vkCmdSetDepthClipNegativeOneToOneEXT = (PFN_vkCmdSetDepthClipNegativeOneToOneEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDepthClipNegativeOneToOneEXT;
    }
    if (procName == std::string("vkCmdSetViewportWScalingEnableNV"))
    {
        // LOG_DEBUG("vkCmdSetViewportWScalingEnableNV");

        if (o_vkCmdSetViewportWScalingEnableNV == nullptr)
            o_vkCmdSetViewportWScalingEnableNV = (PFN_vkCmdSetViewportWScalingEnableNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetViewportWScalingEnableNV;
    }
    if (procName == std::string("vkCmdSetViewportSwizzleNV"))
    {
        // LOG_DEBUG("vkCmdSetViewportSwizzleNV");

        if (o_vkCmdSetViewportSwizzleNV == nullptr)
            o_vkCmdSetViewportSwizzleNV = (PFN_vkCmdSetViewportSwizzleNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetViewportSwizzleNV;
    }
    if (procName == std::string("vkCmdSetCoverageToColorEnableNV"))
    {
        // LOG_DEBUG("vkCmdSetCoverageToColorEnableNV");

        if (o_vkCmdSetCoverageToColorEnableNV == nullptr)
            o_vkCmdSetCoverageToColorEnableNV = (PFN_vkCmdSetCoverageToColorEnableNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetCoverageToColorEnableNV;
    }
    if (procName == std::string("vkCmdSetCoverageToColorLocationNV"))
    {
        // LOG_DEBUG("vkCmdSetCoverageToColorLocationNV");

        if (o_vkCmdSetCoverageToColorLocationNV == nullptr)
            o_vkCmdSetCoverageToColorLocationNV = (PFN_vkCmdSetCoverageToColorLocationNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetCoverageToColorLocationNV;
    }
    if (procName == std::string("vkCmdSetCoverageModulationModeNV"))
    {
        // LOG_DEBUG("vkCmdSetCoverageModulationModeNV");

        if (o_vkCmdSetCoverageModulationModeNV == nullptr)
            o_vkCmdSetCoverageModulationModeNV = (PFN_vkCmdSetCoverageModulationModeNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetCoverageModulationModeNV;
    }
    if (procName == std::string("vkCmdSetCoverageModulationTableEnableNV"))
    {
        // LOG_DEBUG("vkCmdSetCoverageModulationTableEnableNV");

        if (o_vkCmdSetCoverageModulationTableEnableNV == nullptr)
            o_vkCmdSetCoverageModulationTableEnableNV = (PFN_vkCmdSetCoverageModulationTableEnableNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetCoverageModulationTableEnableNV;
    }
    if (procName == std::string("vkCmdSetCoverageModulationTableNV"))
    {
        // LOG_DEBUG("vkCmdSetCoverageModulationTableNV");

        if (o_vkCmdSetCoverageModulationTableNV == nullptr)
            o_vkCmdSetCoverageModulationTableNV = (PFN_vkCmdSetCoverageModulationTableNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetCoverageModulationTableNV;
    }
    if (procName == std::string("vkCmdSetShadingRateImageEnableNV"))
    {
        // LOG_DEBUG("vkCmdSetShadingRateImageEnableNV");

        if (o_vkCmdSetShadingRateImageEnableNV == nullptr)
            o_vkCmdSetShadingRateImageEnableNV = (PFN_vkCmdSetShadingRateImageEnableNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetShadingRateImageEnableNV;
    }
    if (procName == std::string("vkCmdSetRepresentativeFragmentTestEnableNV"))
    {
        // LOG_DEBUG("vkCmdSetRepresentativeFragmentTestEnableNV");

        if (o_vkCmdSetRepresentativeFragmentTestEnableNV == nullptr)
            o_vkCmdSetRepresentativeFragmentTestEnableNV = (PFN_vkCmdSetRepresentativeFragmentTestEnableNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetRepresentativeFragmentTestEnableNV;
    }
    if (procName == std::string("vkCmdSetCoverageReductionModeNV"))
    {
        // LOG_DEBUG("vkCmdSetCoverageReductionModeNV");

        if (o_vkCmdSetCoverageReductionModeNV == nullptr)
            o_vkCmdSetCoverageReductionModeNV = (PFN_vkCmdSetCoverageReductionModeNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetCoverageReductionModeNV;
    }
    if (procName == std::string("vkCmdOpticalFlowExecuteNV"))
    {
        // LOG_DEBUG("vkCmdOpticalFlowExecuteNV");

        if (o_vkCmdOpticalFlowExecuteNV == nullptr)
            o_vkCmdOpticalFlowExecuteNV = (PFN_vkCmdOpticalFlowExecuteNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdOpticalFlowExecuteNV;
    }
    if (procName == std::string("vkCmdBindShadersEXT"))
    {
        // LOG_DEBUG("vkCmdBindShadersEXT");

        if (o_vkCmdBindShadersEXT == nullptr)
            o_vkCmdBindShadersEXT = (PFN_vkCmdBindShadersEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdBindShadersEXT;
    }
    if (procName == std::string("vkCmdSetDepthClampRangeEXT"))
    {
        // LOG_DEBUG("vkCmdSetDepthClampRangeEXT");

        if (o_vkCmdSetDepthClampRangeEXT == nullptr)
            o_vkCmdSetDepthClampRangeEXT = (PFN_vkCmdSetDepthClampRangeEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetDepthClampRangeEXT;
    }
    if (procName == std::string("vkCmdConvertCooperativeVectorMatrixNV"))
    {
        // LOG_DEBUG("vkCmdConvertCooperativeVectorMatrixNV");

        if (o_vkCmdConvertCooperativeVectorMatrixNV == nullptr)
            o_vkCmdConvertCooperativeVectorMatrixNV = (PFN_vkCmdConvertCooperativeVectorMatrixNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdConvertCooperativeVectorMatrixNV;
    }
    if (procName == std::string("vkCmdSetAttachmentFeedbackLoopEnableEXT"))
    {
        // LOG_DEBUG("vkCmdSetAttachmentFeedbackLoopEnableEXT");

        if (o_vkCmdSetAttachmentFeedbackLoopEnableEXT == nullptr)
            o_vkCmdSetAttachmentFeedbackLoopEnableEXT = (PFN_vkCmdSetAttachmentFeedbackLoopEnableEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetAttachmentFeedbackLoopEnableEXT;
    }
    if (procName == std::string("vkCmdBuildClusterAccelerationStructureIndirectNV"))
    {
        // LOG_DEBUG("vkCmdBuildClusterAccelerationStructureIndirectNV");

        if (o_vkCmdBuildClusterAccelerationStructureIndirectNV == nullptr)
            o_vkCmdBuildClusterAccelerationStructureIndirectNV =
                (PFN_vkCmdBuildClusterAccelerationStructureIndirectNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdBuildClusterAccelerationStructureIndirectNV;
    }
    if (procName == std::string("vkCmdBuildPartitionedAccelerationStructuresNV"))
    {
        // LOG_DEBUG("vkCmdBuildPartitionedAccelerationStructuresNV");

        if (o_vkCmdBuildPartitionedAccelerationStructuresNV == nullptr)
            o_vkCmdBuildPartitionedAccelerationStructuresNV =
                (PFN_vkCmdBuildPartitionedAccelerationStructuresNV) original;

        return (PFN_vkVoidFunction) hk_vkCmdBuildPartitionedAccelerationStructuresNV;
    }
    if (procName == std::string("vkCmdPreprocessGeneratedCommandsEXT"))
    {
        // LOG_DEBUG("vkCmdPreprocessGeneratedCommandsEXT");

        if (o_vkCmdPreprocessGeneratedCommandsEXT == nullptr)
            o_vkCmdPreprocessGeneratedCommandsEXT = (PFN_vkCmdPreprocessGeneratedCommandsEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdPreprocessGeneratedCommandsEXT;
    }
    if (procName == std::string("vkCmdExecuteGeneratedCommandsEXT"))
    {
        // LOG_DEBUG("vkCmdExecuteGeneratedCommandsEXT");

        if (o_vkCmdExecuteGeneratedCommandsEXT == nullptr)
            o_vkCmdExecuteGeneratedCommandsEXT = (PFN_vkCmdExecuteGeneratedCommandsEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdExecuteGeneratedCommandsEXT;
    }
    if (procName == std::string("vkCmdBuildAccelerationStructuresKHR"))
    {
        // LOG_DEBUG("vkCmdBuildAccelerationStructuresKHR");

        if (o_vkCmdBuildAccelerationStructuresKHR == nullptr)
            o_vkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdBuildAccelerationStructuresKHR;
    }
    if (procName == std::string("vkCmdBuildAccelerationStructuresIndirectKHR"))
    {
        // LOG_DEBUG("vkCmdBuildAccelerationStructuresIndirectKHR");

        if (o_vkCmdBuildAccelerationStructuresIndirectKHR == nullptr)
            o_vkCmdBuildAccelerationStructuresIndirectKHR = (PFN_vkCmdBuildAccelerationStructuresIndirectKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdBuildAccelerationStructuresIndirectKHR;
    }
    if (procName == std::string("vkCmdCopyAccelerationStructureKHR"))
    {
        // LOG_DEBUG("vkCmdCopyAccelerationStructureKHR");

        if (o_vkCmdCopyAccelerationStructureKHR == nullptr)
            o_vkCmdCopyAccelerationStructureKHR = (PFN_vkCmdCopyAccelerationStructureKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyAccelerationStructureKHR;
    }
    if (procName == std::string("vkCmdCopyAccelerationStructureToMemoryKHR"))
    {
        // LOG_DEBUG("vkCmdCopyAccelerationStructureToMemoryKHR");

        if (o_vkCmdCopyAccelerationStructureToMemoryKHR == nullptr)
            o_vkCmdCopyAccelerationStructureToMemoryKHR = (PFN_vkCmdCopyAccelerationStructureToMemoryKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyAccelerationStructureToMemoryKHR;
    }
    if (procName == std::string("vkCmdCopyMemoryToAccelerationStructureKHR"))
    {
        // LOG_DEBUG("vkCmdCopyMemoryToAccelerationStructureKHR");

        if (o_vkCmdCopyMemoryToAccelerationStructureKHR == nullptr)
            o_vkCmdCopyMemoryToAccelerationStructureKHR = (PFN_vkCmdCopyMemoryToAccelerationStructureKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdCopyMemoryToAccelerationStructureKHR;
    }
    if (procName == std::string("vkCmdWriteAccelerationStructuresPropertiesKHR"))
    {
        // LOG_DEBUG("vkCmdWriteAccelerationStructuresPropertiesKHR");

        if (o_vkCmdWriteAccelerationStructuresPropertiesKHR == nullptr)
            o_vkCmdWriteAccelerationStructuresPropertiesKHR =
                (PFN_vkCmdWriteAccelerationStructuresPropertiesKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdWriteAccelerationStructuresPropertiesKHR;
    }
    if (procName == std::string("vkCmdTraceRaysKHR"))
    {
        // LOG_DEBUG("vkCmdTraceRaysKHR");

        if (o_vkCmdTraceRaysKHR == nullptr)
            o_vkCmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdTraceRaysKHR;
    }
    if (procName == std::string("vkCmdTraceRaysIndirectKHR"))
    {
        // LOG_DEBUG("vkCmdTraceRaysIndirectKHR");

        if (o_vkCmdTraceRaysIndirectKHR == nullptr)
            o_vkCmdTraceRaysIndirectKHR = (PFN_vkCmdTraceRaysIndirectKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdTraceRaysIndirectKHR;
    }
    if (procName == std::string("vkCmdSetRayTracingPipelineStackSizeKHR"))
    {
        // LOG_DEBUG("vkCmdSetRayTracingPipelineStackSizeKHR");

        if (o_vkCmdSetRayTracingPipelineStackSizeKHR == nullptr)
            o_vkCmdSetRayTracingPipelineStackSizeKHR = (PFN_vkCmdSetRayTracingPipelineStackSizeKHR) original;

        return (PFN_vkVoidFunction) hk_vkCmdSetRayTracingPipelineStackSizeKHR;
    }
    if (procName == std::string("vkCmdDrawMeshTasksEXT"))
    {
        // LOG_DEBUG("vkCmdDrawMeshTasksEXT");

        if (o_vkCmdDrawMeshTasksEXT == nullptr)
            o_vkCmdDrawMeshTasksEXT = (PFN_vkCmdDrawMeshTasksEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdDrawMeshTasksEXT;
    }
    if (procName == std::string("vkCmdDrawMeshTasksIndirectEXT"))
    {
        // LOG_DEBUG("vkCmdDrawMeshTasksIndirectEXT");

        if (o_vkCmdDrawMeshTasksIndirectEXT == nullptr)
            o_vkCmdDrawMeshTasksIndirectEXT = (PFN_vkCmdDrawMeshTasksIndirectEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdDrawMeshTasksIndirectEXT;
    }
    if (procName == std::string("vkCmdDrawMeshTasksIndirectCountEXT"))
    {
        // LOG_DEBUG("vkCmdDrawMeshTasksIndirectCountEXT");

        if (o_vkCmdDrawMeshTasksIndirectCountEXT == nullptr)
            o_vkCmdDrawMeshTasksIndirectCountEXT = (PFN_vkCmdDrawMeshTasksIndirectCountEXT) original;

        return (PFN_vkVoidFunction) hk_vkCmdDrawMeshTasksIndirectCountEXT;
    }

    return VK_NULL_HANDLE;
}

void Vulkan_wDx12::InitializeStateTrackerFunctionTable()
{
    static bool initialized = false;

    if (initialized)
        return;

    vk_state::VulkanCmdFns fns {};

    // Core command buffer functions
    fns.CmdBindPipeline = o_vkCmdBindPipeline;
    fns.CmdBindDescriptorSets = o_vkCmdBindDescriptorSets;
    fns.CmdPushConstants = o_vkCmdPushConstants;
    fns.CmdSetViewport = o_vkCmdSetViewport;
    fns.CmdSetScissor = o_vkCmdSetScissor;
    fns.CmdBindVertexBuffers = o_vkCmdBindVertexBuffers;
    fns.CmdBindIndexBuffer = o_vkCmdBindIndexBuffer;
    fns.CmdPipelineBarrier = o_vkCmdPipelineBarrier;

    // Extended dynamic state functions (Vulkan 1.3 / VK_EXT_extended_dynamic_state)
    fns.CmdSetCullMode = o_vkCmdSetCullMode;
    fns.CmdSetFrontFace = o_vkCmdSetFrontFace;
    fns.CmdSetPrimitiveTopology = o_vkCmdSetPrimitiveTopology;
    fns.CmdSetDepthTestEnable = o_vkCmdSetDepthTestEnable;
    fns.CmdSetDepthWriteEnable = o_vkCmdSetDepthWriteEnable;
    fns.CmdSetDepthCompareOp = o_vkCmdSetDepthCompareOp;
    fns.CmdSetDepthBoundsTestEnable = o_vkCmdSetDepthBoundsTestEnable;
    fns.CmdSetStencilTestEnable = o_vkCmdSetStencilTestEnable;
    fns.CmdSetStencilOp = o_vkCmdSetStencilOp;

    // Set the function table in the state tracker
    cmdBufferStateTracker.SetFunctionTable(fns);

    initialized = true;

    LOG_DEBUG("State tracker function table initialized");
}

void Vulkan_wDx12::Hook(HMODULE vulkanModule)
{
    if (o_vkQueueSubmit != nullptr)
        return;

    o_vkQueueSubmit = (PFN_vkQueueSubmit) GetProcAddress(vulkanModule, "vkQueueSubmit");
    o_vkQueueSubmit2 = (PFN_vkQueueSubmit2) GetProcAddress(vulkanModule, "vkQueueSubmit2");
    o_vkQueueSubmit2KHR = (PFN_vkQueueSubmit2KHR) GetProcAddress(vulkanModule, "vkQueueSubmit2KHR");
    o_vkBeginCommandBuffer = (PFN_vkBeginCommandBuffer) GetProcAddress(vulkanModule, "vkBeginCommandBuffer");
    o_vkEndCommandBuffer = (PFN_vkEndCommandBuffer) GetProcAddress(vulkanModule, "vkEndCommandBuffer");
    o_vkResetCommandBuffer = (PFN_vkResetCommandBuffer) GetProcAddress(vulkanModule, "vkResetCommandBuffer");
    o_vkFreeCommandBuffers = (PFN_vkFreeCommandBuffers) GetProcAddress(vulkanModule, "vkFreeCommandBuffers");
    o_vkResetCommandPool = (PFN_vkResetCommandPool) GetProcAddress(vulkanModule, "vkResetCommandPool");
    o_vkCmdExecuteCommands = (PFN_vkCmdExecuteCommands) GetProcAddress(vulkanModule, "vkCmdExecuteCommands");
    o_vkAllocateCommandBuffers =
        (PFN_vkAllocateCommandBuffers) GetProcAddress(vulkanModule, "vkAllocateCommandBuffers");
    o_vkDestroyCommandPool = (PFN_vkDestroyCommandPool) GetProcAddress(vulkanModule, "vkDestroyCommandPool");
    o_vkCreateCommandPool = (PFN_vkCreateCommandPool) GetProcAddress(vulkanModule, "vkCreateCommandPool");

#pragma region vkCmd functions

    o_vkCmdBindPipeline = (PFN_vkCmdBindPipeline) GetProcAddress(vulkanModule, "vkCmdBindPipeline");
    o_vkCmdSetViewport = (PFN_vkCmdSetViewport) GetProcAddress(vulkanModule, "vkCmdSetViewport");
    o_vkCmdSetScissor = (PFN_vkCmdSetScissor) GetProcAddress(vulkanModule, "vkCmdSetScissor");
    o_vkCmdSetLineWidth = (PFN_vkCmdSetLineWidth) GetProcAddress(vulkanModule, "vkCmdSetLineWidth");
    o_vkCmdSetDepthBias = (PFN_vkCmdSetDepthBias) GetProcAddress(vulkanModule, "vkCmdSetDepthBias");
    o_vkCmdSetBlendConstants = (PFN_vkCmdSetBlendConstants) GetProcAddress(vulkanModule, "vkCmdSetBlendConstants");
    o_vkCmdSetDepthBounds = (PFN_vkCmdSetDepthBounds) GetProcAddress(vulkanModule, "vkCmdSetDepthBounds");
    o_vkCmdSetStencilCompareMask =
        (PFN_vkCmdSetStencilCompareMask) GetProcAddress(vulkanModule, "vkCmdSetStencilCompareMask");
    o_vkCmdSetStencilWriteMask =
        (PFN_vkCmdSetStencilWriteMask) GetProcAddress(vulkanModule, "vkCmdSetStencilWriteMask");
    o_vkCmdSetStencilReference =
        (PFN_vkCmdSetStencilReference) GetProcAddress(vulkanModule, "vkCmdSetStencilReference");
    o_vkCmdBindDescriptorSets = (PFN_vkCmdBindDescriptorSets) GetProcAddress(vulkanModule, "vkCmdBindDescriptorSets");
    o_vkCmdBindIndexBuffer = (PFN_vkCmdBindIndexBuffer) GetProcAddress(vulkanModule, "vkCmdBindIndexBuffer");
    o_vkCmdBindVertexBuffers = (PFN_vkCmdBindVertexBuffers) GetProcAddress(vulkanModule, "vkCmdBindVertexBuffers");
    o_vkCmdDraw = (PFN_vkCmdDraw) GetProcAddress(vulkanModule, "vkCmdDraw");
    o_vkCmdDrawIndexed = (PFN_vkCmdDrawIndexed) GetProcAddress(vulkanModule, "vkCmdDrawIndexed");
    o_vkCmdDrawIndirect = (PFN_vkCmdDrawIndirect) GetProcAddress(vulkanModule, "vkCmdDrawIndirect");
    o_vkCmdDrawIndexedIndirect =
        (PFN_vkCmdDrawIndexedIndirect) GetProcAddress(vulkanModule, "vkCmdDrawIndexedIndirect");
    o_vkCmdDispatch = (PFN_vkCmdDispatch) GetProcAddress(vulkanModule, "vkCmdDispatch");
    o_vkCmdDispatchIndirect = (PFN_vkCmdDispatchIndirect) GetProcAddress(vulkanModule, "vkCmdDispatchIndirect");
    o_vkCmdCopyBuffer = (PFN_vkCmdCopyBuffer) GetProcAddress(vulkanModule, "vkCmdCopyBuffer");
    o_vkCmdCopyImage = (PFN_vkCmdCopyImage) GetProcAddress(vulkanModule, "vkCmdCopyImage");
    o_vkCmdBlitImage = (PFN_vkCmdBlitImage) GetProcAddress(vulkanModule, "vkCmdBlitImage");
    o_vkCmdCopyBufferToImage = (PFN_vkCmdCopyBufferToImage) GetProcAddress(vulkanModule, "vkCmdCopyBufferToImage");
    o_vkCmdCopyImageToBuffer = (PFN_vkCmdCopyImageToBuffer) GetProcAddress(vulkanModule, "vkCmdCopyImageToBuffer");
    o_vkCmdUpdateBuffer = (PFN_vkCmdUpdateBuffer) GetProcAddress(vulkanModule, "vkCmdUpdateBuffer");
    o_vkCmdFillBuffer = (PFN_vkCmdFillBuffer) GetProcAddress(vulkanModule, "vkCmdFillBuffer");
    o_vkCmdClearColorImage = (PFN_vkCmdClearColorImage) GetProcAddress(vulkanModule, "vkCmdClearColorImage");
    o_vkCmdClearDepthStencilImage =
        (PFN_vkCmdClearDepthStencilImage) GetProcAddress(vulkanModule, "vkCmdClearDepthStencilImage");
    o_vkCmdClearAttachments = (PFN_vkCmdClearAttachments) GetProcAddress(vulkanModule, "vkCmdClearAttachments");
    o_vkCmdResolveImage = (PFN_vkCmdResolveImage) GetProcAddress(vulkanModule, "vkCmdResolveImage");
    o_vkCmdSetEvent = (PFN_vkCmdSetEvent) GetProcAddress(vulkanModule, "vkCmdSetEvent");
    o_vkCmdResetEvent = (PFN_vkCmdResetEvent) GetProcAddress(vulkanModule, "vkCmdResetEvent");
    o_vkCmdWaitEvents = (PFN_vkCmdWaitEvents) GetProcAddress(vulkanModule, "vkCmdWaitEvents");
    o_vkCmdPipelineBarrier = (PFN_vkCmdPipelineBarrier) GetProcAddress(vulkanModule, "vkCmdPipelineBarrier");
    o_vkCmdBeginQuery = (PFN_vkCmdBeginQuery) GetProcAddress(vulkanModule, "vkCmdBeginQuery");
    o_vkCmdEndQuery = (PFN_vkCmdEndQuery) GetProcAddress(vulkanModule, "vkCmdEndQuery");
    o_vkCmdResetQueryPool = (PFN_vkCmdResetQueryPool) GetProcAddress(vulkanModule, "vkCmdResetQueryPool");
    o_vkCmdWriteTimestamp = (PFN_vkCmdWriteTimestamp) GetProcAddress(vulkanModule, "vkCmdWriteTimestamp");
    o_vkCmdCopyQueryPoolResults =
        (PFN_vkCmdCopyQueryPoolResults) GetProcAddress(vulkanModule, "vkCmdCopyQueryPoolResults");
    o_vkCmdPushConstants = (PFN_vkCmdPushConstants) GetProcAddress(vulkanModule, "vkCmdPushConstants");
    o_vkCmdBeginRenderPass = (PFN_vkCmdBeginRenderPass) GetProcAddress(vulkanModule, "vkCmdBeginRenderPass");
    o_vkCmdNextSubpass = (PFN_vkCmdNextSubpass) GetProcAddress(vulkanModule, "vkCmdNextSubpass");
    o_vkCmdEndRenderPass = (PFN_vkCmdEndRenderPass) GetProcAddress(vulkanModule, "vkCmdEndRenderPass");
    o_vkCmdExecuteCommands = (PFN_vkCmdExecuteCommands) GetProcAddress(vulkanModule, "vkCmdExecuteCommands");
    o_vkCmdSetDeviceMask = (PFN_vkCmdSetDeviceMask) GetProcAddress(vulkanModule, "vkCmdSetDeviceMask");
    o_vkCmdDispatchBase = (PFN_vkCmdDispatchBase) GetProcAddress(vulkanModule, "vkCmdDispatchBase");
    o_vkCmdDrawIndirectCount = (PFN_vkCmdDrawIndirectCount) GetProcAddress(vulkanModule, "vkCmdDrawIndirectCount");
    o_vkCmdDrawIndexedIndirectCount =
        (PFN_vkCmdDrawIndexedIndirectCount) GetProcAddress(vulkanModule, "vkCmdDrawIndexedIndirectCount");
    o_vkCmdBeginRenderPass2 = (PFN_vkCmdBeginRenderPass2) GetProcAddress(vulkanModule, "vkCmdBeginRenderPass2");
    o_vkCmdNextSubpass2 = (PFN_vkCmdNextSubpass2) GetProcAddress(vulkanModule, "vkCmdNextSubpass2");
    o_vkCmdEndRenderPass2 = (PFN_vkCmdEndRenderPass2) GetProcAddress(vulkanModule, "vkCmdEndRenderPass2");
    o_vkCmdSetEvent2 = (PFN_vkCmdSetEvent2) GetProcAddress(vulkanModule, "vkCmdSetEvent2");
    o_vkCmdResetEvent2 = (PFN_vkCmdResetEvent2) GetProcAddress(vulkanModule, "vkCmdResetEvent2");
    o_vkCmdWaitEvents2 = (PFN_vkCmdWaitEvents2) GetProcAddress(vulkanModule, "vkCmdWaitEvents2");
    o_vkCmdPipelineBarrier2 = (PFN_vkCmdPipelineBarrier2) GetProcAddress(vulkanModule, "vkCmdPipelineBarrier2");
    o_vkCmdWriteTimestamp2 = (PFN_vkCmdWriteTimestamp2) GetProcAddress(vulkanModule, "vkCmdWriteTimestamp2");
    o_vkCmdCopyBuffer2 = (PFN_vkCmdCopyBuffer2) GetProcAddress(vulkanModule, "vkCmdCopyBuffer2");
    o_vkCmdCopyImage2 = (PFN_vkCmdCopyImage2) GetProcAddress(vulkanModule, "vkCmdCopyImage2");
    o_vkCmdCopyBufferToImage2 = (PFN_vkCmdCopyBufferToImage2) GetProcAddress(vulkanModule, "vkCmdCopyBufferToImage2");
    o_vkCmdCopyImageToBuffer2 = (PFN_vkCmdCopyImageToBuffer2) GetProcAddress(vulkanModule, "vkCmdCopyImageToBuffer2");
    o_vkCmdBlitImage2 = (PFN_vkCmdBlitImage2) GetProcAddress(vulkanModule, "vkCmdBlitImage2");
    o_vkCmdResolveImage2 = (PFN_vkCmdResolveImage2) GetProcAddress(vulkanModule, "vkCmdResolveImage2");
    o_vkCmdBeginRendering = (PFN_vkCmdBeginRendering) GetProcAddress(vulkanModule, "vkCmdBeginRendering");
    o_vkCmdEndRendering = (PFN_vkCmdEndRendering) GetProcAddress(vulkanModule, "vkCmdEndRendering");
    o_vkCmdSetCullMode = (PFN_vkCmdSetCullMode) GetProcAddress(vulkanModule, "vkCmdSetCullMode");
    o_vkCmdSetFrontFace = (PFN_vkCmdSetFrontFace) GetProcAddress(vulkanModule, "vkCmdSetFrontFace");
    o_vkCmdSetPrimitiveTopology =
        (PFN_vkCmdSetPrimitiveTopology) GetProcAddress(vulkanModule, "vkCmdSetPrimitiveTopology");
    o_vkCmdSetViewportWithCount =
        (PFN_vkCmdSetViewportWithCount) GetProcAddress(vulkanModule, "vkCmdSetViewportWithCount");
    o_vkCmdSetScissorWithCount =
        (PFN_vkCmdSetScissorWithCount) GetProcAddress(vulkanModule, "vkCmdSetScissorWithCount");
    o_vkCmdBindVertexBuffers2 = (PFN_vkCmdBindVertexBuffers2) GetProcAddress(vulkanModule, "vkCmdBindVertexBuffers2");
    o_vkCmdSetDepthTestEnable = (PFN_vkCmdSetDepthTestEnable) GetProcAddress(vulkanModule, "vkCmdSetDepthTestEnable");
    o_vkCmdSetDepthWriteEnable =
        (PFN_vkCmdSetDepthWriteEnable) GetProcAddress(vulkanModule, "vkCmdSetDepthWriteEnable");
    o_vkCmdSetDepthCompareOp = (PFN_vkCmdSetDepthCompareOp) GetProcAddress(vulkanModule, "vkCmdSetDepthCompareOp");
    o_vkCmdSetDepthBoundsTestEnable =
        (PFN_vkCmdSetDepthBoundsTestEnable) GetProcAddress(vulkanModule, "vkCmdSetDepthBoundsTestEnable");
    o_vkCmdSetStencilTestEnable =
        (PFN_vkCmdSetStencilTestEnable) GetProcAddress(vulkanModule, "vkCmdSetStencilTestEnable");
    o_vkCmdSetStencilOp = (PFN_vkCmdSetStencilOp) GetProcAddress(vulkanModule, "vkCmdSetStencilOp");
    o_vkCmdSetRasterizerDiscardEnable =
        (PFN_vkCmdSetRasterizerDiscardEnable) GetProcAddress(vulkanModule, "vkCmdSetRasterizerDiscardEnable");
    o_vkCmdSetDepthBiasEnable = (PFN_vkCmdSetDepthBiasEnable) GetProcAddress(vulkanModule, "vkCmdSetDepthBiasEnable");
    o_vkCmdSetPrimitiveRestartEnable =
        (PFN_vkCmdSetPrimitiveRestartEnable) GetProcAddress(vulkanModule, "vkCmdSetPrimitiveRestartEnable");
    o_vkCmdSetLineStipple = (PFN_vkCmdSetLineStipple) GetProcAddress(vulkanModule, "vkCmdSetLineStipple");
    o_vkCmdBindIndexBuffer2 = (PFN_vkCmdBindIndexBuffer2) GetProcAddress(vulkanModule, "vkCmdBindIndexBuffer2");
    o_vkCmdPushDescriptorSet = (PFN_vkCmdPushDescriptorSet) GetProcAddress(vulkanModule, "vkCmdPushDescriptorSet");
    o_vkCmdPushDescriptorSetWithTemplate =
        (PFN_vkCmdPushDescriptorSetWithTemplate) GetProcAddress(vulkanModule, "vkCmdPushDescriptorSetWithTemplate");
    o_vkCmdSetRenderingAttachmentLocations =
        (PFN_vkCmdSetRenderingAttachmentLocations) GetProcAddress(vulkanModule, "vkCmdSetRenderingAttachmentLocations");
    o_vkCmdSetRenderingInputAttachmentIndices = (PFN_vkCmdSetRenderingInputAttachmentIndices) GetProcAddress(
        vulkanModule, "vkCmdSetRenderingInputAttachmentIndices");
    o_vkCmdBindDescriptorSets2 =
        (PFN_vkCmdBindDescriptorSets2) GetProcAddress(vulkanModule, "vkCmdBindDescriptorSets2");
    o_vkCmdPushConstants2 = (PFN_vkCmdPushConstants2) GetProcAddress(vulkanModule, "vkCmdPushConstants2");
    o_vkCmdPushDescriptorSet2 = (PFN_vkCmdPushDescriptorSet2) GetProcAddress(vulkanModule, "vkCmdPushDescriptorSet2");
    o_vkCmdPushDescriptorSetWithTemplate2 =
        (PFN_vkCmdPushDescriptorSetWithTemplate2) GetProcAddress(vulkanModule, "vkCmdPushDescriptorSetWithTemplate2");
    o_vkCmdBeginVideoCodingKHR =
        (PFN_vkCmdBeginVideoCodingKHR) GetProcAddress(vulkanModule, "vkCmdBeginVideoCodingKHR");
    o_vkCmdEndVideoCodingKHR = (PFN_vkCmdEndVideoCodingKHR) GetProcAddress(vulkanModule, "vkCmdEndVideoCodingKHR");
    o_vkCmdControlVideoCodingKHR =
        (PFN_vkCmdControlVideoCodingKHR) GetProcAddress(vulkanModule, "vkCmdControlVideoCodingKHR");
    o_vkCmdDecodeVideoKHR = (PFN_vkCmdDecodeVideoKHR) GetProcAddress(vulkanModule, "vkCmdDecodeVideoKHR");
    o_vkCmdBeginRenderingKHR = (PFN_vkCmdBeginRenderingKHR) GetProcAddress(vulkanModule, "vkCmdBeginRenderingKHR");
    o_vkCmdEndRenderingKHR = (PFN_vkCmdEndRenderingKHR) GetProcAddress(vulkanModule, "vkCmdEndRenderingKHR");
    o_vkCmdSetDeviceMaskKHR = (PFN_vkCmdSetDeviceMaskKHR) GetProcAddress(vulkanModule, "vkCmdSetDeviceMaskKHR");
    o_vkCmdDispatchBaseKHR = (PFN_vkCmdDispatchBaseKHR) GetProcAddress(vulkanModule, "vkCmdDispatchBaseKHR");
    o_vkCmdPushDescriptorSetKHR =
        (PFN_vkCmdPushDescriptorSetKHR) GetProcAddress(vulkanModule, "vkCmdPushDescriptorSetKHR");
    o_vkCmdPushDescriptorSetWithTemplateKHR = (PFN_vkCmdPushDescriptorSetWithTemplateKHR) GetProcAddress(
        vulkanModule, "vkCmdPushDescriptorSetWithTemplateKHR");
    o_vkCmdBeginRenderPass2KHR =
        (PFN_vkCmdBeginRenderPass2KHR) GetProcAddress(vulkanModule, "vkCmdBeginRenderPass2KHR");
    o_vkCmdNextSubpass2KHR = (PFN_vkCmdNextSubpass2KHR) GetProcAddress(vulkanModule, "vkCmdNextSubpass2KHR");
    o_vkCmdEndRenderPass2KHR = (PFN_vkCmdEndRenderPass2KHR) GetProcAddress(vulkanModule, "vkCmdEndRenderPass2KHR");
    o_vkCmdDrawIndirectCountKHR =
        (PFN_vkCmdDrawIndirectCountKHR) GetProcAddress(vulkanModule, "vkCmdDrawIndirectCountKHR");
    o_vkCmdDrawIndexedIndirectCountKHR =
        (PFN_vkCmdDrawIndexedIndirectCountKHR) GetProcAddress(vulkanModule, "vkCmdDrawIndexedIndirectCountKHR");
    o_vkCmdSetFragmentShadingRateKHR =
        (PFN_vkCmdSetFragmentShadingRateKHR) GetProcAddress(vulkanModule, "vkCmdSetFragmentShadingRateKHR");
    o_vkCmdSetRenderingAttachmentLocationsKHR = (PFN_vkCmdSetRenderingAttachmentLocationsKHR) GetProcAddress(
        vulkanModule, "vkCmdSetRenderingAttachmentLocationsKHR");
    o_vkCmdSetRenderingInputAttachmentIndicesKHR = (PFN_vkCmdSetRenderingInputAttachmentIndicesKHR) GetProcAddress(
        vulkanModule, "vkCmdSetRenderingInputAttachmentIndicesKHR");
    o_vkCmdEncodeVideoKHR = (PFN_vkCmdEncodeVideoKHR) GetProcAddress(vulkanModule, "vkCmdEncodeVideoKHR");
    o_vkCmdSetEvent2KHR = (PFN_vkCmdSetEvent2KHR) GetProcAddress(vulkanModule, "vkCmdSetEvent2KHR");
    o_vkCmdResetEvent2KHR = (PFN_vkCmdResetEvent2KHR) GetProcAddress(vulkanModule, "vkCmdResetEvent2KHR");
    o_vkCmdWaitEvents2KHR = (PFN_vkCmdWaitEvents2KHR) GetProcAddress(vulkanModule, "vkCmdWaitEvents2KHR");
    o_vkCmdPipelineBarrier2KHR =
        (PFN_vkCmdPipelineBarrier2KHR) GetProcAddress(vulkanModule, "vkCmdPipelineBarrier2KHR");
    o_vkCmdWriteTimestamp2KHR = (PFN_vkCmdWriteTimestamp2KHR) GetProcAddress(vulkanModule, "vkCmdWriteTimestamp2KHR");
    o_vkCmdCopyBuffer2KHR = (PFN_vkCmdCopyBuffer2KHR) GetProcAddress(vulkanModule, "vkCmdCopyBuffer2KHR");
    o_vkCmdCopyImage2KHR = (PFN_vkCmdCopyImage2KHR) GetProcAddress(vulkanModule, "vkCmdCopyImage2KHR");
    o_vkCmdCopyBufferToImage2KHR =
        (PFN_vkCmdCopyBufferToImage2KHR) GetProcAddress(vulkanModule, "vkCmdCopyBufferToImage2KHR");
    o_vkCmdCopyImageToBuffer2KHR =
        (PFN_vkCmdCopyImageToBuffer2KHR) GetProcAddress(vulkanModule, "vkCmdCopyImageToBuffer2KHR");
    o_vkCmdBlitImage2KHR = (PFN_vkCmdBlitImage2KHR) GetProcAddress(vulkanModule, "vkCmdBlitImage2KHR");
    o_vkCmdResolveImage2KHR = (PFN_vkCmdResolveImage2KHR) GetProcAddress(vulkanModule, "vkCmdResolveImage2KHR");
    o_vkCmdTraceRaysIndirect2KHR =
        (PFN_vkCmdTraceRaysIndirect2KHR) GetProcAddress(vulkanModule, "vkCmdTraceRaysIndirect2KHR");
    o_vkCmdBindIndexBuffer2KHR =
        (PFN_vkCmdBindIndexBuffer2KHR) GetProcAddress(vulkanModule, "vkCmdBindIndexBuffer2KHR");
    o_vkCmdSetLineStippleKHR = (PFN_vkCmdSetLineStippleKHR) GetProcAddress(vulkanModule, "vkCmdSetLineStippleKHR");
    o_vkCmdBindDescriptorSets2KHR =
        (PFN_vkCmdBindDescriptorSets2KHR) GetProcAddress(vulkanModule, "vkCmdBindDescriptorSets2KHR");
    o_vkCmdPushConstants2KHR = (PFN_vkCmdPushConstants2KHR) GetProcAddress(vulkanModule, "vkCmdPushConstants2KHR");
    o_vkCmdPushDescriptorSet2KHR =
        (PFN_vkCmdPushDescriptorSet2KHR) GetProcAddress(vulkanModule, "vkCmdPushDescriptorSet2KHR");
    o_vkCmdPushDescriptorSetWithTemplate2KHR = (PFN_vkCmdPushDescriptorSetWithTemplate2KHR) GetProcAddress(
        vulkanModule, "vkCmdPushDescriptorSetWithTemplate2KHR");
    o_vkCmdSetDescriptorBufferOffsets2EXT =
        (PFN_vkCmdSetDescriptorBufferOffsets2EXT) GetProcAddress(vulkanModule, "vkCmdSetDescriptorBufferOffsets2EXT");
    o_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT =
        (PFN_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT) GetProcAddress(
            vulkanModule, "vkCmdBindDescriptorBufferEmbeddedSamplers2EXT");
    o_vkCmdDebugMarkerBeginEXT =
        (PFN_vkCmdDebugMarkerBeginEXT) GetProcAddress(vulkanModule, "vkCmdDebugMarkerBeginEXT");
    o_vkCmdDebugMarkerEndEXT = (PFN_vkCmdDebugMarkerEndEXT) GetProcAddress(vulkanModule, "vkCmdDebugMarkerEndEXT");
    o_vkCmdDebugMarkerInsertEXT =
        (PFN_vkCmdDebugMarkerInsertEXT) GetProcAddress(vulkanModule, "vkCmdDebugMarkerInsertEXT");
    o_vkCmdBindTransformFeedbackBuffersEXT =
        (PFN_vkCmdBindTransformFeedbackBuffersEXT) GetProcAddress(vulkanModule, "vkCmdBindTransformFeedbackBuffersEXT");
    o_vkCmdBeginTransformFeedbackEXT =
        (PFN_vkCmdBeginTransformFeedbackEXT) GetProcAddress(vulkanModule, "vkCmdBeginTransformFeedbackEXT");
    o_vkCmdEndTransformFeedbackEXT =
        (PFN_vkCmdEndTransformFeedbackEXT) GetProcAddress(vulkanModule, "vkCmdEndTransformFeedbackEXT");
    o_vkCmdBeginQueryIndexedEXT =
        (PFN_vkCmdBeginQueryIndexedEXT) GetProcAddress(vulkanModule, "vkCmdBeginQueryIndexedEXT");
    o_vkCmdEndQueryIndexedEXT = (PFN_vkCmdEndQueryIndexedEXT) GetProcAddress(vulkanModule, "vkCmdEndQueryIndexedEXT");
    o_vkCmdDrawIndirectByteCountEXT =
        (PFN_vkCmdDrawIndirectByteCountEXT) GetProcAddress(vulkanModule, "vkCmdDrawIndirectByteCountEXT");
    o_vkCmdCuLaunchKernelNVX = (PFN_vkCmdCuLaunchKernelNVX) GetProcAddress(vulkanModule, "vkCmdCuLaunchKernelNVX");
    o_vkCmdDrawIndirectCountAMD =
        (PFN_vkCmdDrawIndirectCountAMD) GetProcAddress(vulkanModule, "vkCmdDrawIndirectCountAMD");
    o_vkCmdDrawIndexedIndirectCountAMD =
        (PFN_vkCmdDrawIndexedIndirectCountAMD) GetProcAddress(vulkanModule, "vkCmdDrawIndexedIndirectCountAMD");
    o_vkCmdBeginConditionalRenderingEXT =
        (PFN_vkCmdBeginConditionalRenderingEXT) GetProcAddress(vulkanModule, "vkCmdBeginConditionalRenderingEXT");
    o_vkCmdEndConditionalRenderingEXT =
        (PFN_vkCmdEndConditionalRenderingEXT) GetProcAddress(vulkanModule, "vkCmdEndConditionalRenderingEXT");
    o_vkCmdSetViewportWScalingNV =
        (PFN_vkCmdSetViewportWScalingNV) GetProcAddress(vulkanModule, "vkCmdSetViewportWScalingNV");
    o_vkCmdSetDiscardRectangleEXT =
        (PFN_vkCmdSetDiscardRectangleEXT) GetProcAddress(vulkanModule, "vkCmdSetDiscardRectangleEXT");
    o_vkCmdSetDiscardRectangleEnableEXT =
        (PFN_vkCmdSetDiscardRectangleEnableEXT) GetProcAddress(vulkanModule, "vkCmdSetDiscardRectangleEnableEXT");
    o_vkCmdSetDiscardRectangleModeEXT =
        (PFN_vkCmdSetDiscardRectangleModeEXT) GetProcAddress(vulkanModule, "vkCmdSetDiscardRectangleModeEXT");
    o_vkCmdBeginDebugUtilsLabelEXT =
        (PFN_vkCmdBeginDebugUtilsLabelEXT) GetProcAddress(vulkanModule, "vkCmdBeginDebugUtilsLabelEXT");
    o_vkCmdEndDebugUtilsLabelEXT =
        (PFN_vkCmdEndDebugUtilsLabelEXT) GetProcAddress(vulkanModule, "vkCmdEndDebugUtilsLabelEXT");
    o_vkCmdInsertDebugUtilsLabelEXT =
        (PFN_vkCmdInsertDebugUtilsLabelEXT) GetProcAddress(vulkanModule, "vkCmdInsertDebugUtilsLabelEXT");
    o_vkCmdSetSampleLocationsEXT =
        (PFN_vkCmdSetSampleLocationsEXT) GetProcAddress(vulkanModule, "vkCmdSetSampleLocationsEXT");
    o_vkCmdBindShadingRateImageNV =
        (PFN_vkCmdBindShadingRateImageNV) GetProcAddress(vulkanModule, "vkCmdBindShadingRateImageNV");
    o_vkCmdSetViewportShadingRatePaletteNV =
        (PFN_vkCmdSetViewportShadingRatePaletteNV) GetProcAddress(vulkanModule, "vkCmdSetViewportShadingRatePaletteNV");
    o_vkCmdSetCoarseSampleOrderNV =
        (PFN_vkCmdSetCoarseSampleOrderNV) GetProcAddress(vulkanModule, "vkCmdSetCoarseSampleOrderNV");
    o_vkCmdBuildAccelerationStructureNV =
        (PFN_vkCmdBuildAccelerationStructureNV) GetProcAddress(vulkanModule, "vkCmdBuildAccelerationStructureNV");
    o_vkCmdCopyAccelerationStructureNV =
        (PFN_vkCmdCopyAccelerationStructureNV) GetProcAddress(vulkanModule, "vkCmdCopyAccelerationStructureNV");
    o_vkCmdTraceRaysNV = (PFN_vkCmdTraceRaysNV) GetProcAddress(vulkanModule, "vkCmdTraceRaysNV");
    o_vkCmdWriteAccelerationStructuresPropertiesNV = (PFN_vkCmdWriteAccelerationStructuresPropertiesNV) GetProcAddress(
        vulkanModule, "vkCmdWriteAccelerationStructuresPropertiesNV");
    o_vkCmdWriteBufferMarkerAMD =
        (PFN_vkCmdWriteBufferMarkerAMD) GetProcAddress(vulkanModule, "vkCmdWriteBufferMarkerAMD");
    o_vkCmdWriteBufferMarker2AMD =
        (PFN_vkCmdWriteBufferMarker2AMD) GetProcAddress(vulkanModule, "vkCmdWriteBufferMarker2AMD");
    o_vkCmdDrawMeshTasksNV = (PFN_vkCmdDrawMeshTasksNV) GetProcAddress(vulkanModule, "vkCmdDrawMeshTasksNV");
    o_vkCmdDrawMeshTasksIndirectNV =
        (PFN_vkCmdDrawMeshTasksIndirectNV) GetProcAddress(vulkanModule, "vkCmdDrawMeshTasksIndirectNV");
    o_vkCmdDrawMeshTasksIndirectCountNV =
        (PFN_vkCmdDrawMeshTasksIndirectCountNV) GetProcAddress(vulkanModule, "vkCmdDrawMeshTasksIndirectCountNV");
    o_vkCmdSetExclusiveScissorEnableNV =
        (PFN_vkCmdSetExclusiveScissorEnableNV) GetProcAddress(vulkanModule, "vkCmdSetExclusiveScissorEnableNV");
    o_vkCmdSetExclusiveScissorNV =
        (PFN_vkCmdSetExclusiveScissorNV) GetProcAddress(vulkanModule, "vkCmdSetExclusiveScissorNV");
    o_vkCmdSetCheckpointNV = (PFN_vkCmdSetCheckpointNV) GetProcAddress(vulkanModule, "vkCmdSetCheckpointNV");
    o_vkCmdSetPerformanceMarkerINTEL =
        (PFN_vkCmdSetPerformanceMarkerINTEL) GetProcAddress(vulkanModule, "vkCmdSetPerformanceMarkerINTEL");
    o_vkCmdSetPerformanceStreamMarkerINTEL =
        (PFN_vkCmdSetPerformanceStreamMarkerINTEL) GetProcAddress(vulkanModule, "vkCmdSetPerformanceStreamMarkerINTEL");
    o_vkCmdSetPerformanceOverrideINTEL =
        (PFN_vkCmdSetPerformanceOverrideINTEL) GetProcAddress(vulkanModule, "vkCmdSetPerformanceOverrideINTEL");
    o_vkCmdSetLineStippleEXT = (PFN_vkCmdSetLineStippleEXT) GetProcAddress(vulkanModule, "vkCmdSetLineStippleEXT");
    o_vkCmdSetCullModeEXT = (PFN_vkCmdSetCullModeEXT) GetProcAddress(vulkanModule, "vkCmdSetCullModeEXT");
    o_vkCmdSetFrontFaceEXT = (PFN_vkCmdSetFrontFaceEXT) GetProcAddress(vulkanModule, "vkCmdSetFrontFaceEXT");
    o_vkCmdSetPrimitiveTopologyEXT =
        (PFN_vkCmdSetPrimitiveTopologyEXT) GetProcAddress(vulkanModule, "vkCmdSetPrimitiveTopologyEXT");
    o_vkCmdSetViewportWithCountEXT =
        (PFN_vkCmdSetViewportWithCountEXT) GetProcAddress(vulkanModule, "vkCmdSetViewportWithCountEXT");
    o_vkCmdSetScissorWithCountEXT =
        (PFN_vkCmdSetScissorWithCountEXT) GetProcAddress(vulkanModule, "vkCmdSetScissorWithCountEXT");
    o_vkCmdBindVertexBuffers2EXT =
        (PFN_vkCmdBindVertexBuffers2EXT) GetProcAddress(vulkanModule, "vkCmdBindVertexBuffers2EXT");
    o_vkCmdSetDepthTestEnableEXT =
        (PFN_vkCmdSetDepthTestEnableEXT) GetProcAddress(vulkanModule, "vkCmdSetDepthTestEnableEXT");
    o_vkCmdSetDepthWriteEnableEXT =
        (PFN_vkCmdSetDepthWriteEnableEXT) GetProcAddress(vulkanModule, "vkCmdSetDepthWriteEnableEXT");
    o_vkCmdSetDepthCompareOpEXT =
        (PFN_vkCmdSetDepthCompareOpEXT) GetProcAddress(vulkanModule, "vkCmdSetDepthCompareOpEXT");
    o_vkCmdSetDepthBoundsTestEnableEXT =
        (PFN_vkCmdSetDepthBoundsTestEnableEXT) GetProcAddress(vulkanModule, "vkCmdSetDepthBoundsTestEnableEXT");
    o_vkCmdSetStencilTestEnableEXT =
        (PFN_vkCmdSetStencilTestEnableEXT) GetProcAddress(vulkanModule, "vkCmdSetStencilTestEnableEXT");
    o_vkCmdSetStencilOpEXT = (PFN_vkCmdSetStencilOpEXT) GetProcAddress(vulkanModule, "vkCmdSetStencilOpEXT");
    o_vkCmdPreprocessGeneratedCommandsNV =
        (PFN_vkCmdPreprocessGeneratedCommandsNV) GetProcAddress(vulkanModule, "vkCmdPreprocessGeneratedCommandsNV");
    o_vkCmdExecuteGeneratedCommandsNV =
        (PFN_vkCmdExecuteGeneratedCommandsNV) GetProcAddress(vulkanModule, "vkCmdExecuteGeneratedCommandsNV");
    o_vkCmdBindPipelineShaderGroupNV =
        (PFN_vkCmdBindPipelineShaderGroupNV) GetProcAddress(vulkanModule, "vkCmdBindPipelineShaderGroupNV");
    o_vkCmdSetDepthBias2EXT = (PFN_vkCmdSetDepthBias2EXT) GetProcAddress(vulkanModule, "vkCmdSetDepthBias2EXT");
    o_vkCmdCudaLaunchKernelNV = (PFN_vkCmdCudaLaunchKernelNV) GetProcAddress(vulkanModule, "vkCmdCudaLaunchKernelNV");
    o_vkCmdBindDescriptorBuffersEXT =
        (PFN_vkCmdBindDescriptorBuffersEXT) GetProcAddress(vulkanModule, "vkCmdBindDescriptorBuffersEXT");
    o_vkCmdSetDescriptorBufferOffsetsEXT =
        (PFN_vkCmdSetDescriptorBufferOffsetsEXT) GetProcAddress(vulkanModule, "vkCmdSetDescriptorBufferOffsetsEXT");
    o_vkCmdBindDescriptorBufferEmbeddedSamplersEXT = (PFN_vkCmdBindDescriptorBufferEmbeddedSamplersEXT) GetProcAddress(
        vulkanModule, "vkCmdBindDescriptorBufferEmbeddedSamplersEXT");
    o_vkCmdSetFragmentShadingRateEnumNV =
        (PFN_vkCmdSetFragmentShadingRateEnumNV) GetProcAddress(vulkanModule, "vkCmdSetFragmentShadingRateEnumNV");
    o_vkCmdSetVertexInputEXT = (PFN_vkCmdSetVertexInputEXT) GetProcAddress(vulkanModule, "vkCmdSetVertexInputEXT");
    o_vkCmdSubpassShadingHUAWEI =
        (PFN_vkCmdSubpassShadingHUAWEI) GetProcAddress(vulkanModule, "vkCmdSubpassShadingHUAWEI");
    o_vkCmdBindInvocationMaskHUAWEI =
        (PFN_vkCmdBindInvocationMaskHUAWEI) GetProcAddress(vulkanModule, "vkCmdBindInvocationMaskHUAWEI");
    o_vkCmdSetPatchControlPointsEXT =
        (PFN_vkCmdSetPatchControlPointsEXT) GetProcAddress(vulkanModule, "vkCmdSetPatchControlPointsEXT");
    o_vkCmdSetRasterizerDiscardEnableEXT =
        (PFN_vkCmdSetRasterizerDiscardEnableEXT) GetProcAddress(vulkanModule, "vkCmdSetRasterizerDiscardEnableEXT");
    o_vkCmdSetDepthBiasEnableEXT =
        (PFN_vkCmdSetDepthBiasEnableEXT) GetProcAddress(vulkanModule, "vkCmdSetDepthBiasEnableEXT");
    o_vkCmdSetLogicOpEXT = (PFN_vkCmdSetLogicOpEXT) GetProcAddress(vulkanModule, "vkCmdSetLogicOpEXT");
    o_vkCmdSetPrimitiveRestartEnableEXT =
        (PFN_vkCmdSetPrimitiveRestartEnableEXT) GetProcAddress(vulkanModule, "vkCmdSetPrimitiveRestartEnableEXT");
    o_vkCmdSetColorWriteEnableEXT =
        (PFN_vkCmdSetColorWriteEnableEXT) GetProcAddress(vulkanModule, "vkCmdSetColorWriteEnableEXT");
    o_vkCmdDrawMultiEXT = (PFN_vkCmdDrawMultiEXT) GetProcAddress(vulkanModule, "vkCmdDrawMultiEXT");
    o_vkCmdDrawMultiIndexedEXT =
        (PFN_vkCmdDrawMultiIndexedEXT) GetProcAddress(vulkanModule, "vkCmdDrawMultiIndexedEXT");
    o_vkCmdBuildMicromapsEXT = (PFN_vkCmdBuildMicromapsEXT) GetProcAddress(vulkanModule, "vkCmdBuildMicromapsEXT");
    o_vkCmdCopyMicromapEXT = (PFN_vkCmdCopyMicromapEXT) GetProcAddress(vulkanModule, "vkCmdCopyMicromapEXT");
    o_vkCmdCopyMicromapToMemoryEXT =
        (PFN_vkCmdCopyMicromapToMemoryEXT) GetProcAddress(vulkanModule, "vkCmdCopyMicromapToMemoryEXT");
    o_vkCmdCopyMemoryToMicromapEXT =
        (PFN_vkCmdCopyMemoryToMicromapEXT) GetProcAddress(vulkanModule, "vkCmdCopyMemoryToMicromapEXT");
    o_vkCmdWriteMicromapsPropertiesEXT =
        (PFN_vkCmdWriteMicromapsPropertiesEXT) GetProcAddress(vulkanModule, "vkCmdWriteMicromapsPropertiesEXT");
    o_vkCmdDrawClusterHUAWEI = (PFN_vkCmdDrawClusterHUAWEI) GetProcAddress(vulkanModule, "vkCmdDrawClusterHUAWEI");
    o_vkCmdDrawClusterIndirectHUAWEI =
        (PFN_vkCmdDrawClusterIndirectHUAWEI) GetProcAddress(vulkanModule, "vkCmdDrawClusterIndirectHUAWEI");
    o_vkCmdCopyMemoryIndirectNV =
        (PFN_vkCmdCopyMemoryIndirectNV) GetProcAddress(vulkanModule, "vkCmdCopyMemoryIndirectNV");
    o_vkCmdCopyMemoryToImageIndirectNV =
        (PFN_vkCmdCopyMemoryToImageIndirectNV) GetProcAddress(vulkanModule, "vkCmdCopyMemoryToImageIndirectNV");
    o_vkCmdDecompressMemoryNV = (PFN_vkCmdDecompressMemoryNV) GetProcAddress(vulkanModule, "vkCmdDecompressMemoryNV");
    o_vkCmdDecompressMemoryIndirectCountNV =
        (PFN_vkCmdDecompressMemoryIndirectCountNV) GetProcAddress(vulkanModule, "vkCmdDecompressMemoryIndirectCountNV");
    o_vkCmdUpdatePipelineIndirectBufferNV =
        (PFN_vkCmdUpdatePipelineIndirectBufferNV) GetProcAddress(vulkanModule, "vkCmdUpdatePipelineIndirectBufferNV");
    o_vkCmdSetDepthClampEnableEXT =
        (PFN_vkCmdSetDepthClampEnableEXT) GetProcAddress(vulkanModule, "vkCmdSetDepthClampEnableEXT");
    o_vkCmdSetPolygonModeEXT = (PFN_vkCmdSetPolygonModeEXT) GetProcAddress(vulkanModule, "vkCmdSetPolygonModeEXT");
    o_vkCmdSetRasterizationSamplesEXT =
        (PFN_vkCmdSetRasterizationSamplesEXT) GetProcAddress(vulkanModule, "vkCmdSetRasterizationSamplesEXT");
    o_vkCmdSetSampleMaskEXT = (PFN_vkCmdSetSampleMaskEXT) GetProcAddress(vulkanModule, "vkCmdSetSampleMaskEXT");
    o_vkCmdSetAlphaToCoverageEnableEXT =
        (PFN_vkCmdSetAlphaToCoverageEnableEXT) GetProcAddress(vulkanModule, "vkCmdSetAlphaToCoverageEnableEXT");
    o_vkCmdSetAlphaToOneEnableEXT =
        (PFN_vkCmdSetAlphaToOneEnableEXT) GetProcAddress(vulkanModule, "vkCmdSetAlphaToOneEnableEXT");
    o_vkCmdSetLogicOpEnableEXT =
        (PFN_vkCmdSetLogicOpEnableEXT) GetProcAddress(vulkanModule, "vkCmdSetLogicOpEnableEXT");
    o_vkCmdSetColorBlendEnableEXT =
        (PFN_vkCmdSetColorBlendEnableEXT) GetProcAddress(vulkanModule, "vkCmdSetColorBlendEnableEXT");
    o_vkCmdSetColorBlendEquationEXT =
        (PFN_vkCmdSetColorBlendEquationEXT) GetProcAddress(vulkanModule, "vkCmdSetColorBlendEquationEXT");
    o_vkCmdSetColorWriteMaskEXT =
        (PFN_vkCmdSetColorWriteMaskEXT) GetProcAddress(vulkanModule, "vkCmdSetColorWriteMaskEXT");
    o_vkCmdSetTessellationDomainOriginEXT =
        (PFN_vkCmdSetTessellationDomainOriginEXT) GetProcAddress(vulkanModule, "vkCmdSetTessellationDomainOriginEXT");
    o_vkCmdSetRasterizationStreamEXT =
        (PFN_vkCmdSetRasterizationStreamEXT) GetProcAddress(vulkanModule, "vkCmdSetRasterizationStreamEXT");
    o_vkCmdSetConservativeRasterizationModeEXT = (PFN_vkCmdSetConservativeRasterizationModeEXT) GetProcAddress(
        vulkanModule, "vkCmdSetConservativeRasterizationModeEXT");
    o_vkCmdSetExtraPrimitiveOverestimationSizeEXT = (PFN_vkCmdSetExtraPrimitiveOverestimationSizeEXT) GetProcAddress(
        vulkanModule, "vkCmdSetExtraPrimitiveOverestimationSizeEXT");
    o_vkCmdSetDepthClipEnableEXT =
        (PFN_vkCmdSetDepthClipEnableEXT) GetProcAddress(vulkanModule, "vkCmdSetDepthClipEnableEXT");
    o_vkCmdSetSampleLocationsEnableEXT =
        (PFN_vkCmdSetSampleLocationsEnableEXT) GetProcAddress(vulkanModule, "vkCmdSetSampleLocationsEnableEXT");
    o_vkCmdSetColorBlendAdvancedEXT =
        (PFN_vkCmdSetColorBlendAdvancedEXT) GetProcAddress(vulkanModule, "vkCmdSetColorBlendAdvancedEXT");
    o_vkCmdSetProvokingVertexModeEXT =
        (PFN_vkCmdSetProvokingVertexModeEXT) GetProcAddress(vulkanModule, "vkCmdSetProvokingVertexModeEXT");
    o_vkCmdSetLineRasterizationModeEXT =
        (PFN_vkCmdSetLineRasterizationModeEXT) GetProcAddress(vulkanModule, "vkCmdSetLineRasterizationModeEXT");
    o_vkCmdSetLineStippleEnableEXT =
        (PFN_vkCmdSetLineStippleEnableEXT) GetProcAddress(vulkanModule, "vkCmdSetLineStippleEnableEXT");
    o_vkCmdSetDepthClipNegativeOneToOneEXT =
        (PFN_vkCmdSetDepthClipNegativeOneToOneEXT) GetProcAddress(vulkanModule, "vkCmdSetDepthClipNegativeOneToOneEXT");
    o_vkCmdSetViewportWScalingEnableNV =
        (PFN_vkCmdSetViewportWScalingEnableNV) GetProcAddress(vulkanModule, "vkCmdSetViewportWScalingEnableNV");
    o_vkCmdSetViewportSwizzleNV =
        (PFN_vkCmdSetViewportSwizzleNV) GetProcAddress(vulkanModule, "vkCmdSetViewportSwizzleNV");
    o_vkCmdSetCoverageToColorEnableNV =
        (PFN_vkCmdSetCoverageToColorEnableNV) GetProcAddress(vulkanModule, "vkCmdSetCoverageToColorEnableNV");
    o_vkCmdSetCoverageToColorLocationNV =
        (PFN_vkCmdSetCoverageToColorLocationNV) GetProcAddress(vulkanModule, "vkCmdSetCoverageToColorLocationNV");
    o_vkCmdSetCoverageModulationModeNV =
        (PFN_vkCmdSetCoverageModulationModeNV) GetProcAddress(vulkanModule, "vkCmdSetCoverageModulationModeNV");
    o_vkCmdSetCoverageModulationTableEnableNV = (PFN_vkCmdSetCoverageModulationTableEnableNV) GetProcAddress(
        vulkanModule, "vkCmdSetCoverageModulationTableEnableNV");
    o_vkCmdSetCoverageModulationTableNV =
        (PFN_vkCmdSetCoverageModulationTableNV) GetProcAddress(vulkanModule, "vkCmdSetCoverageModulationTableNV");
    o_vkCmdSetShadingRateImageEnableNV =
        (PFN_vkCmdSetShadingRateImageEnableNV) GetProcAddress(vulkanModule, "vkCmdSetShadingRateImageEnableNV");
    o_vkCmdSetRepresentativeFragmentTestEnableNV = (PFN_vkCmdSetRepresentativeFragmentTestEnableNV) GetProcAddress(
        vulkanModule, "vkCmdSetRepresentativeFragmentTestEnableNV");
    o_vkCmdSetCoverageReductionModeNV =
        (PFN_vkCmdSetCoverageReductionModeNV) GetProcAddress(vulkanModule, "vkCmdSetCoverageReductionModeNV");
    o_vkCmdOpticalFlowExecuteNV =
        (PFN_vkCmdOpticalFlowExecuteNV) GetProcAddress(vulkanModule, "vkCmdOpticalFlowExecuteNV");
    o_vkCmdBindShadersEXT = (PFN_vkCmdBindShadersEXT) GetProcAddress(vulkanModule, "vkCmdBindShadersEXT");
    o_vkCmdSetDepthClampRangeEXT =
        (PFN_vkCmdSetDepthClampRangeEXT) GetProcAddress(vulkanModule, "vkCmdSetDepthClampRangeEXT");
    o_vkCmdConvertCooperativeVectorMatrixNV = (PFN_vkCmdConvertCooperativeVectorMatrixNV) GetProcAddress(
        vulkanModule, "vkCmdConvertCooperativeVectorMatrixNV");
    o_vkCmdSetAttachmentFeedbackLoopEnableEXT = (PFN_vkCmdSetAttachmentFeedbackLoopEnableEXT) GetProcAddress(
        vulkanModule, "vkCmdSetAttachmentFeedbackLoopEnableEXT");
    o_vkCmdBuildClusterAccelerationStructureIndirectNV =
        (PFN_vkCmdBuildClusterAccelerationStructureIndirectNV) GetProcAddress(
            vulkanModule, "vkCmdBuildClusterAccelerationStructureIndirectNV");
    o_vkCmdBuildPartitionedAccelerationStructuresNV =
        (PFN_vkCmdBuildPartitionedAccelerationStructuresNV) GetProcAddress(
            vulkanModule, "vkCmdBuildPartitionedAccelerationStructuresNV");
    o_vkCmdPreprocessGeneratedCommandsEXT =
        (PFN_vkCmdPreprocessGeneratedCommandsEXT) GetProcAddress(vulkanModule, "vkCmdPreprocessGeneratedCommandsEXT");
    o_vkCmdExecuteGeneratedCommandsEXT =
        (PFN_vkCmdExecuteGeneratedCommandsEXT) GetProcAddress(vulkanModule, "vkCmdExecuteGeneratedCommandsEXT");
    o_vkCmdBuildAccelerationStructuresKHR =
        (PFN_vkCmdBuildAccelerationStructuresKHR) GetProcAddress(vulkanModule, "vkCmdBuildAccelerationStructuresKHR");
    o_vkCmdBuildAccelerationStructuresIndirectKHR = (PFN_vkCmdBuildAccelerationStructuresIndirectKHR) GetProcAddress(
        vulkanModule, "vkCmdBuildAccelerationStructuresIndirectKHR");
    o_vkCmdCopyAccelerationStructureKHR =
        (PFN_vkCmdCopyAccelerationStructureKHR) GetProcAddress(vulkanModule, "vkCmdCopyAccelerationStructureKHR");
    o_vkCmdCopyAccelerationStructureToMemoryKHR = (PFN_vkCmdCopyAccelerationStructureToMemoryKHR) GetProcAddress(
        vulkanModule, "vkCmdCopyAccelerationStructureToMemoryKHR");
    o_vkCmdCopyMemoryToAccelerationStructureKHR = (PFN_vkCmdCopyMemoryToAccelerationStructureKHR) GetProcAddress(
        vulkanModule, "vkCmdCopyMemoryToAccelerationStructureKHR");
    o_vkCmdWriteAccelerationStructuresPropertiesKHR =
        (PFN_vkCmdWriteAccelerationStructuresPropertiesKHR) GetProcAddress(
            vulkanModule, "vkCmdWriteAccelerationStructuresPropertiesKHR");
    o_vkCmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR) GetProcAddress(vulkanModule, "vkCmdTraceRaysKHR");
    o_vkCmdTraceRaysIndirectKHR =
        (PFN_vkCmdTraceRaysIndirectKHR) GetProcAddress(vulkanModule, "vkCmdTraceRaysIndirectKHR");
    o_vkCmdSetRayTracingPipelineStackSizeKHR = (PFN_vkCmdSetRayTracingPipelineStackSizeKHR) GetProcAddress(
        vulkanModule, "vkCmdSetRayTracingPipelineStackSizeKHR");
    o_vkCmdDrawMeshTasksEXT = (PFN_vkCmdDrawMeshTasksEXT) GetProcAddress(vulkanModule, "vkCmdDrawMeshTasksEXT");
    o_vkCmdDrawMeshTasksIndirectEXT =
        (PFN_vkCmdDrawMeshTasksIndirectEXT) GetProcAddress(vulkanModule, "vkCmdDrawMeshTasksIndirectEXT");
    o_vkCmdDrawMeshTasksIndirectCountEXT =
        (PFN_vkCmdDrawMeshTasksIndirectCountEXT) GetProcAddress(vulkanModule, "vkCmdDrawMeshTasksIndirectCountEXT");

#pragma endregion

    if (o_vkQueueSubmit != nullptr)
    {
        LOG_INFO("Attaching Vulkan vkQueueSubmit hook");

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_vkQueueSubmit)
            DetourAttach(&(PVOID&) o_vkQueueSubmit, hk_vkQueueSubmit);

        if (o_vkQueueSubmit2)
            DetourAttach(&(PVOID&) o_vkQueueSubmit2, hk_vkQueueSubmit2);

        if (o_vkQueueSubmit2KHR)
            DetourAttach(&(PVOID&) o_vkQueueSubmit2KHR, hk_vkQueueSubmit2KHR);

        if (o_vkBeginCommandBuffer)
            DetourAttach(&(PVOID&) o_vkBeginCommandBuffer, hk_vkBeginCommandBuffer);

        if (o_vkEndCommandBuffer)
            DetourAttach(&(PVOID&) o_vkEndCommandBuffer, hk_vkEndCommandBuffer);

        if (o_vkResetCommandBuffer)
            DetourAttach(&(PVOID&) o_vkResetCommandBuffer, hk_vkResetCommandBuffer);

        if (o_vkFreeCommandBuffers)
            DetourAttach(&(PVOID&) o_vkFreeCommandBuffers, hk_vkFreeCommandBuffers);

        if (o_vkAllocateCommandBuffers != nullptr)
            DetourAttach(&(PVOID&) o_vkAllocateCommandBuffers, hk_vkAllocateCommandBuffers);

        if (o_vkResetCommandPool != nullptr)
            DetourAttach(&(PVOID&) o_vkResetCommandPool, hk_vkResetCommandPool);

        if (o_vkDestroyCommandPool != nullptr)
            DetourAttach(&(PVOID&) o_vkDestroyCommandPool, hk_vkDestroyCommandPool);

        if (o_vkCmdExecuteCommands)
            DetourAttach(&(PVOID&) o_vkCmdExecuteCommands, hk_vkCmdExecuteCommands);

        if (o_vkCreateCommandPool)
            DetourAttach(&(PVOID&) o_vkCreateCommandPool, hk_vkCreateCommandPool);

#pragma region CommandBuffer detours

        if (o_vkCmdBindPipeline)
            DetourAttach(&(PVOID&) o_vkCmdBindPipeline, hk_vkCmdBindPipeline);

        if (o_vkCmdSetViewport)
            DetourAttach(&(PVOID&) o_vkCmdSetViewport, hk_vkCmdSetViewport);

        if (o_vkCmdSetScissor)
            DetourAttach(&(PVOID&) o_vkCmdSetScissor, hk_vkCmdSetScissor);

        if (o_vkCmdSetLineWidth)
            DetourAttach(&(PVOID&) o_vkCmdSetLineWidth, hk_vkCmdSetLineWidth);

        if (o_vkCmdSetDepthBias)
            DetourAttach(&(PVOID&) o_vkCmdSetDepthBias, hk_vkCmdSetDepthBias);

        if (o_vkCmdSetBlendConstants)
            DetourAttach(&(PVOID&) o_vkCmdSetBlendConstants, hk_vkCmdSetBlendConstants);

        if (o_vkCmdSetDepthBounds)
            DetourAttach(&(PVOID&) o_vkCmdSetDepthBounds, hk_vkCmdSetDepthBounds);

        if (o_vkCmdSetStencilCompareMask)
            DetourAttach(&(PVOID&) o_vkCmdSetStencilCompareMask, hk_vkCmdSetStencilCompareMask);

        if (o_vkCmdSetStencilWriteMask)
            DetourAttach(&(PVOID&) o_vkCmdSetStencilWriteMask, hk_vkCmdSetStencilWriteMask);

        if (o_vkCmdSetStencilReference)
            DetourAttach(&(PVOID&) o_vkCmdSetStencilReference, hk_vkCmdSetStencilReference);

        if (o_vkCmdBindDescriptorSets)
            DetourAttach(&(PVOID&) o_vkCmdBindDescriptorSets, hk_vkCmdBindDescriptorSets);

        if (o_vkCmdBindIndexBuffer)
            DetourAttach(&(PVOID&) o_vkCmdBindIndexBuffer, hk_vkCmdBindIndexBuffer);

        if (o_vkCmdBindVertexBuffers)
            DetourAttach(&(PVOID&) o_vkCmdBindVertexBuffers, hk_vkCmdBindVertexBuffers);

        if (o_vkCmdDraw)
            DetourAttach(&(PVOID&) o_vkCmdDraw, hk_vkCmdDraw);

        if (o_vkCmdDrawIndexed)
            DetourAttach(&(PVOID&) o_vkCmdDrawIndexed, hk_vkCmdDrawIndexed);

        if (o_vkCmdDrawIndirect)
            DetourAttach(&(PVOID&) o_vkCmdDrawIndirect, hk_vkCmdDrawIndirect);

        if (o_vkCmdDrawIndexedIndirect)
            DetourAttach(&(PVOID&) o_vkCmdDrawIndexedIndirect, hk_vkCmdDrawIndexedIndirect);

        if (o_vkCmdDispatch)
            DetourAttach(&(PVOID&) o_vkCmdDispatch, hk_vkCmdDispatch);

        if (o_vkCmdDispatchIndirect)
            DetourAttach(&(PVOID&) o_vkCmdDispatchIndirect, hk_vkCmdDispatchIndirect);

        if (o_vkCmdCopyBuffer)
            DetourAttach(&(PVOID&) o_vkCmdCopyBuffer, hk_vkCmdCopyBuffer);

        if (o_vkCmdCopyImage)
            DetourAttach(&(PVOID&) o_vkCmdCopyImage, hk_vkCmdCopyImage);

        if (o_vkCmdBlitImage)
            DetourAttach(&(PVOID&) o_vkCmdBlitImage, hk_vkCmdBlitImage);

        if (o_vkCmdCopyBufferToImage)
            DetourAttach(&(PVOID&) o_vkCmdCopyBufferToImage, hk_vkCmdCopyBufferToImage);

        if (o_vkCmdCopyImageToBuffer)
            DetourAttach(&(PVOID&) o_vkCmdCopyImageToBuffer, hk_vkCmdCopyImageToBuffer);

        if (o_vkCmdUpdateBuffer)
            DetourAttach(&(PVOID&) o_vkCmdUpdateBuffer, hk_vkCmdUpdateBuffer);

        if (o_vkCmdFillBuffer)
            DetourAttach(&(PVOID&) o_vkCmdFillBuffer, hk_vkCmdFillBuffer);

        if (o_vkCmdClearColorImage)
            DetourAttach(&(PVOID&) o_vkCmdClearColorImage, hk_vkCmdClearColorImage);

        if (o_vkCmdClearDepthStencilImage)
            DetourAttach(&(PVOID&) o_vkCmdClearDepthStencilImage, hk_vkCmdClearDepthStencilImage);

        if (o_vkCmdClearAttachments)
            DetourAttach(&(PVOID&) o_vkCmdClearAttachments, hk_vkCmdClearAttachments);

        if (o_vkCmdResolveImage)
            DetourAttach(&(PVOID&) o_vkCmdResolveImage, hk_vkCmdResolveImage);

        if (o_vkCmdSetEvent)
            DetourAttach(&(PVOID&) o_vkCmdSetEvent, hk_vkCmdSetEvent);

        if (o_vkCmdResetEvent)
            DetourAttach(&(PVOID&) o_vkCmdResetEvent, hk_vkCmdResetEvent);

        if (o_vkCmdWaitEvents)
            DetourAttach(&(PVOID&) o_vkCmdWaitEvents, hk_vkCmdWaitEvents);

        if (o_vkCmdPipelineBarrier)
            DetourAttach(&(PVOID&) o_vkCmdPipelineBarrier, hk_vkCmdPipelineBarrier);

        if (o_vkCmdBeginQuery)
            DetourAttach(&(PVOID&) o_vkCmdBeginQuery, hk_vkCmdBeginQuery);

        if (o_vkCmdEndQuery)
            DetourAttach(&(PVOID&) o_vkCmdEndQuery, hk_vkCmdEndQuery);

        if (o_vkCmdResetQueryPool)
            DetourAttach(&(PVOID&) o_vkCmdResetQueryPool, hk_vkCmdResetQueryPool);

        if (o_vkCmdWriteTimestamp)
            DetourAttach(&(PVOID&) o_vkCmdWriteTimestamp, hk_vkCmdWriteTimestamp);

        if (o_vkCmdCopyQueryPoolResults)
            DetourAttach(&(PVOID&) o_vkCmdCopyQueryPoolResults, hk_vkCmdCopyQueryPoolResults);

        if (o_vkCmdPushConstants)
            DetourAttach(&(PVOID&) o_vkCmdPushConstants, hk_vkCmdPushConstants);

        if (o_vkCmdBeginRenderPass)
            DetourAttach(&(PVOID&) o_vkCmdBeginRenderPass, hk_vkCmdBeginRenderPass);

        if (o_vkCmdNextSubpass)
            DetourAttach(&(PVOID&) o_vkCmdNextSubpass, hk_vkCmdNextSubpass);

        if (o_vkCmdEndRenderPass)
            DetourAttach(&(PVOID&) o_vkCmdEndRenderPass, hk_vkCmdEndRenderPass);

        if (o_vkCmdSetDeviceMask)
            DetourAttach(&(PVOID&) o_vkCmdSetDeviceMask, hk_vkCmdSetDeviceMask);

        if (o_vkCmdDispatchBase)
            DetourAttach(&(PVOID&) o_vkCmdDispatchBase, hk_vkCmdDispatchBase);

        if (o_vkCmdDrawIndirectCount)
            DetourAttach(&(PVOID&) o_vkCmdDrawIndirectCount, hk_vkCmdDrawIndirectCount);

        if (o_vkCmdDrawIndexedIndirectCount)
            DetourAttach(&(PVOID&) o_vkCmdDrawIndexedIndirectCount, hk_vkCmdDrawIndexedIndirectCount);

        if (o_vkCmdBeginRenderPass2)
            DetourAttach(&(PVOID&) o_vkCmdBeginRenderPass2, hk_vkCmdBeginRenderPass2);

        if (o_vkCmdNextSubpass2)
            DetourAttach(&(PVOID&) o_vkCmdNextSubpass2, hk_vkCmdNextSubpass2);

        if (o_vkCmdEndRenderPass2)
            DetourAttach(&(PVOID&) o_vkCmdEndRenderPass2, hk_vkCmdEndRenderPass2);

        if (o_vkCmdSetEvent2)
            DetourAttach(&(PVOID&) o_vkCmdSetEvent2, hk_vkCmdSetEvent2);

        if (o_vkCmdResetEvent2)
            DetourAttach(&(PVOID&) o_vkCmdResetEvent2, hk_vkCmdResetEvent2);

        if (o_vkCmdWaitEvents2)
            DetourAttach(&(PVOID&) o_vkCmdWaitEvents2, hk_vkCmdWaitEvents2);

        if (o_vkCmdPipelineBarrier2)
            DetourAttach(&(PVOID&) o_vkCmdPipelineBarrier2, hk_vkCmdPipelineBarrier2);

        if (o_vkCmdWriteTimestamp2)
            DetourAttach(&(PVOID&) o_vkCmdWriteTimestamp2, hk_vkCmdWriteTimestamp2);

        if (o_vkCmdCopyBuffer2)
            DetourAttach(&(PVOID&) o_vkCmdCopyBuffer2, hk_vkCmdCopyBuffer2);

        if (o_vkCmdCopyImage2)
            DetourAttach(&(PVOID&) o_vkCmdCopyImage2, hk_vkCmdCopyImage2);

        if (o_vkCmdCopyBufferToImage2)
            DetourAttach(&(PVOID&) o_vkCmdCopyBufferToImage2, hk_vkCmdCopyBufferToImage2);

        if (o_vkCmdCopyImageToBuffer2)
            DetourAttach(&(PVOID&) o_vkCmdCopyImageToBuffer2, hk_vkCmdCopyImageToBuffer2);

        if (o_vkCmdBlitImage2)
            DetourAttach(&(PVOID&) o_vkCmdBlitImage2, hk_vkCmdBlitImage2);

        if (o_vkCmdResolveImage2)
            DetourAttach(&(PVOID&) o_vkCmdResolveImage2, hk_vkCmdResolveImage2);

        if (o_vkCmdBeginRendering)
            DetourAttach(&(PVOID&) o_vkCmdBeginRendering, hk_vkCmdBeginRendering);

        if (o_vkCmdEndRendering)
            DetourAttach(&(PVOID&) o_vkCmdEndRendering, hk_vkCmdEndRendering);

        if (o_vkCmdSetCullMode)
            DetourAttach(&(PVOID&) o_vkCmdSetCullMode, hk_vkCmdSetCullMode);

        if (o_vkCmdSetFrontFace)
            DetourAttach(&(PVOID&) o_vkCmdSetFrontFace, hk_vkCmdSetFrontFace);

        if (o_vkCmdSetPrimitiveTopology)
            DetourAttach(&(PVOID&) o_vkCmdSetPrimitiveTopology, hk_vkCmdSetPrimitiveTopology);

        if (o_vkCmdSetViewportWithCount)
            DetourAttach(&(PVOID&) o_vkCmdSetViewportWithCount, hk_vkCmdSetViewportWithCount);

        if (o_vkCmdSetScissorWithCount)
            DetourAttach(&(PVOID&) o_vkCmdSetScissorWithCount, hk_vkCmdSetScissorWithCount);

        if (o_vkCmdBindVertexBuffers2)
            DetourAttach(&(PVOID&) o_vkCmdBindVertexBuffers2, hk_vkCmdBindVertexBuffers2);

        if (o_vkCmdSetDepthTestEnable)
            DetourAttach(&(PVOID&) o_vkCmdSetDepthTestEnable, hk_vkCmdSetDepthTestEnable);

        if (o_vkCmdSetDepthWriteEnable)
            DetourAttach(&(PVOID&) o_vkCmdSetDepthWriteEnable, hk_vkCmdSetDepthWriteEnable);

        if (o_vkCmdSetDepthCompareOp)
            DetourAttach(&(PVOID&) o_vkCmdSetDepthCompareOp, hk_vkCmdSetDepthCompareOp);

        if (o_vkCmdSetDepthBoundsTestEnable)
            DetourAttach(&(PVOID&) o_vkCmdSetDepthBoundsTestEnable, hk_vkCmdSetDepthBoundsTestEnable);

        if (o_vkCmdSetStencilTestEnable)
            DetourAttach(&(PVOID&) o_vkCmdSetStencilTestEnable, hk_vkCmdSetStencilTestEnable);

        if (o_vkCmdSetStencilOp)
            DetourAttach(&(PVOID&) o_vkCmdSetStencilOp, hk_vkCmdSetStencilOp);

        if (o_vkCmdSetRasterizerDiscardEnable)
            DetourAttach(&(PVOID&) o_vkCmdSetRasterizerDiscardEnable, hk_vkCmdSetRasterizerDiscardEnable);

        if (o_vkCmdSetDepthBiasEnable)
            DetourAttach(&(PVOID&) o_vkCmdSetDepthBiasEnable, hk_vkCmdSetDepthBiasEnable);

        if (o_vkCmdSetPrimitiveRestartEnable)
            DetourAttach(&(PVOID&) o_vkCmdSetPrimitiveRestartEnable, hk_vkCmdSetPrimitiveRestartEnable);

        if (o_vkCmdSetLineStipple)
            DetourAttach(&(PVOID&) o_vkCmdSetLineStipple, hk_vkCmdSetLineStipple);

        if (o_vkCmdBindIndexBuffer2)
            DetourAttach(&(PVOID&) o_vkCmdBindIndexBuffer2, hk_vkCmdBindIndexBuffer2);

        if (o_vkCmdPushDescriptorSet)
            DetourAttach(&(PVOID&) o_vkCmdPushDescriptorSet, hk_vkCmdPushDescriptorSet);

        if (o_vkCmdPushDescriptorSetWithTemplate)
            DetourAttach(&(PVOID&) o_vkCmdPushDescriptorSetWithTemplate, hk_vkCmdPushDescriptorSetWithTemplate);

        if (o_vkCmdSetRenderingAttachmentLocations)
            DetourAttach(&(PVOID&) o_vkCmdSetRenderingAttachmentLocations, hk_vkCmdSetRenderingAttachmentLocations);

        if (o_vkCmdSetRenderingInputAttachmentIndices)
            DetourAttach(&(PVOID&) o_vkCmdSetRenderingInputAttachmentIndices,
                         hk_vkCmdSetRenderingInputAttachmentIndices);

        if (o_vkCmdBindDescriptorSets2)
            DetourAttach(&(PVOID&) o_vkCmdBindDescriptorSets2, hk_vkCmdBindDescriptorSets2);

        if (o_vkCmdPushConstants2)
            DetourAttach(&(PVOID&) o_vkCmdPushConstants2, hk_vkCmdPushConstants2);

        if (o_vkCmdPushDescriptorSet2)
            DetourAttach(&(PVOID&) o_vkCmdPushDescriptorSet2, hk_vkCmdPushDescriptorSet2);

        if (o_vkCmdPushDescriptorSetWithTemplate2)
            DetourAttach(&(PVOID&) o_vkCmdPushDescriptorSetWithTemplate2, hk_vkCmdPushDescriptorSetWithTemplate2);

        if (o_vkCmdBeginVideoCodingKHR)
            DetourAttach(&(PVOID&) o_vkCmdBeginVideoCodingKHR, hk_vkCmdBeginVideoCodingKHR);

        if (o_vkCmdEndVideoCodingKHR)
            DetourAttach(&(PVOID&) o_vkCmdEndVideoCodingKHR, hk_vkCmdEndVideoCodingKHR);

        if (o_vkCmdControlVideoCodingKHR)
            DetourAttach(&(PVOID&) o_vkCmdControlVideoCodingKHR, hk_vkCmdControlVideoCodingKHR);

        if (o_vkCmdDecodeVideoKHR)
            DetourAttach(&(PVOID&) o_vkCmdDecodeVideoKHR, hk_vkCmdDecodeVideoKHR);

        if (o_vkCmdBeginRenderingKHR)
            DetourAttach(&(PVOID&) o_vkCmdBeginRenderingKHR, hk_vkCmdBeginRenderingKHR);

        if (o_vkCmdEndRenderingKHR)
            DetourAttach(&(PVOID&) o_vkCmdEndRenderingKHR, hk_vkCmdEndRenderingKHR);

        if (o_vkCmdSetDeviceMaskKHR)
            DetourAttach(&(PVOID&) o_vkCmdSetDeviceMaskKHR, hk_vkCmdSetDeviceMaskKHR);

        if (o_vkCmdDispatchBaseKHR)
            DetourAttach(&(PVOID&) o_vkCmdDispatchBaseKHR, hk_vkCmdDispatchBaseKHR);

        if (o_vkCmdPushDescriptorSetKHR)
            DetourAttach(&(PVOID&) o_vkCmdPushDescriptorSetKHR, hk_vkCmdPushDescriptorSetKHR);

        if (o_vkCmdPushDescriptorSetWithTemplateKHR)
            DetourAttach(&(PVOID&) o_vkCmdPushDescriptorSetWithTemplateKHR, hk_vkCmdPushDescriptorSetWithTemplateKHR);

        if (o_vkCmdBeginRenderPass2KHR)
            DetourAttach(&(PVOID&) o_vkCmdBeginRenderPass2KHR, hk_vkCmdBeginRenderPass2KHR);

        if (o_vkCmdNextSubpass2KHR)
            DetourAttach(&(PVOID&) o_vkCmdNextSubpass2KHR, hk_vkCmdNextSubpass2KHR);

        if (o_vkCmdEndRenderPass2KHR)
            DetourAttach(&(PVOID&) o_vkCmdEndRenderPass2KHR, hk_vkCmdEndRenderPass2KHR);

        if (o_vkCmdDrawIndirectCountKHR)
            DetourAttach(&(PVOID&) o_vkCmdDrawIndirectCountKHR, hk_vkCmdDrawIndirectCountKHR);

        if (o_vkCmdDrawIndexedIndirectCountKHR)
            DetourAttach(&(PVOID&) o_vkCmdDrawIndexedIndirectCountKHR, hk_vkCmdDrawIndexedIndirectCountKHR);

        if (o_vkCmdSetFragmentShadingRateKHR)
            DetourAttach(&(PVOID&) o_vkCmdSetFragmentShadingRateKHR, hk_vkCmdSetFragmentShadingRateKHR);

        if (o_vkCmdSetRenderingAttachmentLocationsKHR)
            DetourAttach(&(PVOID&) o_vkCmdSetRenderingAttachmentLocationsKHR,
                         hk_vkCmdSetRenderingAttachmentLocationsKHR);

        if (o_vkCmdSetRenderingInputAttachmentIndicesKHR)
            DetourAttach(&(PVOID&) o_vkCmdSetRenderingInputAttachmentIndicesKHR,
                         hk_vkCmdSetRenderingInputAttachmentIndicesKHR);

        if (o_vkCmdEncodeVideoKHR)
            DetourAttach(&(PVOID&) o_vkCmdEncodeVideoKHR, hk_vkCmdEncodeVideoKHR);

        if (o_vkCmdSetEvent2KHR)
            DetourAttach(&(PVOID&) o_vkCmdSetEvent2KHR, hk_vkCmdSetEvent2KHR);

        if (o_vkCmdResetEvent2KHR)
            DetourAttach(&(PVOID&) o_vkCmdResetEvent2KHR, hk_vkCmdResetEvent2KHR);

        if (o_vkCmdWaitEvents2KHR)
            DetourAttach(&(PVOID&) o_vkCmdWaitEvents2KHR, hk_vkCmdWaitEvents2KHR);

        if (o_vkCmdPipelineBarrier2KHR)
            DetourAttach(&(PVOID&) o_vkCmdPipelineBarrier2KHR, hk_vkCmdPipelineBarrier2KHR);

        if (o_vkCmdWriteTimestamp2KHR)
            DetourAttach(&(PVOID&) o_vkCmdWriteTimestamp2KHR, hk_vkCmdWriteTimestamp2KHR);

        if (o_vkCmdCopyBuffer2KHR)
            DetourAttach(&(PVOID&) o_vkCmdCopyBuffer2KHR, hk_vkCmdCopyBuffer2KHR);

        if (o_vkCmdCopyImage2KHR)
            DetourAttach(&(PVOID&) o_vkCmdCopyImage2KHR, hk_vkCmdCopyImage2KHR);

        if (o_vkCmdCopyBufferToImage2KHR)
            DetourAttach(&(PVOID&) o_vkCmdCopyBufferToImage2KHR, hk_vkCmdCopyBufferToImage2KHR);

        if (o_vkCmdCopyImageToBuffer2KHR)
            DetourAttach(&(PVOID&) o_vkCmdCopyImageToBuffer2KHR, hk_vkCmdCopyImageToBuffer2KHR);

        if (o_vkCmdBlitImage2KHR)
            DetourAttach(&(PVOID&) o_vkCmdBlitImage2KHR, hk_vkCmdBlitImage2KHR);

        if (o_vkCmdResolveImage2KHR)
            DetourAttach(&(PVOID&) o_vkCmdResolveImage2KHR, hk_vkCmdResolveImage2KHR);

        if (o_vkCmdTraceRaysIndirect2KHR)
            DetourAttach(&(PVOID&) o_vkCmdTraceRaysIndirect2KHR, hk_vkCmdTraceRaysIndirect2KHR);

        if (o_vkCmdBindIndexBuffer2KHR)
            DetourAttach(&(PVOID&) o_vkCmdBindIndexBuffer2KHR, hk_vkCmdBindIndexBuffer2KHR);

        if (o_vkCmdSetLineStippleKHR)
            DetourAttach(&(PVOID&) o_vkCmdSetLineStippleKHR, hk_vkCmdSetLineStippleKHR);

        if (o_vkCmdBindDescriptorSets2KHR)
            DetourAttach(&(PVOID&) o_vkCmdBindDescriptorSets2KHR, hk_vkCmdBindDescriptorSets2KHR);

        if (o_vkCmdPushConstants2KHR)
            DetourAttach(&(PVOID&) o_vkCmdPushConstants2KHR, hk_vkCmdPushConstants2KHR);

        if (o_vkCmdPushDescriptorSet2KHR)
            DetourAttach(&(PVOID&) o_vkCmdPushDescriptorSet2KHR, hk_vkCmdPushDescriptorSet2KHR);

        if (o_vkCmdPushDescriptorSetWithTemplate2KHR)
            DetourAttach(&(PVOID&) o_vkCmdPushDescriptorSetWithTemplate2KHR, hk_vkCmdPushDescriptorSetWithTemplate2KHR);

        if (o_vkCmdSetDescriptorBufferOffsets2EXT)
            DetourAttach(&(PVOID&) o_vkCmdSetDescriptorBufferOffsets2EXT, hk_vkCmdSetDescriptorBufferOffsets2EXT);

        if (o_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT)
            DetourAttach(&(PVOID&) o_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT,
                         hk_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT);

        if (o_vkCmdDebugMarkerBeginEXT)
            DetourAttach(&(PVOID&) o_vkCmdDebugMarkerBeginEXT, hk_vkCmdDebugMarkerBeginEXT);

        if (o_vkCmdDebugMarkerEndEXT)
            DetourAttach(&(PVOID&) o_vkCmdDebugMarkerEndEXT, hk_vkCmdDebugMarkerEndEXT);

        if (o_vkCmdDebugMarkerInsertEXT)
            DetourAttach(&(PVOID&) o_vkCmdDebugMarkerInsertEXT, hk_vkCmdDebugMarkerInsertEXT);

        if (o_vkCmdBindTransformFeedbackBuffersEXT)
            DetourAttach(&(PVOID&) o_vkCmdBindTransformFeedbackBuffersEXT, hk_vkCmdBindTransformFeedbackBuffersEXT);

        if (o_vkCmdBeginTransformFeedbackEXT)
            DetourAttach(&(PVOID&) o_vkCmdBeginTransformFeedbackEXT, hk_vkCmdBeginTransformFeedbackEXT);

        if (o_vkCmdEndTransformFeedbackEXT)
            DetourAttach(&(PVOID&) o_vkCmdEndTransformFeedbackEXT, hk_vkCmdEndTransformFeedbackEXT);

        if (o_vkCmdBeginQueryIndexedEXT)
            DetourAttach(&(PVOID&) o_vkCmdBeginQueryIndexedEXT, hk_vkCmdBeginQueryIndexedEXT);

        if (o_vkCmdEndQueryIndexedEXT)
            DetourAttach(&(PVOID&) o_vkCmdEndQueryIndexedEXT, hk_vkCmdEndQueryIndexedEXT);

        if (o_vkCmdDrawIndirectByteCountEXT)
            DetourAttach(&(PVOID&) o_vkCmdDrawIndirectByteCountEXT, hk_vkCmdDrawIndirectByteCountEXT);

        if (o_vkCmdCuLaunchKernelNVX)
            DetourAttach(&(PVOID&) o_vkCmdCuLaunchKernelNVX, hk_vkCmdCuLaunchKernelNVX);

        if (o_vkCmdDrawIndirectCountAMD)
            DetourAttach(&(PVOID&) o_vkCmdDrawIndirectCountAMD, hk_vkCmdDrawIndirectCountAMD);

        if (o_vkCmdDrawIndexedIndirectCountAMD)
            DetourAttach(&(PVOID&) o_vkCmdDrawIndexedIndirectCountAMD, hk_vkCmdDrawIndexedIndirectCountAMD);

        if (o_vkCmdBeginConditionalRenderingEXT)
            DetourAttach(&(PVOID&) o_vkCmdBeginConditionalRenderingEXT, hk_vkCmdBeginConditionalRenderingEXT);

        if (o_vkCmdEndConditionalRenderingEXT)
            DetourAttach(&(PVOID&) o_vkCmdEndConditionalRenderingEXT, hk_vkCmdEndConditionalRenderingEXT);

        if (o_vkCmdSetViewportWScalingNV)
            DetourAttach(&(PVOID&) o_vkCmdSetViewportWScalingNV, hk_vkCmdSetViewportWScalingNV);

        if (o_vkCmdSetDiscardRectangleEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetDiscardRectangleEXT, hk_vkCmdSetDiscardRectangleEXT);

        if (o_vkCmdSetDiscardRectangleEnableEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetDiscardRectangleEnableEXT, hk_vkCmdSetDiscardRectangleEnableEXT);

        if (o_vkCmdSetDiscardRectangleModeEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetDiscardRectangleModeEXT, hk_vkCmdSetDiscardRectangleModeEXT);

        if (o_vkCmdBeginDebugUtilsLabelEXT)
            DetourAttach(&(PVOID&) o_vkCmdBeginDebugUtilsLabelEXT, hk_vkCmdBeginDebugUtilsLabelEXT);

        if (o_vkCmdEndDebugUtilsLabelEXT)
            DetourAttach(&(PVOID&) o_vkCmdEndDebugUtilsLabelEXT, hk_vkCmdEndDebugUtilsLabelEXT);

        if (o_vkCmdInsertDebugUtilsLabelEXT)
            DetourAttach(&(PVOID&) o_vkCmdInsertDebugUtilsLabelEXT, hk_vkCmdInsertDebugUtilsLabelEXT);

        if (o_vkCmdSetSampleLocationsEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetSampleLocationsEXT, hk_vkCmdSetSampleLocationsEXT);

        if (o_vkCmdBindShadingRateImageNV)
            DetourAttach(&(PVOID&) o_vkCmdBindShadingRateImageNV, hk_vkCmdBindShadingRateImageNV);

        if (o_vkCmdSetViewportShadingRatePaletteNV)
            DetourAttach(&(PVOID&) o_vkCmdSetViewportShadingRatePaletteNV, hk_vkCmdSetViewportShadingRatePaletteNV);

        if (o_vkCmdSetCoarseSampleOrderNV)
            DetourAttach(&(PVOID&) o_vkCmdSetCoarseSampleOrderNV, hk_vkCmdSetCoarseSampleOrderNV);

        if (o_vkCmdBuildAccelerationStructureNV)
            DetourAttach(&(PVOID&) o_vkCmdBuildAccelerationStructureNV, hk_vkCmdBuildAccelerationStructureNV);

        if (o_vkCmdCopyAccelerationStructureNV)
            DetourAttach(&(PVOID&) o_vkCmdCopyAccelerationStructureNV, hk_vkCmdCopyAccelerationStructureNV);

        if (o_vkCmdTraceRaysNV)
            DetourAttach(&(PVOID&) o_vkCmdTraceRaysNV, hk_vkCmdTraceRaysNV);

        if (o_vkCmdWriteAccelerationStructuresPropertiesNV)
            DetourAttach(&(PVOID&) o_vkCmdWriteAccelerationStructuresPropertiesNV,
                         hk_vkCmdWriteAccelerationStructuresPropertiesNV);

        if (o_vkCmdWriteBufferMarkerAMD)
            DetourAttach(&(PVOID&) o_vkCmdWriteBufferMarkerAMD, hk_vkCmdWriteBufferMarkerAMD);

        if (o_vkCmdWriteBufferMarker2AMD)
            DetourAttach(&(PVOID&) o_vkCmdWriteBufferMarker2AMD, hk_vkCmdWriteBufferMarker2AMD);

        if (o_vkCmdDrawMeshTasksNV)
            DetourAttach(&(PVOID&) o_vkCmdDrawMeshTasksNV, hk_vkCmdDrawMeshTasksNV);

        if (o_vkCmdDrawMeshTasksIndirectNV)
            DetourAttach(&(PVOID&) o_vkCmdDrawMeshTasksIndirectNV, hk_vkCmdDrawMeshTasksIndirectNV);

        if (o_vkCmdDrawMeshTasksIndirectCountNV)
            DetourAttach(&(PVOID&) o_vkCmdDrawMeshTasksIndirectCountNV, hk_vkCmdDrawMeshTasksIndirectCountNV);

        if (o_vkCmdSetExclusiveScissorEnableNV)
            DetourAttach(&(PVOID&) o_vkCmdSetExclusiveScissorEnableNV, hk_vkCmdSetExclusiveScissorEnableNV);

        if (o_vkCmdSetExclusiveScissorNV)
            DetourAttach(&(PVOID&) o_vkCmdSetExclusiveScissorNV, hk_vkCmdSetExclusiveScissorNV);

        if (o_vkCmdSetCheckpointNV)
            DetourAttach(&(PVOID&) o_vkCmdSetCheckpointNV, hk_vkCmdSetCheckpointNV);

        if (o_vkCmdSetPerformanceMarkerINTEL)
            DetourAttach(&(PVOID&) o_vkCmdSetPerformanceMarkerINTEL, hk_vkCmdSetPerformanceMarkerINTEL);

        if (o_vkCmdSetPerformanceStreamMarkerINTEL)
            DetourAttach(&(PVOID&) o_vkCmdSetPerformanceStreamMarkerINTEL, hk_vkCmdSetPerformanceStreamMarkerINTEL);

        if (o_vkCmdSetPerformanceOverrideINTEL)
            DetourAttach(&(PVOID&) o_vkCmdSetPerformanceOverrideINTEL, hk_vkCmdSetPerformanceOverrideINTEL);

        if (o_vkCmdSetLineStippleEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetLineStippleEXT, hk_vkCmdSetLineStippleEXT);

        if (o_vkCmdSetCullModeEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetCullModeEXT, hk_vkCmdSetCullModeEXT);

        if (o_vkCmdSetFrontFaceEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetFrontFaceEXT, hk_vkCmdSetFrontFaceEXT);

        if (o_vkCmdSetPrimitiveTopologyEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetPrimitiveTopologyEXT, hk_vkCmdSetPrimitiveTopologyEXT);

        if (o_vkCmdSetViewportWithCountEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetViewportWithCountEXT, hk_vkCmdSetViewportWithCountEXT);

        if (o_vkCmdSetScissorWithCountEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetScissorWithCountEXT, hk_vkCmdSetScissorWithCountEXT);

        if (o_vkCmdBindVertexBuffers2EXT)
            DetourAttach(&(PVOID&) o_vkCmdBindVertexBuffers2EXT, hk_vkCmdBindVertexBuffers2EXT);

        if (o_vkCmdSetDepthTestEnableEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetDepthTestEnableEXT, hk_vkCmdSetDepthTestEnableEXT);

        if (o_vkCmdSetDepthWriteEnableEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetDepthWriteEnableEXT, hk_vkCmdSetDepthWriteEnableEXT);

        if (o_vkCmdSetDepthCompareOpEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetDepthCompareOpEXT, hk_vkCmdSetDepthCompareOpEXT);

        if (o_vkCmdSetDepthBoundsTestEnableEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetDepthBoundsTestEnableEXT, hk_vkCmdSetDepthBoundsTestEnableEXT);

        if (o_vkCmdSetStencilTestEnableEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetStencilTestEnableEXT, hk_vkCmdSetStencilTestEnableEXT);

        if (o_vkCmdSetStencilOpEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetStencilOpEXT, hk_vkCmdSetStencilOpEXT);

        if (o_vkCmdPreprocessGeneratedCommandsNV)
            DetourAttach(&(PVOID&) o_vkCmdPreprocessGeneratedCommandsNV, hk_vkCmdPreprocessGeneratedCommandsNV);

        if (o_vkCmdExecuteGeneratedCommandsNV)
            DetourAttach(&(PVOID&) o_vkCmdExecuteGeneratedCommandsNV, hk_vkCmdExecuteGeneratedCommandsNV);

        if (o_vkCmdBindPipelineShaderGroupNV)
            DetourAttach(&(PVOID&) o_vkCmdBindPipelineShaderGroupNV, hk_vkCmdBindPipelineShaderGroupNV);

        if (o_vkCmdSetDepthBias2EXT)
            DetourAttach(&(PVOID&) o_vkCmdSetDepthBias2EXT, hk_vkCmdSetDepthBias2EXT);

        if (o_vkCmdCudaLaunchKernelNV)
            DetourAttach(&(PVOID&) o_vkCmdCudaLaunchKernelNV, hk_vkCmdCudaLaunchKernelNV);

        if (o_vkCmdBindDescriptorBuffersEXT)
            DetourAttach(&(PVOID&) o_vkCmdBindDescriptorBuffersEXT, hk_vkCmdBindDescriptorBuffersEXT);

        if (o_vkCmdSetDescriptorBufferOffsetsEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetDescriptorBufferOffsetsEXT, hk_vkCmdSetDescriptorBufferOffsetsEXT);

        if (o_vkCmdBindDescriptorBufferEmbeddedSamplersEXT)
            DetourAttach(&(PVOID&) o_vkCmdBindDescriptorBufferEmbeddedSamplersEXT,
                         hk_vkCmdBindDescriptorBufferEmbeddedSamplersEXT);

        if (o_vkCmdSetFragmentShadingRateEnumNV)
            DetourAttach(&(PVOID&) o_vkCmdSetFragmentShadingRateEnumNV, hk_vkCmdSetFragmentShadingRateEnumNV);

        if (o_vkCmdSetVertexInputEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetVertexInputEXT, hk_vkCmdSetVertexInputEXT);

        if (o_vkCmdSubpassShadingHUAWEI)
            DetourAttach(&(PVOID&) o_vkCmdSubpassShadingHUAWEI, hk_vkCmdSubpassShadingHUAWEI);

        if (o_vkCmdBindInvocationMaskHUAWEI)
            DetourAttach(&(PVOID&) o_vkCmdBindInvocationMaskHUAWEI, hk_vkCmdBindInvocationMaskHUAWEI);

        if (o_vkCmdSetPatchControlPointsEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetPatchControlPointsEXT, hk_vkCmdSetPatchControlPointsEXT);

        if (o_vkCmdSetRasterizerDiscardEnableEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetRasterizerDiscardEnableEXT, hk_vkCmdSetRasterizerDiscardEnableEXT);

        if (o_vkCmdSetDepthBiasEnableEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetDepthBiasEnableEXT, hk_vkCmdSetDepthBiasEnableEXT);

        if (o_vkCmdSetLogicOpEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetLogicOpEXT, hk_vkCmdSetLogicOpEXT);

        if (o_vkCmdSetPrimitiveRestartEnableEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetPrimitiveRestartEnableEXT, hk_vkCmdSetPrimitiveRestartEnableEXT);

        if (o_vkCmdSetColorWriteEnableEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetColorWriteEnableEXT, hk_vkCmdSetColorWriteEnableEXT);

        if (o_vkCmdDrawMultiEXT)
            DetourAttach(&(PVOID&) o_vkCmdDrawMultiEXT, hk_vkCmdDrawMultiEXT);

        if (o_vkCmdDrawMultiIndexedEXT)
            DetourAttach(&(PVOID&) o_vkCmdDrawMultiIndexedEXT, hk_vkCmdDrawMultiIndexedEXT);

        if (o_vkCmdBuildMicromapsEXT)
            DetourAttach(&(PVOID&) o_vkCmdBuildMicromapsEXT, hk_vkCmdBuildMicromapsEXT);

        if (o_vkCmdCopyMicromapEXT)
            DetourAttach(&(PVOID&) o_vkCmdCopyMicromapEXT, hk_vkCmdCopyMicromapEXT);

        if (o_vkCmdCopyMicromapToMemoryEXT)
            DetourAttach(&(PVOID&) o_vkCmdCopyMicromapToMemoryEXT, hk_vkCmdCopyMicromapToMemoryEXT);

        if (o_vkCmdCopyMemoryToMicromapEXT)
            DetourAttach(&(PVOID&) o_vkCmdCopyMemoryToMicromapEXT, hk_vkCmdCopyMemoryToMicromapEXT);

        if (o_vkCmdWriteMicromapsPropertiesEXT)
            DetourAttach(&(PVOID&) o_vkCmdWriteMicromapsPropertiesEXT, hk_vkCmdWriteMicromapsPropertiesEXT);

        if (o_vkCmdDrawClusterHUAWEI)
            DetourAttach(&(PVOID&) o_vkCmdDrawClusterHUAWEI, hk_vkCmdDrawClusterHUAWEI);

        if (o_vkCmdDrawClusterIndirectHUAWEI)
            DetourAttach(&(PVOID&) o_vkCmdDrawClusterIndirectHUAWEI, hk_vkCmdDrawClusterIndirectHUAWEI);

        if (o_vkCmdCopyMemoryIndirectNV)
            DetourAttach(&(PVOID&) o_vkCmdCopyMemoryIndirectNV, hk_vkCmdCopyMemoryIndirectNV);

        if (o_vkCmdCopyMemoryToImageIndirectNV)
            DetourAttach(&(PVOID&) o_vkCmdCopyMemoryToImageIndirectNV, hk_vkCmdCopyMemoryToImageIndirectNV);

        if (o_vkCmdDecompressMemoryNV)
            DetourAttach(&(PVOID&) o_vkCmdDecompressMemoryNV, hk_vkCmdDecompressMemoryNV);

        if (o_vkCmdDecompressMemoryIndirectCountNV)
            DetourAttach(&(PVOID&) o_vkCmdDecompressMemoryIndirectCountNV, hk_vkCmdDecompressMemoryIndirectCountNV);

        if (o_vkCmdUpdatePipelineIndirectBufferNV)
            DetourAttach(&(PVOID&) o_vkCmdUpdatePipelineIndirectBufferNV, hk_vkCmdUpdatePipelineIndirectBufferNV);

        if (o_vkCmdSetDepthClampEnableEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetDepthClampEnableEXT, hk_vkCmdSetDepthClampEnableEXT);

        if (o_vkCmdSetPolygonModeEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetPolygonModeEXT, hk_vkCmdSetPolygonModeEXT);

        if (o_vkCmdSetRasterizationSamplesEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetRasterizationSamplesEXT, hk_vkCmdSetRasterizationSamplesEXT);

        if (o_vkCmdSetSampleMaskEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetSampleMaskEXT, hk_vkCmdSetSampleMaskEXT);

        if (o_vkCmdSetAlphaToCoverageEnableEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetAlphaToCoverageEnableEXT, hk_vkCmdSetAlphaToCoverageEnableEXT);

        if (o_vkCmdSetAlphaToOneEnableEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetAlphaToOneEnableEXT, hk_vkCmdSetAlphaToOneEnableEXT);

        if (o_vkCmdSetLogicOpEnableEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetLogicOpEnableEXT, hk_vkCmdSetLogicOpEnableEXT);

        if (o_vkCmdSetColorBlendEnableEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetColorBlendEnableEXT, hk_vkCmdSetColorBlendEnableEXT);

        if (o_vkCmdSetColorBlendEquationEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetColorBlendEquationEXT, hk_vkCmdSetColorBlendEquationEXT);

        if (o_vkCmdSetColorWriteMaskEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetColorWriteMaskEXT, hk_vkCmdSetColorWriteMaskEXT);

        if (o_vkCmdSetTessellationDomainOriginEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetTessellationDomainOriginEXT, hk_vkCmdSetTessellationDomainOriginEXT);

        if (o_vkCmdSetRasterizationStreamEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetRasterizationStreamEXT, hk_vkCmdSetRasterizationStreamEXT);

        if (o_vkCmdSetConservativeRasterizationModeEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetConservativeRasterizationModeEXT,
                         hk_vkCmdSetConservativeRasterizationModeEXT);

        if (o_vkCmdSetExtraPrimitiveOverestimationSizeEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetExtraPrimitiveOverestimationSizeEXT,
                         hk_vkCmdSetExtraPrimitiveOverestimationSizeEXT);

        if (o_vkCmdSetDepthClipEnableEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetDepthClipEnableEXT, hk_vkCmdSetDepthClipEnableEXT);

        if (o_vkCmdSetSampleLocationsEnableEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetSampleLocationsEnableEXT, hk_vkCmdSetSampleLocationsEnableEXT);

        if (o_vkCmdSetColorBlendAdvancedEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetColorBlendAdvancedEXT, hk_vkCmdSetColorBlendAdvancedEXT);

        if (o_vkCmdSetProvokingVertexModeEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetProvokingVertexModeEXT, hk_vkCmdSetProvokingVertexModeEXT);

        if (o_vkCmdSetLineRasterizationModeEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetLineRasterizationModeEXT, hk_vkCmdSetLineRasterizationModeEXT);

        if (o_vkCmdSetLineStippleEnableEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetLineStippleEnableEXT, hk_vkCmdSetLineStippleEnableEXT);

        if (o_vkCmdSetDepthClipNegativeOneToOneEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetDepthClipNegativeOneToOneEXT, hk_vkCmdSetDepthClipNegativeOneToOneEXT);

        if (o_vkCmdSetViewportWScalingEnableNV)
            DetourAttach(&(PVOID&) o_vkCmdSetViewportWScalingEnableNV, hk_vkCmdSetViewportWScalingEnableNV);

        if (o_vkCmdSetViewportSwizzleNV)
            DetourAttach(&(PVOID&) o_vkCmdSetViewportSwizzleNV, hk_vkCmdSetViewportSwizzleNV);

        if (o_vkCmdSetCoverageToColorEnableNV)
            DetourAttach(&(PVOID&) o_vkCmdSetCoverageToColorEnableNV, hk_vkCmdSetCoverageToColorEnableNV);

        if (o_vkCmdSetCoverageToColorLocationNV)
            DetourAttach(&(PVOID&) o_vkCmdSetCoverageToColorLocationNV, hk_vkCmdSetCoverageToColorLocationNV);

        if (o_vkCmdSetCoverageModulationModeNV)
            DetourAttach(&(PVOID&) o_vkCmdSetCoverageModulationModeNV, hk_vkCmdSetCoverageModulationModeNV);

        if (o_vkCmdSetCoverageModulationTableEnableNV)
            DetourAttach(&(PVOID&) o_vkCmdSetCoverageModulationTableEnableNV,
                         hk_vkCmdSetCoverageModulationTableEnableNV);

        if (o_vkCmdSetCoverageModulationTableNV)
            DetourAttach(&(PVOID&) o_vkCmdSetCoverageModulationTableNV, hk_vkCmdSetCoverageModulationTableNV);

        if (o_vkCmdSetShadingRateImageEnableNV)
            DetourAttach(&(PVOID&) o_vkCmdSetShadingRateImageEnableNV, hk_vkCmdSetShadingRateImageEnableNV);

        if (o_vkCmdSetRepresentativeFragmentTestEnableNV)
            DetourAttach(&(PVOID&) o_vkCmdSetRepresentativeFragmentTestEnableNV,
                         hk_vkCmdSetRepresentativeFragmentTestEnableNV);

        if (o_vkCmdSetCoverageReductionModeNV)
            DetourAttach(&(PVOID&) o_vkCmdSetCoverageReductionModeNV, hk_vkCmdSetCoverageReductionModeNV);

        if (o_vkCmdOpticalFlowExecuteNV)
            DetourAttach(&(PVOID&) o_vkCmdOpticalFlowExecuteNV, hk_vkCmdOpticalFlowExecuteNV);

        if (o_vkCmdBindShadersEXT)
            DetourAttach(&(PVOID&) o_vkCmdBindShadersEXT, hk_vkCmdBindShadersEXT);

        if (o_vkCmdSetDepthClampRangeEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetDepthClampRangeEXT, hk_vkCmdSetDepthClampRangeEXT);

        if (o_vkCmdConvertCooperativeVectorMatrixNV)
            DetourAttach(&(PVOID&) o_vkCmdConvertCooperativeVectorMatrixNV, hk_vkCmdConvertCooperativeVectorMatrixNV);

        if (o_vkCmdSetAttachmentFeedbackLoopEnableEXT)
            DetourAttach(&(PVOID&) o_vkCmdSetAttachmentFeedbackLoopEnableEXT,
                         hk_vkCmdSetAttachmentFeedbackLoopEnableEXT);

        if (o_vkCmdBuildClusterAccelerationStructureIndirectNV)
            DetourAttach(&(PVOID&) o_vkCmdBuildClusterAccelerationStructureIndirectNV,
                         hk_vkCmdBuildClusterAccelerationStructureIndirectNV);

        if (o_vkCmdBuildPartitionedAccelerationStructuresNV)
            DetourAttach(&(PVOID&) o_vkCmdBuildPartitionedAccelerationStructuresNV,
                         hk_vkCmdBuildPartitionedAccelerationStructuresNV);

        if (o_vkCmdPreprocessGeneratedCommandsEXT)
            DetourAttach(&(PVOID&) o_vkCmdPreprocessGeneratedCommandsEXT, hk_vkCmdPreprocessGeneratedCommandsEXT);

        if (o_vkCmdExecuteGeneratedCommandsEXT)
            DetourAttach(&(PVOID&) o_vkCmdExecuteGeneratedCommandsEXT, hk_vkCmdExecuteGeneratedCommandsEXT);

        if (o_vkCmdBuildAccelerationStructuresKHR)
            DetourAttach(&(PVOID&) o_vkCmdBuildAccelerationStructuresKHR, hk_vkCmdBuildAccelerationStructuresKHR);

        if (o_vkCmdBuildAccelerationStructuresIndirectKHR)
            DetourAttach(&(PVOID&) o_vkCmdBuildAccelerationStructuresIndirectKHR,
                         hk_vkCmdBuildAccelerationStructuresIndirectKHR);

        if (o_vkCmdCopyAccelerationStructureKHR)
            DetourAttach(&(PVOID&) o_vkCmdCopyAccelerationStructureKHR, hk_vkCmdCopyAccelerationStructureKHR);

        if (o_vkCmdCopyAccelerationStructureToMemoryKHR)
            DetourAttach(&(PVOID&) o_vkCmdCopyAccelerationStructureToMemoryKHR,
                         hk_vkCmdCopyAccelerationStructureToMemoryKHR);

        if (o_vkCmdCopyMemoryToAccelerationStructureKHR)
            DetourAttach(&(PVOID&) o_vkCmdCopyMemoryToAccelerationStructureKHR,
                         hk_vkCmdCopyMemoryToAccelerationStructureKHR);

        if (o_vkCmdWriteAccelerationStructuresPropertiesKHR)
            DetourAttach(&(PVOID&) o_vkCmdWriteAccelerationStructuresPropertiesKHR,
                         hk_vkCmdWriteAccelerationStructuresPropertiesKHR);

        if (o_vkCmdTraceRaysKHR)
            DetourAttach(&(PVOID&) o_vkCmdTraceRaysKHR, hk_vkCmdTraceRaysKHR);

        if (o_vkCmdTraceRaysIndirectKHR)
            DetourAttach(&(PVOID&) o_vkCmdTraceRaysIndirectKHR, hk_vkCmdTraceRaysIndirectKHR);

        if (o_vkCmdSetRayTracingPipelineStackSizeKHR)
            DetourAttach(&(PVOID&) o_vkCmdSetRayTracingPipelineStackSizeKHR, hk_vkCmdSetRayTracingPipelineStackSizeKHR);

        if (o_vkCmdDrawMeshTasksEXT)
            DetourAttach(&(PVOID&) o_vkCmdDrawMeshTasksEXT, hk_vkCmdDrawMeshTasksEXT);

        if (o_vkCmdDrawMeshTasksIndirectEXT)
            DetourAttach(&(PVOID&) o_vkCmdDrawMeshTasksIndirectEXT, hk_vkCmdDrawMeshTasksIndirectEXT);

        if (o_vkCmdDrawMeshTasksIndirectCountEXT)
            DetourAttach(&(PVOID&) o_vkCmdDrawMeshTasksIndirectCountEXT, hk_vkCmdDrawMeshTasksIndirectCountEXT);

#pragma endregion

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to attach Vulkan hooks: %d", detourResult);

            o_vkCmdBindPipeline = nullptr;
            o_vkCmdSetViewport = nullptr;
            o_vkCmdSetScissor = nullptr;
            o_vkCmdSetLineWidth = nullptr;
            o_vkCmdSetDepthBias = nullptr;
            o_vkCmdSetBlendConstants = nullptr;
            o_vkCmdSetDepthBounds = nullptr;
            o_vkCmdSetStencilCompareMask = nullptr;
            o_vkCmdSetStencilWriteMask = nullptr;
            o_vkCmdSetStencilReference = nullptr;
            o_vkCmdBindDescriptorSets = nullptr;
            o_vkCmdBindIndexBuffer = nullptr;
            o_vkCmdBindVertexBuffers = nullptr;
            o_vkCmdDraw = nullptr;
            o_vkCmdDrawIndexed = nullptr;
            o_vkCmdDrawIndirect = nullptr;
            o_vkCmdDrawIndexedIndirect = nullptr;
            o_vkCmdDispatch = nullptr;
            o_vkCmdDispatchIndirect = nullptr;
            o_vkCmdCopyBuffer = nullptr;
            o_vkCmdCopyImage = nullptr;
            o_vkCmdBlitImage = nullptr;
            o_vkCmdCopyBufferToImage = nullptr;
            o_vkCmdCopyImageToBuffer = nullptr;
            o_vkCmdUpdateBuffer = nullptr;
            o_vkCmdFillBuffer = nullptr;
            o_vkCmdClearColorImage = nullptr;
            o_vkCmdClearDepthStencilImage = nullptr;
            o_vkCmdClearAttachments = nullptr;
            o_vkCmdResolveImage = nullptr;
            o_vkCmdSetEvent = nullptr;
            o_vkCmdResetEvent = nullptr;
            o_vkCmdWaitEvents = nullptr;
            o_vkCmdPipelineBarrier = nullptr;
            o_vkCmdBeginQuery = nullptr;
            o_vkCmdEndQuery = nullptr;
            o_vkCmdResetQueryPool = nullptr;
            o_vkCmdWriteTimestamp = nullptr;
            o_vkCmdCopyQueryPoolResults = nullptr;
            o_vkCmdPushConstants = nullptr;
            o_vkCmdBeginRenderPass = nullptr;
            o_vkCmdNextSubpass = nullptr;
            o_vkCmdEndRenderPass = nullptr;
            o_vkCmdSetDeviceMask = nullptr;
            o_vkCmdDispatchBase = nullptr;
            o_vkCmdDrawIndirectCount = nullptr;
            o_vkCmdDrawIndexedIndirectCount = nullptr;
            o_vkCmdBeginRenderPass2 = nullptr;
            o_vkCmdNextSubpass2 = nullptr;
            o_vkCmdEndRenderPass2 = nullptr;
            o_vkCmdSetEvent2 = nullptr;
            o_vkCmdResetEvent2 = nullptr;
            o_vkCmdWaitEvents2 = nullptr;
            o_vkCmdPipelineBarrier2 = nullptr;
            o_vkCmdWriteTimestamp2 = nullptr;
            o_vkCmdCopyBuffer2 = nullptr;
            o_vkCmdCopyImage2 = nullptr;
            o_vkCmdCopyBufferToImage2 = nullptr;
            o_vkCmdCopyImageToBuffer2 = nullptr;
            o_vkCmdBlitImage2 = nullptr;
            o_vkCmdResolveImage2 = nullptr;
            o_vkCmdBeginRendering = nullptr;
            o_vkCmdEndRendering = nullptr;
            o_vkCmdSetCullMode = nullptr;
            o_vkCmdSetFrontFace = nullptr;
            o_vkCmdSetPrimitiveTopology = nullptr;
            o_vkCmdSetViewportWithCount = nullptr;
            o_vkCmdSetScissorWithCount = nullptr;
            o_vkCmdBindVertexBuffers2 = nullptr;
            o_vkCmdSetDepthTestEnable = nullptr;
            o_vkCmdSetDepthWriteEnable = nullptr;
            o_vkCmdSetDepthCompareOp = nullptr;
            o_vkCmdSetDepthBoundsTestEnable = nullptr;
            o_vkCmdSetStencilTestEnable = nullptr;
            o_vkCmdSetStencilOp = nullptr;
            o_vkCmdSetRasterizerDiscardEnable = nullptr;
            o_vkCmdSetDepthBiasEnable = nullptr;
            o_vkCmdSetPrimitiveRestartEnable = nullptr;
            o_vkCmdSetLineStipple = nullptr;
            o_vkCmdBindIndexBuffer2 = nullptr;
            o_vkCmdPushDescriptorSet = nullptr;
            o_vkCmdPushDescriptorSetWithTemplate = nullptr;
            o_vkCmdSetRenderingAttachmentLocations = nullptr;
            o_vkCmdSetRenderingInputAttachmentIndices = nullptr;
            o_vkCmdBindDescriptorSets2 = nullptr;
            o_vkCmdPushConstants2 = nullptr;
            o_vkCmdPushDescriptorSet2 = nullptr;
            o_vkCmdPushDescriptorSetWithTemplate2 = nullptr;
            o_vkCmdBeginVideoCodingKHR = nullptr;
            o_vkCmdEndVideoCodingKHR = nullptr;
            o_vkCmdControlVideoCodingKHR = nullptr;
            o_vkCmdDecodeVideoKHR = nullptr;
            o_vkCmdBeginRenderingKHR = nullptr;
            o_vkCmdEndRenderingKHR = nullptr;
            o_vkCmdSetDeviceMaskKHR = nullptr;
            o_vkCmdDispatchBaseKHR = nullptr;
            o_vkCmdPushDescriptorSetKHR = nullptr;
            o_vkCmdPushDescriptorSetWithTemplateKHR = nullptr;
            o_vkCmdBeginRenderPass2KHR = nullptr;
            o_vkCmdNextSubpass2KHR = nullptr;
            o_vkCmdEndRenderPass2KHR = nullptr;
            o_vkCmdDrawIndirectCountKHR = nullptr;
            o_vkCmdDrawIndexedIndirectCountKHR = nullptr;
            o_vkCmdSetFragmentShadingRateKHR = nullptr;
            o_vkCmdSetRenderingAttachmentLocationsKHR = nullptr;
            o_vkCmdSetRenderingInputAttachmentIndicesKHR = nullptr;
            o_vkCmdEncodeVideoKHR = nullptr;
            o_vkCmdSetEvent2KHR = nullptr;
            o_vkCmdResetEvent2KHR = nullptr;
            o_vkCmdWaitEvents2KHR = nullptr;
            o_vkCmdPipelineBarrier2KHR = nullptr;
            o_vkCmdWriteTimestamp2KHR = nullptr;
            o_vkCmdCopyBuffer2KHR = nullptr;
            o_vkCmdCopyImage2KHR = nullptr;
            o_vkCmdCopyBufferToImage2KHR = nullptr;
            o_vkCmdCopyImageToBuffer2KHR = nullptr;
            o_vkCmdBlitImage2KHR = nullptr;
            o_vkCmdResolveImage2KHR = nullptr;
            o_vkCmdTraceRaysIndirect2KHR = nullptr;
            o_vkCmdBindIndexBuffer2KHR = nullptr;
            o_vkCmdSetLineStippleKHR = nullptr;
            o_vkCmdBindDescriptorSets2KHR = nullptr;
            o_vkCmdPushConstants2KHR = nullptr;
            o_vkCmdPushDescriptorSet2KHR = nullptr;
            o_vkCmdPushDescriptorSetWithTemplate2KHR = nullptr;
            o_vkCmdSetDescriptorBufferOffsets2EXT = nullptr;
            o_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT = nullptr;
            o_vkCmdDebugMarkerBeginEXT = nullptr;
            o_vkCmdDebugMarkerEndEXT = nullptr;
            o_vkCmdDebugMarkerInsertEXT = nullptr;
            o_vkCmdBindTransformFeedbackBuffersEXT = nullptr;
            o_vkCmdBeginTransformFeedbackEXT = nullptr;
            o_vkCmdEndTransformFeedbackEXT = nullptr;
            o_vkCmdBeginQueryIndexedEXT = nullptr;
            o_vkCmdEndQueryIndexedEXT = nullptr;
            o_vkCmdDrawIndirectByteCountEXT = nullptr;
            o_vkCmdCuLaunchKernelNVX = nullptr;
            o_vkCmdDrawIndirectCountAMD = nullptr;
            o_vkCmdDrawIndexedIndirectCountAMD = nullptr;
            o_vkCmdBeginConditionalRenderingEXT = nullptr;
            o_vkCmdEndConditionalRenderingEXT = nullptr;
            o_vkCmdSetViewportWScalingNV = nullptr;
            o_vkCmdSetDiscardRectangleEXT = nullptr;
            o_vkCmdSetDiscardRectangleEnableEXT = nullptr;
            o_vkCmdSetDiscardRectangleModeEXT = nullptr;
            o_vkCmdBeginDebugUtilsLabelEXT = nullptr;
            o_vkCmdEndDebugUtilsLabelEXT = nullptr;
            o_vkCmdInsertDebugUtilsLabelEXT = nullptr;
            o_vkCmdSetSampleLocationsEXT = nullptr;
            o_vkCmdBindShadingRateImageNV = nullptr;
            o_vkCmdSetViewportShadingRatePaletteNV = nullptr;
            o_vkCmdSetCoarseSampleOrderNV = nullptr;
            o_vkCmdBuildAccelerationStructureNV = nullptr;
            o_vkCmdCopyAccelerationStructureNV = nullptr;
            o_vkCmdTraceRaysNV = nullptr;
            o_vkCmdWriteAccelerationStructuresPropertiesNV = nullptr;
            o_vkCmdWriteBufferMarkerAMD = nullptr;
            o_vkCmdWriteBufferMarker2AMD = nullptr;
            o_vkCmdDrawMeshTasksNV = nullptr;
            o_vkCmdDrawMeshTasksIndirectNV = nullptr;
            o_vkCmdDrawMeshTasksIndirectCountNV = nullptr;
            o_vkCmdSetExclusiveScissorEnableNV = nullptr;
            o_vkCmdSetExclusiveScissorNV = nullptr;
            o_vkCmdSetCheckpointNV = nullptr;
            o_vkCmdSetPerformanceMarkerINTEL = nullptr;
            o_vkCmdSetPerformanceStreamMarkerINTEL = nullptr;
            o_vkCmdSetPerformanceOverrideINTEL = nullptr;
            o_vkCmdSetLineStippleEXT = nullptr;
            o_vkCmdSetCullModeEXT = nullptr;
            o_vkCmdSetFrontFaceEXT = nullptr;
            o_vkCmdSetPrimitiveTopologyEXT = nullptr;
            o_vkCmdSetViewportWithCountEXT = nullptr;
            o_vkCmdSetScissorWithCountEXT = nullptr;
            o_vkCmdBindVertexBuffers2EXT = nullptr;
            o_vkCmdSetDepthTestEnableEXT = nullptr;
            o_vkCmdSetDepthWriteEnableEXT = nullptr;
            o_vkCmdSetDepthCompareOpEXT = nullptr;
            o_vkCmdSetDepthBoundsTestEnableEXT = nullptr;
            o_vkCmdSetStencilTestEnableEXT = nullptr;
            o_vkCmdSetStencilOpEXT = nullptr;
            o_vkCmdPreprocessGeneratedCommandsNV = nullptr;
            o_vkCmdExecuteGeneratedCommandsNV = nullptr;
            o_vkCmdBindPipelineShaderGroupNV = nullptr;
            o_vkCmdSetDepthBias2EXT = nullptr;
            o_vkCmdCudaLaunchKernelNV = nullptr;
            o_vkCmdBindDescriptorBuffersEXT = nullptr;
            o_vkCmdSetDescriptorBufferOffsetsEXT = nullptr;
            o_vkCmdBindDescriptorBufferEmbeddedSamplersEXT = nullptr;
            o_vkCmdSetFragmentShadingRateEnumNV = nullptr;
            o_vkCmdSetVertexInputEXT = nullptr;
            o_vkCmdSubpassShadingHUAWEI = nullptr;
            o_vkCmdBindInvocationMaskHUAWEI = nullptr;
            o_vkCmdSetPatchControlPointsEXT = nullptr;
            o_vkCmdSetRasterizerDiscardEnableEXT = nullptr;
            o_vkCmdSetDepthBiasEnableEXT = nullptr;
            o_vkCmdSetLogicOpEXT = nullptr;
            o_vkCmdSetPrimitiveRestartEnableEXT = nullptr;
            o_vkCmdSetColorWriteEnableEXT = nullptr;
            o_vkCmdDrawMultiEXT = nullptr;
            o_vkCmdDrawMultiIndexedEXT = nullptr;
            o_vkCmdBuildMicromapsEXT = nullptr;
            o_vkCmdCopyMicromapEXT = nullptr;
            o_vkCmdCopyMicromapToMemoryEXT = nullptr;
            o_vkCmdCopyMemoryToMicromapEXT = nullptr;
            o_vkCmdWriteMicromapsPropertiesEXT = nullptr;
            o_vkCmdDrawClusterHUAWEI = nullptr;
            o_vkCmdDrawClusterIndirectHUAWEI = nullptr;
            o_vkCmdCopyMemoryIndirectNV = nullptr;
            o_vkCmdCopyMemoryToImageIndirectNV = nullptr;
            o_vkCmdDecompressMemoryNV = nullptr;
            o_vkCmdDecompressMemoryIndirectCountNV = nullptr;
            o_vkCmdUpdatePipelineIndirectBufferNV = nullptr;
            o_vkCmdSetDepthClampEnableEXT = nullptr;
            o_vkCmdSetPolygonModeEXT = nullptr;
            o_vkCmdSetRasterizationSamplesEXT = nullptr;
            o_vkCmdSetSampleMaskEXT = nullptr;
            o_vkCmdSetAlphaToCoverageEnableEXT = nullptr;
            o_vkCmdSetAlphaToOneEnableEXT = nullptr;
            o_vkCmdSetLogicOpEnableEXT = nullptr;
            o_vkCmdSetColorBlendEnableEXT = nullptr;
            o_vkCmdSetColorBlendEquationEXT = nullptr;
            o_vkCmdSetColorWriteMaskEXT = nullptr;
            o_vkCmdSetTessellationDomainOriginEXT = nullptr;
            o_vkCmdSetRasterizationStreamEXT = nullptr;
            o_vkCmdSetConservativeRasterizationModeEXT = nullptr;
            o_vkCmdSetExtraPrimitiveOverestimationSizeEXT = nullptr;
            o_vkCmdSetDepthClipEnableEXT = nullptr;
            o_vkCmdSetSampleLocationsEnableEXT = nullptr;
            o_vkCmdSetColorBlendAdvancedEXT = nullptr;
            o_vkCmdSetProvokingVertexModeEXT = nullptr;
            o_vkCmdSetLineRasterizationModeEXT = nullptr;
            o_vkCmdSetLineStippleEnableEXT = nullptr;
            o_vkCmdSetDepthClipNegativeOneToOneEXT = nullptr;
            o_vkCmdSetViewportWScalingEnableNV = nullptr;
            o_vkCmdSetViewportSwizzleNV = nullptr;
            o_vkCmdSetCoverageToColorEnableNV = nullptr;
            o_vkCmdSetCoverageToColorLocationNV = nullptr;
            o_vkCmdSetCoverageModulationModeNV = nullptr;
            o_vkCmdSetCoverageModulationTableEnableNV = nullptr;
            o_vkCmdSetCoverageModulationTableNV = nullptr;
            o_vkCmdSetShadingRateImageEnableNV = nullptr;
            o_vkCmdSetRepresentativeFragmentTestEnableNV = nullptr;
            o_vkCmdSetCoverageReductionModeNV = nullptr;
            o_vkCmdOpticalFlowExecuteNV = nullptr;
            o_vkCmdBindShadersEXT = nullptr;
            o_vkCmdSetDepthClampRangeEXT = nullptr;
            o_vkCmdConvertCooperativeVectorMatrixNV = nullptr;
            o_vkCmdSetAttachmentFeedbackLoopEnableEXT = nullptr;
            o_vkCmdBuildClusterAccelerationStructureIndirectNV = nullptr;
            o_vkCmdTraceRaysKHR = nullptr;
            o_vkCmdTraceRaysIndirectKHR = nullptr;
            o_vkCmdSetRayTracingPipelineStackSizeKHR = nullptr;
            o_vkCmdDrawMeshTasksEXT = nullptr;
            o_vkCmdDrawMeshTasksIndirectEXT = nullptr;
            o_vkCmdDrawMeshTasksIndirectCountEXT = nullptr;

            return;
        }

        InitializeStateTrackerFunctionTable();
    }
}

void Vulkan_wDx12::Unhook()
{
    if (o_vkQueueSubmit == nullptr)
        return;

    LOG_INFO("Detaching Vulkan hooks");

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_vkQueueSubmit)
        DetourDetach(&(PVOID&) o_vkQueueSubmit, hk_vkQueueSubmit);

    if (o_vkQueueSubmit2)
        DetourDetach(&(PVOID&) o_vkQueueSubmit2, hk_vkQueueSubmit2);

    if (o_vkQueueSubmit2KHR)
        DetourDetach(&(PVOID&) o_vkQueueSubmit2KHR, hk_vkQueueSubmit2);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to detach Vulkan hooks: %d", detourResult);
    }
    else
    {
        o_vkQueueSubmit = nullptr;
        o_vkQueueSubmit2 = nullptr;
        o_vkQueueSubmit2KHR = nullptr;
    }
}
