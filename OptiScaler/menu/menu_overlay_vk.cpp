#include "pch.h"
#include "menu_overlay_base.h"
#include "menu_overlay_vk.h"

#include <Util.h>
#include <Config.h>
#include <SysUtils.h>

#include <imgui/imgui_impl_vulkan.h>
#include <imgui/imgui_impl_win32.h>

#include "menu_common.h"
#include <shaders/menu_blur/MenuBlur_Vk.h>
#include <misc/DeferredRelease.h>
#include <framegen/IFGFeature_Vk.h>
#include <State.h>

#include <magic_enum.hpp>

// Vulkan overlay code adopted from here:
// https://gist.github.com/mem99/0ec31ca302927457f86b1d6756aaa8c4
// Need to check resize & recreate fixes

static std::atomic<bool> _vulkanObjectsCreated { false };
static std::mutex _vkCleanMutex;

// Family of every queue, games can present from queues the menu can't submit to
static std::unordered_map<VkQueue, uint32_t> _queueFamilies;
static std::mutex _queueFamiliesMutex;

// imgui stuff
struct ImGui_ImplVulkan_InitInfo _ImVulkan_Info = {};
struct ImGui_ImplVulkanH_Frame* _ImVulkan_Frames = VK_NULL_HANDLE;
static VkSemaphore* _ImVulkan_Semaphores = VK_NULL_HANDLE;
static VkRenderPass _vkRenderPass = VK_NULL_HANDLE;
static uint32_t _scImageCount;

// Swapchain the menu's frames were made for, other swapchains are presented without the menu
static VkSwapchainKHR _menuSwapchain = VK_NULL_HANDLE;
static VkSwapchainKHR _menuOldSwapchain = VK_NULL_HANDLE;
static ULONG64 _frameCount;

// Window of a destroyed menu swapchain, probe windows can stay alive
static std::atomic<HWND> _releasedHwnd { nullptr };

// menu background blur
// Raw pointer, static destruction at exit would use an already destroyed device
static MenuBlur_Vk* _menuBlur = nullptr;
static VkFormat _scFormat = VK_FORMAT_UNDEFINED;
static VkExtent2D _scExtent {};
static VkImageUsageFlags _scUsage = 0;

// Uses a descriptor from the ImGui pool, needs to go before ImGui_ImplVulkan_Shutdown
static void DestroyMenuBlur()
{
    MenuCommon::SetBackgroundBlur(ImTextureID_Invalid);

    delete _menuBlur;
    _menuBlur = nullptr;
}

static void SetVkObjectName(VkDevice device, VkInstance instance, VkObjectType objectType, uint64_t objectHandle,
                            const char* name)
{
    static PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT = nullptr;

    if (vkSetDebugUtilsObjectNameEXT == nullptr)
        vkSetDebugUtilsObjectNameEXT = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT"));

    VkDebugUtilsObjectNameInfoEXT info {};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = objectType;
    info.objectHandle = objectHandle;
    info.pObjectName = name;

    vkSetDebugUtilsObjectNameEXT(device, &info);
}

static void CreateVulkanObjects(VkDevice device, VkPhysicalDevice pd, VkInstance instance, HWND hwnd,
                                const VkSwapchainCreateInfoKHR* pCreateInfo, VkSwapchainKHR* pSwapchain)
{
    LOG_FUNC();

    if (device == VK_NULL_HANDLE || pCreateInfo == nullptr || *pSwapchain == VK_NULL_HANDLE)
    {
        LOG_WARN(
            "device({0:X}) == VK_NULL_HANDLE || pCreateInfo({1:X}) == nullptr || *pSwapchain({2:X}) == VK_NULL_HANDLE",
            (UINT64) device, (UINT64) pCreateInfo, (UINT64) *pSwapchain);
        return;
    }

    // Also objects of a half finished creation
    MenuOverlayVk::DestroyVulkanObjects(false);

    LOG_DEBUG("Swapchain format: {}", magic_enum::enum_name(pCreateInfo->imageFormat));

    const bool srgbTarget = IFGFeature_Vk::WithoutSrgb(pCreateInfo->imageFormat) != pCreateInfo->imageFormat;
    const bool srgbChanged = std::exchange(State::Instance().menuTargetSrgb, srgbTarget) != srgbTarget;

    // Initialize ImGui
    if (!MenuOverlayBase::IsInited() || MenuOverlayBase::Handle() != hwnd)
    {
        if (MenuOverlayBase::IsInited())
            MenuOverlayBase::Shutdown();

        LOG_DEBUG("MenuOverlayBase::Init");
        MenuOverlayBase::Init(hwnd, false);
    }
    else if (srgbChanged)
    {
        MenuOverlayBase::ApplyThemeStyle();
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize.x = static_cast<float>(pCreateInfo->imageExtent.width);
    io.DisplaySize.y = static_cast<float>(pCreateInfo->imageExtent.height);

    _scFormat = pCreateInfo->imageFormat;
    _scExtent = pCreateInfo->imageExtent;
    _scUsage = pCreateInfo->imageUsage;

    VkResult result;

    // Get swapchain image count.
    _scImageCount = 0;
    result = vkGetSwapchainImagesKHR(device, *pSwapchain, &_scImageCount, NULL);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR("vkGetSwapchainImagesKHR error: {0:X}", (UINT) result);
        return;
    }

    LOG_DEBUG("Swapchain image count: {}, game minImageCount: {}", _scImageCount, pCreateInfo->minImageCount);

    // ImGui needs at least 2 images, don't crash the game if the swapchain reports less
    if (_scImageCount < 2)
    {
        LOG_ERROR("Swapchain reports only {} images, skipping menu init for this swapchain", _scImageCount);
        return;
    }

    std::vector<VkImage> images(_scImageCount);
    result = vkGetSwapchainImagesKHR(device, *pSwapchain, &_scImageCount, images.data());
    if (result != VK_SUCCESS && result != VK_INCOMPLETE)
    {
        LOG_ERROR("vkGetSwapchainImagesKHR error: {0:X}", (UINT) result);
        return;
    }

    // Alloc ImGui frame structure/semaphores for every image.
    // For convenience, I am using ImGui_ImplVulkanH_Frame in imgui_impl_vulkan.h
    if (!_vulkanObjectsCreated)
    {
        _ImVulkan_Frames = (ImGui_ImplVulkanH_Frame*) IM_ALLOC(sizeof(ImGui_ImplVulkanH_Frame) * _scImageCount);
        _ImVulkan_Semaphores = (VkSemaphore*) IM_ALLOC(sizeof(VkSemaphore) * _scImageCount);
        memset(_ImVulkan_Frames, 0, sizeof(ImGui_ImplVulkanH_Frame) * _scImageCount);
        memset(_ImVulkan_Semaphores, 0, sizeof(VkSemaphore) * _scImageCount);
    }

    // Destroy can free a half finished creation
    _ImVulkan_Info.Device = device;
    _ImVulkan_Info.ImageCount = _scImageCount;

    // Select queue family.
    uint32_t queueFamily = 0;
    {
        // get count
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, NULL);

        // get queues
        if (count > 0)
        {
            std::vector<VkQueueFamilyProperties> queues(count);
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, queues.data());

            // find graphic queue
            for (uint32_t i = 0; i < count; i++)
            {
                if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                {
                    queueFamily = i;
                    break;
                }
            }
        }
        else
        {
            LOG_WARN("PD Queue property count is 0!");
            return;
        }
    }

    // Get device queue
    VkQueue queue;
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    // Create the render pool
    VkDescriptorPool pool = VK_NULL_HANDLE;
    {
        VkDescriptorPoolSize sampler_pool_size = {};
        sampler_pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sampler_pool_size.descriptorCount = 8; // required by ImGui 1.92

        VkDescriptorPoolCreateInfo desc_pool_info = {};
        desc_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        desc_pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        desc_pool_info.maxSets = 8;
        desc_pool_info.poolSizeCount = 1;
        desc_pool_info.pPoolSizes = &sampler_pool_size;

        result = vkCreateDescriptorPool(device, &desc_pool_info, NULL, &pool);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR("vkCreateDescriptorPool error: {0:X}", (UINT) result);
            return;
        }

        _ImVulkan_Info.DescriptorPool = pool;
    }

    // Create the render pass
    {
        VkAttachmentDescription attachment_desc = {};

        attachment_desc.format = pCreateInfo->imageFormat;
        attachment_desc.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment_desc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment_desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment_desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment_desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment_desc.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        attachment_desc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference color_attachment = {};
        color_attachment.attachment = 0;
        color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment;

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo render_pass_info = {};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_info.attachmentCount = 1;
        render_pass_info.pAttachments = &attachment_desc;
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = 1;
        render_pass_info.pDependencies = &dependency;

        result = vkCreateRenderPass(device, &render_pass_info, NULL, &_vkRenderPass);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR("vkCreateRenderPass error: {0:X}", (UINT) result);
            return;
        }
    }

    // Create The Image Views
    {
        VkImageViewCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;

        info.format = pCreateInfo->imageFormat;
        info.components.r = VK_COMPONENT_SWIZZLE_R;
        info.components.g = VK_COMPONENT_SWIZZLE_G;
        info.components.b = VK_COMPONENT_SWIZZLE_B;
        info.components.a = VK_COMPONENT_SWIZZLE_A;

        VkImageSubresourceRange image_range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        info.subresourceRange = image_range;

        for (uint32_t i = 0; i < _scImageCount; i++)
        {
            ImGui_ImplVulkanH_Frame* fd = &_ImVulkan_Frames[i];
            fd->Backbuffer = images[i];
            info.image = fd->Backbuffer;

            result = vkCreateImageView(device, &info, NULL, &fd->BackbufferView);
            if (result != VK_SUCCESS)
            {
                LOG_ERROR("vkCreateImageView error: {0:X}", (UINT) result);
                return;
            }

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(device, instance, VK_OBJECT_TYPE_IMAGE_VIEW, (UINT64) fd->BackbufferView,
                            "ImGui Backbuffer View");
#endif
        }
    }

    // Create frame Buffer
    {
        VkImageView attachment[1];
        VkFramebufferCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = _vkRenderPass;
        info.attachmentCount = 1;
        info.pAttachments = attachment;

        info.width = pCreateInfo->imageExtent.width;
        info.height = pCreateInfo->imageExtent.height;

        info.layers = 1;

        for (uint32_t i = 0; i < _scImageCount; i++)
        {
            ImGui_ImplVulkanH_Frame* fd = &_ImVulkan_Frames[i];
            attachment[0] = fd->BackbufferView;
            result = vkCreateFramebuffer(device, &info, NULL, &fd->Framebuffer);
            if (result != VK_SUCCESS)
            {
                LOG_ERROR("vkCreateFramebuffer error: {0:X}", (UINT) result);
                return;
            }

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(device, instance, VK_OBJECT_TYPE_FRAMEBUFFER, (UINT64) fd->Framebuffer,
                            "ImGui Backbuffer Framebuffer");
#endif
        }
    }

    // Create command pools, command buffers, fences, and semaphores for every image
    for (uint32_t i = 0; i < _scImageCount; i++)
    {
        ImGui_ImplVulkanH_Frame* fd = &_ImVulkan_Frames[i];
        VkSemaphore* fsd = &_ImVulkan_Semaphores[i];
        {
            VkCommandPoolCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            info.queueFamilyIndex = queueFamily;
            result = vkCreateCommandPool(device, &info, NULL, &fd->CommandPool);
            if (result != VK_SUCCESS)
            {
                LOG_ERROR("vkCreateCommandPool error: {0:X}", (UINT) result);
                return;
            }

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(device, instance, VK_OBJECT_TYPE_COMMAND_POOL, (UINT64) fd->CommandPool,
                            "ImGui Backbuffer Command Pool");
#endif
        }

        {
            VkCommandBufferAllocateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            info.commandPool = fd->CommandPool;
            info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            info.commandBufferCount = 1;
            result = vkAllocateCommandBuffers(device, &info, &fd->CommandBuffer);
            if (result != VK_SUCCESS)
            {
                LOG_ERROR("vkAllocateCommandBuffers error: {0:X}", (UINT) result);
                return;
            }

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(device, instance, VK_OBJECT_TYPE_COMMAND_BUFFER, (UINT64) fd->CommandBuffer,
                            "ImGui Backbuffer Command Buffer");
#endif
        }

        {
            VkFenceCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            result = vkCreateFence(device, &info, NULL, &fd->Fence);
            if (result != VK_SUCCESS)
            {
                LOG_ERROR("vkCreateFence error: {0:X}", (UINT) result);
                return;
            }

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(device, instance, VK_OBJECT_TYPE_FENCE, (UINT64) fd->Fence, "ImGui Backbuffer Fence");
#endif
        }

        {
            VkSemaphoreCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            result = vkCreateSemaphore(device, &info, NULL, fsd);
            if (result != VK_SUCCESS)
            {
                LOG_ERROR("vkCreateSemaphore error: {0:X}", (UINT) result);
                return;
            }

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(device, instance, VK_OBJECT_TYPE_SEMAPHORE, (UINT64) *fsd, "ImGui Backbuffer Semaphore");
#endif
        }
    }

    // Initialize ImGui, textures are uploaded while rendering
    {
        _ImVulkan_Info.Instance = instance;
        _ImVulkan_Info.PhysicalDevice = pd;
        _ImVulkan_Info.Device = device;
        _ImVulkan_Info.QueueFamily = queueFamily;
        _ImVulkan_Info.Queue = queue;
        _ImVulkan_Info.DescriptorPool = pool;
        _ImVulkan_Info.Subpass = 0;
        _ImVulkan_Info.MinImageCount = std::clamp(pCreateInfo->minImageCount, 2u, _scImageCount);
        _ImVulkan_Info.ImageCount = _scImageCount;
        _ImVulkan_Info.Allocator = NULL;
        _ImVulkan_Info.RenderPass = _vkRenderPass;

        bool initResult = ImGui_ImplVulkan_Init(&_ImVulkan_Info);
        LOG_DEBUG("ImGui_ImplVulkan_Init result: {}", initResult);

        if (!initResult)
            return;
    }

    // Present reads both under the lock
    {
        std::lock_guard lock(_vkCleanMutex);
        _menuSwapchain = *pSwapchain;
        _vulkanObjectsCreated = true;
    }

    LOG_FUNC_RESULT(true);
}

void MenuOverlayVk::DestroyVulkanObjects(bool shutdown, VkDevice device, VkSwapchainKHR swapchain)
{
    if (_ImVulkan_Info.Device == VK_NULL_HANDLE)
        return;

    if (!shutdown)
        LOG_FUNC();

    _vkCleanMutex.lock();

    // Another thread can be done with it meanwhile, or have made the objects of a new swapchain
    if (_ImVulkan_Info.Device == VK_NULL_HANDLE ||
        (swapchain != VK_NULL_HANDLE && (swapchain != _menuSwapchain || device != _ImVulkan_Info.Device)))
    {
        _vkCleanMutex.unlock();
        return;
    }

    // Only the menu's own submits use these, waiting the whole device would touch the game's queues
    std::vector<VkFence> fences;
    for (uint32_t i = 0; i < _ImVulkan_Info.ImageCount; i++)
    {
        if (_ImVulkan_Frames != nullptr && _ImVulkan_Frames[i].Fence != VK_NULL_HANDLE)
            fences.push_back(_ImVulkan_Frames[i].Fence);
    }

    if (!fences.empty())
    {
        auto result = vkWaitForFences(_ImVulkan_Info.Device, static_cast<uint32_t>(fences.size()), fences.data(),
                                      VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS && !shutdown)
            LOG_WARN("vkWaitForFences error: {0:X}", (UINT) result);
    }

    if (!shutdown)
        DestroyMenuBlur();

    // ImGui's pipeline and descriptor sets come from the render pass and pool below
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().BackendRendererUserData != nullptr &&
        ImGui::GetIO().BackendRendererName != nullptr &&
        strcmp(ImGui::GetIO().BackendRendererName, "imgui_impl_vulkan") == 0)
        ImGui_ImplVulkan_Shutdown(false);

    if (_vkRenderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(_ImVulkan_Info.Device, _vkRenderPass, VK_NULL_HANDLE);
        _vkRenderPass = VK_NULL_HANDLE;
    }

    if (_ImVulkan_Info.DescriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(_ImVulkan_Info.Device, _ImVulkan_Info.DescriptorPool, VK_NULL_HANDLE);

    for (uint32_t i = 0; i < _ImVulkan_Info.ImageCount; i++)
    {
        ImGui_ImplVulkanH_Frame* fd = &_ImVulkan_Frames[i];

        if (fd->Fence != VK_NULL_HANDLE)
        {
            vkDestroyFence(_ImVulkan_Info.Device, fd->Fence, VK_NULL_HANDLE);
            fd->Fence = VK_NULL_HANDLE;
        }

        if (fd->CommandBuffer != VK_NULL_HANDLE)
        {
            vkFreeCommandBuffers(_ImVulkan_Info.Device, fd->CommandPool, 1, &fd->CommandBuffer);
            fd->CommandBuffer = VK_NULL_HANDLE;
        }

        if (fd->CommandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(_ImVulkan_Info.Device, fd->CommandPool, VK_NULL_HANDLE);
            fd->CommandPool = VK_NULL_HANDLE;
        }

        if (fd->BackbufferView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(_ImVulkan_Info.Device, fd->BackbufferView, VK_NULL_HANDLE);
            fd->BackbufferView = VK_NULL_HANDLE;
        }

        if (fd->Framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(_ImVulkan_Info.Device, fd->Framebuffer, VK_NULL_HANDLE);
            fd->Framebuffer = VK_NULL_HANDLE;
        }

        // A present can still be waiting on it
        if (_ImVulkan_Semaphores[i] != VK_NULL_HANDLE)
        {
            DeferredRelease::Enqueue([vkDevice = _ImVulkan_Info.Device, semaphore = _ImVulkan_Semaphores[i]]()
                                     { vkDestroySemaphore(vkDevice, semaphore, VK_NULL_HANDLE); },
                                     (uint64_t) _ImVulkan_Info.Device);
            _ImVulkan_Semaphores[i] = VK_NULL_HANDLE;
        }
    }

    // Allocated again with the next swapchain's image count
    IM_FREE(_ImVulkan_Frames);
    IM_FREE(_ImVulkan_Semaphores);
    _ImVulkan_Frames = nullptr;
    _ImVulkan_Semaphores = nullptr;

    _ImVulkan_Info = {};
    _vulkanObjectsCreated = false;
    _menuSwapchain = VK_NULL_HANDLE;

    if (swapchain != VK_NULL_HANDLE)
        _releasedHwnd = MenuOverlayBase::Handle();

    _vkCleanMutex.unlock();
}

void MenuOverlayVk::ReleaseDevice(VkDevice device)
{
    if (device != VK_NULL_HANDLE && device == _ImVulkan_Info.Device)
        DestroyVulkanObjects(false);
}

void MenuOverlayVk::DeviceCreated(VkDevice device, const VkDeviceCreateInfo* pCreateInfo)
{
    std::lock_guard lock(_queueFamiliesMutex);

    for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++)
    {
        auto& info = pCreateInfo->pQueueCreateInfos[i];

        // Queues with flags need vkGetDeviceQueue2, not presented from in practice
        if (info.flags != 0)
            continue;

        for (uint32_t index = 0; index < info.queueCount; index++)
        {
            VkQueue queue = VK_NULL_HANDLE;
            vkGetDeviceQueue(device, info.queueFamilyIndex, index, &queue);
            _queueFamilies[queue] = info.queueFamilyIndex;
        }
    }
}

bool MenuOverlayVk::QueuePresent(VkQueue queue, VkPresentInfoKHR* pPresentInfo)
{
    LOG_FUNC();

    if (!_vulkanObjectsCreated)
        return true;

    if (!MenuOverlayBase::IsInited())
        return true;

    if (pPresentInfo->swapchainCount == 0)
        return false;

    // Cleanup on another thread can be waiting on this present's queue, skip the menu instead of blocking
    std::unique_lock cleanLock(_vkCleanMutex, std::try_to_lock);
    if (!cleanLock.owns_lock() || !_vulkanObjectsCreated || pPresentInfo->pSwapchains[0] != _menuSwapchain)
        return true;

    LOG_DEBUG("rendering menu, swapchain count: {0}", pPresentInfo->swapchainCount);

    ImGuiIO& io = ImGui::GetIO();
    (void) io;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    _frameCount++;

    {
        auto semaphoreIndex = _frameCount % _scImageCount;

        // With DLSSG the present comes from Streamline's thread, while the game's queue
        // can be waiting on that thread. Using the presenting queue avoids a deadlock.
        VkQueue menuQueue = _ImVulkan_Info.Queue;
        {
            std::lock_guard lock(_queueFamiliesMutex);
            if (auto it = _queueFamilies.find(queue);
                it != _queueFamilies.end() && it->second == _ImVulkan_Info.QueueFamily)
            {
                menuQueue = queue;
            }
        }

        ImGui_ImplVulkan_SetQueue(menuQueue);
        ImGui_ImplVulkan_NewFrame();

        // Blur images are only created once the menu is opened, menu decides if they are used
        bool blurReady = false;
        if (_menuBlur || MenuOverlayBase::IsVisible())
        {
            if (!_menuBlur)
                _menuBlur = new MenuBlur_Vk("MenuBlur", _ImVulkan_Info.Device, _ImVulkan_Info.PhysicalDevice);

            blurReady = _menuBlur->Prepare(_scFormat, _scExtent, _scUsage);
        }

        if (blurReady)
            MenuCommon::SetBackgroundBlur(_menuBlur->TextureId(), _menuBlur->UVScale());
        else
            MenuCommon::SetBackgroundBlur(ImTextureID_Invalid);

        if (MenuOverlayBase::RenderMenu())
        {
            uint32_t idx = pPresentInfo->pImageIndices[0];
            ImGui_ImplVulkanH_Frame* fd = &_ImVulkan_Frames[idx];
            const bool blurMenu = blurReady && MenuCommon::BackgroundBlurUsed();

            if (fd->Fence == VK_NULL_HANDLE)
                return true;

            if (auto wResult = vkWaitForFences(_ImVulkan_Info.Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
                wResult != VK_SUCCESS)
            {
                LOG_ERROR("vkWaitForFences error: {}", (int32_t) wResult);
                return true;
            }

            {
                vkResetCommandPool(_ImVulkan_Info.Device, fd->CommandPool, 0);
                VkCommandBufferBeginInfo info = {};
                info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                vkBeginCommandBuffer(fd->CommandBuffer, &info);
            }

            // Needs to be outside of the render pass
            if (blurMenu)
                _menuBlur->Dispatch(fd->CommandBuffer, fd->Backbuffer);

            {
                VkRenderPassBeginInfo info = {};
                info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                info.renderPass = _vkRenderPass;
                info.framebuffer = fd->Framebuffer;
                info.renderArea.extent = _scExtent;
                vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
            }

            ImGui::Render();
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), fd->CommandBuffer);

            // Submit command buffer
            vkCmdEndRenderPass(fd->CommandBuffer);
            auto ecbResult = vkEndCommandBuffer(fd->CommandBuffer);
            if (ecbResult != VK_SUCCESS)
            {
                LOG_ERROR("vkQueueSubmit error: {0:X}", (UINT) ecbResult);
                return false;
            }

            // Submit queue and semaphores
            LOG_DEBUG("waitSemaphoreCount: {0}", pPresentInfo->waitSemaphoreCount);
            // Blur copies the image with a transfer, so everything has to wait for the game
            std::vector<VkPipelineStageFlags> waitStages(pPresentInfo->waitSemaphoreCount,
                                                         blurMenu ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
                                                                  : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

            VkSubmitInfo submit_info = {};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &fd->CommandBuffer;
            submit_info.pWaitDstStageMask = waitStages.data();
            submit_info.waitSemaphoreCount = pPresentInfo->waitSemaphoreCount;
            submit_info.pWaitSemaphores = pPresentInfo->pWaitSemaphores;
            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores = &_ImVulkan_Semaphores[semaphoreIndex];

            // Reset only when a submit follows, destroy waits on it
            vkResetFences(_ImVulkan_Info.Device, 1, &fd->Fence);
            auto qResult = vkQueueSubmit(menuQueue, 1, &submit_info, fd->Fence);
            if (qResult != VK_SUCCESS)
            {
                LOG_ERROR("vkQueueSubmit error: {0:X}", (UINT) qResult);

                // Failed submit leaves the fence unsignaled, waits on it would never return
                VkFenceCreateInfo fenceInfo {};
                fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

                vkDestroyFence(_ImVulkan_Info.Device, fd->Fence, VK_NULL_HANDLE);
                if (vkCreateFence(_ImVulkan_Info.Device, &fenceInfo, VK_NULL_HANDLE, &fd->Fence) != VK_SUCCESS)
                    fd->Fence = VK_NULL_HANDLE;

                return false;
            }

            pPresentInfo->waitSemaphoreCount = 1;
            pPresentInfo->pWaitSemaphores = &_ImVulkan_Semaphores[semaphoreIndex];
        }
    }

    return true;
}

void MenuOverlayVk::CreateSwapchain(VkDevice device, VkPhysicalDevice pd, VkInstance instance, HWND hwnd,
                                    const VkSwapchainCreateInfoKHR* pCreateInfo,
                                    const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
{
    LOG_FUNC();

    if (OnOtherWindow(hwnd))
    {
        LOG_DEBUG("Menu is on window {:X}, skipping swapchain of {:X}", (UINT64) MenuOverlayBase::Handle(),
                  (UINT64) hwnd);
        return;
    }

    if (MenuOverlayBase::Handle() != hwnd)
    {
        LOG_DEBUG("MenuOverlayBase::Handle() != _hwnd");

        if (MenuOverlayBase::IsInited())
        {
            DestroyVulkanObjects(false);
            LOG_DEBUG("MenuOverlayBase::Shutdown();");
            MenuOverlayBase::Shutdown();
        }

        LOG_DEBUG("MenuOverlayBase::Init({0:X})", (UINT64) hwnd);
        MenuOverlayBase::Init(hwnd, false);
    }

    CreateVulkanObjects(device, pd, instance, hwnd, pCreateInfo, pSwapchain);

    if (_vulkanObjectsCreated)
    {
        _menuOldSwapchain = pCreateInfo->oldSwapchain;
        _releasedHwnd = nullptr;
        MenuOverlayBase::VulkanReady();
        LOG_DEBUG("Vulkan ready");
    }
}

void MenuOverlayVk::DestroySwapchain(VkDevice device, VkSwapchainKHR swapchain)
{
    // Swapchain handles are only unique per device
    if (swapchain == VK_NULL_HANDLE || swapchain != _menuSwapchain || device != _ImVulkan_Info.Device)
        return;

    // FG can give the new swapchain the retired one's handle, that destroy is the old one
    if (swapchain == _menuOldSwapchain)
    {
        _menuOldSwapchain = VK_NULL_HANDLE;
        return;
    }

    LOG_DEBUG("Menu swapchain {:X} destroyed", (UINT64) swapchain);

    // Image views and framebuffers of its images go before it
    DestroyVulkanObjects(false, device, swapchain);
}

bool MenuOverlayVk::OnOtherWindow(HWND hwnd)
{
    auto menuHwnd = MenuOverlayBase::Handle();
    return MenuOverlayBase::IsInited() && menuHwnd != hwnd && menuHwnd != _releasedHwnd && IsWindow(menuHwnd);
}
