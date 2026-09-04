#include "pch.h"
#include "menu_overlay_base.h"
#include "menu_overlay_vk.h"

#include <Util.h>
#include <Config.h>
#include <SysUtils.h>

#include <imgui/imgui_impl_vulkan.h>
#include <imgui/imgui_impl_win32.h>

#include <misc/IdentifyGpu.h>

// Vulkan overlay code adopted from here:
// https://gist.github.com/mem99/0ec31ca302927457f86b1d6756aaa8c4
// Need to check resize & recreate fixes

static bool _isInited = false;

static bool _vulkanObjectsCreated = false;
static std::mutex _vkCleanMutex;

// imgui stuff
struct ImGui_ImplVulkan_InitInfo _ImVulkan_Info = {};
struct ImGui_ImplVulkanH_Frame* _ImVulkan_Frames = VK_NULL_HANDLE;
static VkSemaphore* _ImVulkan_Semaphores = VK_NULL_HANDLE;
static VkRenderPass _vkRenderPass = VK_NULL_HANDLE;
static uint32_t _scImageCount;
static ULONG64 _frameCount;

static void DestroyVulkanObjectsLocked(bool shutdown);

// These hooks see vkd3d-proton's presents as well as a native Vulkan game's. swapchainApi is DX12
// only once a wrapped DXGI swapchain has presented from a D3D12 queue, which under Proton means
// vkd3d-proton; MenuOverlayDx draws those titles on the game's own queue. Ordering holds: the game's
// first Present sets swapchainApi before vkd3d creates the VkSwapchain inside it.
static bool DxOverlayOwnsBackend()
{
    return State::Instance().swapchainApi == API::DX12 && IdentifyGpu::getPrimaryGpu().usesDxvk;
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

// ImGui_ImplVulkan_InitInfo::CheckVkResultFn. The backend calls it on every result, successes
// included; filter or the async logger's 8192-slot blocking queue stalls the present thread.
static void CheckVkResult(VkResult result)
{
    if (result == VK_SUCCESS)
        return;

    LOG_ERROR("ImGui Vulkan backend error: {0:X}", (UINT) result);
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

    // Teardown of the ImGui backend and of the per-frame handles must appear atomic to QueuePresent.
    if (_vulkanObjectsCreated)
    {
        std::lock_guard<std::mutex> lock(_vkCleanMutex);

        LOG_DEBUG("_vulkanObjectsCreated, releasing objects");

        if (ImGui::GetIO().BackendRendererUserData != nullptr)
            ImGui_ImplVulkan_Shutdown(false);

        DestroyVulkanObjectsLocked(false);

        _vulkanObjectsCreated = false;
    }

    // Below the teardown, so a swapchain recreate still releases the objects of the old one. ImGui
    // holds one renderer backend at a time in io.BackendRendererUserData; leaving it unclaimed is what
    // lets MenuOverlayDx take it.
    if (DxOverlayOwnsBackend())
    {
        LOG_DEBUG("vkd3d-proton D3D12 swapchain, MenuOverlayDx draws the overlay");
        return;
    }

    // Initialize ImGui
    if (!MenuOverlayBase::IsInited() || MenuOverlayBase::Handle() != hwnd)
    {
        if (MenuOverlayBase::IsInited())
            MenuOverlayBase::Shutdown();

        LOG_DEBUG("MenuOverlayBase::Init");
        MenuOverlayBase::Init(hwnd, false);
    }

    // Starts here, not above: MenuOverlayBase::Init reaches D3D12 device creation, which re-enters the
    // Vulkan hooks and DestroyVulkanObjects on this thread. Everything below is Vulkan object creation
    // and the ImGui backend, with no path back into the hooks.
    std::lock_guard<std::mutex> lock(_vkCleanMutex);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize.x = static_cast<float>(pCreateInfo->imageExtent.width);
    io.DisplaySize.y = static_cast<float>(pCreateInfo->imageExtent.height);

    VkResult result;

    // Get swapchain image count.
    result = vkGetSwapchainImagesKHR(device, *pSwapchain, &_scImageCount, NULL);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR("vkGetSwapchainImagesKHR error: {0:X}", (UINT) result);
        return;
    }

    VkImage images[8];
    result = vkGetSwapchainImagesKHR(device, *pSwapchain, &_scImageCount, images);
    if (result != VK_SUCCESS)
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
    }

    // Select queue family.
    uint32_t queueFamily = 0;
    {
        // get count
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, NULL);

        // get queues
        if (count > 0)
        {
            VkQueueFamilyProperties queues[8];
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, queues);

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
            SetVkObjectName(device, instance, VK_OBJECT_TYPE_SEMAPHORE, (UINT64) fsd, "ImGui Backbuffer Semaphore");
#endif
        }
    }

    // Initialize ImGui and upload fonts
    {
        _ImVulkan_Info.Instance = instance;
        _ImVulkan_Info.PhysicalDevice = pd;
        _ImVulkan_Info.Device = device;
        _ImVulkan_Info.QueueFamily = queueFamily;
        _ImVulkan_Info.Queue = queue;
        _ImVulkan_Info.DescriptorPool = pool;
        _ImVulkan_Info.Subpass = 0;
        _ImVulkan_Info.MinImageCount = pCreateInfo->minImageCount;
        _ImVulkan_Info.ImageCount = _scImageCount;
        _ImVulkan_Info.Allocator = NULL;
        _ImVulkan_Info.RenderPass = _vkRenderPass;
        _ImVulkan_Info.CheckVkResultFn = CheckVkResult;

        bool initResult = ImGui_ImplVulkan_Init(&_ImVulkan_Info);
        LOG_DEBUG("ImGui_ImplVulkan_Init result: {}", initResult);

        if (!initResult)
            return;

        // Upload Fonts
        // Use any command queue
        VkCommandPool command_pool = _ImVulkan_Frames[0].CommandPool;
        VkCommandBuffer command_buffer = _ImVulkan_Frames[0].CommandBuffer;
        result = vkResetCommandPool(device, command_pool, 0);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR("vkBeginCommandBuffer error: {0:X}", (UINT) result);
            return;
        }

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        result = vkBeginCommandBuffer(command_buffer, &begin_info);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR("vkBeginCommandBuffer error: {0:X}", (UINT) result);
            return;
        }

        // initResult = ImGui_ImplVulkan_CreateFontsTexture();
        // LOG_DEBUG("ImGui_ImplVulkan_CreateFontsTexture result: {}", initResult);

        VkSubmitInfo end_info = {};
        end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        end_info.commandBufferCount = 1;
        end_info.pCommandBuffers = &command_buffer;

        result = vkEndCommandBuffer(command_buffer);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR("vkEndCommandBuffer error: {0:X}", (UINT) result);
            return;
        }

        result = vkQueueSubmit(queue, 1, &end_info, VK_NULL_HANDLE);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR("vkQueueSubmit error: {0:X}", (UINT) result);
            return;
        }

        result = vkDeviceWaitIdle(device);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR("vkDeviceWaitIdle error: {0:X}", (UINT) result);
            return;
        }
    }

    _vulkanObjectsCreated = true;
    State::Instance().menuOverlayIsVulkan = true;
    LOG_FUNC_RESULT(_vulkanObjectsCreated);
}

// Caller holds _vkCleanMutex.
static void DestroyVulkanObjectsLocked(bool shutdown)
{
    State::Instance().menuOverlayIsVulkan = false;

    // _ImVulkan_Info is zeroed at the tail under this lock; read it here, not before.
    if (_ImVulkan_Info.Device == VK_NULL_HANDLE)
        return;

    if (!shutdown)
        LOG_FUNC();

    auto result = vkDeviceWaitIdle(_ImVulkan_Info.Device);
    if (result != VK_SUCCESS && !shutdown)
        LOG_WARN("vkDeviceWaitIdle error: {0:X}", (UINT) result);

    if (shutdown)
    {
        if (_vkRenderPass)
            vkDestroyRenderPass(_ImVulkan_Info.Device, _vkRenderPass, VK_NULL_HANDLE);

        if (_ImVulkan_Info.DescriptorPool)
            vkDestroyDescriptorPool(_ImVulkan_Info.Device, _ImVulkan_Info.DescriptorPool, VK_NULL_HANDLE);
    }

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

        if (_ImVulkan_Semaphores[i] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(_ImVulkan_Info.Device, _ImVulkan_Semaphores[i], VK_NULL_HANDLE);
            _ImVulkan_Semaphores[i] = VK_NULL_HANDLE;
        }
    }

    _ImVulkan_Info = {};
}

void MenuOverlayVk::DestroyVulkanObjects(bool shutdown)
{
    std::lock_guard<std::mutex> lock(_vkCleanMutex);
    DestroyVulkanObjectsLocked(shutdown);
}

bool MenuOverlayVk::QueuePresent(VkQueue queue, VkPresentInfoKHR* pPresentInfo)
{
    LOG_FUNC();

    // Serialises against CreateVulkanObjects/DestroyVulkanObjects: vkDeviceWaitIdle there requires no
    // concurrent submit on any queue of the device, and the per-frame handles used below are destroyed
    // under this lock. Dropped across RenderMenu, which re-enters both on this thread.
    std::unique_lock<std::mutex> lock(_vkCleanMutex);

    if (!_vulkanObjectsCreated)
        return true;

    if (!MenuOverlayBase::IsInited() || _ImVulkan_Info.Device == VK_NULL_HANDLE)
        return true;

    if (pPresentInfo->swapchainCount == 0)
        return false;

    LOG_DEBUG("rendering menu, swapchain count: {0}", pPresentInfo->swapchainCount);

    ImGuiIO& io = ImGui::GetIO();
    (void) io;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    _frameCount++;

    {
        ImGui_ImplVulkan_NewFrame();

        if (State::Instance().delayMenuRenderBy > 0)
            State::Instance().delayMenuRenderBy--;

        // RenderMenu runs game, Streamline and DXVK code that reaches DestroyVulkanObjects and
        // CreateVulkanObjects through the Vulkan hooks on this thread. _vkCleanMutex is not recursive.
        lock.unlock();
        auto haveFrame = MenuOverlayBase::RenderMenu();
        lock.lock();

        // Objects can have been torn down and rebuilt while the lock was released.
        auto objectsUsable = _vulkanObjectsCreated && _ImVulkan_Info.Device != VK_NULL_HANDLE &&
                             pPresentInfo->pImageIndices[0] < _ImVulkan_Info.ImageCount;

        if (haveFrame)
        {
            if (State::Instance().delayMenuRenderBy == 0 && objectsUsable)
            {
                auto semaphoreIndex = _frameCount % _scImageCount;
                uint32_t idx = pPresentInfo->pImageIndices[0];
                ImGui_ImplVulkanH_Frame* fd = &_ImVulkan_Frames[idx];

                vkWaitForFences(_ImVulkan_Info.Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
                vkResetFences(_ImVulkan_Info.Device, 1, &fd->Fence);

                {
                    vkResetCommandPool(_ImVulkan_Info.Device, fd->CommandPool, 0);
                    VkCommandBufferBeginInfo info = {};
                    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                    info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                    vkBeginCommandBuffer(fd->CommandBuffer, &info);
                }

                {
                    VkRenderPassBeginInfo info = {};
                    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    info.renderPass = _vkRenderPass;
                    info.framebuffer = fd->Framebuffer;
                    info.renderArea.extent.width = static_cast<uint32_t>(ImGui::GetIO().DisplaySize.x);
                    info.renderArea.extent.height = static_cast<uint32_t>(ImGui::GetIO().DisplaySize.y);
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
                VkPipelineStageFlags waitStages[8] = { VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT };

                VkSubmitInfo submit_info = {};
                submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submit_info.commandBufferCount = 1;
                submit_info.pCommandBuffers = &fd->CommandBuffer;
                submit_info.pWaitDstStageMask = waitStages;
                submit_info.waitSemaphoreCount = pPresentInfo->waitSemaphoreCount;
                submit_info.pWaitSemaphores = pPresentInfo->pWaitSemaphores;
                submit_info.signalSemaphoreCount = 1;
                submit_info.pSignalSemaphores = &_ImVulkan_Semaphores[semaphoreIndex];

                auto qResult = vkQueueSubmit(_ImVulkan_Info.Queue, 1, &submit_info, fd->Fence);
                if (qResult != VK_SUCCESS)
                {
                    LOG_ERROR("vkQueueSubmit error: {0:X}", (UINT) qResult);
                    return false;
                }

                pPresentInfo->waitSemaphoreCount = 1;
                pPresentInfo->pWaitSemaphores = &_ImVulkan_Semaphores[semaphoreIndex];
            }
            else
            {
                // To make RenderMenu happy as it expects this
                ImGui::Render();
            }
        }
    }

    return true;
}

void MenuOverlayVk::CreateSwapchain(VkDevice device, VkPhysicalDevice pd, VkInstance instance, HWND hwnd,
                                    const VkSwapchainCreateInfoKHR* pCreateInfo,
                                    const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
{
    LOG_FUNC();

    // The predicate also guards the shutdown below: where MenuOverlayDx owns ImGui, the renderer
    // backend behind io.BackendRendererUserData is a DX one and ImGui_ImplVulkan_Shutdown would free
    // it as if it were Vulkan. CreateVulkanObjects still runs, to release objects of its own.
    if (MenuOverlayBase::Handle() != hwnd && !DxOverlayOwnsBackend())
    {
        LOG_DEBUG("MenuOverlayBase::Handle() != _hwnd");

        if (MenuOverlayBase::IsInited())
        {
            {
                // Frees the backend QueuePresent draws through.
                std::lock_guard<std::mutex> lock(_vkCleanMutex);
                ImGui_ImplVulkan_Shutdown(false);
            }

            LOG_DEBUG("MenuOverlayBase::Shutdown();");
            MenuOverlayBase::Shutdown();
        }

        LOG_DEBUG("MenuOverlayBase::Init({0:X})", (UINT64) hwnd);
        MenuOverlayBase::Init(hwnd, false);
    }

    CreateVulkanObjects(device, pd, instance, hwnd, pCreateInfo, pSwapchain);

    if (_ImVulkan_Info.Device != VK_NULL_HANDLE)
    {
        _isInited = true;
        MenuOverlayBase::VulkanReady();
        LOG_DEBUG("Vulkan ready");
    }
}
