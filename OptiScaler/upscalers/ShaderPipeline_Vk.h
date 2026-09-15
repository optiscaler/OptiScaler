#pragma once

#include <shaders/Shader_Vk.h>
#include <functional>
#include <vector>

struct ShaderPass_Vk
{
    std::function<VkImageInfo(const VkImageInfo& nextOutput)> Setup;
    std::function<bool(const VkImageInfo& input, const VkImageInfo& output)> Dispatch;
    VkImageInfo inputBuffer {};
    VkImageInfo outputBuffer {};
};
using ShaderPipeline_Vk = std::vector<ShaderPass_Vk>;

// Match DX12: reverse setup routes resources, forward dispatch executes every effect.
inline VkImageInfo SetupShaderPipeline(ShaderPipeline_Vk& pipeline, VkImageInfo output)
{
    for (auto it = pipeline.rbegin(); it != pipeline.rend(); ++it)
    {
        it->inputBuffer = it->outputBuffer = {};
        const auto input = it->Setup(output);
        if (input.Image)
        {
            it->inputBuffer = input;
            it->outputBuffer = output;
            output = input;
        }
    }
    return output;
}
inline bool DispatchShaderPipeline(ShaderPipeline_Vk& pipeline)
{
    for (auto& pass : pipeline)
        if (pass.inputBuffer.Image && pass.outputBuffer.Image && !pass.Dispatch(pass.inputBuffer, pass.outputBuffer))
            return false;
    return true;
}
