#pragma once

#include "DlssNrFeature_Vk.h"
#include "DlssNrFeature_Dx12.h"
#include <shaders/dlssnr/DlssNr_Guides.h>
#include "PassProfiles.h"
#include <nvsdk_ngx_vk.h>
#include <shaders/output_scaling/OS_Vk.h>
#include <mutex>

namespace DlssNr
{

// Each layer has its own driver-created feature and capability parameter map.
// Sharing either would couple temporal histories when two passes use the same profile.
struct NgxPassVk
{
    NVSDK_NGX_Handle* feature = nullptr;
    NVSDK_NGX_Parameter* parameters = nullptr;
};

// One image this pass owns: the storage, the view, and the NGX wrapper that describes it. Kept
// together because they are created, resized and destroyed as one thing.
struct OwnedImage
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    NVSDK_NGX_Resource_VK ngx {};
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;

    bool Valid() const { return image != VK_NULL_HANDLE && view != VK_NULL_HANDLE; }
};

struct VkState
{
    bool failed = false;
    const char* reason = "";

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    bool ngxInitialised = false;
    NgxPassVk models[DlssNr::MaxPassCount] {};
    Profiles::NrPassTuning builtTuning[DlssNr::MaxPassCount] {};
    unsigned int builtPreset[DlssNr::MaxPassCount] {};
    unsigned int builtStyle[DlssNr::MaxPassCount] {};
    unsigned int activePasses = 0;
    VkEvent creationReady = VK_NULL_HANDLE;
    bool creationPending = false;

    // What the model writes, the proxy it is shown, and the frame as the upscaler left it.
    OwnedImage output;
    OwnedImage scratch;
    OwnedImage passClamp;
    OwnedImage proxy;
    OwnedImage keep;
    bool beforeSr = false;
    bool rayReconstruction = false;

    // The proxy at the model's working size, when that is below the frame. The model -- 98% of the
    // cost -- then runs on this instead of the full proxy, which is the whole point of the working
    // scale slider. Unused (and never created) at scale 1, so the default path is unchanged.
    OwnedImage proxySmall;

    // Supersampling (working scale > 1): the model runs above native, superUp enlarges the proxy to
    // that size and superDown averages the answer (output) back into outputNative at native for a 1:1
    // composite. nrScaler is the filter both were built with, so a changed DlssNrScalingDownscaler
    // rebuilds them. Unused and never created at scale <= 1.
    OwnedImage outputNative;
    std::unique_ptr<OS_Vk> superUp;
    std::unique_ptr<OS_Vk> superDown;
    Scaler nrScaler = Scaler::Count;

    DlssNr_Vk* pass = nullptr;

    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t workWidth = 0;
    uint32_t workHeight = 0;
    bool reset = true;
    unsigned long long frames = 0;

    // Timing. A pair of timestamps per frame across a ring, read back three frames later: a query
    // read the frame it was written stalls the CPU on the GPU, which would cost more than the pass
    // it is measuring. Vulkan reports ticks, and timestampPeriod is how many nanoseconds a tick is.
    VkQueryPool queryPool = VK_NULL_HANDLE;
    float timestampPeriod = 0.0f;
    unsigned long long timedFrames = 0;
    std::optional<double> lastGpuTime;

    // Whether the game hands over an exposure texture, and what it said when it did.
    bool exposureOffered = false;

    // The game's own exposure, read off its 1x1 texture, and the scale it multiplied its buffer by.
    //
    // gameExposure holds its last good value rather than resetting when a frame arrives without a
    // texture: GTA V dropped it three times in one session on the D3D12 path, and falling back to a
    // default on those frames is a flicker, not a fallback.
    float gameExposure = 0.0f;
    float gamePreExposure = 1.0f;

    // The exposure's courier: an 8x8 R32_FLOAT image the meter writes, and a ring of host-visible
    // buffers it is copied into. Only texel (0,0) is ever read -- the rest of the grid belongs to the
    // frame-statistics meter that was removed from the shared shader, and 8x8 is here only so that a
    // single 8x8 thread group lands entirely inside the image.
    OwnedImage meter;
    VkBuffer meterReadback[4] = {};
    VkDeviceMemory meterReadbackMemory[4] = {};
    void* meterMapped[4] = {};
    unsigned long long meterFrames = 0;
};

// The grid the meter writes, and the size of one readback. 8 * 8 * sizeof(float).
constexpr uint32_t kMeterSide = 8;
constexpr VkDeviceSize kMeterBytes = kMeterSide * kMeterSide * sizeof(float);

// Four, so the slot being read is four frames behind the slot being written and the read never waits
// on the GPU. Same depth as the D3D12 meter's ring, for the same reason.
constexpr unsigned long long kMeterSlots = 4;

// Four frames of pairs. Three would do, four keeps the modulo cheap and the slot being written well
// clear of the slot being read.
constexpr uint32_t kTimingSlots = 4;

struct ModelVk::Impl
{
    VkState state;
    bool reported = false;
    bool warnedVkSuper = false;
    bool saidEncoding = false;
    float loggedExposure = -1.0f;
    bool saidExposure = false;
    bool warnedDeferred = false;
    std::mutex mutex;
    uint64_t retryGeneration = ReadControlRequests().retryGeneration;
    Impl(DlssNr_Vk& shader) { state.pass = &shader; }
    ~Impl()
    {
        Shutdown();
        ClearStatus(this);
    }

    void Fail(const char* why);
    void DestroyImage(OwnedImage& img);
    uint32_t FindMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties);
    static VkImageInfo ImageInfoOf(const OwnedImage& img);
    bool CreateImage(OwnedImage& img, uint32_t width, uint32_t height, VkFormat format, bool readWrite);
    bool CreateMeterReadback();
    void DestroyMeterReadback();
    void Transition(VkCommandBuffer cmd, OwnedImage& img, VkImageLayout to);
    void TransitionForeign(VkCommandBuffer cmd, VkImage image, VkImageSubresourceRange range, VkImageLayout from,
                           VkImageLayout to);
    bool InitDriver(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device);
    void ReleaseModels();
    static void SetTuning(NVSDK_NGX_Parameter* parameters, const Profiles::NrPassTuning& tuning,
                          unsigned int style);
    bool CreateModel(VkCommandBuffer commandBuffer, unsigned int passIndex, unsigned int width,
                     unsigned int height, const Config& config);
    NVSDK_NGX_Result EvaluateModel(VkCommandBuffer commandBuffer, unsigned int passIndex,
                                  NVSDK_NGX_Resource_VK* colour, NVSDK_NGX_Resource_VK* depth,
                                  NVSDK_NGX_Resource_VK* motion, NVSDK_NGX_Resource_VK* output,
                                  unsigned int width, unsigned int height, const GuideRegions& guides,
                                  bool depthInverted, float mvX, float mvY, const Config& config);
    bool FormatCanHoldLinearHdr(VkFormat format);
    bool PrepareModels(VkCommandBuffer cmdBuffer, const DlssNrFrameInfo_Vk& frame, uint32_t width, uint32_t height, uint32_t workWidth, uint32_t workHeight, float workScale, unsigned int passes);
    bool Evaluate(VkCommandBuffer cmdBuffer, const VkImageInfo& colourInfo, const VkImageInfo& depthInfo,
                  const VkImageInfo& motionInfo, const VkImageInfo& target, const DlssNrFrameInfo_Vk& frame,
                  VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkImageLayout inputLayout);
    void Shutdown();
};
} // namespace DlssNr
