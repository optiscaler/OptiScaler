#pragma once
#include "MenuBlur_Common.h"

#include <shaders/Shader_Vk.h>
#include <imgui/imgui.h>

// Blurs the swapchain image for the background of the overlay menu
class MenuBlur_Vk : public Shader_Vk, public MenuBlur_Common
{
  private:
    struct BlurImage
    {
        VkImage Image = VK_NULL_HANDLE;
        VkImageView View = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
    };

    // Descriptor sets of the passes, every set is a sampled input + storage output
    enum PassSet : uint32_t
    {
        DownsampleSet = 0, // copy -> blur A
        HorizontalSet,     // blur A -> blur B
        VerticalSet,       // blur B -> blur A
        PassSetCount
    };

    VkDescriptorSet _passSets[PassSetCount] = {};

    // Swapchain images usually can't be sampled, blur reads from a copy
    BlurImage _copy;
    // Final result is always in A
    BlurImage _blurA;
    BlurImage _blurB;

    VkExtent2D _extent {};
    VkFormat _swapchainFormat = VK_FORMAT_UNDEFINED;

    // Blur A registered as ImGui texture
    VkDescriptorSet _imguiTexture = VK_NULL_HANDLE;

    // Part of the blurred texture covered by the swapchain image
    ImVec2 _uvScale { 1.0f, 1.0f };

    bool CreateImage(BlurImage& OutImage, uint32_t InWidth, uint32_t InHeight, VkFormat InFormat,
                     VkImageUsageFlags InUsage);
    void DestroyImage(BlurImage& InImage);
    void ReleaseImages();
    void DispatchPass(VkCommandBuffer InCmdBuffer, PassSet InSet, BlurMode InMode, float InSpacing);

    static void ComputeBarrier(VkCommandBuffer InCmdBuffer, VkPipelineStageFlags InDstStage, VkAccessFlags InDstAccess);

  public:
    // Makes sure blur images match the swapchain, false if the swapchain image can't be blurred
    bool Prepare(VkFormat InFormat, VkExtent2D InExtent, VkImageUsageFlags InUsage);

    // Records the blur before the menu render pass, swapchain image is in PRESENT_SRC layout before and after
    bool Dispatch(VkCommandBuffer InCmdBuffer, VkImage InSwapchainImage);

    ImTextureID TextureId() const { return (ImTextureID) _imguiTexture; }
    ImVec2 UVScale() const { return _uvScale; }

    MenuBlur_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice);
    ~MenuBlur_Vk();
};
