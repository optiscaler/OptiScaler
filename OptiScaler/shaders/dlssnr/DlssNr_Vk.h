#pragma once

// The composition pass on Vulkan.
//
// Same shader as the D3D12 pass, compiled to SPIR-V from the same source: the modes, the white point,
// the proxy encode and the transfer back are all shared, and any behavioural difference between the
// two APIs would be a bug rather than a design. What differs is only how a compute dispatch is
// expressed.
//
// Three things are worth knowing before reading the implementation.
//
// Every binding is written every dispatch. The shader declares all seven resources at file scope and
// branches on gMode, so all of them are statically reachable from the entry point and Vulkan requires
// a valid descriptor for each one whether a given mode reads it or not. Slots a mode has no use for
// get a 1x1 dummy rather than a null handle.
//
// Constants are slotted rather than overwritten. A single mapped uniform buffer would be wrong here:
// encode and resolve run in the same frame with different constants, and the second write would land
// before the first dispatch had read it. The buffer holds a ring of slots and each dispatch takes the
// next, at an offset the device's own alignment rule allows.
//
// Layouts are the caller's to declare and this pass's to respect. It never guesses what state an
// image arrived in.

#include "SysUtils.h"
#include <shaders/Shader_Vk.h>
#include "DlssNr_Common.h"
#include <memory>

namespace DlssNr
{
class ModelVk;
class FinishedVk;
}

// NGX's Vulkan guide wrappers also state whether the image supports storage access.
// Keep that metadata alongside the shared frame properties when rebuilding explicit resources.
struct DlssNrFrameInfo_Vk : DlssNrFrameInfo
{
    bool DepthReadWrite = false;
    bool MotionReadWrite = false;
    unsigned int ColorSubrectBaseX = 0;
    unsigned int ColorSubrectBaseY = 0;
};

class DlssNr_Vk : public Shader_Vk
{
    // Enough slots for several dispatches per frame across the frames that can be in flight. Encode
    // and resolve are two; the debug views and the exposure fetch are the others.
    static constexpr uint32_t kSlotsPerFrame = 12;
    static constexpr uint32_t kFramesInFlight = 4;
    static constexpr uint32_t kSlots = kSlotsPerFrame * kFramesInFlight;

    std::unique_ptr<DlssNr::ModelVk> _model;
    std::unique_ptr<DlssNr::FinishedVk> _finished;
    VkPipeline _finishedPipeline = VK_NULL_HANDLE;
    VkImageLayout _intermediateLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkDeviceSize _slotStride = 0;   // sizeof(DlssNrConstants), rounded up to the device's alignment
    uint32_t _slot = 0;             // next slot to hand out, wrapping

    // Stands in for a resource a given mode does not read. One pixel, never sampled for its content,
    // present only because Vulkan will not accept an unwritten binding.
    VkImage _dummyImage = VK_NULL_HANDLE;
    VkDeviceMemory _dummyMemory = VK_NULL_HANDLE;
    VkImageView _dummyView = VK_NULL_HANDLE;
    bool _dummyReady = false;

    bool CreateDummy(VkCommandBuffer cmdList);

    void WriteDescriptors(VkDescriptorSet set, VkDeviceSize constantOffset, VkImageView source, VkImageView model,
                          VkImageView original, VkImageView motion, VkImageView target, VkImageView keep,
                          VkImageLayout sourceLayout, VkImageLayout motionLayout);

  public:
    DlssNr_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice);
    ~DlssNr_Vk();

    VkImageInfo PrepareInput(VkCommandBuffer cmd, const VkImageInfo& nextOutput);
    void SetImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout before, VkImageLayout after,
                        VkImageSubresourceRange range)
    {
        Shader_Vk::SetImageLayout(cmd, image, before, after, range);
    }
    bool Dispatch(VkCommandBuffer cmd, const VkImageInfo& colour, const VkImageInfo& depth, const VkImageInfo& motion,
                  const VkImageInfo& output, const DlssNrFrameInfo_Vk& frame, VkInstance instance,
                  VkImageLayout inputLayout = VK_IMAGE_LAYOUT_GENERAL, bool* modelRan = nullptr);
    void CaptureFinished(VkCommandBuffer cmd, const VkImageInfo& depth, const VkImageInfo& motion,
                         const DlssNrFrameInfo_Vk& frame, VkInstance instance);

    // One dispatch of the composition shader.
    //
    // Any of the four read views may be VK_NULL_HANDLE, in which case the dummy is bound; the two
    // written views may not, because a mode that writes nothing has no reason to run. This records
    // the dispatch and the barrier that follows it, not the transitions that got them there.
    //
    // The source and motion slots are the two that ever carry an image this pass does not own -- the
    // frame the upscaler wrote, and the game's exposure -- and a descriptor has to name the layout
    // its image will be in when the shader runs. Those two are therefore the caller's to state.
    // Everything else is ours and is in the layout this pass put it in.
    //
    // The default is what a resource read by a compute shader is normally in, and is what every slot
    // holding one of our own images uses.
    bool Dispatch(VkCommandBuffer InCmdList, const DlssNrConstants& InConstants, uint32_t InThreadsX,
                  uint32_t InThreadsY, VkImageView InSource, VkImageView InModel, VkImageView InOriginal,
                  VkImageView InMotion, VkImageView InTarget, VkImageView InKeep,
                  VkImageLayout InSourceLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VkImageLayout InMotionLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, bool finishedColor = false,
                  // Initialize to UINT32_MAX; identical bindings/constants within one model chain only.
                  uint32_t* immutableSlot = nullptr);
};
