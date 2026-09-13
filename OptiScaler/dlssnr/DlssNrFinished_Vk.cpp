#include "pch.h"
#include "DlssNrFinished_Vk.h"
#include <Config.h>
#include <hooks/VulkanwDx12_Hooks.h>
#include <algorithm>
#include <array>
#include <mutex>
#include <vector>

namespace DlssNr
{
namespace
{
std::recursive_mutex finishedMutex;
std::vector<FinishedVk*> owners;
struct Swapchain
{
    VkDevice device = VK_NULL_HANDLE;
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR space {};
    VkExtent2D size {};
    VkImageUsageFlags usage = 0;
    std::vector<VkImage> images;
} screen;
std::string status = "Waiting for a finished Vulkan picture.";
uint64_t presentEpoch = 0;
void Say(const char* message)
{
    if (status != message)
    {
        status = message;
        LOG_INFO("DLSS-NR Vulkan finished picture: {}", message);
    }
}
struct Image
{
    VkImageInfo info {};
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    void Destroy(VkDevice device)
    {
        if (info.ImageView)
            vkDestroyImageView(device, info.ImageView, nullptr);
        if (info.Image)
            vkDestroyImage(device, info.Image, nullptr);
        if (memory)
            vkFreeMemory(device, memory, nullptr);
        info = {};
        memory = VK_NULL_HANDLE;
        layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    bool Ensure(VkDevice device, VkPhysicalDevice physical, uint32_t width, uint32_t height, VkFormat format)
    {
        if (info.Image && info.Width == width && info.Height == height && info.Format == format)
            return true;
        Destroy(device); // caller has retired the slot
        VkImageCreateInfo ci { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = format;
        ci.extent = { width, height, 1 };
        ci.mipLevels = ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (vkCreateImage(device, &ci, nullptr, &info.Image) != VK_SUCCESS)
            return false;
        VkMemoryRequirements need {};
        vkGetImageMemoryRequirements(device, info.Image, &need);
        VkPhysicalDeviceMemoryProperties props {};
        vkGetPhysicalDeviceMemoryProperties(physical, &props);
        uint32_t type = UINT32_MAX;
        for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
            if ((need.memoryTypeBits & (1u << i)) &&
                (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            {
                type = i;
                break;
            }
        if (type == UINT32_MAX)
            return false;
        VkMemoryAllocateInfo ai { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        ai.allocationSize = need.size;
        ai.memoryTypeIndex = type;
        if (vkAllocateMemory(device, &ai, nullptr, &memory) != VK_SUCCESS ||
            vkBindImageMemory(device, info.Image, memory, 0) != VK_SUCCESS)
            return false;
        VkImageViewCreateInfo vi { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vi.image = info.Image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(device, &vi, nullptr, &info.ImageView) != VK_SUCCESS)
            return false;
        info.SubresourceRange = vi.subresourceRange;
        info.Format = format;
        info.Width = width;
        info.Height = height;
        return true;
    }
};
void Transition(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to)
{
    VkImageMemoryBarrier b { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.oldLayout = from;
    b.newLayout = to;
    b.image = image;
    b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.srcAccessMask = from == VK_IMAGE_LAYOUT_UNDEFINED || from == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                          ? 0
                          : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask =
        to == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ? 0 : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
}
void Transition(VkCommandBuffer cmd, Image& image, VkImageLayout to)
{
    Transition(cmd, image.info.Image, image.layout, to);
    image.layout = to;
}
void Blit(VkCommandBuffer cmd, VkImage from, VkImage to, VkExtent2D size)
{
    VkImageBlit region {};
    region.srcSubresource = region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.srcOffsets[1] = region.dstOffsets[1] = { (int) size.width, (int) size.height, 1 };
    vkCmdBlitImage(cmd, from, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, to, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &region, VK_FILTER_NEAREST);
}
} // namespace
struct FinishedVk::Impl
{
    DlssNr_Vk& shader;
    VkDevice device;
    VkPhysicalDevice physical;
    struct Slot
    {
        Image depth, motion, input, linear, output, encoded;
        DlssNrFrameInfo_Vk frame {};
        VkInstance instance = VK_NULL_HANDLE;
        VkCommandBuffer producer = VK_NULL_HANDLE, cmd = VK_NULL_HANDLE;
        VkCommandPool pool = VK_NULL_HANDLE, producerPool = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        VkEvent captured = VK_NULL_HANDLE;
        VkFence done = VK_NULL_HANDLE;
        uint32_t family = UINT32_MAX;
        uint64_t epoch = 0, serial = 0;
        bool pending = false, submitted = false, validCapture = false;
    };
    std::array<Slot, 4> slots;
    std::vector<VkSemaphore> presentReady;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    uint64_t serial = 0, frames = 0;
    Impl(DlssNr_Vk& s, VkDevice d, VkPhysicalDevice p) : shader(s), device(d), physical(p) {}
    ~Impl()
    {
        if (vkDeviceWaitIdle(device) != VK_SUCCESS)
            return;
        for (auto& s : slots)
        {
            s.depth.Destroy(device);
            s.motion.Destroy(device);
            s.input.Destroy(device);
            s.linear.Destroy(device);
            s.output.Destroy(device);
            s.encoded.Destroy(device);
            if (s.captured)
                vkDestroyEvent(device, s.captured, nullptr);
            if (s.done)
                vkDestroyFence(device, s.done, nullptr);
            if (s.pool)
                vkDestroyCommandPool(device, s.pool, nullptr);
        }
        for (auto semaphore : presentReady)
            if (semaphore)
                vkDestroySemaphore(device, semaphore, nullptr);
    }
    void Capture(VkCommandBuffer cmd, const VkImageInfo& depth, const VkImageInfo& motion,
                 const DlssNrFrameInfo_Vk& frame, VkInstance instance)
    {
        if (!Config::Instance()->DlssNrEnabled.value_or_default() ||
            !Config::Instance()->DlssNrFinishedPicture.value_or_default())
        {
            for (auto& s : slots)
                if (s.submitted)
                    s.pending = false;
            return;
        }
        if (screen.device != device || !screen.handle)
        {
            Say("Enable NR before creating the Vulkan swapchain; restart the game if NR was enabled during play.");
            return;
        }
        if (Config::Instance()->DlssNrRunBeforeSr.value_or_default() ||
            Config::Instance()->DlssNrDeferredDlss.value_or_default())
        {
            Say("Native Vulkan finished-picture NR runs at presentation; disable Generate model before upscale and "
                "Generate before SR, apply after SR.");
            return;
        }
        if (!depth.ImageView || !motion.ImageView)
        {
            Say("Waiting for Vulkan depth and movement data.");
            return;
        }
        auto family = Vulkan_wDx12::cmdBufferStateTracker.GetCommandBufferQueueFamily(cmd);
        auto level = Vulkan_wDx12::cmdBufferStateTracker.GetCommandBufferLevel(cmd);
        if (!family || !level || *level != VK_COMMAND_BUFFER_LEVEL_PRIMARY)
        {
            Say("Waiting for a tracked primary Vulkan command buffer.");
            return;
        }
        Slot* next = nullptr;
        for (auto& s : slots)
        {
            if (s.pending && s.submitted && s.epoch + 1 < presentEpoch &&
                vkGetEventStatus(device, s.captured) == VK_EVENT_SET)
                s.pending = false;
            if (!s.pending && (!s.done || vkGetFenceStatus(device, s.done) == VK_SUCCESS) &&
                (!s.submitted || vkGetEventStatus(device, s.captured) == VK_EVENT_SET))
            {
                next = &s;
                break;
            }
        }
        if (!next)
            return;
        auto& s = *next;
        if (s.pool && s.family != *family)
            return;
        if (!s.pool)
        {
            VkCommandPoolCreateInfo pi { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
            pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pi.queueFamilyIndex = *family;
            if (vkCreateCommandPool(device, &pi, nullptr, &s.pool) != VK_SUCCESS)
                return;
            s.family = *family;
            VkCommandBufferAllocateInfo ai { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            ai.commandPool = s.pool;
            ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = 1;
            VkFenceCreateInfo fi { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            VkEventCreateInfo ei { VK_STRUCTURE_TYPE_EVENT_CREATE_INFO };
            if (vkAllocateCommandBuffers(device, &ai, &s.cmd) != VK_SUCCESS ||
                vkCreateFence(device, &fi, nullptr, &s.done) != VK_SUCCESS ||
                vkCreateEvent(device, &ei, nullptr, &s.captured) != VK_SUCCESS)
                return;
        }
        if (!s.cmd || !s.done || !s.captured)
            return;
        if (!s.depth.Ensure(device, physical, depth.Width, depth.Height, VK_FORMAT_R32_SFLOAT) ||
            !s.motion.Ensure(device, physical, motion.Width, motion.Height, VK_FORMAT_R32G32_SFLOAT))
            return;
        if (vkResetEvent(device, s.captured) != VK_SUCCESS)
            return;
        auto copy = [&](const VkImageInfo& source, Image& dest, bool readWrite)
        {
            Transition(cmd, dest, VK_IMAGE_LAYOUT_GENERAL);
            DlssNrConstants c {};
            c.Mode = DlssNrMode_Downsample;
            c.Width = source.Width;
            c.Height = source.Height;
            return shader.Dispatch(cmd, c, c.Width, c.Height, source.ImageView, VK_NULL_HANDLE, VK_NULL_HANDLE,
                                   VK_NULL_HANDLE, dest.info.ImageView, VK_NULL_HANDLE,
                                   readWrite ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        };
        s.validCapture = copy(depth, s.depth, frame.DepthReadWrite) && copy(motion, s.motion, frame.MotionReadWrite);
        vkCmdSetEvent(cmd, s.captured, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        s.frame = frame;
        s.frame.ExposureTexture = nullptr;
        s.frame.PreExposure = 1.0f;
        s.frame.FinishedPicture = true;
        s.frame.BeforeUpscale = false;
        s.frame.DepthReadWrite = s.frame.MotionReadWrite = false;
        s.instance = instance;
        s.producer = cmd;
        s.producerPool = Vulkan_wDx12::cmdBufferStateTracker.GetCommandBufferPool(cmd);
        s.queue = VK_NULL_HANDLE;
        s.epoch = presentEpoch;
        s.serial = ++serial;
        s.pending = true;
        s.submitted = false;
    }
    bool Present(VkQueue queue, VkPresentInfoKHR* present)
    {
        if (!Config::Instance()->DlssNrEnabled.value_or_default() ||
            !Config::Instance()->DlssNrFinishedPicture.value_or_default() || screen.device != device ||
            present->swapchainCount != 1 || present->pSwapchains[0] != screen.handle)
            return false;
        const uint32_t index = present->pImageIndices[0];
        if (index >= screen.images.size())
            return false;
        Slot* latest = nullptr;
        for (auto& s : slots)
            if (s.pending && s.submitted && s.validCapture && s.queue == queue && s.epoch + 1 >= presentEpoch &&
                s.frame.OutputWidth == screen.size.width && s.frame.OutputHeight == screen.size.height &&
                (!latest || s.serial > latest->serial))
                latest = &s;
        if (!latest)
            return false;
        auto& s = *latest;
        if ((screen.usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) !=
            (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        {
            Say("The Vulkan swapchain does not support finished-picture transfers.");
            return false;
        }
        const bool pq = screen.space == VK_COLOR_SPACE_HDR10_ST2084_EXT;
        const bool scrgb = screen.space == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
        const bool srgb = screen.format == VK_FORMAT_R8G8B8A8_SRGB || screen.format == VK_FORMAT_B8G8R8A8_SRGB ||
                          screen.format == VK_FORMAT_A8B8G8R8_SRGB_PACK32;
        if (!pq && !scrgb && screen.space != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            Say("This Vulkan screen colour space is not supported.");
            return false;
        }
        VkFormatProperties props {};
        vkGetPhysicalDeviceFormatProperties(physical, screen.format, &props);
        if ((props.optimalTilingFeatures & (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT)) !=
            (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT))
            return false;
        if (swapchain != screen.handle)
        {
            if (vkDeviceWaitIdle(device) != VK_SUCCESS)
                return false;
            for (auto semaphore : presentReady)
                if (semaphore)
                    vkDestroySemaphore(device, semaphore, nullptr);
            presentReady.assign(screen.images.size(), VK_NULL_HANDLE);
            swapchain = screen.handle;
        }
        if (!presentReady[index])
        {
            VkSemaphoreCreateInfo ci { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            if (vkCreateSemaphore(device, &ci, nullptr, &presentReady[index]) != VK_SUCCESS)
                return false;
        }
        auto ensure = [&](Image& image)
        {
            return image.Ensure(device, physical, screen.size.width, screen.size.height, VK_FORMAT_R16G16B16A16_SFLOAT);
        };
        if (!ensure(s.input) || !ensure(s.output) || (pq && (!ensure(s.linear) || !ensure(s.encoded))))
            return false;
        if (vkResetCommandPool(device, s.pool, 0) != VK_SUCCESS)
            return false;
        VkCommandBufferBeginInfo bi { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(s.cmd, &bi) != VK_SUCCESS)
            return false;
        Transition(s.cmd, screen.images[index], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        Transition(s.cmd, s.input, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        Blit(s.cmd, screen.images[index], s.input.info.Image, screen.size);
        Transition(s.cmd, s.input, VK_IMAGE_LAYOUT_GENERAL);
        Transition(s.cmd, s.output, VK_IMAGE_LAYOUT_GENERAL);
        Transition(s.cmd, s.depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Transition(s.cmd, s.motion, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Image* input = &s.input;
        bool colorReady = true;
        if (pq)
        {
            Transition(s.cmd, s.linear, VK_IMAGE_LAYOUT_GENERAL);
            DlssNrConstants conversion {};
            conversion.Width = screen.size.width;
            conversion.Height = screen.size.height;
            Transition(s.cmd, s.input, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            colorReady = shader.Dispatch(s.cmd, conversion, conversion.Width, conversion.Height, s.input.info.ImageView,
                            VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, s.linear.info.ImageView, VK_NULL_HANDLE,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true);
            input = &s.linear;
        }
        auto frame = s.frame;
        frame.ColourIsLinearHdr = pq || scrgb || srgb;
        frame.WhitePointOverride = (pq || scrgb) ? 203.0f / 80.0f : srgb ? 1.0f : 0.0f;
        bool ran = false;
        if (colorReady)
            shader.Dispatch(s.cmd, input->info, s.depth.info, s.motion.info, s.output.info, frame, s.instance,
                            VK_IMAGE_LAYOUT_GENERAL, &ran);
        Image* result = &s.output;
        if (pq && ran)
        {
            Transition(s.cmd, s.output, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            Transition(s.cmd, s.encoded, VK_IMAGE_LAYOUT_GENERAL);
            DlssNrConstants conversion {};
            conversion.Mode = 1;
            conversion.Width = screen.size.width;
            conversion.Height = screen.size.height;
            colorReady = shader.Dispatch(s.cmd, conversion, conversion.Width, conversion.Height, s.output.info.ImageView,
                            VK_NULL_HANDLE, s.input.info.ImageView, VK_NULL_HANDLE, s.encoded.info.ImageView,
                            VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true);
            result = &s.encoded;
        }
        // The original presentable image is only modified after a successful model evaluation.
        if (ran && colorReady && Config::Instance()->DlssNrApplyModel.value_or_default())
        {
            Transition(s.cmd, *result, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            Transition(s.cmd, screen.images[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            Blit(s.cmd, result->info.Image, screen.images[index], screen.size);
            Transition(s.cmd, screen.images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }
        else
            Transition(s.cmd, screen.images[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        if (vkEndCommandBuffer(s.cmd) != VK_SUCCESS)
            return false;
        if (vkResetFences(device, 1, &s.done) != VK_SUCCESS)
            return false;
        std::vector<VkPipelineStageFlags> stages(present->waitSemaphoreCount, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        VkSubmitInfo submit { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.waitSemaphoreCount = present->waitSemaphoreCount;
        submit.pWaitSemaphores = present->pWaitSemaphores;
        submit.pWaitDstStageMask = stages.data();
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &s.cmd;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &presentReady[index];
        if (vkQueueSubmit(queue, 1, &submit, s.done) != VK_SUCCESS)
        {
            Say("Vulkan finished-picture submission failed.");
            return false;
        }
        for (auto& old : slots)
            if (old.submitted && old.serial <= s.serial)
                old.pending = false;
        present->waitSemaphoreCount = 1;
        present->pWaitSemaphores = &presentReady[index];
        Say(ran ? "Applying NR to the finished Vulkan picture." : "Preparing NR for the finished Vulkan picture.");
        if (ran && (++frames == 1 || frames % 300 == 0))
            LOG_INFO("DLSS-NR Vulkan finished picture: {} frames, {}x{}", frames, screen.size.width,
                     screen.size.height);
        return true;
    }
};
FinishedVk::FinishedVk(DlssNr_Vk& shader, VkDevice device, VkPhysicalDevice physical)
    : impl(std::make_unique<Impl>(shader, device, physical))
{
    std::lock_guard lock(finishedMutex);
    owners.push_back(this);
}
FinishedVk::~FinishedVk()
{
    std::lock_guard lock(finishedMutex);
    std::erase(owners, this);
    impl.reset();
}
void FinishedVk::Capture(VkCommandBuffer cmd, const VkImageInfo& depth, const VkImageInfo& motion,
                         const DlssNrFrameInfo_Vk& frame, VkInstance instance)
{
    std::lock_guard lock(finishedMutex);
    impl->Capture(cmd, depth, motion, frame, instance);
}
void FinishedVk::Submitted(VkQueue queue, VkCommandBuffer cmd)
{
    for (auto& s : impl->slots)
        if (s.pending && !s.submitted && s.producer == cmd)
        {
            s.submitted = true;
            s.queue = queue;
        }
}
void FinishedVk::Reset(VkCommandBuffer cmd)
{
    for (auto& s : impl->slots)
        if (s.pending && !s.submitted && s.producer == cmd)
            s.pending = false;
}
void FinishedVk::ResetPool(VkCommandPool pool)
{
    for (auto& s : impl->slots)
        if (s.pending && !s.submitted && s.producerPool == pool)
            s.pending = false;
}
bool FinishedVk::Present(VkQueue queue, VkPresentInfoKHR* present) { return impl->Present(queue, present); }
void FinishedVkSwapchain(VkDevice device, VkSwapchainKHR swapchain, const VkSwapchainCreateInfoKHR& info)
{
    std::lock_guard lock(finishedMutex);
    screen = {};
    screen.device = device;
    screen.handle = swapchain;
    screen.format = info.imageFormat;
    screen.space = info.imageColorSpace;
    screen.size = info.imageExtent;
    screen.usage = info.imageUsage;
    uint32_t count = 0;
    if (vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr) != VK_SUCCESS)
        return;
    screen.images.resize(count);
    vkGetSwapchainImagesKHR(device, swapchain, &count, screen.images.data());
}
void FinishedVkSubmitted(VkQueue queue, VkCommandBuffer cmd)
{
    std::lock_guard lock(finishedMutex);
    for (auto* owner : owners)
        owner->Submitted(queue, cmd);
}
void FinishedVkReset(VkCommandBuffer cmd)
{
    std::lock_guard lock(finishedMutex);
    for (auto* owner : owners)
        owner->Reset(cmd);
}
void FinishedVkResetPool(VkCommandPool pool)
{
    std::lock_guard lock(finishedMutex);
    for (auto* owner : owners)
        owner->ResetPool(pool);
}
void FinishedVkPresent(VkQueue queue, VkPresentInfoKHR* present)
{
    std::lock_guard lock(finishedMutex);
    for (auto* owner : owners)
        if (owner->Present(queue, present))
            break;
    ++presentEpoch;
}
std::string FinishedVkStatus()
{
    std::lock_guard lock(finishedMutex);
    return status;
}
} // namespace DlssNr
