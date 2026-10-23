#pragma once
// API of an XeFG-on-Vulkan engine, for OptiScaler's XeFG Vulkan frame generation output (XeFG_Vk).
//
// The engine runs libxess_fg and libxell on the game's VkDevice and presents the real and generated frames through a
// VkSwapchainKHR of its own. OptiScaler hooks the game's device and swap chain calls, provides the depth and motion
// vectors, and drives this API.
//
// Plain C, Vulkan types only. Every call is from OptiScaler's threads; the engine locks internally. Versioned:
// OptiScaler asks for MFG_XEFG_VK_API_VERSION and checks `size`.
#include <windows.h>
#include <vulkan/vulkan.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MFG_XEFG_VK_API_VERSION 1u
#define MFG_XEFG_VK_GET_API_NAME "MfgXeFGVkGetApi"

// Returns the engine's unhooked implementation of a device function (OptiScaler's originals, past its own hooks),
// or NULL for the loader's. The engine resolves the swap chain and present functions it calls through this, so its
// own presents never re-enter OptiScaler's hooks. For "xell..." names it returns the real XeLL's function when
// OptiScaler sends libxell's exports to its XeLL input (the engine's XeLL context is XeFG's, never the game's); NULL
// for a redirected one fails the engine closed.
typedef PFN_vkVoidFunction (VKAPI_PTR* PFN_MfgXeFGVkOriginalProc)(void* user, const char* name);

typedef struct MfgXeFGVkDeviceInfo
{
    uint32_t size;                              // sizeof(MfgXeFGVkDeviceInfo)
    VkInstance instance;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr;   // the loader's entry
    uint32_t instanceApiVersion;                // the API version the instance was created with (1.3 or later)
    VkPhysicalDevice physicalDevice;
} MfgXeFGVkDeviceInfo;

// One input image, read as a sampled image in `layout` (SHADER_READ_ONLY_OPTIMAL or GENERAL).
typedef struct MfgXeFGVkImage
{
    VkImage image;
    VkImageView view;                           // a view the engine samples (depth aspect for depth formats)
    VkFormat format;
    VkImageLayout layout;
    uint32_t width, height;                     // the used area
} MfgXeFGVkImage;

typedef struct MfgXeFGVkFrame
{
    uint32_t size;                              // sizeof(MfgXeFGVkFrame)
    MfgXeFGVkImage depth;                       // render size
    MfgXeFGVkImage motion;                      // render or display size (resampled to render size by the engine)
    float jitterX, jitterY;                     // pixels
    float motionScaleX, motionScaleY;           // to pixels of the motion vector image
    float cameraNear, cameraFar, cameraVFov;    // 0 when unknown (the engine uses a generic projection)
    float frameTimeMs;
    uint32_t reset;                             // 1: history invalid (camera cut)
} MfgXeFGVkFrame;

enum MfgXeFGVkInitFlags
{
    MFG_XEFG_VK_INIT_INVERTED_DEPTH = 1u << 0,
};

typedef struct MfgXeFGVkStatus
{
    uint32_t size;                              // sizeof(MfgXeFGVkStatus)
    uint32_t active;                            // a swap chain presents through XeFG
    uint32_t framesPerBaseFrame;                // presented frames per rendered frame (interpolated + 1)
    uint64_t basePresents, vulkanPresents;
    uint64_t missingPrograms;
    char reason[64];                            // the engine's last state or refusal ("active", "programs_missing", ...)
    // Filled when `size` covers them: the low-latency output the last frame slept in
    // (MfgXeFGVkLatencyMode), and whether that output applied the FPS limit (the host's own limiter then stays out).
    uint32_t latencyMode;
    uint32_t latencyLimited;
} MfgXeFGVkStatus;

// Who sleeps for the engine's swap chains. XeFG keeps XeLL's low-latency mode and markers in every mode
// (it generates no frames without them); only the sleep, the FPS limit and the driver's latency markers move.
enum MfgXeFGVkLatencyMode
{
    MFG_XEFG_VK_LATENCY_XELL = 0,               // XeLL at the game's acquisition (the default, and every fallback)
    MFG_XEFG_VK_LATENCY_REFLEX = 1,             // Reflex (VK_NV_low_latency2) on the engine's VkSwapchainKHR
    MFG_XEFG_VK_LATENCY_ANTILAG2 = 2,           // Anti-Lag 2 (VK_AMD_anti_lag) on the device
};

typedef struct MfgXeFGVkLatency
{
    uint32_t size;                              // sizeof(MfgXeFGVkLatency)
    uint32_t mode;                              // MfgXeFGVkLatencyMode
    uint32_t lowLatency;                        // Reflex low latency mode / Anti-Lag 2 on (0: only the FPS limit)
    uint32_t boost;                             // Reflex boost
    float outputFpsLimit;                       // presented frames per second, real and generated (0: none)
} MfgXeFGVkLatency;

typedef struct MfgXeFGVkApi
{
    uint32_t size;                              // sizeof(MfgXeFGVkApi)
    uint32_t version;                           // MFG_XEFG_VK_API_VERSION

    // vkCreateDevice. Returns an edit token and the create info to pass to vkCreateDevice (the engine's queues,
    // extensions and features added; valid until DeviceCreated or DiscardDevice), or NULL: the device can't host
    // XeFG and stays as the game asked (MfgXeFGVkStatus.reason says why). The host keeps VK_NV_low_latency2 calls on
    // the engine's swap chains away from the driver (the engine runs XeLL itself).
    void* (VKAPI_PTR* PrepareDevice)(const MfgXeFGVkDeviceInfo* info, const VkDeviceCreateInfo* createInfo,
                                     const VkDeviceCreateInfo** edited);
    void (VKAPI_PTR* DiscardDevice)(void* edit);
    VkBool32 (VKAPI_PTR* DeviceCreated)(void* edit, VkDevice device, PFN_MfgXeFGVkOriginalProc originals, void* user);
    void (VKAPI_PTR* DeviceDestroying)(VkDevice device);

    // The game's swap chain on `window`, replaced by one whose images are XeFG's back buffers. VK_SUCCESS with the
    // handle the game gets; anything else leaves the game's own swap chain to the caller.
    VkResult (VKAPI_PTR* CreateSwapchain)(VkDevice device, const VkSwapchainCreateInfoKHR* createInfo, HWND window,
                                          uint32_t initFlags, VkSwapchainKHR* swapchain);
    // VK_TRUE when the handle was the engine's (destroyed) or the game's old one it retires itself.
    VkBool32 (VKAPI_PTR* DestroySwapchain)(VkDevice device, VkSwapchainKHR swapchain);
    VkResult (VKAPI_PTR* GetSwapchainImages)(VkDevice device, VkSwapchainKHR swapchain, uint32_t* count, VkImage* images);
    VkResult (VKAPI_PTR* AcquireNextImage)(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore,
                                           VkFence fence, uint32_t* index);
    // The frame's inputs, copied into the engine's images on `command` (the game's queue family, submitted before
    // the frame's QueuePresent). The images stay in their layouts.
    VkResult (VKAPI_PTR* RecordFrame)(VkSwapchainKHR swapchain, VkCommandBuffer command, const MfgXeFGVkFrame* frame);
    VkResult (VKAPI_PTR* QueuePresent)(VkQueue queue, const VkPresentInfoKHR* presentInfo);
    void (VKAPI_PTR* SetHdrMetadata)(VkDevice device, VkSwapchainKHR swapchain, const VkHdrMetadataEXT* metadata);
    // vkWaitForPresentKHR/vkWaitForPresent2KHR on the engine's swap chain.
    VkResult (VKAPI_PTR* WaitForPresent)(VkDevice device, VkSwapchainKHR swapchain, uint64_t presentId, uint64_t timeout);

    void (VKAPI_PTR* SetEnabled)(VkSwapchainKHR swapchain, VkBool32 enabled);
    // Interpolated frames per rendered frame, 0 .. MaxInterpolatedFrames().
    VkBool32 (VKAPI_PTR* SetInterpolatedFrames)(VkSwapchainKHR swapchain, uint32_t count);
    uint32_t (VKAPI_PTR* MaxInterpolatedFrames)(void);
    VkBool32 (VKAPI_PTR* OwnsSwapchain)(VkSwapchainKHR swapchain);
    void (VKAPI_PTR* GetStatus)(MfgXeFGVkStatus* status);
    // vkReleaseSwapchainImagesKHR/EXT (swapchain_maintenance1) on the engine's swap chain. The engine serves that
    // extension's present fences in QueuePresent; the host routes this call (the driver doesn't know the handle).
    VkResult (VKAPI_PTR* ReleaseSwapchainImages)(VkDevice device, VkSwapchainKHR swapchain, uint32_t imageIndexCount,
                                                 const uint32_t* imageIndices);
    // XeFG's debug view (its interpolated frames tagged), applied at the next present.
    void (VKAPI_PTR* SetDebugView)(VkSwapchainKHR swapchain, VkBool32 enabled);
    // The swap-chain entry points the host answers itself for the engine's swap chains (they never reach
    // the driver with the engine's handle), MFG_XEFG_VK_ROUTE_* bits. A device that enables an extension whose entry
    // points are not all routed stays native. Call before PrepareDevice; a host that never calls it routes none.
    void (VKAPI_PTR* SetHostRoutes)(uint32_t routes);
    // Present when `size` covers it: the low-latency output, from the next frame on. Called before
    // PrepareDevice, the mode also asks for its device extensions where the GPU offers them (Reflex: VK_NV_low_latency2
    // and VK_KHR_present_id with its feature; Anti-Lag 2: VK_AMD_anti_lag with its feature). The engine takes the
    // driver's vkSetLatencySleepModeNV, vkLatencySleepNV, vkSetLatencyMarkerNV, vkQueueNotifyOutOfBandNV,
    // vkGetLatencyTimingsNV and vkAntiLagUpdateAMD from the originals callback only (the host's own Reflex wrappers
    // must not see its calls);
    // without them, or before its swap chain exists, a frame sleeps in XeLL.
    void (VKAPI_PTR* SetLatency)(const MfgXeFGVkLatency* latency);
} MfgXeFGVkApi;

// SetHostRoutes bits: what the host does for the engine's swap chains (other handles go to the driver as usual).
#define MFG_XEFG_VK_ROUTE_ACQUIRE_NEXT_IMAGE_2  0x01u  // vkAcquireNextImage2KHR (core 1.1, VK_KHR_device_group): AcquireNextImage
#define MFG_XEFG_VK_ROUTE_FULL_SCREEN_EXCLUSIVE 0x02u  // vkAcquire/ReleaseFullScreenExclusiveModeEXT: VK_SUCCESS
#define MFG_XEFG_VK_ROUTE_DISPLAY_TIMING        0x04u  // vkGetRefreshCycleDurationGOOGLE: the window's display;
                                                       // vkGetPastPresentationTimingGOOGLE: none yet (count 0)
#define MFG_XEFG_VK_ROUTE_LOCAL_DIMMING         0x08u  // vkSetLocalDimmingAMD: ignored
#define MFG_XEFG_VK_ROUTE_SWAPCHAIN_STATUS      0x10u  // vkGetSwapchainStatusKHR: VK_SUCCESS
#define MFG_XEFG_VK_ROUTE_SWAPCHAIN_COUNTER     0x20u  // vkGetSwapchainCounterEXT: VK_ERROR_OUT_OF_DATE_KHR (no counters)
#define MFG_XEFG_VK_ROUTE_OBJECT_DATA           0x40u  // debug names/tags and private data on the swap-chain handle: kept by the host
#define MFG_XEFG_VK_ROUTES_ALL                  0x7Fu

// Exported by the engine (MfgXeFGVkGetApi). NULL when `version` is not supported or the engine is unavailable.
typedef const MfgXeFGVkApi* (VKAPI_PTR* PFN_MfgXeFGVkGetApi)(uint32_t version);

#ifdef __cplusplus
}
#endif
