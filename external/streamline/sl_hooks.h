/*
* Copyright (c) 2022-2023 NVIDIA CORPORATION. All rights reserved
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

#pragma once

#include "sl.h"

struct VkPhysicalDevice_T;
struct VkDevice_T;
struct VkInstance_T;

using VkPhysicalDevice = VkPhysicalDevice_T*;
using VkDevice = VkDevice_T*;
using VkInstance = VkInstance_T*;


namespace sl
{

//! NOTE: Adding new hooks require sl.interposer to be recompiled
//! 
//! IMPORTANT: Since SL interposer proxies supports many different versions of various D3D/DXGI interfaces 
//! we use only base interface names for our hooks. 
//! 
//! For example if API was added in IDXGISwapChain5::FUNCTION it is still named eIDXGISwapChain_FUNCTION (there is no 5 in the name)
//! 
enum class FunctionHookID : uint32_t
{
    //! Mandatory - IDXGIFactory*
    eIDXGIFactory_CreateSwapChain,
    eIDXGIFactory_CreateSwapChainForHwnd,
    eIDXGIFactory_CreateSwapChainForCoreWindow,
    
    //! Mandatory - IDXGISwapChain*
    eIDXGISwapChain_Present,
    eIDXGISwapChain_Present1,
    eIDXGISwapChain_GetBuffer,
    eIDXGISwapChain_GetDesc,
    eIDXGISwapChain_ResizeBuffers,
    eIDXGISwapChain_ResizeBuffers1,
    eIDXGISwapChain_GetCurrentBackBufferIndex,
    eIDXGISwapChain_SetFullscreenState,
    //! Internal - please ignore when doing manual hooking
    eIDXGISwapChain_Destroyed,

    //! Mandatory - ID3D12Device*
    eID3D12Device_CreateCommandQueue,

    //! Mandatory - Vulkan
    eVulkan_Present,
    eVulkan_CreateSwapchainKHR,
    eVulkan_DestroySwapchainKHR,
    eVulkan_GetSwapchainImagesKHR,
    eVulkan_AcquireNextImageKHR,
    eVulkan_DeviceWaitIdle,
    eVulkan_CreateWin32SurfaceKHR,
    eVulkan_DestroySurfaceKHR,

    eMaxNum
};

#ifndef SL_CASE_STR
#define SL_CASE_STR(a) case a : return #a;
#endif

inline const char* getFunctionHookIDAsStr(FunctionHookID v)
{
    switch (v)
    {
        SL_CASE_STR(FunctionHookID::eIDXGIFactory_CreateSwapChain);
        SL_CASE_STR(FunctionHookID::eIDXGIFactory_CreateSwapChainForHwnd);
        SL_CASE_STR(FunctionHookID::eIDXGIFactory_CreateSwapChainForCoreWindow);
        SL_CASE_STR(FunctionHookID::eIDXGISwapChain_Present);
        SL_CASE_STR(FunctionHookID::eIDXGISwapChain_Present1);
        SL_CASE_STR(FunctionHookID::eIDXGISwapChain_GetBuffer);
        SL_CASE_STR(FunctionHookID::eIDXGISwapChain_GetDesc);
        SL_CASE_STR(FunctionHookID::eIDXGISwapChain_ResizeBuffers);
        SL_CASE_STR(FunctionHookID::eIDXGISwapChain_ResizeBuffers1);
        SL_CASE_STR(FunctionHookID::eIDXGISwapChain_GetCurrentBackBufferIndex);
        SL_CASE_STR(FunctionHookID::eIDXGISwapChain_SetFullscreenState);
        SL_CASE_STR(FunctionHookID::eIDXGISwapChain_Destroyed);
        SL_CASE_STR(FunctionHookID::eID3D12Device_CreateCommandQueue);
        SL_CASE_STR(FunctionHookID::eVulkan_Present);
        SL_CASE_STR(FunctionHookID::eVulkan_CreateSwapchainKHR);
        SL_CASE_STR(FunctionHookID::eVulkan_DestroySwapchainKHR);
        SL_CASE_STR(FunctionHookID::eVulkan_GetSwapchainImagesKHR);
        SL_CASE_STR(FunctionHookID::eVulkan_AcquireNextImageKHR);
        SL_CASE_STR(FunctionHookID::eVulkan_DeviceWaitIdle);
        SL_CASE_STR(FunctionHookID::eVulkan_CreateWin32SurfaceKHR);
        SL_CASE_STR(FunctionHookID::eVulkan_DestroySurfaceKHR);
        case FunctionHookID::eMaxNum: break;
    };
    return "Unknown";
}

} // namespace sl
