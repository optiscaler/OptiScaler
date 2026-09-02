#include "pch.h"

#include "DlssNrFeature_Vk.h"

#include <Config.h>
#include <State.h>
#include <Util.h>
#include <NVNGX_Parameter.h>

#include <shaders/dlssnr/DlssNr_Vk.h>

#include <memory>
#include <mutex>
#include <string>

namespace DlssNr
{

namespace
{

// The forwarder's Vulkan surface. The model checks its caller's module path and requires nvngx.dll in
// it, whichever API is being used, so these calls go through the same shim the D3D12 path does.
using PFN_VkProbe = int(__cdecl*)(const wchar_t*);
using PFN_VkInit = int(__cdecl*)(const wchar_t*, const wchar_t*, void*, void*, void*, int);
using PFN_VkCreate = void*(__cdecl*)(void*, void*, unsigned int, unsigned int, int, float, int, float, float, float,
                                     int, int);
using PFN_VkEvaluate = int(__cdecl*)(void*, void*, void*, void*, void*, void*, void*, unsigned int, unsigned int,
                                     unsigned int, unsigned int, int, int, float, int, float, float, float, int, float,
                                     float);
using PFN_VkRelease = void(__cdecl*)(void*);

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

    HMODULE forwarder = nullptr;
    PFN_VkProbe probe = nullptr;
    PFN_VkInit init = nullptr;
    PFN_VkCreate create = nullptr;
    PFN_VkEvaluate evaluate = nullptr;
    PFN_VkRelease release = nullptr;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    bool ngxInitialised = false;
    void* feature = nullptr;
    NVSDK_NGX_Parameter* capabilityParams = nullptr;

    // What the model writes, the proxy it is shown, and the frame as the upscaler left it.
    OwnedImage output;
    OwnedImage proxy;
    OwnedImage keep;

    std::unique_ptr<DlssNr_Vk> pass;

    uint32_t width = 0;
    uint32_t height = 0;
    bool reset = true;
    unsigned long long frames = 0;
};

VkState g_vk;
std::mutex g_vkMutex;

void Fail(const char* why)
{
    if (g_vk.failed)
        return;

    g_vk.failed = true;
    g_vk.reason = why;
    LOG_ERROR("DLSS-NR Vulkan unavailable: {}", why);
}

// ---------------------------------------------------------------------------------------------
// Images this pass owns
// ---------------------------------------------------------------------------------------------

void DestroyImage(OwnedImage& img)
{
    if (g_vk.device == VK_NULL_HANDLE)
        return;

    if (img.view != VK_NULL_HANDLE)
        vkDestroyImageView(g_vk.device, img.view, nullptr);

    if (img.image != VK_NULL_HANDLE)
        vkDestroyImage(g_vk.device, img.image, nullptr);

    if (img.memory != VK_NULL_HANDLE)
        vkFreeMemory(g_vk.device, img.memory, nullptr);

    img = OwnedImage {};
}

uint32_t FindMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProps {};
    vkGetPhysicalDeviceMemoryProperties(g_vk.physicalDevice, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
    {
        if ((typeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }

    return UINT32_MAX;
}

// STORAGE and SAMPLED both, because every one of these is written by one dispatch and read by the
// next; TRANSFER_SRC so a capture can copy it out without a second surface.
bool CreateImage(OwnedImage& img, uint32_t width, uint32_t height, VkFormat format, bool readWrite)
{
    DestroyImage(img);

    VkImageCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = { width, height, 1 };
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(g_vk.device, &info, nullptr, &img.image) != VK_SUCCESS)
    {
        LOG_ERROR("DLSS-NR Vulkan: could not create a {}x{} image", width, height);
        return false;
    }

    VkMemoryRequirements req {};
    vkGetImageMemoryRequirements(g_vk.device, img.image, &req);

    VkMemoryAllocateInfo alloc {};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = FindMemoryTypeIndex(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (alloc.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(g_vk.device, &alloc, nullptr, &img.memory) != VK_SUCCESS ||
        vkBindImageMemory(g_vk.device, img.image, img.memory, 0) != VK_SUCCESS)
    {
        LOG_ERROR("DLSS-NR Vulkan: could not back a {}x{} image", width, height);
        DestroyImage(img);
        return false;
    }

    VkImageViewCreateInfo view {};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = img.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    if (vkCreateImageView(g_vk.device, &view, nullptr, &img.view) != VK_SUCCESS)
    {
        LOG_ERROR("DLSS-NR Vulkan: could not view a {}x{} image", width, height);
        DestroyImage(img);
        return false;
    }

    img.width = width;
    img.height = height;
    img.format = format;
    img.layout = VK_IMAGE_LAYOUT_UNDEFINED;

    // The NGX wrapper. Filled once, because none of it changes until the image is recreated.
    img.ngx.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
    img.ngx.Resource.ImageViewInfo.ImageView = img.view;
    img.ngx.Resource.ImageViewInfo.Image = img.image;
    img.ngx.Resource.ImageViewInfo.SubresourceRange = view.subresourceRange;
    img.ngx.Resource.ImageViewInfo.Format = format;
    img.ngx.Resource.ImageViewInfo.Width = width;
    img.ngx.Resource.ImageViewInfo.Height = height;
    img.ngx.ReadWrite = readWrite;

    return true;
}

// A layout transition with the access masks that go with it. Vulkan has no equivalent of D3D12's
// state promotion, so every read and every write says which layout it needs and this is how it gets
// there. Tracked per image so a no-op transition is not recorded.
void Transition(VkCommandBuffer cmd, OwnedImage& img, VkImageLayout to)
{
    if (img.image == VK_NULL_HANDLE || img.layout == to)
        return;

    VkImageMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = img.layout;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = img.image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    img.layout = to;
}

// A resource the game owns. Its layout is the game's business, so this records the transition and
// puts it back exactly as it was rather than tracking it.
void TransitionForeign(VkCommandBuffer cmd, VkImage image, VkImageSubresourceRange range, VkImageLayout from,
                       VkImageLayout to)
{
    if (image == VK_NULL_HANDLE || from == to)
        return;

    VkImageMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = range;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
}

// ---------------------------------------------------------------------------------------------
// Bring-up
// ---------------------------------------------------------------------------------------------

bool LoadForwarder()
{
    if (g_vk.forwarder != nullptr)
        return g_vk.create != nullptr;

    auto path = Util::FindFilePath(Util::DllPath().remove_filename(), "nvngx.dll_dlssnr.dll");

    if (!path.has_value())
        path = Util::FindFilePath(Util::ExePath().remove_filename(), "nvngx.dll_dlssnr.dll");

    if (!path.has_value())
    {
        Fail("nvngx.dll_dlssnr.dll was not found beside OptiScaler or the game");
        return false;
    }

    g_vk.forwarder = LoadLibraryW(path->wstring().c_str());

    if (g_vk.forwarder == nullptr)
    {
        Fail("the forwarder would not load");
        return false;
    }

    g_vk.probe = (PFN_VkProbe) GetProcAddress(g_vk.forwarder, "dlssnr_vk_probe");
    g_vk.init = (PFN_VkInit) GetProcAddress(g_vk.forwarder, "dlssnr_vk_init");
    g_vk.create = (PFN_VkCreate) GetProcAddress(g_vk.forwarder, "dlssnr_vk_create");
    g_vk.evaluate = (PFN_VkEvaluate) GetProcAddress(g_vk.forwarder, "dlssnr_vk_evaluate");
    g_vk.release = (PFN_VkRelease) GetProcAddress(g_vk.forwarder, "dlssnr_vk_release");

    if (g_vk.init == nullptr || g_vk.create == nullptr || g_vk.evaluate == nullptr)
    {
        Fail("the forwarder is missing its Vulkan entry points");
        return false;
    }

    return true;
}

// Whether a format can hold linear, open-ended light. A frame the game already tone mapped has white
// at 1 and must not be encoded a second time; an 8-bit or normalised format cannot be scene-referred
// whatever the game says. The D3D12 path asks the same question of DXGI formats.
bool FormatCanHoldLinearHdr(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_R32G32B32A32_SFLOAT:
    case VK_FORMAT_R16G16B16_SFLOAT:
    case VK_FORMAT_R32G32B32_SFLOAT:
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
    case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
        return true;
    default:
        return false;
    }
}

// The create flags the game gave its own upscaler, which is where HDR and inverted depth are stated.
// Read from the parameter block rather than configured, because they describe the game's buffers and
// getting either wrong is silent: an encoded frame encoded twice, or depth read backwards.
unsigned int GameCreateFlags(NVSDK_NGX_Parameter* params)
{
    unsigned int flags = 0;

    if (params != nullptr)
        params->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, &flags);

    return flags;
}

std::optional<std::filesystem::path> FindSnippet()
{
    auto snippet = Util::FindFilePath(Util::DllPath().remove_filename(), "nvngx_dlssnr.dll");

    if (!snippet.has_value())
        snippet = Util::FindFilePath(Util::ExePath().remove_filename(), "nvngx_dlssnr.dll");

    return snippet;
}

} // namespace

// ---------------------------------------------------------------------------------------------

bool IsRunningVk() { return g_vk.feature != nullptr && !g_vk.failed; }

const char* FailureReasonVk() { return g_vk.failed ? g_vk.reason : ""; }

unsigned long long FramesVk() { return g_vk.frames; }

void EvaluateAfterUpscaleVk(VkCommandBuffer cmdBuffer, NVSDK_NGX_Parameter* params, VkInstance instance,
                            VkPhysicalDevice physicalDevice, VkDevice device)
{
    auto& cfg = *Config::Instance();

    if (!cfg.DlssNrEnabled.value_or_default())
        return;

    if (cmdBuffer == VK_NULL_HANDLE || params == nullptr || device == VK_NULL_HANDLE ||
        physicalDevice == VK_NULL_HANDLE)
        return;

    std::lock_guard<std::mutex> lock(g_vkMutex);

    if (g_vk.failed)
        return;

    // The game's own resources, already wrapped: NGX hands Vulkan resources over as
    // NVSDK_NGX_Resource_VK, so only this pass's own images need building.
    NVSDK_NGX_Resource_VK* colour = nullptr;
    NVSDK_NGX_Resource_VK* depth = nullptr;
    NVSDK_NGX_Resource_VK* motion = nullptr;

    params->Get(NVSDK_NGX_Parameter_Output, (void**) &colour);
    params->Get(NVSDK_NGX_Parameter_Depth, (void**) &depth);
    params->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &motion);

    if (colour == nullptr || depth == nullptr || motion == nullptr)
    {
        static bool said = false;

        if (!said)
        {
            said = true;
            LOG_INFO("DLSS-NR Vulkan: the parameter block carried no {}",
                     colour == nullptr ? "output" : (depth == nullptr ? "depth" : "motion vectors"));
        }

        return;
    }

    const uint32_t width = colour->Resource.ImageViewInfo.Width;
    const uint32_t height = colour->Resource.ImageViewInfo.Height;
    const uint32_t guideWidth = depth->Resource.ImageViewInfo.Width;
    const uint32_t guideHeight = depth->Resource.ImageViewInfo.Height;

    if (width == 0 || height == 0)
        return;

    g_vk.instance = instance;
    g_vk.physicalDevice = physicalDevice;

    // A device change invalidates everything. Rebuild rather than reuse handles from a dead device.
    if (g_vk.device != device)
    {
        ShutdownVk();
        g_vk.device = device;
        g_vk.instance = instance;
        g_vk.physicalDevice = physicalDevice;
    }

    if (!LoadForwarder())
        return;

    // Initialise NGX on this device, once. The snippet path is the model itself; the forwarder loads
    // it so the caller gate sees a module named nvngx.dll.
    if (!g_vk.ngxInitialised)
    {
        auto snippet = FindSnippet();

        if (!snippet.has_value())
        {
            Fail("nvngx_dlssnr.dll was not found beside OptiScaler or the game");
            return;
        }

        const int probe = g_vk.probe != nullptr ? g_vk.probe(snippet->wstring().c_str()) : 0;

        // Four bits, one per entry point. Anything short of fifteen means the model's Vulkan surface
        // is not entirely reachable and there is no point going further.
        if (probe != 15)
        {
            LOG_ERROR("DLSS-NR Vulkan: the model's Vulkan surface is incomplete (probe {})", probe);
            Fail("the model does not expose a complete Vulkan surface");
            return;
        }

        const int result =
            g_vk.init(snippet->wstring().c_str(), State::Instance().NVNGX_ApplicationDataPath.c_str(),
                      (void*) instance, (void*) physicalDevice, (void*) device, 0x0000015);

        if (result != 1)
        {
            LOG_ERROR("DLSS-NR Vulkan: NVSDK_NGX_VULKAN_Init_Ext returned {}", result);
            Fail("the model would not initialise on this Vulkan device");
            return;
        }

        g_vk.ngxInitialised = true;
        LOG_INFO("DLSS-NR Vulkan: the model initialised on this device");
    }

    if (g_vk.capabilityParams == nullptr)
    {
        if (NVSDK_NGX_VULKAN_AllocateParameters(&g_vk.capabilityParams) != NVSDK_NGX_Result_Success ||
            g_vk.capabilityParams == nullptr)
        {
            Fail("a parameter block could not be allocated");
            return;
        }
    }

    if (g_vk.pass == nullptr)
    {
        g_vk.pass = std::make_unique<DlssNr_Vk>("Neural Rendering", device, physicalDevice);

        if (!g_vk.pass->IsInit())
        {
            g_vk.pass.reset();
            Fail("the composition pass could not be created");
            return;
        }
    }

    // Resize. The model's feature is built for a size and has to be rebuilt when it changes.
    if (g_vk.width != width || g_vk.height != height)
    {
        if (g_vk.feature != nullptr && g_vk.release != nullptr)
        {
            g_vk.release(g_vk.feature);
            g_vk.feature = nullptr;
        }

        const VkFormat working = VK_FORMAT_R16G16B16A16_SFLOAT;

        if (!CreateImage(g_vk.output, width, height, working, true) ||
            !CreateImage(g_vk.proxy, width, height, working, true) ||
            !CreateImage(g_vk.keep, width, height, working, true))
        {
            Fail("the pass could not allocate its own surfaces");
            return;
        }

        g_vk.width = width;
        g_vk.height = height;
        g_vk.reset = true;
    }

    if (g_vk.feature == nullptr)
    {
        g_vk.feature = g_vk.create(
            (void*) cmdBuffer, g_vk.capabilityParams, width, height, (int) cfg.DlssNrPreset.value_or_default(),
            cfg.DlssNrIntensity.value_or_default(), (int) cfg.DlssNrStyle.value_or_default(),
            cfg.DlssNrLocalStructure.value_or_default(), cfg.DlssNrLocalTone.value_or_default(),
            cfg.DlssNrSkinStructure.value_or_default(), cfg.DlssNrAutoMask.value_or_default() ? 1 : 0, 1);

        if (g_vk.feature == nullptr)
        {
            Fail("the model would not build a feature on this device");
            return;
        }

        LOG_INFO("DLSS-NR Vulkan: feature up at {}x{}", width, height);
        g_vk.reset = true;
    }

    // -----------------------------------------------------------------------------------------
    // Encode: the frame the upscaler wrote -> a display-referred proxy, plus an untouched copy
    // -----------------------------------------------------------------------------------------

    const unsigned int createFlags = GameCreateFlags(params);
    const bool gameSaysHdr = (createFlags & NVSDK_NGX_DLSS_Feature_Flags_IsHDR) != 0;
    const bool depthInverted = (createFlags & NVSDK_NGX_DLSS_Feature_Flags_DepthInverted) != 0;

    // Both have to agree. A game can set the HDR flag on a buffer that cannot hold open-ended light,
    // and encoding an already tone-mapped frame a second time looks washed out and banded.
    const bool linearHdr = gameSaysHdr && FormatCanHoldLinearHdr(colour->Resource.ImageViewInfo.Format);

    // The slider only. The exposure source rides on the D3D12 meter's readback, which has no Vulkan
    // counterpart yet, so this path is deliberately manual rather than quietly reading nothing.
    const float whitePoint = cfg.DlssNrWhitePointScale.value_or_default();

    static bool saidEncoding = false;

    if (!saidEncoding)
    {
        saidEncoding = true;
        LOG_INFO("DLSS-NR Vulkan: the game's buffer is {} (flag {}, format {}), depth {}",
                 linearHdr ? "linear HDR" : "already tone-mapped", gameSaysHdr ? "set" : "clear",
                 (int) colour->Resource.ImageViewInfo.Format, depthInverted ? "inverted" : "normal");
    }

    DlssNrConstants encode {};
    encode.Mode = DlssNrMode_Encode;
    encode.Width = width;
    encode.Height = height;
    encode.WhitePoint = whitePoint;
    encode.Passthrough = linearHdr ? 0u : 1u;
    encode.TransferStrength = cfg.DlssNrTransferStrength.value_or_default();
    encode.ColourStrength = cfg.DlssNrColourStrength.value_or_default();
    encode.MaxRatio = cfg.DlssNrMaxRatio.value_or_default();
    encode.Transfer = cfg.DlssNrTransfer.value_or_default();
    encode.DebugScale = cfg.DlssNrWhitePointScale.value_or_default();
    encode.GuideWidth = guideWidth;
    encode.GuideHeight = guideHeight;

    const VkImageSubresourceRange colourRange = colour->Resource.ImageViewInfo.SubresourceRange;

    // The game's colour is read here and written at the end. Its layout on arrival is GENERAL, which
    // is what NGX requires of a resource it is handed, so it is left alone.
    Transition(cmdBuffer, g_vk.proxy, VK_IMAGE_LAYOUT_GENERAL);
    Transition(cmdBuffer, g_vk.keep, VK_IMAGE_LAYOUT_GENERAL);

    if (!g_vk.pass->Dispatch(cmdBuffer, encode, width, height, colour->Resource.ImageViewInfo.ImageView,
                             VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, g_vk.proxy.view, g_vk.keep.view))
    {
        Fail("the encode dispatch failed");
        return;
    }

    // -----------------------------------------------------------------------------------------
    // The model
    // -----------------------------------------------------------------------------------------

    Transition(cmdBuffer, g_vk.output, VK_IMAGE_LAYOUT_GENERAL);

    const int evaluated = g_vk.evaluate(
        (void*) cmdBuffer, g_vk.feature, g_vk.capabilityParams, &g_vk.proxy.ngx, depth, motion, &g_vk.output.ngx, width,
        height, guideWidth, guideHeight, depthInverted ? 1 : 0, g_vk.reset ? 1 : 0,
        cfg.DlssNrIntensity.value_or_default(), (int) cfg.DlssNrStyle.value_or_default(),
        cfg.DlssNrLocalStructure.value_or_default(), cfg.DlssNrLocalTone.value_or_default(),
        cfg.DlssNrSkinStructure.value_or_default(), cfg.DlssNrAutoMask.value_or_default() ? 1 : 0, 1.0f, 1.0f);

    g_vk.reset = false;
    g_vk.frames++;

    if (evaluated != 1)
    {
        LOG_ERROR("DLSS-NR Vulkan: evaluate returned {}", evaluated);
        Fail("the model refused to evaluate");
        return;
    }

    // -----------------------------------------------------------------------------------------
    // Resolve: proxy + the model's answer + the untouched copy -> the frame
    // -----------------------------------------------------------------------------------------

    DlssNrConstants resolve = encode;
    resolve.Mode = DlssNrMode_Resolve;

    Transition(cmdBuffer, g_vk.proxy, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Transition(cmdBuffer, g_vk.output, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Transition(cmdBuffer, g_vk.keep, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    if (!g_vk.pass->Dispatch(cmdBuffer, resolve, width, height, g_vk.proxy.view, g_vk.output.view, g_vk.keep.view,
                             VK_NULL_HANDLE, colour->Resource.ImageViewInfo.ImageView, VK_NULL_HANDLE))
    {
        Fail("the resolve dispatch failed");
        return;
    }

    static bool reported = false;

    if (!reported && g_vk.frames > 2)
    {
        reported = true;
        LOG_INFO("DLSS-NR Vulkan: running natively at {}x{}, guides {}x{}", width, height, guideWidth, guideHeight);
    }
}

void ShutdownVk()
{
    if (g_vk.feature != nullptr && g_vk.release != nullptr)
        g_vk.release(g_vk.feature);

    g_vk.feature = nullptr;

    DestroyImage(g_vk.output);
    DestroyImage(g_vk.proxy);
    DestroyImage(g_vk.keep);

    g_vk.pass.reset();

    if (g_vk.capabilityParams != nullptr)
    {
        NVSDK_NGX_VULKAN_DestroyParameters(g_vk.capabilityParams);
        g_vk.capabilityParams = nullptr;
    }

    g_vk.device = VK_NULL_HANDLE;
    g_vk.width = 0;
    g_vk.height = 0;
    g_vk.ngxInitialised = false;
    g_vk.reset = true;
}

} // namespace DlssNr
