#include "pch.h"
#include "Hudfix_Vk.h"

#include <State.h>
#include <Config.h>

#include <hooks/Hook_Utils.h>
#include <hooks/Vulkan_Hooks.h>
#include <framegen/IFGFeature_Vk.h>

#include <detours/detours.h>
#include <ankerl/unordered_dense.h>

#include <shared_mutex>

namespace
{
struct ImageInfo
{
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent {};
    bool swapchain = false;
};

struct AttachmentOps
{
    VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

// A color attachment of a render pass instance with its layouts before and after the instance
struct Attachment
{
    VkImage image = VK_NULL_HANDLE;
    bool load = false;
    bool store = false;
    VkImageLayout beginLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout endLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct StoredImage
{
    VkImage image = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

// Image of a descriptor set slot, image is null when the slot got an untracked view.
// Storage images are hudless candidates, sampled ones are only traced as inputs of the swapchain's final write.
struct DescriptorImage
{
    uint32_t binding = 0;
    uint32_t element = 0;
    VkImage image = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool storage = true;
};

struct SetUpdate
{
    VkDescriptorSet set = VK_NULL_HANDLE;
    DescriptorImage image;
};

struct TemplateEntry
{
    uint32_t binding = 0;
    uint32_t arrayElement = 0;
    uint32_t count = 0;
    size_t offset = 0;
    size_t stride = 0;
    bool storage = true;
};

// Image entries of a descriptor update template
struct TemplateInfo
{
    std::vector<TemplateEntry> entries;
    bool computePush = false;
};

// Only the first sets are tracked, games rarely bind more for compute
constexpr uint32_t MaxComputeSets = 8;

// Compute descriptor sets of a command buffer being recorded
struct ComputeBindings
{
    std::array<VkDescriptorSet, MaxComputeSets> sets {};
    std::array<std::vector<DescriptorImage>, MaxComputeSets> pushed;
};

// Bindless tables are large arrays, like DX12's descriptor table budget only their first elements are candidates
constexpr uint32_t MaxArrayElements = 16;
} // namespace

// The search for a frame's hudless runs from its upscale until the hudless is found or the frame is presented
static std::atomic<uint64_t> _upscaleFrame { 0 };
static std::atomic<uint64_t> _searchDoneFrame { 0 };

// Last frame the hudless was rebuilt from the game's UI texture
static std::atomic<uint64_t> _hudlessFromUiFrame { 0 };

static std::mutex _captureMutex;
static int _candidateCount = 0;

static std::shared_mutex _trackMutex;
static ankerl::unordered_dense::map<VkImage, ImageInfo> _images;
static ankerl::unordered_dense::map<VkImageView, VkImage> _views;
static ankerl::unordered_dense::map<VkFramebuffer, std::vector<VkImage>> _framebuffers;
static ankerl::unordered_dense::map<VkRenderPass, std::vector<AttachmentOps>> _renderPasses;

// Attachments to check when the command buffer's current render pass instance ends
static std::mutex _pendingMutex;
static ankerl::unordered_dense::map<VkCommandBuffer, std::vector<StoredImage>> _pendingEnds;

static std::shared_mutex _descriptorMutex;
static ankerl::unordered_dense::map<VkDescriptorSet, std::vector<DescriptorImage>> _setImages;
static ankerl::unordered_dense::map<VkDescriptorUpdateTemplate, TemplateInfo> _templates;

// Command buffers are recorded by one thread at a time, the map only guards the lookup
static std::shared_mutex _computeBindingsMutex;
static ankerl::unordered_dense::map<VkCommandBuffer, std::unique_ptr<ComputeBindings>> _computeBindings;

static PFN_vkCreateImage o_CreateImage = nullptr;
static PFN_vkDestroyImage o_DestroyImage = nullptr;
static PFN_vkCreateImageView o_CreateImageView = nullptr;
static PFN_vkDestroyImageView o_DestroyImageView = nullptr;
static PFN_vkCreateFramebuffer o_CreateFramebuffer = nullptr;
static PFN_vkDestroyFramebuffer o_DestroyFramebuffer = nullptr;
static PFN_vkCreateRenderPass o_CreateRenderPass = nullptr;
static PFN_vkCreateRenderPass2 o_CreateRenderPass2 = nullptr;
static PFN_vkCreateRenderPass2 o_CreateRenderPass2KHR = nullptr;
static PFN_vkDestroyRenderPass o_DestroyRenderPass = nullptr;
static PFN_vkCmdBeginRenderPass o_CmdBeginRenderPass = nullptr;
static PFN_vkCmdBeginRenderPass2 o_CmdBeginRenderPass2 = nullptr;
static PFN_vkCmdBeginRenderPass2 o_CmdBeginRenderPass2KHR = nullptr;
static PFN_vkCmdEndRenderPass o_CmdEndRenderPass = nullptr;
static PFN_vkCmdEndRenderPass2 o_CmdEndRenderPass2 = nullptr;
static PFN_vkCmdEndRenderPass2 o_CmdEndRenderPass2KHR = nullptr;
static PFN_vkCmdBeginRendering o_CmdBeginRendering = nullptr;
static PFN_vkCmdBeginRendering o_CmdBeginRenderingKHR = nullptr;
static PFN_vkCmdEndRendering o_CmdEndRendering = nullptr;
static PFN_vkCmdEndRendering o_CmdEndRenderingKHR = nullptr;
static PFN_vkAllocateDescriptorSets o_AllocateDescriptorSets = nullptr;
static PFN_vkUpdateDescriptorSets o_UpdateDescriptorSets = nullptr;
static PFN_vkCreateDescriptorUpdateTemplate o_CreateDescriptorUpdateTemplate = nullptr;
static PFN_vkCreateDescriptorUpdateTemplate o_CreateDescriptorUpdateTemplateKHR = nullptr;
static PFN_vkDestroyDescriptorUpdateTemplate o_DestroyDescriptorUpdateTemplate = nullptr;
static PFN_vkDestroyDescriptorUpdateTemplate o_DestroyDescriptorUpdateTemplateKHR = nullptr;
static PFN_vkUpdateDescriptorSetWithTemplate o_UpdateDescriptorSetWithTemplate = nullptr;
static PFN_vkUpdateDescriptorSetWithTemplate o_UpdateDescriptorSetWithTemplateKHR = nullptr;
static PFN_vkBeginCommandBuffer o_BeginCommandBuffer = nullptr;
static PFN_vkCmdBindDescriptorSets o_CmdBindDescriptorSets = nullptr;
static PFN_vkCmdPushDescriptorSet o_CmdPushDescriptorSet = nullptr;
static PFN_vkCmdPushDescriptorSet o_CmdPushDescriptorSetKHR = nullptr;
static PFN_vkCmdPushDescriptorSetWithTemplate o_CmdPushDescriptorSetWithTemplate = nullptr;
static PFN_vkCmdPushDescriptorSetWithTemplate o_CmdPushDescriptorSetWithTemplateKHR = nullptr;
static PFN_vkCmdDispatch o_CmdDispatch = nullptr;
static PFN_vkCmdDispatchIndirect o_CmdDispatchIndirect = nullptr;
static PFN_vkCmdDispatchBase o_CmdDispatchBase = nullptr;
static PFN_vkCmdDispatchBase o_CmdDispatchBaseKHR = nullptr;

template <typename T> static const T* FindInChain(const void* next, VkStructureType type)
{
    for (auto item = (const VkBaseInStructure*) next; item != nullptr; item = item->pNext)
    {
        if (item->sType == type)
            return (const T*) item;
    }

    return nullptr;
}

static bool HudfixEnabled()
{
    return State::Instance().activeFgInput == FGInput::Upscaler && Config::Instance()->FGHUDFix.value_or_default();
}

static bool IsFGRunning()
{
    if (!HudfixEnabled() || !Config::Instance()->IsFGEnabled())
        return false;

    auto fg = State::Instance().currentVkFG;
    return fg != nullptr && fg->IsActive() && !fg->IsPaused();
}

static bool IsSearching()
{
    const auto upscaleFrame = _upscaleFrame.load(std::memory_order_acquire);
    if (upscaleFrame <= _searchDoneFrame.load(std::memory_order_acquire))
        return false;

    // The UI texture the hudless got rebuilt from last frame will be there again
    if (auto uiFrame = _hudlessFromUiFrame.load(std::memory_order_acquire); uiFrame != 0 && upscaleFrame <= uiFrame + 1)
    {
        return false;
    }

    return IsFGRunning();
}

static ComputeBindings* FindComputeBindings(VkCommandBuffer cmdBuffer, bool create);

void Hudfix_Vk::UpscaleEnd(VkCommandBuffer cmdBuffer)
{
    // Sets bound by the upscaler's dispatches would make its internal images candidates of the game's next dispatch.
    // Games rebind their sets after the upscale.
    if (auto bindings = FindComputeBindings(cmdBuffer, false))
        *bindings = {};

    std::scoped_lock lock(_captureMutex);
    _candidateCount = 0;
    _upscaleFrame.fetch_add(1, std::memory_order_release);
}

void Hudfix_Vk::PresentStart() { _searchDoneFrame.store(_upscaleFrame.load(std::memory_order_acquire)); }

// The image as it is now, FG copies it into the command buffer
static VkFGResource CopiedResource(FG_ResourceType type, VkCommandBuffer cmdBuffer, VkImage image, VkFormat format,
                                   VkExtent2D extent, VkImageLayout layout)
{
    VkFGResource resource {};
    resource.type = type;
    resource.image = image;
    resource.format = format;
    resource.layout = layout;
    resource.imageWidth = extent.width;
    resource.imageHeight = extent.height;
    resource.width = extent.width;
    resource.height = extent.height;
    resource.cmdBuffer = cmdBuffer;
    resource.validity = FG_ResourceValidity::ValidNow;
    return resource;
}

void Hudfix_Vk::CheckImage(VkCommandBuffer cmdBuffer, VkImage image, VkFormat format, VkExtent2D extent,
                           VkImageLayout layout)
{
    if (!IsSearching() || image == VK_NULL_HANDLE || layout == VK_IMAGE_LAYOUT_UNDEFINED ||
        layout == VK_IMAGE_LAYOUT_PREINITIALIZED)
    {
        return;
    }

    auto fg = State::Instance().currentVkFG;
    auto swapchainExtent = fg->SwapchainExtent();

    if (extent.width != swapchainExtent.width || extent.height != swapchainExtent.height)
        return;

    // The FG copy blits other formats to the swapchain's
    if (!Config::Instance()->FGHUDFixExtended.value_or_default() &&
        IFGFeature_Vk::WithoutSrgb(format) != IFGFeature_Vk::WithoutSrgb(fg->SwapchainFormat()))
    {
        return;
    }

    std::scoped_lock lock(_captureMutex);

    auto frame = _upscaleFrame.load(std::memory_order_acquire);
    if (_searchDoneFrame.load(std::memory_order_acquire) >= frame)
        return;

    ++_candidateCount;
    LOG_TRACE("Candidate: {:X}, format: {}, layout: {}, frame: {}, count: {}", (size_t) image, (UINT) format,
              (UINT) layout, frame, _candidateCount);

    if (_candidateCount < Config::Instance()->FGHUDLimit.value_or_default())
        return;

    auto resource = CopiedResource(FG_ResourceType::HudlessColor, cmdBuffer, image, format, extent, layout);

    if (fg->SetResource(&resource))
        LOG_DEBUG("Hudless: {:X}, frame: {}, candidate: {}", (size_t) image, frame, _candidateCount);

    _searchDoneFrame.store(frame, std::memory_order_release);
}

void Hudfix_Vk::AddSwapchainImages(const VkImage* images, uint32_t count)
{
    auto fg = State::Instance().currentVkFG;
    if (fg == nullptr || o_CreateImage == nullptr)
        return;

    std::unique_lock lock(_trackMutex);

    for (uint32_t i = 0; i < count; i++)
        _images[images[i]] = { fg->SwapchainFormat(), fg->SwapchainExtent(), true };
}

static void CheckTrackedImage(VkCommandBuffer cmdBuffer, VkImage image, VkImageLayout layout)
{
    ImageInfo info;
    {
        std::shared_lock lock(_trackMutex);

        auto it = _images.find(image);
        if (it == _images.end())
            return;

        info = it->second;
    }

    Hudfix_Vk::CheckImage(cmdBuffer, image, info.format, info.extent, layout);
}

// Copies can't be recorded inside a render pass instance, so attachments are checked right before and after one
static void BeginPass(VkCommandBuffer cmdBuffer, const std::vector<Attachment>& attachments)
{
    std::vector<StoredImage> stored;

    for (auto& attachment : attachments)
    {
        if (attachment.load)
            CheckTrackedImage(cmdBuffer, attachment.image, attachment.beginLayout);

        if (attachment.store)
            stored.push_back({ attachment.image, attachment.endLayout });
    }

    std::scoped_lock lock(_pendingMutex);

    if (stored.empty())
        _pendingEnds.erase(cmdBuffer);
    else
        _pendingEnds[cmdBuffer] = std::move(stored);
}

static void EndPass(VkCommandBuffer cmdBuffer)
{
    std::vector<StoredImage> stored;
    {
        std::scoped_lock lock(_pendingMutex);

        auto it = _pendingEnds.find(cmdBuffer);
        if (it == _pendingEnds.end())
            return;

        stored = std::move(it->second);
        _pendingEnds.erase(it);
    }

    for (auto& image : stored)
        CheckTrackedImage(cmdBuffer, image.image, image.layout);
}

static std::vector<Attachment> RenderPassAttachments(const VkRenderPassBeginInfo* beginInfo)
{
    std::vector<Attachment> attachments;
    std::shared_lock lock(_trackMutex);

    auto pass = _renderPasses.find(beginInfo->renderPass);
    if (pass == _renderPasses.end())
        return attachments;

    // Imageless framebuffers get their views at begin
    std::vector<VkImage> images;
    if (auto viewsInfo = FindInChain<VkRenderPassAttachmentBeginInfo>(
            beginInfo->pNext, VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO))
    {
        for (uint32_t i = 0; i < viewsInfo->attachmentCount; i++)
        {
            auto view = _views.find(viewsInfo->pAttachments[i]);
            images.push_back(view != _views.end() ? view->second : VK_NULL_HANDLE);
        }
    }
    else if (auto framebuffer = _framebuffers.find(beginInfo->framebuffer); framebuffer != _framebuffers.end())
    {
        images = framebuffer->second;
    }

    auto& ops = pass->second;
    for (size_t i = 0; i < ops.size() && i < images.size(); i++)
    {
        attachments.push_back({ images[i], ops[i].loadOp == VK_ATTACHMENT_LOAD_OP_LOAD,
                                ops[i].storeOp == VK_ATTACHMENT_STORE_OP_STORE, ops[i].initialLayout,
                                ops[i].finalLayout });
    }

    return attachments;
}

static std::vector<Attachment> RenderingAttachments(const VkRenderingInfo* renderingInfo)
{
    std::vector<Attachment> attachments;
    std::shared_lock lock(_trackMutex);

    // Nothing can be recorded between a suspended render pass instance and the one resuming it
    const bool resuming = renderingInfo->flags & VK_RENDERING_RESUMING_BIT;
    const bool suspending = renderingInfo->flags & VK_RENDERING_SUSPENDING_BIT;

    for (uint32_t i = 0; i < renderingInfo->colorAttachmentCount; i++)
    {
        auto& color = renderingInfo->pColorAttachments[i];

        auto view = _views.find(color.imageView);
        if (view == _views.end())
            continue;

        attachments.push_back({ view->second, color.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD && !resuming,
                                color.storeOp == VK_ATTACHMENT_STORE_OP_STORE && !suspending, color.imageLayout,
                                color.imageLayout });
    }

    return attachments;
}

template <typename T> static std::vector<AttachmentOps> AttachmentOpsOf(uint32_t count, const T* descriptions)
{
    std::vector<AttachmentOps> ops(count);

    for (uint32_t i = 0; i < count; i++)
    {
        ops[i] = { descriptions[i].loadOp, descriptions[i].storeOp, descriptions[i].initialLayout,
                   descriptions[i].finalLayout };
    }

    return ops;
}

static bool IsImageDescriptor(VkDescriptorType type)
{
    return type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
           type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
}

// Caller holds _trackMutex
static VkImage ViewImageLocked(VkImageView view)
{
    auto it = _views.find(view);
    return it != _views.end() ? it->second : VK_NULL_HANDLE;
}

static void ApplyUpdate(std::vector<DescriptorImage>& images, const DescriptorImage& update)
{
    auto it = std::ranges::find_if(images, [&](const DescriptorImage& image)
                                   { return image.binding == update.binding && image.element == update.element; });

    if (update.image == VK_NULL_HANDLE)
    {
        if (it != images.end())
            images.erase(it);
    }
    else if (it != images.end())
    {
        *it = update;
    }
    else
    {
        images.push_back(update);
    }
}

static void CollectWrites(uint32_t count, const VkWriteDescriptorSet* writes, std::vector<SetUpdate>& updates)
{
    std::shared_lock lock(_trackMutex);

    for (uint32_t w = 0; w < count; w++)
    {
        auto& write = writes[w];

        if (!IsImageDescriptor(write.descriptorType) || write.pImageInfo == nullptr)
            continue;

        const bool storage = write.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

        for (uint32_t i = 0; i < write.descriptorCount && write.dstArrayElement + i < MaxArrayElements; i++)
        {
            auto& info = write.pImageInfo[i];
            updates.push_back({ write.dstSet,
                                { write.dstBinding, write.dstArrayElement + i, ViewImageLocked(info.imageView),
                                  info.imageLayout, storage } });
        }
    }
}

static void CollectTemplate(const TemplateInfo& info, VkDescriptorSet set, const void* data,
                            std::vector<SetUpdate>& updates)
{
    std::shared_lock lock(_trackMutex);

    for (auto& entry : info.entries)
    {
        for (uint32_t i = 0; i < entry.count && entry.arrayElement + i < MaxArrayElements; i++)
        {
            auto image = (const VkDescriptorImageInfo*) ((const uint8_t*) data + entry.offset + i * entry.stride);
            updates.push_back({ set,
                                { entry.binding, entry.arrayElement + i, ViewImageLocked(image->imageView),
                                  image->imageLayout, entry.storage } });
        }
    }
}

static void ApplySetUpdates(const std::vector<SetUpdate>& updates)
{
    if (updates.empty())
        return;

    std::unique_lock lock(_descriptorMutex);

    for (auto& update : updates)
    {
        if (update.image.image != VK_NULL_HANDLE)
            ApplyUpdate(_setImages[update.set], update.image);
        else if (auto it = _setImages.find(update.set); it != _setImages.end())
            ApplyUpdate(it->second, update.image);
    }
}

static std::optional<TemplateInfo> FindTemplate(VkDescriptorUpdateTemplate descriptorTemplate)
{
    std::shared_lock lock(_descriptorMutex);

    auto it = _templates.find(descriptorTemplate);
    if (it == _templates.end())
        return std::nullopt;

    return it->second;
}

static ComputeBindings* FindComputeBindings(VkCommandBuffer cmdBuffer, bool create)
{
    {
        std::shared_lock lock(_computeBindingsMutex);

        if (auto it = _computeBindings.find(cmdBuffer); it != _computeBindings.end())
            return it->second.get();
    }

    if (!create)
        return nullptr;

    std::unique_lock lock(_computeBindingsMutex);

    auto& bindings = _computeBindings[cmdBuffer];
    if (bindings == nullptr)
        bindings = std::make_unique<ComputeBindings>();

    return bindings.get();
}

// Binding state is only needed while HUDFix can search
static bool TracksBindings(VkPipelineBindPoint bindPoint)
{
    return bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE && HudfixEnabled();
}

static void PushImages(VkCommandBuffer cmdBuffer, uint32_t set, const std::vector<SetUpdate>& updates)
{
    if (set >= MaxComputeSets)
        return;

    auto bindings = FindComputeBindings(cmdBuffer, true);
    bindings->sets[set] = VK_NULL_HANDLE;

    for (auto& update : updates)
        ApplyUpdate(bindings->pushed[set], update.image);
}

static bool IsUiFormat(VkFormat format)
{
    return format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB ||
           format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
}

// The dispatch just blended the UI texture on the frame. FG gets the UI and, by removing it again, the hudless.
static void UseUiTexture(VkCommandBuffer cmdBuffer, const DescriptorImage& ui, const ImageInfo& uiInfo,
                         const DescriptorImage& frame, const ImageInfo& frameInfo)
{
    auto& state = State::Instance();
    auto config = Config::Instance();
    auto fg = state.currentVkFG;

    auto uiResource =
        CopiedResource(FG_ResourceType::UIColor, cmdBuffer, ui.image, uiInfo.format, uiInfo.extent, ui.layout);

    if (!fg->SetResource(&uiResource))
        return;

    const auto upscaleFrame = _upscaleFrame.load(std::memory_order_acquire);
    LOG_DEBUG("UI: {:X}, frame: {}", (size_t) ui.image, upscaleFrame);

    // FSR-FG has no UI input, the rebuilt hudless is its only way to know about the UI
    if (!config->FGHUDFixUIUnblend.value_or(state.activeFgOutput != FGOutput::DLSSG))
        return;

    auto hudless = CopiedResource(FG_ResourceType::HudlessColor, cmdBuffer, frame.image, frameInfo.format,
                                  frameInfo.extent, frame.layout);

    if (fg->SetResource(&hudless))
    {
        fg->RemoveUiFromHudless(cmdBuffer, config->FGUIPremultipliedAlpha.value_or_default());
        _hudlessFromUiFrame.store(upscaleFrame, std::memory_order_release);
    }
}

// Games compositing their UI from a separate texture read it in the dispatch writing the swapchain.
// Its inputs are traced to find such textures.
static void CheckSwapchainWrite(VkCommandBuffer cmdBuffer, const std::vector<DescriptorImage>& images)
{
    const bool useUi = Config::Instance()->FGHUDFixUITexture.value_or_default();

    std::optional<std::pair<DescriptorImage, ImageInfo>> ui;
    std::pair<DescriptorImage, ImageInfo> frame;
    {
        std::shared_lock lock(_trackMutex);

        auto output = std::ranges::find_if(images,
                                           [](const DescriptorImage& image)
                                           {
                                               auto it = _images.find(image.image);
                                               return image.storage && it != _images.end() && it->second.swapchain;
                                           });

        if (output == images.end())
            return;

        frame = { *output, _images.find(output->image)->second };

        for (auto& image : images)
        {
            auto it = _images.find(image.image);
            if (image.storage || it == _images.end())
                continue;

            auto& info = it->second;
            LOG_TRACE("Swapchain: {:X}, input: {:X}, binding: {}, element: {}, format: {}, extent: {}x{}",
                      (size_t) output->image, (size_t) image.image, image.binding, image.element, (UINT) info.format,
                      info.extent.width, info.extent.height);

            if (useUi && !ui && IsUiFormat(info.format) && info.extent.width == frame.second.extent.width &&
                info.extent.height == frame.second.extent.height)
            {
                ui = { image, info };
            }
        }
    }

    if (ui)
        UseUiTexture(cmdBuffer, ui->first, ui->second, frame.first, frame.second);
}

// Compute shaders write their outputs to storage images, copies after the dispatch see what it wrote
static void CheckDispatch(VkCommandBuffer cmdBuffer)
{
    // Bindings are only tracked with HUDFix enabled
    auto bindings = FindComputeBindings(cmdBuffer, false);
    if (bindings == nullptr || !IsFGRunning())
        return;

    std::vector<DescriptorImage> images;
    auto add = [&images](const DescriptorImage& image)
    {
        if (image.image != VK_NULL_HANDLE &&
            std::ranges::none_of(images, [&](const DescriptorImage& added)
                                 { return added.image == image.image && added.storage == image.storage; }))
        {
            images.push_back(image);
        }
    };

    {
        std::shared_lock lock(_descriptorMutex);

        for (uint32_t i = 0; i < MaxComputeSets; i++)
        {
            if (auto it = _setImages.find(bindings->sets[i]); it != _setImages.end())
                std::ranges::for_each(it->second, add);

            std::ranges::for_each(bindings->pushed[i], add);
        }
    }

    CheckSwapchainWrite(cmdBuffer, images);

    if (!IsSearching())
        return;

    for (auto& image : images)
    {
        if (image.storage)
            CheckTrackedImage(cmdBuffer, image.image, image.layout);
    }
}

#pragma region Hooks

VALIDATE_HOOK(hkCreateImage, PFN_vkCreateImage)
static VkResult hkCreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo,
                              const VkAllocationCallbacks* pAllocator, VkImage* pImage)
{
    auto result = o_CreateImage(device, pCreateInfo, pAllocator, pImage);

    // Hudless candidates are rendered to or written by compute shaders
    const VkImageUsageFlags candidateUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;

    if (result == VK_SUCCESS && pCreateInfo->imageType == VK_IMAGE_TYPE_2D &&
        pCreateInfo->samples == VK_SAMPLE_COUNT_1_BIT && (pCreateInfo->usage & candidateUsage))
    {
        std::unique_lock lock(_trackMutex);
        _images[*pImage] = { pCreateInfo->format, { pCreateInfo->extent.width, pCreateInfo->extent.height } };
    }

    return result;
}

VALIDATE_HOOK(hkDestroyImage, PFN_vkDestroyImage)
static void hkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator)
{
    {
        std::unique_lock lock(_trackMutex);
        _images.erase(image);
    }

    o_DestroyImage(device, image, pAllocator);
}

VALIDATE_HOOK(hkCreateImageView, PFN_vkCreateImageView)
static VkResult hkCreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo,
                                  const VkAllocationCallbacks* pAllocator, VkImageView* pView)
{
    auto result = o_CreateImageView(device, pCreateInfo, pAllocator, pView);

    // Other mips are smaller than the image
    if (result == VK_SUCCESS && pCreateInfo->subresourceRange.baseMipLevel == 0)
    {
        std::unique_lock lock(_trackMutex);

        if (_images.contains(pCreateInfo->image))
            _views[*pView] = pCreateInfo->image;
    }

    return result;
}

VALIDATE_HOOK(hkDestroyImageView, PFN_vkDestroyImageView)
static void hkDestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator)
{
    {
        std::unique_lock lock(_trackMutex);
        _views.erase(imageView);
    }

    o_DestroyImageView(device, imageView, pAllocator);
}

VALIDATE_HOOK(hkCreateFramebuffer, PFN_vkCreateFramebuffer)
static VkResult hkCreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo* pCreateInfo,
                                    const VkAllocationCallbacks* pAllocator, VkFramebuffer* pFramebuffer)
{
    auto result = o_CreateFramebuffer(device, pCreateInfo, pAllocator, pFramebuffer);

    if (result == VK_SUCCESS && (pCreateInfo->flags & VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT) == 0)
    {
        std::unique_lock lock(_trackMutex);

        std::vector<VkImage> images(pCreateInfo->attachmentCount, VK_NULL_HANDLE);
        for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++)
        {
            if (auto view = _views.find(pCreateInfo->pAttachments[i]); view != _views.end())
                images[i] = view->second;
        }

        _framebuffers[*pFramebuffer] = std::move(images);
    }

    return result;
}

VALIDATE_HOOK(hkDestroyFramebuffer, PFN_vkDestroyFramebuffer)
static void hkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* pAllocator)
{
    {
        std::unique_lock lock(_trackMutex);
        _framebuffers.erase(framebuffer);
    }

    o_DestroyFramebuffer(device, framebuffer, pAllocator);
}

VALIDATE_HOOK(hkCreateRenderPass, PFN_vkCreateRenderPass)
static VkResult hkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo,
                                   const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass)
{
    auto result = o_CreateRenderPass(device, pCreateInfo, pAllocator, pRenderPass);

    if (result == VK_SUCCESS)
    {
        std::unique_lock lock(_trackMutex);
        _renderPasses[*pRenderPass] = AttachmentOpsOf(pCreateInfo->attachmentCount, pCreateInfo->pAttachments);
    }

    return result;
}

// Drivers can return different functions for the core and KHR names, these hooks take the original to call
template <PFN_vkCreateRenderPass2& Original>
static VkResult hkCreateRenderPass2(VkDevice device, const VkRenderPassCreateInfo2* pCreateInfo,
                                    const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass)
{
    auto result = Original(device, pCreateInfo, pAllocator, pRenderPass);

    if (result == VK_SUCCESS)
    {
        std::unique_lock lock(_trackMutex);
        _renderPasses[*pRenderPass] = AttachmentOpsOf(pCreateInfo->attachmentCount, pCreateInfo->pAttachments);
    }

    return result;
}

VALIDATE_HOOK(hkDestroyRenderPass, PFN_vkDestroyRenderPass)
static void hkDestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks* pAllocator)
{
    {
        std::unique_lock lock(_trackMutex);
        _renderPasses.erase(renderPass);
    }

    o_DestroyRenderPass(device, renderPass, pAllocator);
}

VALIDATE_HOOK(hkCmdBeginRenderPass, PFN_vkCmdBeginRenderPass)
static void hkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin,
                                 VkSubpassContents contents)
{
    BeginPass(commandBuffer, IsSearching() ? RenderPassAttachments(pRenderPassBegin) : std::vector<Attachment> {});
    o_CmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
}

template <PFN_vkCmdBeginRenderPass2& Original>
static void hkCmdBeginRenderPass2(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin,
                                  const VkSubpassBeginInfo* pSubpassBeginInfo)
{
    BeginPass(commandBuffer, IsSearching() ? RenderPassAttachments(pRenderPassBegin) : std::vector<Attachment> {});
    Original(commandBuffer, pRenderPassBegin, pSubpassBeginInfo);
}

VALIDATE_HOOK(hkCmdEndRenderPass, PFN_vkCmdEndRenderPass)
static void hkCmdEndRenderPass(VkCommandBuffer commandBuffer)
{
    o_CmdEndRenderPass(commandBuffer);
    EndPass(commandBuffer);
}

template <PFN_vkCmdEndRenderPass2& Original>
static void hkCmdEndRenderPass2(VkCommandBuffer commandBuffer, const VkSubpassEndInfo* pSubpassEndInfo)
{
    Original(commandBuffer, pSubpassEndInfo);
    EndPass(commandBuffer);
}

template <PFN_vkCmdBeginRendering& Original>
static void hkCmdBeginRendering(VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo)
{
    BeginPass(commandBuffer, IsSearching() ? RenderingAttachments(pRenderingInfo) : std::vector<Attachment> {});
    Original(commandBuffer, pRenderingInfo);
}

template <PFN_vkCmdEndRendering& Original> static void hkCmdEndRendering(VkCommandBuffer commandBuffer)
{
    Original(commandBuffer);
    EndPass(commandBuffer);
}

VALIDATE_MEMBER_HOOK(hkCreateRenderPass2<o_CreateRenderPass2>, PFN_vkCreateRenderPass2)
VALIDATE_MEMBER_HOOK(hkCmdBeginRenderPass2<o_CmdBeginRenderPass2>, PFN_vkCmdBeginRenderPass2)
VALIDATE_MEMBER_HOOK(hkCmdEndRenderPass2<o_CmdEndRenderPass2>, PFN_vkCmdEndRenderPass2)
VALIDATE_MEMBER_HOOK(hkCmdBeginRendering<o_CmdBeginRendering>, PFN_vkCmdBeginRendering)
VALIDATE_MEMBER_HOOK(hkCmdEndRendering<o_CmdEndRendering>, PFN_vkCmdEndRendering)

VALIDATE_HOOK(hkAllocateDescriptorSets, PFN_vkAllocateDescriptorSets)
static VkResult hkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo,
                                         VkDescriptorSet* pDescriptorSets)
{
    auto result = o_AllocateDescriptorSets(device, pAllocateInfo, pDescriptorSets);

    // Freed sets aren't tracked, their handles can come back here with a new set
    if (result == VK_SUCCESS)
    {
        std::scoped_lock lock(_descriptorMutex);

        for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; i++)
            _setImages.erase(pDescriptorSets[i]);
    }

    return result;
}

VALIDATE_HOOK(hkUpdateDescriptorSets, PFN_vkUpdateDescriptorSets)
static void hkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount,
                                   const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount,
                                   const VkCopyDescriptorSet* pDescriptorCopies)
{
    o_UpdateDescriptorSets(device, descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies);

    // ponytail: descriptor copies aren't followed, track them if a game builds its compute sets by copying
    std::vector<SetUpdate> updates;
    CollectWrites(descriptorWriteCount, pDescriptorWrites, updates);
    ApplySetUpdates(updates);
}

template <PFN_vkCreateDescriptorUpdateTemplate& Original>
static VkResult hkCreateDescriptorUpdateTemplate(VkDevice device,
                                                 const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo,
                                                 const VkAllocationCallbacks* pAllocator,
                                                 VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate)
{
    auto result = Original(device, pCreateInfo, pAllocator, pDescriptorUpdateTemplate);

    if (result != VK_SUCCESS)
        return result;

    TemplateInfo info;
    info.computePush = pCreateInfo->templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR &&
                       pCreateInfo->pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE;

    for (uint32_t i = 0; i < pCreateInfo->descriptorUpdateEntryCount; i++)
    {
        auto& entry = pCreateInfo->pDescriptorUpdateEntries[i];

        if (IsImageDescriptor(entry.descriptorType))
        {
            info.entries.push_back({ entry.dstBinding, entry.dstArrayElement, entry.descriptorCount, entry.offset,
                                     entry.stride, entry.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE });
        }
    }

    if (!info.entries.empty())
    {
        std::scoped_lock lock(_descriptorMutex);
        _templates[*pDescriptorUpdateTemplate] = std::move(info);
    }

    return result;
}

template <PFN_vkDestroyDescriptorUpdateTemplate& Original>
static void hkDestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                              const VkAllocationCallbacks* pAllocator)
{
    {
        std::scoped_lock lock(_descriptorMutex);
        _templates.erase(descriptorUpdateTemplate);
    }

    Original(device, descriptorUpdateTemplate, pAllocator);
}

template <PFN_vkUpdateDescriptorSetWithTemplate& Original>
static void hkUpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet descriptorSet,
                                              VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void* pData)
{
    Original(device, descriptorSet, descriptorUpdateTemplate, pData);

    if (auto info = FindTemplate(descriptorUpdateTemplate))
    {
        std::vector<SetUpdate> updates;
        CollectTemplate(*info, descriptorSet, pData, updates);
        ApplySetUpdates(updates);
    }
}

VALIDATE_HOOK(hkBeginCommandBuffer, PFN_vkBeginCommandBuffer)
static VkResult hkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo)
{
    // Bindings don't carry over to a new recording
    if (auto bindings = FindComputeBindings(commandBuffer, false))
        *bindings = {};

    return o_BeginCommandBuffer(commandBuffer, pBeginInfo);
}

VALIDATE_HOOK(hkCmdBindDescriptorSets, PFN_vkCmdBindDescriptorSets)
static void hkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                                    VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount,
                                    const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount,
                                    const uint32_t* pDynamicOffsets)
{
    o_CmdBindDescriptorSets(commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount, pDescriptorSets,
                            dynamicOffsetCount, pDynamicOffsets);

    if (!TracksBindings(pipelineBindPoint))
        return;

    auto bindings = FindComputeBindings(commandBuffer, true);

    for (uint32_t i = 0; i < descriptorSetCount && firstSet + i < MaxComputeSets; i++)
    {
        bindings->sets[firstSet + i] = pDescriptorSets[i];
        bindings->pushed[firstSet + i].clear();
    }
}

template <PFN_vkCmdPushDescriptorSet& Original>
static void hkCmdPushDescriptorSet(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                                   VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount,
                                   const VkWriteDescriptorSet* pDescriptorWrites)
{
    Original(commandBuffer, pipelineBindPoint, layout, set, descriptorWriteCount, pDescriptorWrites);

    if (!TracksBindings(pipelineBindPoint))
        return;

    std::vector<SetUpdate> updates;
    CollectWrites(descriptorWriteCount, pDescriptorWrites, updates);
    PushImages(commandBuffer, set, updates);
}

template <PFN_vkCmdPushDescriptorSetWithTemplate& Original>
static void hkCmdPushDescriptorSetWithTemplate(VkCommandBuffer commandBuffer,
                                               VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                               VkPipelineLayout layout, uint32_t set, const void* pData)
{
    Original(commandBuffer, descriptorUpdateTemplate, layout, set, pData);

    if (!TracksBindings(VK_PIPELINE_BIND_POINT_COMPUTE))
        return;

    if (auto info = FindTemplate(descriptorUpdateTemplate); info && info->computePush)
    {
        std::vector<SetUpdate> updates;
        CollectTemplate(*info, VK_NULL_HANDLE, pData, updates);
        PushImages(commandBuffer, set, updates);
    }
}

VALIDATE_HOOK(hkCmdDispatch, PFN_vkCmdDispatch)
static void hkCmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY,
                          uint32_t groupCountZ)
{
    o_CmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
    CheckDispatch(commandBuffer);
}

VALIDATE_HOOK(hkCmdDispatchIndirect, PFN_vkCmdDispatchIndirect)
static void hkCmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset)
{
    o_CmdDispatchIndirect(commandBuffer, buffer, offset);
    CheckDispatch(commandBuffer);
}

template <PFN_vkCmdDispatchBase& Original>
static void hkCmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY,
                              uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    Original(commandBuffer, baseGroupX, baseGroupY, baseGroupZ, groupCountX, groupCountY, groupCountZ);
    CheckDispatch(commandBuffer);
}

VALIDATE_MEMBER_HOOK(hkCreateDescriptorUpdateTemplate<o_CreateDescriptorUpdateTemplate>,
                     PFN_vkCreateDescriptorUpdateTemplate)
VALIDATE_MEMBER_HOOK(hkDestroyDescriptorUpdateTemplate<o_DestroyDescriptorUpdateTemplate>,
                     PFN_vkDestroyDescriptorUpdateTemplate)
VALIDATE_MEMBER_HOOK(hkUpdateDescriptorSetWithTemplate<o_UpdateDescriptorSetWithTemplate>,
                     PFN_vkUpdateDescriptorSetWithTemplate)
VALIDATE_MEMBER_HOOK(hkCmdPushDescriptorSet<o_CmdPushDescriptorSet>, PFN_vkCmdPushDescriptorSet)
VALIDATE_MEMBER_HOOK(hkCmdPushDescriptorSetWithTemplate<o_CmdPushDescriptorSetWithTemplate>,
                     PFN_vkCmdPushDescriptorSetWithTemplate)
VALIDATE_MEMBER_HOOK(hkCmdDispatchBase<o_CmdDispatchBase>, PFN_vkCmdDispatchBase)

#pragma endregion

void Hudfix_Vk::HookDevice(VkDevice device)
{
    if (o_CreateImage != nullptr || Config::Instance()->FGDisableHUDFix.value_or_default())
        return;

    LOG_FUNC();

    // Games can load device functions from the instance. On Wine those are separate stubs for each name,
    // including KHR aliases the device itself doesn't hand out. Elsewhere the instance gives loader trampolines
    // that end in the already hooked driver functions.
    auto get = [device](const char* name)
    {
        auto& state = State::Instance();
        auto func = VulkanHooks::GetDeviceProcAddr(device, name);

        if (func == nullptr && state.isRunningOnLinux && state.VulkanInstance != VK_NULL_HANDLE)
            func = VulkanHooks::GetInstanceProcAddr(state.VulkanInstance, name);

        return func;
    };

    // A KHR alias only needs its own hook when it isn't the core function
    auto getAlias = [&get](const char* name, auto core)
    {
        auto alias = (decltype(core)) get(name);
        return alias != core ? alias : nullptr;
    };

    o_CreateImage = (PFN_vkCreateImage) get("vkCreateImage");
    o_DestroyImage = (PFN_vkDestroyImage) get("vkDestroyImage");
    o_CreateImageView = (PFN_vkCreateImageView) get("vkCreateImageView");
    o_DestroyImageView = (PFN_vkDestroyImageView) get("vkDestroyImageView");
    o_CreateFramebuffer = (PFN_vkCreateFramebuffer) get("vkCreateFramebuffer");
    o_DestroyFramebuffer = (PFN_vkDestroyFramebuffer) get("vkDestroyFramebuffer");
    o_CreateRenderPass = (PFN_vkCreateRenderPass) get("vkCreateRenderPass");
    o_CreateRenderPass2 = (PFN_vkCreateRenderPass2) get("vkCreateRenderPass2");
    o_CreateRenderPass2KHR = getAlias("vkCreateRenderPass2KHR", o_CreateRenderPass2);
    o_DestroyRenderPass = (PFN_vkDestroyRenderPass) get("vkDestroyRenderPass");
    o_CmdBeginRenderPass = (PFN_vkCmdBeginRenderPass) get("vkCmdBeginRenderPass");
    o_CmdBeginRenderPass2 = (PFN_vkCmdBeginRenderPass2) get("vkCmdBeginRenderPass2");
    o_CmdBeginRenderPass2KHR = getAlias("vkCmdBeginRenderPass2KHR", o_CmdBeginRenderPass2);
    o_CmdEndRenderPass = (PFN_vkCmdEndRenderPass) get("vkCmdEndRenderPass");
    o_CmdEndRenderPass2 = (PFN_vkCmdEndRenderPass2) get("vkCmdEndRenderPass2");
    o_CmdEndRenderPass2KHR = getAlias("vkCmdEndRenderPass2KHR", o_CmdEndRenderPass2);
    o_CmdBeginRendering = (PFN_vkCmdBeginRendering) get("vkCmdBeginRendering");
    o_CmdBeginRenderingKHR = getAlias("vkCmdBeginRenderingKHR", o_CmdBeginRendering);
    o_CmdEndRendering = (PFN_vkCmdEndRendering) get("vkCmdEndRendering");
    o_CmdEndRenderingKHR = getAlias("vkCmdEndRenderingKHR", o_CmdEndRendering);
    o_AllocateDescriptorSets = (PFN_vkAllocateDescriptorSets) get("vkAllocateDescriptorSets");
    o_UpdateDescriptorSets = (PFN_vkUpdateDescriptorSets) get("vkUpdateDescriptorSets");
    o_CreateDescriptorUpdateTemplate = (PFN_vkCreateDescriptorUpdateTemplate) get("vkCreateDescriptorUpdateTemplate");
    o_CreateDescriptorUpdateTemplateKHR =
        getAlias("vkCreateDescriptorUpdateTemplateKHR", o_CreateDescriptorUpdateTemplate);
    o_DestroyDescriptorUpdateTemplate =
        (PFN_vkDestroyDescriptorUpdateTemplate) get("vkDestroyDescriptorUpdateTemplate");
    o_DestroyDescriptorUpdateTemplateKHR =
        getAlias("vkDestroyDescriptorUpdateTemplateKHR", o_DestroyDescriptorUpdateTemplate);
    o_UpdateDescriptorSetWithTemplate =
        (PFN_vkUpdateDescriptorSetWithTemplate) get("vkUpdateDescriptorSetWithTemplate");
    o_UpdateDescriptorSetWithTemplateKHR =
        getAlias("vkUpdateDescriptorSetWithTemplateKHR", o_UpdateDescriptorSetWithTemplate);
    o_BeginCommandBuffer = (PFN_vkBeginCommandBuffer) get("vkBeginCommandBuffer");
    o_CmdBindDescriptorSets = (PFN_vkCmdBindDescriptorSets) get("vkCmdBindDescriptorSets");
    o_CmdPushDescriptorSet = (PFN_vkCmdPushDescriptorSet) get("vkCmdPushDescriptorSet");
    o_CmdPushDescriptorSetKHR = getAlias("vkCmdPushDescriptorSetKHR", o_CmdPushDescriptorSet);
    o_CmdPushDescriptorSetWithTemplate =
        (PFN_vkCmdPushDescriptorSetWithTemplate) get("vkCmdPushDescriptorSetWithTemplate");
    o_CmdPushDescriptorSetWithTemplateKHR =
        getAlias("vkCmdPushDescriptorSetWithTemplateKHR", o_CmdPushDescriptorSetWithTemplate);
    o_CmdDispatch = (PFN_vkCmdDispatch) get("vkCmdDispatch");
    o_CmdDispatchIndirect = (PFN_vkCmdDispatchIndirect) get("vkCmdDispatchIndirect");
    o_CmdDispatchBase = (PFN_vkCmdDispatchBase) get("vkCmdDispatchBase");
    o_CmdDispatchBaseKHR = getAlias("vkCmdDispatchBaseKHR", o_CmdDispatchBase);

    auto attach = [](const char* name, auto& original, auto hook)
    {
        if (original == nullptr)
            return;

        if (auto error = DetourAttach(&(PVOID&) original, (PVOID) hook); error != NO_ERROR)
            LOG_ERROR("Can't hook {}, error: {}", name, error);
    };

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    attach("vkCreateImage", o_CreateImage, hkCreateImage);
    attach("vkDestroyImage", o_DestroyImage, hkDestroyImage);
    attach("vkCreateImageView", o_CreateImageView, hkCreateImageView);
    attach("vkDestroyImageView", o_DestroyImageView, hkDestroyImageView);
    attach("vkCreateFramebuffer", o_CreateFramebuffer, hkCreateFramebuffer);
    attach("vkDestroyFramebuffer", o_DestroyFramebuffer, hkDestroyFramebuffer);
    attach("vkCreateRenderPass", o_CreateRenderPass, hkCreateRenderPass);
    attach("vkCreateRenderPass2", o_CreateRenderPass2, hkCreateRenderPass2<o_CreateRenderPass2>);
    attach("vkCreateRenderPass2KHR", o_CreateRenderPass2KHR, hkCreateRenderPass2<o_CreateRenderPass2KHR>);
    attach("vkDestroyRenderPass", o_DestroyRenderPass, hkDestroyRenderPass);
    attach("vkCmdBeginRenderPass", o_CmdBeginRenderPass, hkCmdBeginRenderPass);
    attach("vkCmdBeginRenderPass2", o_CmdBeginRenderPass2, hkCmdBeginRenderPass2<o_CmdBeginRenderPass2>);
    attach("vkCmdBeginRenderPass2KHR", o_CmdBeginRenderPass2KHR, hkCmdBeginRenderPass2<o_CmdBeginRenderPass2KHR>);
    attach("vkCmdEndRenderPass", o_CmdEndRenderPass, hkCmdEndRenderPass);
    attach("vkCmdEndRenderPass2", o_CmdEndRenderPass2, hkCmdEndRenderPass2<o_CmdEndRenderPass2>);
    attach("vkCmdEndRenderPass2KHR", o_CmdEndRenderPass2KHR, hkCmdEndRenderPass2<o_CmdEndRenderPass2KHR>);
    attach("vkCmdBeginRendering", o_CmdBeginRendering, hkCmdBeginRendering<o_CmdBeginRendering>);
    attach("vkCmdBeginRenderingKHR", o_CmdBeginRenderingKHR, hkCmdBeginRendering<o_CmdBeginRenderingKHR>);
    attach("vkCmdEndRendering", o_CmdEndRendering, hkCmdEndRendering<o_CmdEndRendering>);
    attach("vkCmdEndRenderingKHR", o_CmdEndRenderingKHR, hkCmdEndRendering<o_CmdEndRenderingKHR>);
    attach("vkAllocateDescriptorSets", o_AllocateDescriptorSets, hkAllocateDescriptorSets);
    attach("vkUpdateDescriptorSets", o_UpdateDescriptorSets, hkUpdateDescriptorSets);
    attach("vkCreateDescriptorUpdateTemplate", o_CreateDescriptorUpdateTemplate,
           hkCreateDescriptorUpdateTemplate<o_CreateDescriptorUpdateTemplate>);
    attach("vkCreateDescriptorUpdateTemplateKHR", o_CreateDescriptorUpdateTemplateKHR,
           hkCreateDescriptorUpdateTemplate<o_CreateDescriptorUpdateTemplateKHR>);
    attach("vkDestroyDescriptorUpdateTemplate", o_DestroyDescriptorUpdateTemplate,
           hkDestroyDescriptorUpdateTemplate<o_DestroyDescriptorUpdateTemplate>);
    attach("vkDestroyDescriptorUpdateTemplateKHR", o_DestroyDescriptorUpdateTemplateKHR,
           hkDestroyDescriptorUpdateTemplate<o_DestroyDescriptorUpdateTemplateKHR>);
    attach("vkUpdateDescriptorSetWithTemplate", o_UpdateDescriptorSetWithTemplate,
           hkUpdateDescriptorSetWithTemplate<o_UpdateDescriptorSetWithTemplate>);
    attach("vkUpdateDescriptorSetWithTemplateKHR", o_UpdateDescriptorSetWithTemplateKHR,
           hkUpdateDescriptorSetWithTemplate<o_UpdateDescriptorSetWithTemplateKHR>);
    attach("vkBeginCommandBuffer", o_BeginCommandBuffer, hkBeginCommandBuffer);
    attach("vkCmdBindDescriptorSets", o_CmdBindDescriptorSets, hkCmdBindDescriptorSets);
    attach("vkCmdPushDescriptorSet", o_CmdPushDescriptorSet, hkCmdPushDescriptorSet<o_CmdPushDescriptorSet>);
    attach("vkCmdPushDescriptorSetKHR", o_CmdPushDescriptorSetKHR, hkCmdPushDescriptorSet<o_CmdPushDescriptorSetKHR>);
    attach("vkCmdPushDescriptorSetWithTemplate", o_CmdPushDescriptorSetWithTemplate,
           hkCmdPushDescriptorSetWithTemplate<o_CmdPushDescriptorSetWithTemplate>);
    attach("vkCmdPushDescriptorSetWithTemplateKHR", o_CmdPushDescriptorSetWithTemplateKHR,
           hkCmdPushDescriptorSetWithTemplate<o_CmdPushDescriptorSetWithTemplateKHR>);
    attach("vkCmdDispatch", o_CmdDispatch, hkCmdDispatch);
    attach("vkCmdDispatchIndirect", o_CmdDispatchIndirect, hkCmdDispatchIndirect);
    attach("vkCmdDispatchBase", o_CmdDispatchBase, hkCmdDispatchBase<o_CmdDispatchBase>);
    attach("vkCmdDispatchBaseKHR", o_CmdDispatchBaseKHR, hkCmdDispatchBase<o_CmdDispatchBaseKHR>);

    if (auto result = DetourTransactionCommit(); result != NO_ERROR)
        LOG_ERROR("Failed to hook, error code: {:X}", result);
}
