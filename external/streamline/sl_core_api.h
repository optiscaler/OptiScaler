/*
* Copyright (c) 2022-2024 NVIDIA CORPORATION. All rights reserved
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

#include <limits.h>

#include "sl_struct.h"
#include "sl_consts.h"
#include "sl_version.h"
#include "sl_result.h"
#include "sl_appidentity.h"
#include "sl_device_wrappers.h"

#include "sl_core_types.h"

#if defined(SL_INTERPOSER)
    #if defined(_WIN32)
        #define SL_API extern "C" __declspec(dllexport)
    #else
        #error Unsupported Platform!
    #endif
#else
#define SL_API extern "C"
#endif

#pragma region SL_API

//! Streamline core API functions (check feature specific headers for additional APIs)
//! 
using PFun_slInit = sl::Result(const sl::Preferences& pref, uint64_t sdkVersion);
using PFun_slShutdown = sl::Result();
using PFun_slIsFeatureSupported = sl::Result(sl::Feature feature, const sl::AdapterInfo& adapterInfo);
using PFun_slIsFeatureLoaded = sl::Result(sl::Feature feature, bool& loaded);
using PFun_slSetFeatureLoaded = sl::Result(sl::Feature feature, bool loaded);
using PFun_slEvaluateFeature = sl::Result(sl::Feature feature, const sl::FrameToken& frame, const sl::BaseStructure** inputs, uint32_t numInputs, sl::CommandBuffer* cmdBuffer);
using PFun_slAllocateResources = sl::Result(sl::CommandBuffer* cmdBuffer, sl::Feature feature, const sl::ViewportHandle& viewport);
using PFun_slFreeResources = sl::Result(sl::Feature feature, const sl::ViewportHandle& viewport);
using PFun_slSetTag
#if __cplusplus >= 201402L
[[deprecated("Use the version of this function that takes a sl::FrameToken instead - slSetTagForFrame and set sl::PreferenceFlags::eUseFrameBasedResourceTagging.")]]
#endif
= sl::Result(const sl::ViewportHandle& viewport, const sl::ResourceTag* tags, uint32_t numTags, sl::CommandBuffer* cmdBuffer);
using PFun_slSetTagForFrame = sl::Result(const sl::FrameToken& frame, const sl::ViewportHandle& viewport, const sl::ResourceTag* tags, uint32_t numTags, sl::CommandBuffer* cmdBuffer);
using PFun_slGetFeatureRequirements = sl::Result(sl::Feature feature, sl::FeatureRequirements& requirements);
using PFun_slGetFeatureVersion = sl::Result(sl::Feature feature, sl::FeatureVersion& version);
using PFun_slUpgradeInterface = sl::Result(void** baseInterface);
using PFun_slSetConstants = sl::Result(const sl::Constants& values, const sl::FrameToken& frame, const sl::ViewportHandle& viewport);
using PFun_slGetNativeInterface = sl::Result(void* proxyInterface, void** baseInterface);
using PFun_slGetFeatureFunction = sl::Result(sl::Feature feature, const char* functionName, void*& function);
using PFun_slGetNewFrameToken = sl::Result(sl::FrameToken*& token, const uint32_t* frameIndex);
using PFun_slSetD3DDevice = sl::Result(void* d3dDevice);


//! Initializes the SL module
//!
//! Call this method when the game is initializing. 
//!
//! @param pref Specifies preferred behavior for the SL library (SL will keep a copy)
//! @param sdkVersion Current SDK version
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! This method is NOT thread safe.
SL_API sl::Result slInit(const sl::Preferences &pref, uint64_t sdkVersion = sl::kSDKVersion);

//! Shuts down the SL module
//!
//! Call this method when the game is shutting down. 
//!
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! This method is NOT thread safe.
SL_API sl::Result slShutdown();

//! Checks if a specific feature is supported or not.
//!
//! Call this method to check if a certain e* (see above) is available.
//!
//! @param feature Specifies which feature to use
//! @param adapterInfo Adapter to check (optional)
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! NOTE: If adapter info is null SL will return general feature compatibility with the OS,
//! installed drivers or any other requirements not directly related to the adapter.
//! 
//! This method is NOT thread safe.
SL_API sl::Result slIsFeatureSupported(sl::Feature feature, const sl::AdapterInfo& adapterInfo);

//! Checks if specified feature is loaded or not.
//!
//! Call this method to check if feature is loaded.
//! All requested features are loaded by default and have to be unloaded explicitly if needed.
//!
//! @param feature Specifies which feature to check
//! @param loaded Value specifying if feature is loaded or unloaded.
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! This method is NOT thread safe and requires DX/VK device to be created before calling it.
SL_API sl::Result slIsFeatureLoaded(sl::Feature feature, bool& loaded);

//! Sets the specified feature to either loaded or unloaded state.
//!
//! Call this method to load or unload certain e*. 
//!
//! NOTE: All requested features are loaded by default and have to be unloaded explicitly if needed.
//!
//! @param feature Specifies which feature to check
//! @param loaded Value specifying if feature should be loaded or unloaded.
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! NOTE: When this method is called no other DXGI/D3D/Vulkan APIs should be invoked in parallel so
//! make sure to flush your pipeline before calling this method.
//!
//! This method is NOT thread safe and requires DX/VK device to be created before calling it.
SL_API sl::Result slSetFeatureLoaded(sl::Feature feature, bool loaded);

//! NOTE: sl::PreferenceFlags::eUseFrameBasedResourceTagging must be set when using this API.
//! Tags resource globally
//!
//! Call this method to tag the appropriate buffers in global scope.
//!
//! @param frame Specifies the frame this tag applies to. Frame token can be obtained using slGetNewFrameToken API.
//! @param viewport Specifies viewport this tag applies to
//! @param tags Pointer to resources tags, set to null to remove the specified tag
//! @param numTags Number of resource tags in the provided list
//! @param cmdBuffer Command buffer to use (optional and can be null if ALL tags are null or have eValidUntilPresent life-cycle)
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! IMPORTANT: GPU payload that generates content for the provided tag(s) MUST be either already submitted to the provided command buffer 
//! or some other command buffer which is guaranteed, by the host application, to be executed BEFORE the provided command buffer.
//! 
//! This method is thread safe and requires DX/VK device to be created before calling it.
SL_API sl::Result slSetTagForFrame(const sl::FrameToken& frame, const sl::ViewportHandle& viewport, const sl::ResourceTag* resources, uint32_t numResources, sl::CommandBuffer* cmdBuffer);

//! NOTE: This API has now been DEPRECATED in favor of the new slSetTagForFrame API above.
//! Tags resource globally
//!
//! Call this method to tag the appropriate buffers in global scope.
//!
//! @param viewport Specifies viewport this tag applies to
//! @param tags Pointer to resources tags, set to null to remove the specified tag
//! @param numTags Number of resource tags in the provided list
//! @param cmdBuffer Command buffer to use (optional and can be null if ALL tags are null or have eValidUntilPresent life-cycle)
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! IMPORTANT: GPU payload that generates content for the provided tag(s) MUST be either already submitted to the provided command buffer 
//! or some other command buffer which is guaranteed, by the host application, to be executed BEFORE the provided command buffer.
//! 
//! This method is thread safe and requires DX/VK device to be created before calling it.
SL_API
#if __cplusplus >= 201402L
[[deprecated("Use the version of this function that takes a sl::FrameToken instead - slSetTagForFrame and set sl::PreferenceFlags::eUseFrameBasedResourceTagging.")]]
#endif
sl::Result slSetTag(const sl::ViewportHandle& viewport, const sl::ResourceTag* tags, uint32_t numTags, sl::CommandBuffer* cmdBuffer);

//! Sets common constants.
//!
//! Call this method to provide the required data (SL will keep a copy).
//!
//! @param values Common constants required by SL plugins (SL will keep a copy)
//! @param frame Index of the current frame
//! @param viewport Unique id (can be viewport id | instance id etc.)
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//! 
//! This method is thread safe and requires DX/VK device to be created before calling it.
SL_API sl::Result slSetConstants(const sl::Constants& values, const sl::FrameToken& frame, const sl::ViewportHandle& viewport);

//! Returns feature's requirements
//!
//! Call this method to check what is required to run certain eFeature* (see above).
//! This method must be called after init otherwise it will always return an error.
//!
//! @param feature Specifies which feature to check
//! @param requirements Data structure with feature's requirements
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! This method is NOT thread safe.
SL_API sl::Result slGetFeatureRequirements(sl::Feature feature, sl::FeatureRequirements& requirements);

//! Returns feature's version
//!
//! Call this method to check version for a certain eFeature* (see above).
//! This method must be called after init otherwise it will always return an error.
//!
//! @param feature Specifies which feature to check
//! @param version Data structure with feature's version
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! This method is thread safe.
SL_API sl::Result slGetFeatureVersion(sl::Feature feature, sl::FeatureVersion& version);

//! Allocates resources for the specified feature.
//!
//! Call this method to explicitly allocate resources
//! for an instance of the specified feature.
//! 
//! @param cmdBuffer Command buffer to use (must be created on device where feature is supported but can be null if not needed)
//! @param feature Feature we are working with
//! @param viewport Unique id (viewport handle)
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! This method is NOT thread safe and requires DX/VK device to be created before calling it.
SL_API sl::Result slAllocateResources(sl::CommandBuffer* cmdBuffer, sl::Feature feature, const sl::ViewportHandle& viewport);

//! Frees resources for the specified feature.
//!
//! Call this method to explicitly free resources
//! for an instance of the specified feature.
//! 
//! @param feature Feature we are working with
//! @param viewport Unique id (viewport handle)
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! IMPORTANT: If slEvaluateFeature is pending on a command list, that command list must be flushed
//! before calling this method to prevent invalid resource access on the GPU.
//!
//! IMPORTANT: If slEvaluateFeature is pending on a command list, that command list must be flushed
//! before calling this method to prevent invalid resource access on the GPU.
//!
//! This method is NOT thread safe and requires DX/VK device to be created before calling it.
SL_API sl::Result slFreeResources(sl::Feature feature, const sl::ViewportHandle& viewport);

//! NOTE: sl::PreferenceFlags::eUseFrameBasedResourceTagging must be set when using this API to do
//! frame-based resource tagging for multiple frames in flight at the same time.
//! Evaluates feature
//! 
//! Use this method to mark the section in your rendering pipeline
//! where specific feature should be injected.
//!
//! @param feature Feature we are working with
//! @param frame Current frame handle obtained from SL
//! @param inputs The chained structures providing the input data (viewport, tags, constants etc)
//! @param numInputs Number of inputs
//! @param cmdBuffer Command buffer to use (must be created on device where feature is supported)
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//! 
//! IMPORTANT: Frame and viewport must match whatever is used to set common and or feature options and constants (if any)
//! 
//! NOTE: It is allowed to pass in buffer tags as inputs, they are considered to be a "local" tags and do NOT interact with
//! same tags sent in the global scope using slSetTag API.
//!
//! This method is NOT thread safe and requires DX/VK device to be created before calling it.
SL_API sl::Result slEvaluateFeature(sl::Feature feature, const sl::FrameToken& frame, const sl::BaseStructure** inputs, uint32_t numInputs, sl::CommandBuffer* cmdBuffer);

//! Upgrade interface
//! 
//! Use this method to upgrade basic D3D or DXGI interface to an SL proxy.
//! 
//! @param baseInterface Pointer to a pointer to the base interface (for example ID3D12Device etc.) to be replaced in place.
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//! 
//! IMPORTANT: This method should ONLY be used to support 3rd party SDKs like AMD AGS
//! which bypass SL or when using manual hooking.
//!
//! This method is NOT thread safe and should be called IMMEDIATELY after base interface is created.
SL_API sl::Result slUpgradeInterface(void** baseInterface);

//! Obtain native interface
//! 
//! Use this method to obtain underlying D3D or DXGI interface from an SL proxy.
//! 
//! IMPORTANT: When calling NVAPI or other 3rd party SDKs from your application 
//! it is recommended to provide native interfaces instead of SL proxies.
//! 
//! @param proxyInterface Pointer to the SL proxy (D3D device, swap-chain etc)
//! @param baseInterface Pointer to a pointer to the base interface be returned.
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//! 
//! This method is NOT thread safe
SL_API sl::Result slGetNativeInterface(void* proxyInterface, void** baseInterface);

//! Gets specific feature's function
//!
//! Call this method to obtain various functions for the specified feature. See sl_$feature.h for details.
//!
//! @param feature Feature we are working with
//! @param functionName The name of the API to obtain (declared in sl_[$feature].h
//! @param function Pointer to the function to return
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//! 
//! IMPORTANT: Must be called AFTER device is set by calling either slSetD3DDevice or slSetVulkanInfo.
//!
//! This method is thread safe.
SL_API sl::Result slGetFeatureFunction(sl::Feature feature, const char* functionName, void*& function);

//! Gets unique frame token
//!
//! Call this method to obtain token for the unique frame identification.
//!
//! @param handle Frame token to return
//! @param frameIndex Frame index (optional, if not provided SL internal frame counting is used)
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//! 
//! NOTE: Normally SL would not expect more that 3 frames in flight due to added latency.
//!
//! This method is thread safe.
SL_API sl::Result slGetNewFrameToken(sl::FrameToken*& token, const uint32_t* frameIndex = nullptr);

//! Set D3D device to use
//! 
//! Use this method to specify which D3D device should be used.
//! 
//! @param d3dDevice D3D device to use
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! This method is NOT thread safe and should be called IMMEDIATELY after main device is created.
SL_API sl::Result slSetD3DDevice(void* d3dDevice);

#pragma endregion SL_API
