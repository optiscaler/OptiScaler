#pragma once

#include <pch.h>

#include "CommandBuffer_StateTracker.h"

#include <vulkan/vulkan.hpp>

#ifdef VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan_win32.h>
#endif

#include "Hook_Utils.h"

class Vulkan_wDx12
{
  public:
    struct PendingSubmission
    {
        VkCommandBuffer submitCommandBuffer = VK_NULL_HANDLE;
        VkSemaphore resourceCopySemaphore = VK_NULL_HANDLE;
        ID3D12Fence* resourceCopyFence = nullptr;
        uint64_t resourceCopyValue = 0;
        VkSemaphore copyBackSemaphore = VK_NULL_HANDLE;
        uint64_t d3d12CompleteValue = 0;
        uint64_t copyBackValue = 0;
        VkCommandBuffer copyBackCommandBuffer = VK_NULL_HANDLE;
        VkCommandBuffer barrierCommandBuffer = VK_NULL_HANDLE;
    };

    static bool RegisterPendingSubmission(const PendingSubmission& submission);
    static void CancelPendingSubmission(VkCommandBuffer commandBuffer);
    static bool RegisterVirtualCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBuffer virtualCommandBuffer);
    static VkCommandBuffer RemoveVirtualCommandBuffer(VkCommandBuffer commandBuffer);

  private:
    static VkResult hk_vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence);
    static VkResult hk_vkQueueSubmit2(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2* pSubmits,
                                      VkFence fence);
    static VkResult hk_vkQueueSubmit2KHR(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2* pSubmits,
                                         VkFence fence);
    static VkResult hk_vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo);
    static VkResult hk_vkEndCommandBuffer(VkCommandBuffer commandBuffer);
    static VkResult hk_vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags);
    static void hk_vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount,
                                        const VkCommandBuffer* pCommandBuffers);
    static VkResult hk_vkResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags);
    static VkResult hk_vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo,
                                                VkCommandBuffer* pCommandBuffers);
    static void hk_vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool,
                                        const VkAllocationCallbacks* pAllocator);

    static void hk_vkCmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount,
                                        const VkCommandBuffer* pCommandBuffers);
    static VkResult hk_vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo,
                                           const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool);
    static PFN_vkVoidFunction GetAddress(const PFN_vkVoidFunction original, const char* pName);
    static void InitializeStateTrackerFunctionTable();
    static bool TakePendingSubmission(VkCommandBuffer commandBuffer, PendingSubmission& submission);
    static VkCommandBuffer GetVirtualCommandBuffer(VkCommandBuffer commandBuffer);

    inline static std::mutex pendingSubmissionMutex;
    inline static std::unordered_map<VkCommandBuffer, PendingSubmission> pendingSubmissions;

#pragma region Command Buffer Hooks

    static void hk_vkCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                                     VkPipeline pipeline);
    static void hk_vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount,
                                    const VkViewport* pViewports);
    static void hk_vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount,
                                   const VkRect2D* pScissors);
    static void hk_vkCmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth);
    static void hk_vkCmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp,
                                     float depthBiasSlopeFactor);
    static void hk_vkCmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4]);
    static void hk_vkCmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds);
    static void hk_vkCmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
                                              uint32_t compareMask);
    static void hk_vkCmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
                                            uint32_t writeMask);
    static void hk_vkCmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
                                            uint32_t reference);
    static void hk_vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                                           VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount,
                                           const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount,
                                           const uint32_t* pDynamicOffsets);
    static void hk_vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                        VkIndexType indexType);
    static void hk_vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount,
                                          const VkBuffer* pBuffers, const VkDeviceSize* pOffsets);
    static void hk_vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount,
                             uint32_t firstVertex, uint32_t firstInstance);
    static void hk_vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount,
                                    uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
    static void hk_vkCmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                     uint32_t drawCount, uint32_t stride);
    static void hk_vkCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                            uint32_t drawCount, uint32_t stride);
    static void hk_vkCmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY,
                                 uint32_t groupCountZ);
    static void hk_vkCmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset);
    static void hk_vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer,
                                   uint32_t regionCount, const VkBufferCopy* pRegions);
    static void hk_vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                                  VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
                                  const VkImageCopy* pRegions);
    static void hk_vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                                  VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
                                  const VkImageBlit* pRegions, VkFilter filter);
    static void hk_vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage,
                                          VkImageLayout dstImageLayout, uint32_t regionCount,
                                          const VkBufferImageCopy* pRegions);
    static void hk_vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                                          VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions);
    static void hk_vkCmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset,
                                     VkDeviceSize dataSize, const void* pData);
    static void hk_vkCmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset,
                                   VkDeviceSize size, uint32_t data);
    static void hk_vkCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
                                        const VkClearColorValue* pColor, uint32_t rangeCount,
                                        const VkImageSubresourceRange* pRanges);
    static void hk_vkCmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
                                               const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount,
                                               const VkImageSubresourceRange* pRanges);
    static void hk_vkCmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                                         const VkClearAttachment* pAttachments, uint32_t rectCount,
                                         const VkClearRect* pRects);
    static void hk_vkCmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                                     VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
                                     const VkImageResolve* pRegions);
    static void hk_vkCmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask);
    static void hk_vkCmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask);
    static void hk_vkCmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents,
                                   VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
                                   uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers,
                                   uint32_t bufferMemoryBarrierCount,
                                   const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount,
                                   const VkImageMemoryBarrier* pImageMemoryBarriers);
    static void hk_vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
                                        VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
                                        uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers,
                                        uint32_t bufferMemoryBarrierCount,
                                        const VkBufferMemoryBarrier* pBufferMemoryBarriers,
                                        uint32_t imageMemoryBarrierCount,
                                        const VkImageMemoryBarrier* pImageMemoryBarriers);
    static void hk_vkCmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query,
                                   VkQueryControlFlags flags);
    static void hk_vkCmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query);
    static void hk_vkCmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery,
                                       uint32_t queryCount);
    static void hk_vkCmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage,
                                       VkQueryPool queryPool, uint32_t query);
    static void hk_vkCmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery,
                                             uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset,
                                             VkDeviceSize stride, VkQueryResultFlags flags);
    static void hk_vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout,
                                      VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size,
                                      const void* pValues);
    static void hk_vkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin,
                                        VkSubpassContents contents);
    static void hk_vkCmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents);
    static void hk_vkCmdEndRenderPass(VkCommandBuffer commandBuffer);
    static void hk_vkCmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask);
    static void hk_vkCmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY,
                                     uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY,
                                     uint32_t groupCountZ);
    static void hk_vkCmdDrawIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                          VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                          uint32_t stride);
    static void hk_vkCmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                 VkBuffer countBuffer, VkDeviceSize countBufferOffset,
                                                 uint32_t maxDrawCount, uint32_t stride);
    static void hk_vkCmdBeginRenderPass2(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin,
                                         const VkSubpassBeginInfo* pSubpassBeginInfo);
    static void hk_vkCmdNextSubpass2(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo* pSubpassBeginInfo,
                                     const VkSubpassEndInfo* pSubpassEndInfo);
    static void hk_vkCmdEndRenderPass2(VkCommandBuffer commandBuffer, const VkSubpassEndInfo* pSubpassEndInfo);
    static void hk_vkCmdSetEvent2(VkCommandBuffer commandBuffer, VkEvent event,
                                  const VkDependencyInfo* pDependencyInfo);
    static void hk_vkCmdResetEvent2(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags2 stageMask);
    static void hk_vkCmdWaitEvents2(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents,
                                    const VkDependencyInfo* pDependencyInfos);
    static void hk_vkCmdPipelineBarrier2(VkCommandBuffer commandBuffer, const VkDependencyInfo* pDependencyInfo);
    static void hk_vkCmdWriteTimestamp2(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage,
                                        VkQueryPool queryPool, uint32_t query);
    static void hk_vkCmdCopyBuffer2(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2* pCopyBufferInfo);
    static void hk_vkCmdCopyImage2(VkCommandBuffer commandBuffer, const VkCopyImageInfo2* pCopyImageInfo);
    static void hk_vkCmdCopyBufferToImage2(VkCommandBuffer commandBuffer,
                                           const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo);
    static void hk_vkCmdCopyImageToBuffer2(VkCommandBuffer commandBuffer,
                                           const VkCopyImageToBufferInfo2* pCopyImageToBufferInfo);
    static void hk_vkCmdBlitImage2(VkCommandBuffer commandBuffer, const VkBlitImageInfo2* pBlitImageInfo);
    static void hk_vkCmdResolveImage2(VkCommandBuffer commandBuffer, const VkResolveImageInfo2* pResolveImageInfo);
    static void hk_vkCmdBeginRendering(VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo);
    static void hk_vkCmdEndRendering(VkCommandBuffer commandBuffer);
    static void hk_vkCmdSetCullMode(VkCommandBuffer commandBuffer, VkCullModeFlags cullMode);
    static void hk_vkCmdSetFrontFace(VkCommandBuffer commandBuffer, VkFrontFace frontFace);
    static void hk_vkCmdSetPrimitiveTopology(VkCommandBuffer commandBuffer, VkPrimitiveTopology primitiveTopology);
    static void hk_vkCmdSetViewportWithCount(VkCommandBuffer commandBuffer, uint32_t viewportCount,
                                             const VkViewport* pViewports);
    static void hk_vkCmdSetScissorWithCount(VkCommandBuffer commandBuffer, uint32_t scissorCount,
                                            const VkRect2D* pScissors);
    static void hk_vkCmdBindVertexBuffers2(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount,
                                           const VkBuffer* pBuffers, const VkDeviceSize* pOffsets,
                                           const VkDeviceSize* pSizes, const VkDeviceSize* pStrides);
    static void hk_vkCmdSetDepthTestEnable(VkCommandBuffer commandBuffer, VkBool32 depthTestEnable);
    static void hk_vkCmdSetDepthWriteEnable(VkCommandBuffer commandBuffer, VkBool32 depthWriteEnable);
    static void hk_vkCmdSetDepthCompareOp(VkCommandBuffer commandBuffer, VkCompareOp depthCompareOp);
    static void hk_vkCmdSetDepthBoundsTestEnable(VkCommandBuffer commandBuffer, VkBool32 depthBoundsTestEnable);
    static void hk_vkCmdSetStencilTestEnable(VkCommandBuffer commandBuffer, VkBool32 stencilTestEnable);
    static void hk_vkCmdSetStencilOp(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, VkStencilOp failOp,
                                     VkStencilOp passOp, VkStencilOp depthFailOp, VkCompareOp compareOp);
    static void hk_vkCmdSetRasterizerDiscardEnable(VkCommandBuffer commandBuffer, VkBool32 rasterizerDiscardEnable);
    static void hk_vkCmdSetDepthBiasEnable(VkCommandBuffer commandBuffer, VkBool32 depthBiasEnable);
    static void hk_vkCmdSetPrimitiveRestartEnable(VkCommandBuffer commandBuffer, VkBool32 primitiveRestartEnable);
    static void hk_vkCmdSetLineStipple(VkCommandBuffer commandBuffer, uint32_t lineStippleFactor,
                                       uint16_t lineStipplePattern);
    static void hk_vkCmdBindIndexBuffer2(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                         VkDeviceSize size, VkIndexType indexType);
    static void hk_vkCmdPushDescriptorSet(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                                          VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount,
                                          const VkWriteDescriptorSet* pDescriptorWrites);
    static void hk_vkCmdPushDescriptorSetWithTemplate(VkCommandBuffer commandBuffer,
                                                      VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                                      VkPipelineLayout layout, uint32_t set, const void* pData);
    static void hk_vkCmdSetRenderingAttachmentLocations(VkCommandBuffer commandBuffer,
                                                        const VkRenderingAttachmentLocationInfo* pLocationInfo);
    static void
    hk_vkCmdSetRenderingInputAttachmentIndices(VkCommandBuffer commandBuffer,
                                               const VkRenderingInputAttachmentIndexInfo* pInputAttachmentIndexInfo);
    static void hk_vkCmdBindDescriptorSets2(VkCommandBuffer commandBuffer,
                                            const VkBindDescriptorSetsInfo* pBindDescriptorSetsInfo);
    static void hk_vkCmdPushConstants2(VkCommandBuffer commandBuffer, const VkPushConstantsInfo* pPushConstantsInfo);
    static void hk_vkCmdPushDescriptorSet2(VkCommandBuffer commandBuffer,
                                           const VkPushDescriptorSetInfo* pPushDescriptorSetInfo);
    static void hk_vkCmdPushDescriptorSetWithTemplate2(
        VkCommandBuffer commandBuffer, const VkPushDescriptorSetWithTemplateInfo* pPushDescriptorSetWithTemplateInfo);
    static void hk_vkCmdBeginVideoCodingKHR(VkCommandBuffer commandBuffer, const VkVideoBeginCodingInfoKHR* pBeginInfo);
    static void hk_vkCmdEndVideoCodingKHR(VkCommandBuffer commandBuffer, const VkVideoEndCodingInfoKHR* pEndCodingInfo);
    static void hk_vkCmdControlVideoCodingKHR(VkCommandBuffer commandBuffer,
                                              const VkVideoCodingControlInfoKHR* pCodingControlInfo);
    static void hk_vkCmdDecodeVideoKHR(VkCommandBuffer commandBuffer, const VkVideoDecodeInfoKHR* pDecodeInfo);
    static void hk_vkCmdBeginRenderingKHR(VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo);
    static void hk_vkCmdEndRenderingKHR(VkCommandBuffer commandBuffer);
    static void hk_vkCmdSetDeviceMaskKHR(VkCommandBuffer commandBuffer, uint32_t deviceMask);
    static void hk_vkCmdDispatchBaseKHR(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY,
                                        uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY,
                                        uint32_t groupCountZ);
    static void hk_vkCmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                                             VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount,
                                             const VkWriteDescriptorSet* pDescriptorWrites);
    static void hk_vkCmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer,
                                                         VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                                         VkPipelineLayout layout, uint32_t set, const void* pData);
    static void hk_vkCmdBeginRenderPass2KHR(VkCommandBuffer commandBuffer,
                                            const VkRenderPassBeginInfo* pRenderPassBegin,
                                            const VkSubpassBeginInfo* pSubpassBeginInfo);
    static void hk_vkCmdNextSubpass2KHR(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo* pSubpassBeginInfo,
                                        const VkSubpassEndInfo* pSubpassEndInfo);
    static void hk_vkCmdEndRenderPass2KHR(VkCommandBuffer commandBuffer, const VkSubpassEndInfo* pSubpassEndInfo);
    static void hk_vkCmdDrawIndirectCountKHR(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                             VkBuffer countBuffer, VkDeviceSize countBufferOffset,
                                             uint32_t maxDrawCount, uint32_t stride);
    static void hk_vkCmdDrawIndexedIndirectCountKHR(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                    VkBuffer countBuffer, VkDeviceSize countBufferOffset,
                                                    uint32_t maxDrawCount, uint32_t stride);
    static void hk_vkCmdSetFragmentShadingRateKHR(VkCommandBuffer commandBuffer, const VkExtent2D* pFragmentSize,
                                                  const VkFragmentShadingRateCombinerOpKHR combinerOps[2]);
    static void hk_vkCmdSetRenderingAttachmentLocationsKHR(VkCommandBuffer commandBuffer,
                                                           const VkRenderingAttachmentLocationInfo* pLocationInfo);
    static void
    hk_vkCmdSetRenderingInputAttachmentIndicesKHR(VkCommandBuffer commandBuffer,
                                                  const VkRenderingInputAttachmentIndexInfo* pInputAttachmentIndexInfo);
    static void hk_vkCmdEncodeVideoKHR(VkCommandBuffer commandBuffer, const VkVideoEncodeInfoKHR* pEncodeInfo);
    static void hk_vkCmdSetEvent2KHR(VkCommandBuffer commandBuffer, VkEvent event,
                                     const VkDependencyInfo* pDependencyInfo);
    static void hk_vkCmdResetEvent2KHR(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags2 stageMask);
    static void hk_vkCmdWaitEvents2KHR(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents,
                                       const VkDependencyInfo* pDependencyInfos);
    static void hk_vkCmdPipelineBarrier2KHR(VkCommandBuffer commandBuffer, const VkDependencyInfo* pDependencyInfo);
    static void hk_vkCmdWriteTimestamp2KHR(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage,
                                           VkQueryPool queryPool, uint32_t query);
    static void hk_vkCmdCopyBuffer2KHR(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2* pCopyBufferInfo);
    static void hk_vkCmdCopyImage2KHR(VkCommandBuffer commandBuffer, const VkCopyImageInfo2* pCopyImageInfo);
    static void hk_vkCmdCopyBufferToImage2KHR(VkCommandBuffer commandBuffer,
                                              const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo);
    static void hk_vkCmdCopyImageToBuffer2KHR(VkCommandBuffer commandBuffer,
                                              const VkCopyImageToBufferInfo2* pCopyImageToBufferInfo);
    static void hk_vkCmdBlitImage2KHR(VkCommandBuffer commandBuffer, const VkBlitImageInfo2* pBlitImageInfo);
    static void hk_vkCmdResolveImage2KHR(VkCommandBuffer commandBuffer, const VkResolveImageInfo2* pResolveImageInfo);
    static void hk_vkCmdTraceRaysIndirect2KHR(VkCommandBuffer commandBuffer, VkDeviceAddress indirectDeviceAddress);
    static void hk_vkCmdBindIndexBuffer2KHR(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                            VkDeviceSize size, VkIndexType indexType);
    static void hk_vkCmdSetLineStippleKHR(VkCommandBuffer commandBuffer, uint32_t lineStippleFactor,
                                          uint16_t lineStipplePattern);
    static void hk_vkCmdBindDescriptorSets2KHR(VkCommandBuffer commandBuffer,
                                               const VkBindDescriptorSetsInfo* pBindDescriptorSetsInfo);
    static void hk_vkCmdPushConstants2KHR(VkCommandBuffer commandBuffer, const VkPushConstantsInfo* pPushConstantsInfo);
    static void hk_vkCmdPushDescriptorSet2KHR(VkCommandBuffer commandBuffer,
                                              const VkPushDescriptorSetInfo* pPushDescriptorSetInfo);
    static void hk_vkCmdPushDescriptorSetWithTemplate2KHR(
        VkCommandBuffer commandBuffer, const VkPushDescriptorSetWithTemplateInfo* pPushDescriptorSetWithTemplateInfo);
    static void
    hk_vkCmdSetDescriptorBufferOffsets2EXT(VkCommandBuffer commandBuffer,
                                           const VkSetDescriptorBufferOffsetsInfoEXT* pSetDescriptorBufferOffsetsInfo);
    static void hk_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT(
        VkCommandBuffer commandBuffer,
        const VkBindDescriptorBufferEmbeddedSamplersInfoEXT* pBindDescriptorBufferEmbeddedSamplersInfo);
    static void hk_vkCmdDebugMarkerBeginEXT(VkCommandBuffer commandBuffer,
                                            const VkDebugMarkerMarkerInfoEXT* pMarkerInfo);
    static void hk_vkCmdDebugMarkerEndEXT(VkCommandBuffer commandBuffer);
    static void hk_vkCmdDebugMarkerInsertEXT(VkCommandBuffer commandBuffer,
                                             const VkDebugMarkerMarkerInfoEXT* pMarkerInfo);
    static void hk_vkCmdBindTransformFeedbackBuffersEXT(VkCommandBuffer commandBuffer, uint32_t firstBinding,
                                                        uint32_t bindingCount, const VkBuffer* pBuffers,
                                                        const VkDeviceSize* pOffsets, const VkDeviceSize* pSizes);
    static void hk_vkCmdBeginTransformFeedbackEXT(VkCommandBuffer commandBuffer, uint32_t firstCounterBuffer,
                                                  uint32_t counterBufferCount, const VkBuffer* pCounterBuffers,
                                                  const VkDeviceSize* pCounterBufferOffsets);
    static void hk_vkCmdEndTransformFeedbackEXT(VkCommandBuffer commandBuffer, uint32_t firstCounterBuffer,
                                                uint32_t counterBufferCount, const VkBuffer* pCounterBuffers,
                                                const VkDeviceSize* pCounterBufferOffsets);
    static void hk_vkCmdBeginQueryIndexedEXT(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query,
                                             VkQueryControlFlags flags, uint32_t index);
    static void hk_vkCmdEndQueryIndexedEXT(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query,
                                           uint32_t index);
    static void hk_vkCmdDrawIndirectByteCountEXT(VkCommandBuffer commandBuffer, uint32_t instanceCount,
                                                 uint32_t firstInstance, VkBuffer counterBuffer,
                                                 VkDeviceSize counterBufferOffset, uint32_t counterOffset,
                                                 uint32_t vertexStride);
    static void hk_vkCmdCuLaunchKernelNVX(VkCommandBuffer commandBuffer, const VkCuLaunchInfoNVX* pLaunchInfo);
    static void hk_vkCmdDrawIndirectCountAMD(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                             VkBuffer countBuffer, VkDeviceSize countBufferOffset,
                                             uint32_t maxDrawCount, uint32_t stride);
    static void hk_vkCmdDrawIndexedIndirectCountAMD(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                    VkBuffer countBuffer, VkDeviceSize countBufferOffset,
                                                    uint32_t maxDrawCount, uint32_t stride);
    static void
    hk_vkCmdBeginConditionalRenderingEXT(VkCommandBuffer commandBuffer,
                                         const VkConditionalRenderingBeginInfoEXT* pConditionalRenderingBegin);
    static void hk_vkCmdEndConditionalRenderingEXT(VkCommandBuffer commandBuffer);
    static void hk_vkCmdSetViewportWScalingNV(VkCommandBuffer commandBuffer, uint32_t firstViewport,
                                              uint32_t viewportCount, const VkViewportWScalingNV* pViewportWScalings);
    static void hk_vkCmdSetDiscardRectangleEXT(VkCommandBuffer commandBuffer, uint32_t firstDiscardRectangle,
                                               uint32_t discardRectangleCount, const VkRect2D* pDiscardRectangles);
    static void hk_vkCmdSetDiscardRectangleEnableEXT(VkCommandBuffer commandBuffer, VkBool32 discardRectangleEnable);
    static void hk_vkCmdSetDiscardRectangleModeEXT(VkCommandBuffer commandBuffer,
                                                   VkDiscardRectangleModeEXT discardRectangleMode);
    static void hk_vkCmdBeginDebugUtilsLabelEXT(VkCommandBuffer commandBuffer, const VkDebugUtilsLabelEXT* pLabelInfo);
    static void hk_vkCmdEndDebugUtilsLabelEXT(VkCommandBuffer commandBuffer);
    static void hk_vkCmdInsertDebugUtilsLabelEXT(VkCommandBuffer commandBuffer, const VkDebugUtilsLabelEXT* pLabelInfo);
    static void hk_vkCmdSetSampleLocationsEXT(VkCommandBuffer commandBuffer,
                                              const VkSampleLocationsInfoEXT* pSampleLocationsInfo);
    static void hk_vkCmdBindShadingRateImageNV(VkCommandBuffer commandBuffer, VkImageView imageView,
                                               VkImageLayout imageLayout);
    static void hk_vkCmdSetViewportShadingRatePaletteNV(VkCommandBuffer commandBuffer, uint32_t firstViewport,
                                                        uint32_t viewportCount,
                                                        const VkShadingRatePaletteNV* pShadingRatePalettes);
    static void hk_vkCmdSetCoarseSampleOrderNV(VkCommandBuffer commandBuffer, VkCoarseSampleOrderTypeNV sampleOrderType,
                                               uint32_t customSampleOrderCount,
                                               const VkCoarseSampleOrderCustomNV* pCustomSampleOrders);
    static void hk_vkCmdBuildAccelerationStructureNV(VkCommandBuffer commandBuffer,
                                                     const VkAccelerationStructureInfoNV* pInfo, VkBuffer instanceData,
                                                     VkDeviceSize instanceOffset, VkBool32 update,
                                                     VkAccelerationStructureNV dst, VkAccelerationStructureNV src,
                                                     VkBuffer scratch, VkDeviceSize scratchOffset);
    static void hk_vkCmdCopyAccelerationStructureNV(VkCommandBuffer commandBuffer, VkAccelerationStructureNV dst,
                                                    VkAccelerationStructureNV src,
                                                    VkCopyAccelerationStructureModeKHR mode);
    static void hk_vkCmdTraceRaysNV(VkCommandBuffer commandBuffer, VkBuffer raygenShaderBindingTableBuffer,
                                    VkDeviceSize raygenShaderBindingOffset, VkBuffer missShaderBindingTableBuffer,
                                    VkDeviceSize missShaderBindingOffset, VkDeviceSize missShaderBindingStride,
                                    VkBuffer hitShaderBindingTableBuffer, VkDeviceSize hitShaderBindingOffset,
                                    VkDeviceSize hitShaderBindingStride, VkBuffer callableShaderBindingTableBuffer,
                                    VkDeviceSize callableShaderBindingOffset, VkDeviceSize callableShaderBindingStride,
                                    uint32_t width, uint32_t height, uint32_t depth);
    static void
    hk_vkCmdWriteAccelerationStructuresPropertiesNV(VkCommandBuffer commandBuffer, uint32_t accelerationStructureCount,
                                                    const VkAccelerationStructureNV* pAccelerationStructures,
                                                    VkQueryType queryType, VkQueryPool queryPool, uint32_t firstQuery);
    static void hk_vkCmdWriteBufferMarkerAMD(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage,
                                             VkBuffer dstBuffer, VkDeviceSize dstOffset, uint32_t marker);
    static void hk_vkCmdWriteBufferMarker2AMD(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage,
                                              VkBuffer dstBuffer, VkDeviceSize dstOffset, uint32_t marker);
    static void hk_vkCmdDrawMeshTasksNV(VkCommandBuffer commandBuffer, uint32_t taskCount, uint32_t firstTask);
    static void hk_vkCmdDrawMeshTasksIndirectNV(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                uint32_t drawCount, uint32_t stride);
    static void hk_vkCmdDrawMeshTasksIndirectCountNV(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                                     VkDeviceSize offset, VkBuffer countBuffer,
                                                     VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                                     uint32_t stride);
    static void hk_vkCmdSetExclusiveScissorEnableNV(VkCommandBuffer commandBuffer, uint32_t firstExclusiveScissor,
                                                    uint32_t exclusiveScissorCount,
                                                    const VkBool32* pExclusiveScissorEnables);
    static void hk_vkCmdSetExclusiveScissorNV(VkCommandBuffer commandBuffer, uint32_t firstExclusiveScissor,
                                              uint32_t exclusiveScissorCount, const VkRect2D* pExclusiveScissors);
    static void hk_vkCmdSetCheckpointNV(VkCommandBuffer commandBuffer, const void* pCheckpointMarker);
    static VkResult hk_vkCmdSetPerformanceMarkerINTEL(VkCommandBuffer commandBuffer,
                                                      const VkPerformanceMarkerInfoINTEL* pMarkerInfo);
    static VkResult hk_vkCmdSetPerformanceStreamMarkerINTEL(VkCommandBuffer commandBuffer,
                                                            const VkPerformanceStreamMarkerInfoINTEL* pMarkerInfo);
    static VkResult hk_vkCmdSetPerformanceOverrideINTEL(VkCommandBuffer commandBuffer,
                                                        const VkPerformanceOverrideInfoINTEL* pOverrideInfo);
    static void hk_vkCmdSetLineStippleEXT(VkCommandBuffer commandBuffer, uint32_t lineStippleFactor,
                                          uint16_t lineStipplePattern);
    static void hk_vkCmdSetCullModeEXT(VkCommandBuffer commandBuffer, VkCullModeFlags cullMode);
    static void hk_vkCmdSetFrontFaceEXT(VkCommandBuffer commandBuffer, VkFrontFace frontFace);
    static void hk_vkCmdSetPrimitiveTopologyEXT(VkCommandBuffer commandBuffer, VkPrimitiveTopology primitiveTopology);
    static void hk_vkCmdSetViewportWithCountEXT(VkCommandBuffer commandBuffer, uint32_t viewportCount,
                                                const VkViewport* pViewports);
    static void hk_vkCmdSetScissorWithCountEXT(VkCommandBuffer commandBuffer, uint32_t scissorCount,
                                               const VkRect2D* pScissors);
    static void hk_vkCmdBindVertexBuffers2EXT(VkCommandBuffer commandBuffer, uint32_t firstBinding,
                                              uint32_t bindingCount, const VkBuffer* pBuffers,
                                              const VkDeviceSize* pOffsets, const VkDeviceSize* pSizes,
                                              const VkDeviceSize* pStrides);
    static void hk_vkCmdSetDepthTestEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthTestEnable);
    static void hk_vkCmdSetDepthWriteEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthWriteEnable);
    static void hk_vkCmdSetDepthCompareOpEXT(VkCommandBuffer commandBuffer, VkCompareOp depthCompareOp);
    static void hk_vkCmdSetDepthBoundsTestEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthBoundsTestEnable);
    static void hk_vkCmdSetStencilTestEnableEXT(VkCommandBuffer commandBuffer, VkBool32 stencilTestEnable);
    static void hk_vkCmdSetStencilOpEXT(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, VkStencilOp failOp,
                                        VkStencilOp passOp, VkStencilOp depthFailOp, VkCompareOp compareOp);
    static void hk_vkCmdPreprocessGeneratedCommandsNV(VkCommandBuffer commandBuffer,
                                                      const VkGeneratedCommandsInfoNV* pGeneratedCommandsInfo);
    static void hk_vkCmdExecuteGeneratedCommandsNV(VkCommandBuffer commandBuffer, VkBool32 isPreprocessed,
                                                   const VkGeneratedCommandsInfoNV* pGeneratedCommandsInfo);
    static void hk_vkCmdBindPipelineShaderGroupNV(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                                                  VkPipeline pipeline, uint32_t groupIndex);
    static void hk_vkCmdSetDepthBias2EXT(VkCommandBuffer commandBuffer, const VkDepthBiasInfoEXT* pDepthBiasInfo);
    static void hk_vkCmdCudaLaunchKernelNV(VkCommandBuffer commandBuffer, const VkCudaLaunchInfoNV* pLaunchInfo);
    static void hk_vkCmdBindDescriptorBuffersEXT(VkCommandBuffer commandBuffer, uint32_t bufferCount,
                                                 const VkDescriptorBufferBindingInfoEXT* pBindingInfos);
    static void hk_vkCmdSetDescriptorBufferOffsetsEXT(VkCommandBuffer commandBuffer,
                                                      VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout,
                                                      uint32_t firstSet, uint32_t setCount,
                                                      const uint32_t* pBufferIndices, const VkDeviceSize* pOffsets);
    static void hk_vkCmdBindDescriptorBufferEmbeddedSamplersEXT(VkCommandBuffer commandBuffer,
                                                                VkPipelineBindPoint pipelineBindPoint,
                                                                VkPipelineLayout layout, uint32_t set);
    static void hk_vkCmdSetFragmentShadingRateEnumNV(VkCommandBuffer commandBuffer, VkFragmentShadingRateNV shadingRate,
                                                     const VkFragmentShadingRateCombinerOpKHR combinerOps[2]);
    static void hk_vkCmdSetVertexInputEXT(VkCommandBuffer commandBuffer, uint32_t vertexBindingDescriptionCount,
                                          const VkVertexInputBindingDescription2EXT* pVertexBindingDescriptions,
                                          uint32_t vertexAttributeDescriptionCount,
                                          const VkVertexInputAttributeDescription2EXT* pVertexAttributeDescriptions);
    static void hk_vkCmdSubpassShadingHUAWEI(VkCommandBuffer commandBuffer);
    static void hk_vkCmdBindInvocationMaskHUAWEI(VkCommandBuffer commandBuffer, VkImageView imageView,
                                                 VkImageLayout imageLayout);
    static void hk_vkCmdSetPatchControlPointsEXT(VkCommandBuffer commandBuffer, uint32_t patchControlPoints);
    static void hk_vkCmdSetRasterizerDiscardEnableEXT(VkCommandBuffer commandBuffer, VkBool32 rasterizerDiscardEnable);
    static void hk_vkCmdSetDepthBiasEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthBiasEnable);
    static void hk_vkCmdSetLogicOpEXT(VkCommandBuffer commandBuffer, VkLogicOp logicOp);
    static void hk_vkCmdSetPrimitiveRestartEnableEXT(VkCommandBuffer commandBuffer, VkBool32 primitiveRestartEnable);
    static void hk_vkCmdSetColorWriteEnableEXT(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                                               const VkBool32* pColorWriteEnables);
    static void hk_vkCmdDrawMultiEXT(VkCommandBuffer commandBuffer, uint32_t drawCount,
                                     const VkMultiDrawInfoEXT* pVertexInfo, uint32_t instanceCount,
                                     uint32_t firstInstance, uint32_t stride);
    static void hk_vkCmdDrawMultiIndexedEXT(VkCommandBuffer commandBuffer, uint32_t drawCount,
                                            const VkMultiDrawIndexedInfoEXT* pIndexInfo, uint32_t instanceCount,
                                            uint32_t firstInstance, uint32_t stride, const int32_t* pVertexOffset);
    static void hk_vkCmdBuildMicromapsEXT(VkCommandBuffer commandBuffer, uint32_t infoCount,
                                          const VkMicromapBuildInfoEXT* pInfos);
    static void hk_vkCmdCopyMicromapEXT(VkCommandBuffer commandBuffer, const VkCopyMicromapInfoEXT* pInfo);
    static void hk_vkCmdCopyMicromapToMemoryEXT(VkCommandBuffer commandBuffer,
                                                const VkCopyMicromapToMemoryInfoEXT* pInfo);
    static void hk_vkCmdCopyMemoryToMicromapEXT(VkCommandBuffer commandBuffer,
                                                const VkCopyMemoryToMicromapInfoEXT* pInfo);
    static void hk_vkCmdWriteMicromapsPropertiesEXT(VkCommandBuffer commandBuffer, uint32_t micromapCount,
                                                    const VkMicromapEXT* pMicromaps, VkQueryType queryType,
                                                    VkQueryPool queryPool, uint32_t firstQuery);
    static void hk_vkCmdDrawClusterHUAWEI(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY,
                                          uint32_t groupCountZ);
    static void hk_vkCmdDrawClusterIndirectHUAWEI(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset);
    static void hk_vkCmdCopyMemoryIndirectNV(VkCommandBuffer commandBuffer, VkDeviceAddress copyBufferAddress,
                                             uint32_t copyCount, uint32_t stride);
    static void hk_vkCmdCopyMemoryToImageIndirectNV(VkCommandBuffer commandBuffer, VkDeviceAddress copyBufferAddress,
                                                    uint32_t copyCount, uint32_t stride, VkImage dstImage,
                                                    VkImageLayout dstImageLayout,
                                                    const VkImageSubresourceLayers* pImageSubresources);
    static void hk_vkCmdDecompressMemoryNV(VkCommandBuffer commandBuffer, uint32_t decompressRegionCount,
                                           const VkDecompressMemoryRegionNV* pDecompressMemoryRegions);
    static void hk_vkCmdDecompressMemoryIndirectCountNV(VkCommandBuffer commandBuffer,
                                                        VkDeviceAddress indirectCommandsAddress,
                                                        VkDeviceAddress indirectCommandsCountAddress, uint32_t stride);
    static void hk_vkCmdUpdatePipelineIndirectBufferNV(VkCommandBuffer commandBuffer,
                                                       VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline);
    static void hk_vkCmdSetDepthClampEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthClampEnable);
    static void hk_vkCmdSetPolygonModeEXT(VkCommandBuffer commandBuffer, VkPolygonMode polygonMode);
    static void hk_vkCmdSetRasterizationSamplesEXT(VkCommandBuffer commandBuffer,
                                                   VkSampleCountFlagBits rasterizationSamples);
    static void hk_vkCmdSetSampleMaskEXT(VkCommandBuffer commandBuffer, VkSampleCountFlagBits samples,
                                         const VkSampleMask* pSampleMask);
    static void hk_vkCmdSetAlphaToCoverageEnableEXT(VkCommandBuffer commandBuffer, VkBool32 alphaToCoverageEnable);
    static void hk_vkCmdSetAlphaToOneEnableEXT(VkCommandBuffer commandBuffer, VkBool32 alphaToOneEnable);
    static void hk_vkCmdSetLogicOpEnableEXT(VkCommandBuffer commandBuffer, VkBool32 logicOpEnable);
    static void hk_vkCmdSetColorBlendEnableEXT(VkCommandBuffer commandBuffer, uint32_t firstAttachment,
                                               uint32_t attachmentCount, const VkBool32* pColorBlendEnables);
    static void hk_vkCmdSetColorBlendEquationEXT(VkCommandBuffer commandBuffer, uint32_t firstAttachment,
                                                 uint32_t attachmentCount,
                                                 const VkColorBlendEquationEXT* pColorBlendEquations);
    static void hk_vkCmdSetColorWriteMaskEXT(VkCommandBuffer commandBuffer, uint32_t firstAttachment,
                                             uint32_t attachmentCount, const VkColorComponentFlags* pColorWriteMasks);
    static void hk_vkCmdSetTessellationDomainOriginEXT(VkCommandBuffer commandBuffer,
                                                       VkTessellationDomainOrigin domainOrigin);
    static void hk_vkCmdSetRasterizationStreamEXT(VkCommandBuffer commandBuffer, uint32_t rasterizationStream);
    static void
    hk_vkCmdSetConservativeRasterizationModeEXT(VkCommandBuffer commandBuffer,
                                                VkConservativeRasterizationModeEXT conservativeRasterizationMode);
    static void hk_vkCmdSetExtraPrimitiveOverestimationSizeEXT(VkCommandBuffer commandBuffer,
                                                               float extraPrimitiveOverestimationSize);
    static void hk_vkCmdSetDepthClipEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthClipEnable);
    static void hk_vkCmdSetSampleLocationsEnableEXT(VkCommandBuffer commandBuffer, VkBool32 sampleLocationsEnable);
    static void hk_vkCmdSetColorBlendAdvancedEXT(VkCommandBuffer commandBuffer, uint32_t firstAttachment,
                                                 uint32_t attachmentCount,
                                                 const VkColorBlendAdvancedEXT* pColorBlendAdvanced);
    static void hk_vkCmdSetProvokingVertexModeEXT(VkCommandBuffer commandBuffer,
                                                  VkProvokingVertexModeEXT provokingVertexMode);
    static void hk_vkCmdSetLineRasterizationModeEXT(VkCommandBuffer commandBuffer,
                                                    VkLineRasterizationModeEXT lineRasterizationMode);
    static void hk_vkCmdSetLineStippleEnableEXT(VkCommandBuffer commandBuffer, VkBool32 stippledLineEnable);
    static void hk_vkCmdSetDepthClipNegativeOneToOneEXT(VkCommandBuffer commandBuffer, VkBool32 negativeOneToOne);
    static void hk_vkCmdSetViewportWScalingEnableNV(VkCommandBuffer commandBuffer, VkBool32 viewportWScalingEnable);
    static void hk_vkCmdSetViewportSwizzleNV(VkCommandBuffer commandBuffer, uint32_t firstViewport,
                                             uint32_t viewportCount, const VkViewportSwizzleNV* pViewportSwizzles);
    static void hk_vkCmdSetCoverageToColorEnableNV(VkCommandBuffer commandBuffer, VkBool32 coverageToColorEnable);
    static void hk_vkCmdSetCoverageToColorLocationNV(VkCommandBuffer commandBuffer, uint32_t coverageToColorLocation);
    static void hk_vkCmdSetCoverageModulationModeNV(VkCommandBuffer commandBuffer,
                                                    VkCoverageModulationModeNV coverageModulationMode);
    static void hk_vkCmdSetCoverageModulationTableEnableNV(VkCommandBuffer commandBuffer,
                                                           VkBool32 coverageModulationTableEnable);
    static void hk_vkCmdSetCoverageModulationTableNV(VkCommandBuffer commandBuffer,
                                                     uint32_t coverageModulationTableCount,
                                                     const float* pCoverageModulationTable);
    static void hk_vkCmdSetShadingRateImageEnableNV(VkCommandBuffer commandBuffer, VkBool32 shadingRateImageEnable);
    static void hk_vkCmdSetRepresentativeFragmentTestEnableNV(VkCommandBuffer commandBuffer,
                                                              VkBool32 representativeFragmentTestEnable);
    static void hk_vkCmdSetCoverageReductionModeNV(VkCommandBuffer commandBuffer,
                                                   VkCoverageReductionModeNV coverageReductionMode);
    static void hk_vkCmdOpticalFlowExecuteNV(VkCommandBuffer commandBuffer, VkOpticalFlowSessionNV session,
                                             const VkOpticalFlowExecuteInfoNV* pExecuteInfo);
    static void hk_vkCmdBindShadersEXT(VkCommandBuffer commandBuffer, uint32_t stageCount,
                                       const VkShaderStageFlagBits* pStages, const VkShaderEXT* pShaders);
    static void hk_vkCmdSetDepthClampRangeEXT(VkCommandBuffer commandBuffer, VkDepthClampModeEXT depthClampMode,
                                              const VkDepthClampRangeEXT* pDepthClampRange);
    static void hk_vkCmdConvertCooperativeVectorMatrixNV(VkCommandBuffer commandBuffer, uint32_t infoCount,
                                                         const VkConvertCooperativeVectorMatrixInfoNV* pInfos);
    static void hk_vkCmdSetAttachmentFeedbackLoopEnableEXT(VkCommandBuffer commandBuffer,
                                                           VkImageAspectFlags aspectMask);
    static void hk_vkCmdBuildClusterAccelerationStructureIndirectNV(
        VkCommandBuffer commandBuffer, const VkClusterAccelerationStructureCommandsInfoNV* pCommandInfos);
    static void
    hk_vkCmdBuildPartitionedAccelerationStructuresNV(VkCommandBuffer commandBuffer,
                                                     const VkBuildPartitionedAccelerationStructureInfoNV* pBuildInfo);
    static void hk_vkCmdPreprocessGeneratedCommandsEXT(VkCommandBuffer commandBuffer,
                                                       const VkGeneratedCommandsInfoEXT* pGeneratedCommandsInfo,
                                                       VkCommandBuffer stateCommandBuffer);
    static void hk_vkCmdExecuteGeneratedCommandsEXT(VkCommandBuffer commandBuffer, VkBool32 isPreprocessed,
                                                    const VkGeneratedCommandsInfoEXT* pGeneratedCommandsInfo);
    static void
    hk_vkCmdBuildAccelerationStructuresKHR(VkCommandBuffer commandBuffer, uint32_t infoCount,
                                           const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
                                           const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos);
    static void hk_vkCmdBuildAccelerationStructuresIndirectKHR(
        VkCommandBuffer commandBuffer, uint32_t infoCount, const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
        const VkDeviceAddress* pIndirectDeviceAddresses, const uint32_t* pIndirectStrides,
        const uint32_t* const* ppMaxPrimitiveCounts);
    static void hk_vkCmdCopyAccelerationStructureKHR(VkCommandBuffer commandBuffer,
                                                     const VkCopyAccelerationStructureInfoKHR* pInfo);
    static void hk_vkCmdCopyAccelerationStructureToMemoryKHR(VkCommandBuffer commandBuffer,
                                                             const VkCopyAccelerationStructureToMemoryInfoKHR* pInfo);
    static void hk_vkCmdCopyMemoryToAccelerationStructureKHR(VkCommandBuffer commandBuffer,
                                                             const VkCopyMemoryToAccelerationStructureInfoKHR* pInfo);
    static void
    hk_vkCmdWriteAccelerationStructuresPropertiesKHR(VkCommandBuffer commandBuffer, uint32_t accelerationStructureCount,
                                                     const VkAccelerationStructureKHR* pAccelerationStructures,
                                                     VkQueryType queryType, VkQueryPool queryPool, uint32_t firstQuery);
    static void hk_vkCmdTraceRaysKHR(VkCommandBuffer commandBuffer,
                                     const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable,
                                     const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable,
                                     const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable,
                                     const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable, uint32_t width,
                                     uint32_t height, uint32_t depth);
    static void hk_vkCmdTraceRaysIndirectKHR(VkCommandBuffer commandBuffer,
                                             const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable,
                                             const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable,
                                             const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable,
                                             const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable,
                                             VkDeviceAddress indirectDeviceAddress);
    static void hk_vkCmdSetRayTracingPipelineStackSizeKHR(VkCommandBuffer commandBuffer, uint32_t pipelineStackSize);
    static void hk_vkCmdDrawMeshTasksEXT(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY,
                                         uint32_t groupCountZ);
    static void hk_vkCmdDrawMeshTasksIndirectEXT(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                 uint32_t drawCount, uint32_t stride);
    static void hk_vkCmdDrawMeshTasksIndirectCountEXT(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                                      VkDeviceSize offset, VkBuffer countBuffer,
                                                      VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                                      uint32_t stride);

    VALIDATE_MEMBER_HOOK(hk_vkQueueSubmit, PFN_vkQueueSubmit)
    VALIDATE_MEMBER_HOOK(hk_vkQueueSubmit2, PFN_vkQueueSubmit2)
    VALIDATE_MEMBER_HOOK(hk_vkQueueSubmit2KHR, PFN_vkQueueSubmit2)
    VALIDATE_MEMBER_HOOK(hk_vkBeginCommandBuffer, PFN_vkBeginCommandBuffer)
    VALIDATE_MEMBER_HOOK(hk_vkEndCommandBuffer, PFN_vkEndCommandBuffer)
    VALIDATE_MEMBER_HOOK(hk_vkResetCommandBuffer, PFN_vkResetCommandBuffer)
    VALIDATE_MEMBER_HOOK(hk_vkCmdExecuteCommands, PFN_vkCmdExecuteCommands)
    VALIDATE_MEMBER_HOOK(hk_vkFreeCommandBuffers, PFN_vkFreeCommandBuffers)
    VALIDATE_MEMBER_HOOK(hk_vkResetCommandPool, PFN_vkResetCommandPool)
    VALIDATE_MEMBER_HOOK(hk_vkAllocateCommandBuffers, PFN_vkAllocateCommandBuffers)
    VALIDATE_MEMBER_HOOK(hk_vkDestroyCommandPool, PFN_vkDestroyCommandPool)
    VALIDATE_MEMBER_HOOK(hk_vkCreateCommandPool, PFN_vkCreateCommandPool)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBindPipeline, PFN_vkCmdBindPipeline)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetViewport, PFN_vkCmdSetViewport)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetScissor, PFN_vkCmdSetScissor)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetLineWidth, PFN_vkCmdSetLineWidth)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDepthBias, PFN_vkCmdSetDepthBias)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetBlendConstants, PFN_vkCmdSetBlendConstants)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDepthBounds, PFN_vkCmdSetDepthBounds)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetStencilCompareMask, PFN_vkCmdSetStencilCompareMask)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetStencilWriteMask, PFN_vkCmdSetStencilWriteMask)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetStencilReference, PFN_vkCmdSetStencilReference)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBindDescriptorSets, PFN_vkCmdBindDescriptorSets)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBindIndexBuffer, PFN_vkCmdBindIndexBuffer)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBindVertexBuffers, PFN_vkCmdBindVertexBuffers)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDraw, PFN_vkCmdDraw)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDrawIndexed, PFN_vkCmdDrawIndexed)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDrawIndirect, PFN_vkCmdDrawIndirect)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDrawIndexedIndirect, PFN_vkCmdDrawIndexedIndirect)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDispatch, PFN_vkCmdDispatch)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDispatchIndirect, PFN_vkCmdDispatchIndirect)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyBuffer, PFN_vkCmdCopyBuffer)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyImage, PFN_vkCmdCopyImage)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBlitImage, PFN_vkCmdBlitImage)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyBufferToImage, PFN_vkCmdCopyBufferToImage)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyImageToBuffer, PFN_vkCmdCopyImageToBuffer)
    VALIDATE_MEMBER_HOOK(hk_vkCmdUpdateBuffer, PFN_vkCmdUpdateBuffer)
    VALIDATE_MEMBER_HOOK(hk_vkCmdFillBuffer, PFN_vkCmdFillBuffer)
    VALIDATE_MEMBER_HOOK(hk_vkCmdClearColorImage, PFN_vkCmdClearColorImage)
    VALIDATE_MEMBER_HOOK(hk_vkCmdClearDepthStencilImage, PFN_vkCmdClearDepthStencilImage)
    VALIDATE_MEMBER_HOOK(hk_vkCmdClearAttachments, PFN_vkCmdClearAttachments)
    VALIDATE_MEMBER_HOOK(hk_vkCmdResolveImage, PFN_vkCmdResolveImage)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetEvent, PFN_vkCmdSetEvent)
    VALIDATE_MEMBER_HOOK(hk_vkCmdResetEvent, PFN_vkCmdResetEvent)
    VALIDATE_MEMBER_HOOK(hk_vkCmdWaitEvents, PFN_vkCmdWaitEvents)
    VALIDATE_MEMBER_HOOK(hk_vkCmdPipelineBarrier, PFN_vkCmdPipelineBarrier)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBeginQuery, PFN_vkCmdBeginQuery)
    VALIDATE_MEMBER_HOOK(hk_vkCmdEndQuery, PFN_vkCmdEndQuery)
    VALIDATE_MEMBER_HOOK(hk_vkCmdResetQueryPool, PFN_vkCmdResetQueryPool)
    VALIDATE_MEMBER_HOOK(hk_vkCmdWriteTimestamp, PFN_vkCmdWriteTimestamp)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyQueryPoolResults, PFN_vkCmdCopyQueryPoolResults)
    VALIDATE_MEMBER_HOOK(hk_vkCmdPushConstants, PFN_vkCmdPushConstants)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBeginRenderPass, PFN_vkCmdBeginRenderPass)
    VALIDATE_MEMBER_HOOK(hk_vkCmdNextSubpass, PFN_vkCmdNextSubpass)
    VALIDATE_MEMBER_HOOK(hk_vkCmdEndRenderPass, PFN_vkCmdEndRenderPass)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDeviceMask, PFN_vkCmdSetDeviceMask)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDispatchBase, PFN_vkCmdDispatchBase)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDrawIndirectCount, PFN_vkCmdDrawIndirectCount)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDrawIndexedIndirectCount, PFN_vkCmdDrawIndexedIndirectCount)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBeginRenderPass2, PFN_vkCmdBeginRenderPass2)
    VALIDATE_MEMBER_HOOK(hk_vkCmdNextSubpass2, PFN_vkCmdNextSubpass2)
    VALIDATE_MEMBER_HOOK(hk_vkCmdEndRenderPass2, PFN_vkCmdEndRenderPass2)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetEvent2, PFN_vkCmdSetEvent2)
    VALIDATE_MEMBER_HOOK(hk_vkCmdResetEvent2, PFN_vkCmdResetEvent2)
    VALIDATE_MEMBER_HOOK(hk_vkCmdWaitEvents2, PFN_vkCmdWaitEvents2)
    VALIDATE_MEMBER_HOOK(hk_vkCmdPipelineBarrier2, PFN_vkCmdPipelineBarrier2)
    VALIDATE_MEMBER_HOOK(hk_vkCmdWriteTimestamp2, PFN_vkCmdWriteTimestamp2)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyBuffer2, PFN_vkCmdCopyBuffer2)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyImage2, PFN_vkCmdCopyImage2)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyBufferToImage2, PFN_vkCmdCopyBufferToImage2)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyImageToBuffer2, PFN_vkCmdCopyImageToBuffer2)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBlitImage2, PFN_vkCmdBlitImage2)
    VALIDATE_MEMBER_HOOK(hk_vkCmdResolveImage2, PFN_vkCmdResolveImage2)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBeginRendering, PFN_vkCmdBeginRendering)
    VALIDATE_MEMBER_HOOK(hk_vkCmdEndRendering, PFN_vkCmdEndRendering)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetCullMode, PFN_vkCmdSetCullMode)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetFrontFace, PFN_vkCmdSetFrontFace)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetPrimitiveTopology, PFN_vkCmdSetPrimitiveTopology)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetViewportWithCount, PFN_vkCmdSetViewportWithCount)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetScissorWithCount, PFN_vkCmdSetScissorWithCount)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBindVertexBuffers2, PFN_vkCmdBindVertexBuffers2)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDepthTestEnable, PFN_vkCmdSetDepthTestEnable)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDepthWriteEnable, PFN_vkCmdSetDepthWriteEnable)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDepthCompareOp, PFN_vkCmdSetDepthCompareOp)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDepthBoundsTestEnable, PFN_vkCmdSetDepthBoundsTestEnable)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetStencilTestEnable, PFN_vkCmdSetStencilTestEnable)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetStencilOp, PFN_vkCmdSetStencilOp)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetRasterizerDiscardEnable, PFN_vkCmdSetRasterizerDiscardEnable)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDepthBiasEnable, PFN_vkCmdSetDepthBiasEnable)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetPrimitiveRestartEnable, PFN_vkCmdSetPrimitiveRestartEnable)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetLineStipple, PFN_vkCmdSetLineStipple)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBindIndexBuffer2, PFN_vkCmdBindIndexBuffer2)
    VALIDATE_MEMBER_HOOK(hk_vkCmdPushDescriptorSet, PFN_vkCmdPushDescriptorSet)
    VALIDATE_MEMBER_HOOK(hk_vkCmdPushDescriptorSetWithTemplate, PFN_vkCmdPushDescriptorSetWithTemplate)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetRenderingAttachmentLocations, PFN_vkCmdSetRenderingAttachmentLocations)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetRenderingInputAttachmentIndices, PFN_vkCmdSetRenderingInputAttachmentIndices)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBindDescriptorSets2, PFN_vkCmdBindDescriptorSets2)
    VALIDATE_MEMBER_HOOK(hk_vkCmdPushConstants2, PFN_vkCmdPushConstants2)
    VALIDATE_MEMBER_HOOK(hk_vkCmdPushDescriptorSet2, PFN_vkCmdPushDescriptorSet2)
    VALIDATE_MEMBER_HOOK(hk_vkCmdPushDescriptorSetWithTemplate2, PFN_vkCmdPushDescriptorSetWithTemplate2)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBeginVideoCodingKHR, PFN_vkCmdBeginVideoCodingKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdEndVideoCodingKHR, PFN_vkCmdEndVideoCodingKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdControlVideoCodingKHR, PFN_vkCmdControlVideoCodingKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDecodeVideoKHR, PFN_vkCmdDecodeVideoKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBeginRenderingKHR, PFN_vkCmdBeginRenderingKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdEndRenderingKHR, PFN_vkCmdEndRenderingKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDeviceMaskKHR, PFN_vkCmdSetDeviceMaskKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDispatchBaseKHR, PFN_vkCmdDispatchBaseKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdPushDescriptorSetKHR, PFN_vkCmdPushDescriptorSetKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdPushDescriptorSetWithTemplateKHR, PFN_vkCmdPushDescriptorSetWithTemplateKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBeginRenderPass2KHR, PFN_vkCmdBeginRenderPass2KHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdNextSubpass2KHR, PFN_vkCmdNextSubpass2KHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdEndRenderPass2KHR, PFN_vkCmdEndRenderPass2KHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDrawIndirectCountKHR, PFN_vkCmdDrawIndirectCountKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDrawIndexedIndirectCountKHR, PFN_vkCmdDrawIndexedIndirectCountKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetFragmentShadingRateKHR, PFN_vkCmdSetFragmentShadingRateKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetRenderingAttachmentLocationsKHR, PFN_vkCmdSetRenderingAttachmentLocationsKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetRenderingInputAttachmentIndicesKHR, PFN_vkCmdSetRenderingInputAttachmentIndicesKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdEncodeVideoKHR, PFN_vkCmdEncodeVideoKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetEvent2KHR, PFN_vkCmdSetEvent2KHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdResetEvent2KHR, PFN_vkCmdResetEvent2KHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdWaitEvents2KHR, PFN_vkCmdWaitEvents2KHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdPipelineBarrier2KHR, PFN_vkCmdPipelineBarrier2KHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdWriteTimestamp2KHR, PFN_vkCmdWriteTimestamp2KHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyBuffer2KHR, PFN_vkCmdCopyBuffer2KHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyImage2KHR, PFN_vkCmdCopyImage2KHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyBufferToImage2KHR, PFN_vkCmdCopyBufferToImage2KHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyImageToBuffer2KHR, PFN_vkCmdCopyImageToBuffer2KHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBlitImage2KHR, PFN_vkCmdBlitImage2KHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdResolveImage2KHR, PFN_vkCmdResolveImage2KHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdTraceRaysIndirect2KHR, PFN_vkCmdTraceRaysIndirect2KHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBindIndexBuffer2KHR, PFN_vkCmdBindIndexBuffer2KHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetLineStippleKHR, PFN_vkCmdSetLineStippleKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBindDescriptorSets2KHR, PFN_vkCmdBindDescriptorSets2KHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdPushConstants2KHR, PFN_vkCmdPushConstants2KHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdPushDescriptorSet2KHR, PFN_vkCmdPushDescriptorSet2KHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdPushDescriptorSetWithTemplate2KHR, PFN_vkCmdPushDescriptorSetWithTemplate2KHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDescriptorBufferOffsets2EXT, PFN_vkCmdSetDescriptorBufferOffsets2EXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT,
                         PFN_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDebugMarkerBeginEXT, PFN_vkCmdDebugMarkerBeginEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDebugMarkerEndEXT, PFN_vkCmdDebugMarkerEndEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDebugMarkerInsertEXT, PFN_vkCmdDebugMarkerInsertEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBindTransformFeedbackBuffersEXT, PFN_vkCmdBindTransformFeedbackBuffersEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBeginTransformFeedbackEXT, PFN_vkCmdBeginTransformFeedbackEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdEndTransformFeedbackEXT, PFN_vkCmdEndTransformFeedbackEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBeginQueryIndexedEXT, PFN_vkCmdBeginQueryIndexedEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdEndQueryIndexedEXT, PFN_vkCmdEndQueryIndexedEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDrawIndirectByteCountEXT, PFN_vkCmdDrawIndirectByteCountEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCuLaunchKernelNVX, PFN_vkCmdCuLaunchKernelNVX)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDrawIndirectCountAMD, PFN_vkCmdDrawIndirectCountAMD)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDrawIndexedIndirectCountAMD, PFN_vkCmdDrawIndexedIndirectCountAMD)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBeginConditionalRenderingEXT, PFN_vkCmdBeginConditionalRenderingEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdEndConditionalRenderingEXT, PFN_vkCmdEndConditionalRenderingEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetViewportWScalingNV, PFN_vkCmdSetViewportWScalingNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDiscardRectangleEXT, PFN_vkCmdSetDiscardRectangleEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDiscardRectangleEnableEXT, PFN_vkCmdSetDiscardRectangleEnableEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDiscardRectangleModeEXT, PFN_vkCmdSetDiscardRectangleModeEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBeginDebugUtilsLabelEXT, PFN_vkCmdBeginDebugUtilsLabelEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdEndDebugUtilsLabelEXT, PFN_vkCmdEndDebugUtilsLabelEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdInsertDebugUtilsLabelEXT, PFN_vkCmdInsertDebugUtilsLabelEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetSampleLocationsEXT, PFN_vkCmdSetSampleLocationsEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBindShadingRateImageNV, PFN_vkCmdBindShadingRateImageNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetViewportShadingRatePaletteNV, PFN_vkCmdSetViewportShadingRatePaletteNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetCoarseSampleOrderNV, PFN_vkCmdSetCoarseSampleOrderNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBuildAccelerationStructureNV, PFN_vkCmdBuildAccelerationStructureNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyAccelerationStructureNV, PFN_vkCmdCopyAccelerationStructureNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdTraceRaysNV, PFN_vkCmdTraceRaysNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdWriteAccelerationStructuresPropertiesNV,
                         PFN_vkCmdWriteAccelerationStructuresPropertiesNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdWriteBufferMarkerAMD, PFN_vkCmdWriteBufferMarkerAMD)
    VALIDATE_MEMBER_HOOK(hk_vkCmdWriteBufferMarker2AMD, PFN_vkCmdWriteBufferMarker2AMD)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDrawMeshTasksNV, PFN_vkCmdDrawMeshTasksNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDrawMeshTasksIndirectNV, PFN_vkCmdDrawMeshTasksIndirectNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDrawMeshTasksIndirectCountNV, PFN_vkCmdDrawMeshTasksIndirectCountNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetExclusiveScissorEnableNV, PFN_vkCmdSetExclusiveScissorEnableNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetExclusiveScissorNV, PFN_vkCmdSetExclusiveScissorNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetCheckpointNV, PFN_vkCmdSetCheckpointNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetPerformanceMarkerINTEL, PFN_vkCmdSetPerformanceMarkerINTEL)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetPerformanceStreamMarkerINTEL, PFN_vkCmdSetPerformanceStreamMarkerINTEL)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetPerformanceOverrideINTEL, PFN_vkCmdSetPerformanceOverrideINTEL)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetLineStippleEXT, PFN_vkCmdSetLineStippleEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetCullModeEXT, PFN_vkCmdSetCullModeEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetFrontFaceEXT, PFN_vkCmdSetFrontFaceEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetPrimitiveTopologyEXT, PFN_vkCmdSetPrimitiveTopologyEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetViewportWithCountEXT, PFN_vkCmdSetViewportWithCountEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetScissorWithCountEXT, PFN_vkCmdSetScissorWithCountEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBindVertexBuffers2EXT, PFN_vkCmdBindVertexBuffers2EXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDepthTestEnableEXT, PFN_vkCmdSetDepthTestEnableEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDepthWriteEnableEXT, PFN_vkCmdSetDepthWriteEnableEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDepthCompareOpEXT, PFN_vkCmdSetDepthCompareOpEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDepthBoundsTestEnableEXT, PFN_vkCmdSetDepthBoundsTestEnableEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetStencilTestEnableEXT, PFN_vkCmdSetStencilTestEnableEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetStencilOpEXT, PFN_vkCmdSetStencilOpEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdPreprocessGeneratedCommandsNV, PFN_vkCmdPreprocessGeneratedCommandsNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdExecuteGeneratedCommandsNV, PFN_vkCmdExecuteGeneratedCommandsNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBindPipelineShaderGroupNV, PFN_vkCmdBindPipelineShaderGroupNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDepthBias2EXT, PFN_vkCmdSetDepthBias2EXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCudaLaunchKernelNV, PFN_vkCmdCudaLaunchKernelNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBindDescriptorBuffersEXT, PFN_vkCmdBindDescriptorBuffersEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDescriptorBufferOffsetsEXT, PFN_vkCmdSetDescriptorBufferOffsetsEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBindDescriptorBufferEmbeddedSamplersEXT,
                         PFN_vkCmdBindDescriptorBufferEmbeddedSamplersEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetFragmentShadingRateEnumNV, PFN_vkCmdSetFragmentShadingRateEnumNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetVertexInputEXT, PFN_vkCmdSetVertexInputEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSubpassShadingHUAWEI, PFN_vkCmdSubpassShadingHUAWEI)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBindInvocationMaskHUAWEI, PFN_vkCmdBindInvocationMaskHUAWEI)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetPatchControlPointsEXT, PFN_vkCmdSetPatchControlPointsEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetRasterizerDiscardEnableEXT, PFN_vkCmdSetRasterizerDiscardEnableEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDepthBiasEnableEXT, PFN_vkCmdSetDepthBiasEnableEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetLogicOpEXT, PFN_vkCmdSetLogicOpEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetPrimitiveRestartEnableEXT, PFN_vkCmdSetPrimitiveRestartEnableEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetColorWriteEnableEXT, PFN_vkCmdSetColorWriteEnableEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDrawMultiEXT, PFN_vkCmdDrawMultiEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDrawMultiIndexedEXT, PFN_vkCmdDrawMultiIndexedEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBuildMicromapsEXT, PFN_vkCmdBuildMicromapsEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyMicromapEXT, PFN_vkCmdCopyMicromapEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyMicromapToMemoryEXT, PFN_vkCmdCopyMicromapToMemoryEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyMemoryToMicromapEXT, PFN_vkCmdCopyMemoryToMicromapEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdWriteMicromapsPropertiesEXT, PFN_vkCmdWriteMicromapsPropertiesEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDrawClusterHUAWEI, PFN_vkCmdDrawClusterHUAWEI)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDrawClusterIndirectHUAWEI, PFN_vkCmdDrawClusterIndirectHUAWEI)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyMemoryIndirectNV, PFN_vkCmdCopyMemoryIndirectNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyMemoryToImageIndirectNV, PFN_vkCmdCopyMemoryToImageIndirectNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDecompressMemoryNV, PFN_vkCmdDecompressMemoryNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDecompressMemoryIndirectCountNV, PFN_vkCmdDecompressMemoryIndirectCountNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdUpdatePipelineIndirectBufferNV, PFN_vkCmdUpdatePipelineIndirectBufferNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDepthClampEnableEXT, PFN_vkCmdSetDepthClampEnableEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetPolygonModeEXT, PFN_vkCmdSetPolygonModeEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetRasterizationSamplesEXT, PFN_vkCmdSetRasterizationSamplesEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetSampleMaskEXT, PFN_vkCmdSetSampleMaskEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetAlphaToCoverageEnableEXT, PFN_vkCmdSetAlphaToCoverageEnableEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetAlphaToOneEnableEXT, PFN_vkCmdSetAlphaToOneEnableEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetLogicOpEnableEXT, PFN_vkCmdSetLogicOpEnableEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetColorBlendEnableEXT, PFN_vkCmdSetColorBlendEnableEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetColorBlendEquationEXT, PFN_vkCmdSetColorBlendEquationEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetColorWriteMaskEXT, PFN_vkCmdSetColorWriteMaskEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetTessellationDomainOriginEXT, PFN_vkCmdSetTessellationDomainOriginEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetRasterizationStreamEXT, PFN_vkCmdSetRasterizationStreamEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetConservativeRasterizationModeEXT, PFN_vkCmdSetConservativeRasterizationModeEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetExtraPrimitiveOverestimationSizeEXT,
                         PFN_vkCmdSetExtraPrimitiveOverestimationSizeEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDepthClipEnableEXT, PFN_vkCmdSetDepthClipEnableEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetSampleLocationsEnableEXT, PFN_vkCmdSetSampleLocationsEnableEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetColorBlendAdvancedEXT, PFN_vkCmdSetColorBlendAdvancedEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetProvokingVertexModeEXT, PFN_vkCmdSetProvokingVertexModeEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetLineRasterizationModeEXT, PFN_vkCmdSetLineRasterizationModeEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetLineStippleEnableEXT, PFN_vkCmdSetLineStippleEnableEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDepthClipNegativeOneToOneEXT, PFN_vkCmdSetDepthClipNegativeOneToOneEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetViewportWScalingEnableNV, PFN_vkCmdSetViewportWScalingEnableNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetViewportSwizzleNV, PFN_vkCmdSetViewportSwizzleNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetCoverageToColorEnableNV, PFN_vkCmdSetCoverageToColorEnableNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetCoverageToColorLocationNV, PFN_vkCmdSetCoverageToColorLocationNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetCoverageModulationModeNV, PFN_vkCmdSetCoverageModulationModeNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetCoverageModulationTableEnableNV, PFN_vkCmdSetCoverageModulationTableEnableNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetCoverageModulationTableNV, PFN_vkCmdSetCoverageModulationTableNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetShadingRateImageEnableNV, PFN_vkCmdSetShadingRateImageEnableNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetRepresentativeFragmentTestEnableNV, PFN_vkCmdSetRepresentativeFragmentTestEnableNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetCoverageReductionModeNV, PFN_vkCmdSetCoverageReductionModeNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdOpticalFlowExecuteNV, PFN_vkCmdOpticalFlowExecuteNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBindShadersEXT, PFN_vkCmdBindShadersEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetDepthClampRangeEXT, PFN_vkCmdSetDepthClampRangeEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdConvertCooperativeVectorMatrixNV, PFN_vkCmdConvertCooperativeVectorMatrixNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetAttachmentFeedbackLoopEnableEXT, PFN_vkCmdSetAttachmentFeedbackLoopEnableEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBuildClusterAccelerationStructureIndirectNV,
                         PFN_vkCmdBuildClusterAccelerationStructureIndirectNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBuildPartitionedAccelerationStructuresNV,
                         PFN_vkCmdBuildPartitionedAccelerationStructuresNV)
    VALIDATE_MEMBER_HOOK(hk_vkCmdPreprocessGeneratedCommandsEXT, PFN_vkCmdPreprocessGeneratedCommandsEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdExecuteGeneratedCommandsEXT, PFN_vkCmdExecuteGeneratedCommandsEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBuildAccelerationStructuresKHR, PFN_vkCmdBuildAccelerationStructuresKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdBuildAccelerationStructuresIndirectKHR,
                         PFN_vkCmdBuildAccelerationStructuresIndirectKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyAccelerationStructureKHR, PFN_vkCmdCopyAccelerationStructureKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyAccelerationStructureToMemoryKHR, PFN_vkCmdCopyAccelerationStructureToMemoryKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdCopyMemoryToAccelerationStructureKHR, PFN_vkCmdCopyMemoryToAccelerationStructureKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdWriteAccelerationStructuresPropertiesKHR,
                         PFN_vkCmdWriteAccelerationStructuresPropertiesKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdTraceRaysKHR, PFN_vkCmdTraceRaysKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdTraceRaysIndirectKHR, PFN_vkCmdTraceRaysIndirectKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdSetRayTracingPipelineStackSizeKHR, PFN_vkCmdSetRayTracingPipelineStackSizeKHR)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDrawMeshTasksEXT, PFN_vkCmdDrawMeshTasksEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDrawMeshTasksIndirectEXT, PFN_vkCmdDrawMeshTasksIndirectEXT)
    VALIDATE_MEMBER_HOOK(hk_vkCmdDrawMeshTasksIndirectCountEXT, PFN_vkCmdDrawMeshTasksIndirectCountEXT)

#pragma endregion

  public:
    inline static vk_state::CommandBufferStateTracker cmdBufferStateTracker;

    // Command-buffer redirection and queue-submit transactions are keyed by command buffer and synchronized.
    // This allows recording/end/submit to occur on different application threads without global single-slot state.

    static void Hook(HMODULE vulkanModule);
    static void Unhook();
    static PFN_vkVoidFunction GetDeviceProcAddr(const PFN_vkVoidFunction original, const char* pName);
    static PFN_vkVoidFunction GetInstanceProcAddr(const PFN_vkVoidFunction original, const char* pName);
    static void EndCmdBuffer(VkCommandBuffer commandBuffer);
};
