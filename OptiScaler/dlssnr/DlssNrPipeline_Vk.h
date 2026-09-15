#pragma once

#include <upscalers/ShaderPipeline_Vk.h>
#include <shaders/dlssnr/DlssNr_Vk.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_vk.h>

namespace DlssNr
{
inline VkImageInfo ImageInfo(const NVSDK_NGX_Resource_VK* resource)
{
    if (!resource || resource->Type != NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW)
        return {};
    const auto& image = resource->Resource.ImageViewInfo;
    return { image.ImageView, image.Image, image.SubresourceRange, image.Format, image.Width, image.Height };
}
inline NVSDK_NGX_Resource_VK WrapImage(const VkImageInfo& image, bool readWrite)
{
    NVSDK_NGX_Resource_VK resource {};
    resource.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
    resource.Resource.ImageViewInfo = { image.ImageView, image.Image, image.SubresourceRange,
                                        image.Format,    image.Width, image.Height };
    resource.ReadWrite = readWrite;
    return resource;
}
inline VkImageInfo ParameterImage(NVSDK_NGX_Parameter* parameters, const char* key)
{
    NVSDK_NGX_Resource_VK* image = nullptr;
    parameters->Get(key, (void**) &image);
    return ImageInfo(image);
}
// The v2 guide ABI supports depth/MV offsets; composition still needs origin-zero colour/output.
inline bool HasSupportedSubrects(NVSDK_NGX_Parameter* parameters, bool beforeUpscale)
{
    const char* x = beforeUpscale ? NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X
                                  : NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X;
    const char* y = beforeUpscale ? NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y
                                  : NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y;
    unsigned int offsetX = 0, offsetY = 0;
    parameters->Get(x, &offsetX);
    parameters->Get(y, &offsetY);
    if (offsetX || offsetY)
        return false;
    if (beforeUpscale)
    {
        const auto colour = ParameterImage(parameters, NVSDK_NGX_Parameter_Color);
        unsigned int width = 0, height = 0;
        parameters->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, &width);
        parameters->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, &height);
        return colour.Image && ((width == 0) == (height == 0)) && width <= colour.Width && height <= colour.Height;
    }
    return true;
}
inline DlssNrFrameInfo_Vk FrameInfo(NVSDK_NGX_Parameter* parameters, bool beforeUpscale)
{
    DlssNrFrameInfo_Vk frame {};
    NVSDK_NGX_Resource_VK* depth = nullptr;
    NVSDK_NGX_Resource_VK* motion = nullptr;
    parameters->Get(NVSDK_NGX_Parameter_Depth, (void**) &depth);
    parameters->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &motion);
    frame.DepthReadWrite = depth && depth->ReadWrite;
    frame.MotionReadWrite = motion && motion->ReadWrite;
    unsigned int flags = 0;
    int reset = 0;
    parameters->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, &flags);
    parameters->Get(NVSDK_NGX_Parameter_Reset, &reset);
    frame.DepthInverted = (flags & NVSDK_NGX_DLSS_Feature_Flags_DepthInverted) != 0;
    frame.MotionVectorsLowResolution = (flags & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes) != 0;
    frame.ColourIsLinearHdr = (flags & NVSDK_NGX_DLSS_Feature_Flags_IsHDR) != 0;
    frame.Reset = reset != 0;
    frame.BeforeUpscale = beforeUpscale;
    parameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &frame.MvScaleX);
    parameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &frame.MvScaleY);
    parameters->Get(NVSDK_NGX_Parameter_ExposureTexture, &frame.ExposureTexture);
    parameters->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, &frame.PreExposure);
    parameters->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, &frame.RenderSubrectWidth);
    parameters->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, &frame.RenderSubrectHeight);
    parameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, &frame.ColorSubrectBaseX);
    parameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, &frame.ColorSubrectBaseY);
    parameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, &frame.DepthSubrectBaseX);
    parameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, &frame.DepthSubrectBaseY);
    parameters->Get(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X, &frame.MotionSubrectBaseX);
    parameters->Get(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, &frame.MotionSubrectBaseY);
    const auto output = ParameterImage(parameters, NVSDK_NGX_Parameter_Output);
    frame.OutputWidth = output.Width;
    frame.OutputHeight = output.Height;
    unsigned int width = 0, height = 0;
    parameters->Get(NVSDK_NGX_Parameter_OutWidth, &width);
    parameters->Get(NVSDK_NGX_Parameter_OutHeight, &height);
    if (width && height)
    {
        frame.OutputWidth = width;
        frame.OutputHeight = height;
    }
    return frame;
}
inline ShaderPass_Vk MakePass(DlssNr_Vk& shader, VkCommandBuffer cmd, VkInstance instance, const VkImageInfo& depth,
                              const VkImageInfo& motion, DlssNrFrameInfo_Vk frame, const VkImageInfo& colour = {})
{
    return { [&shader, cmd, frame, colour](const VkImageInfo& output)
             { return frame.BeforeUpscale ? colour : shader.PrepareInput(cmd, output); },
             [&shader, cmd, instance, depth, motion, frame](const VkImageInfo& input, const VkImageInfo& output)
             {
                 return shader.Dispatch(cmd, input, depth, motion, output, frame, instance,
                                        frame.BeforeUpscale ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                            : VK_IMAGE_LAYOUT_GENERAL);
             } };
}
inline VkImageInfo PrepareInput(DlssNr_Vk& shader, VkCommandBuffer cmd, VkInstance instance, const VkImageInfo& colour,
                                const VkImageInfo& depth, const VkImageInfo& motion, DlssNrFrameInfo_Vk frame)
{
    if (!colour.Image)
        return {};
    const auto scratch = shader.PrepareInput(cmd, colour);
    if (!scratch.Image)
        return {};
    frame.BeforeUpscale = true;
    ShaderPipeline_Vk pipeline;
    pipeline.push_back(MakePass(shader, cmd, instance, depth, motion, frame, colour));
    SetupShaderPipeline(pipeline, scratch);
    if (pipeline.front().inputBuffer.Image && DispatchShaderPipeline(pipeline))
        return scratch;
    return {};
}
// Override parameter pointers locally, preserving both caller-owned resource wrappers even on failure.
class ScopedVkParameters
{
    NVSDK_NGX_Parameter* _parameters;
    void* _colour = nullptr;
    void* _output = nullptr;
    NVSDK_NGX_Resource_VK _scratchColour {};
    NVSDK_NGX_Resource_VK _scratchOutput {};
    DlssNr_Vk* _shader = nullptr;
    VkCommandBuffer _cmd = VK_NULL_HANDLE;

  public:
    explicit ScopedVkParameters(NVSDK_NGX_Parameter* parameters) : _parameters(parameters)
    {
        parameters->Get(NVSDK_NGX_Parameter_Color, &_colour);
        parameters->Get(NVSDK_NGX_Parameter_Output, &_output);
    }
    ~ScopedVkParameters()
    {
        _parameters->Set(NVSDK_NGX_Parameter_Color, _colour);
        _parameters->Set(NVSDK_NGX_Parameter_Output, _output);
        if (_shader)
        {
            auto image = ImageInfo(&_scratchColour);
            _shader->SetImageLayout(_cmd, image.Image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_IMAGE_LAYOUT_GENERAL, image.SubresourceRange);
        }
    }
    void SetColour(DlssNr_Vk& shader, VkCommandBuffer cmd, const VkImageInfo& image)
    {
        _scratchColour = WrapImage(image, false);
        _shader = &shader;
        _cmd = cmd;
        shader.SetImageLayout(cmd, image.Image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              image.SubresourceRange);
        _parameters->Set(NVSDK_NGX_Parameter_Color, (void*) &_scratchColour);
    }
    void SetOutput(const VkImageInfo& image)
    {
        _scratchOutput = WrapImage(image, true);
        _parameters->Set(NVSDK_NGX_Parameter_Output, (void*) &_scratchOutput);
    }
};
} // namespace DlssNr
