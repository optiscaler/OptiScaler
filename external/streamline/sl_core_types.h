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
#include <vector>

#include "sl_struct.h"
#include "sl_consts.h"
#include "sl_version.h"
#include "sl_result.h"
#include "sl_appidentity.h"
#include "sl_device_wrappers.h"

typedef struct ID3D11Resource   ID3D11Resource;
typedef struct ID3D11Buffer     ID3D11Buffer;
typedef struct ID3D11Texture2D  ID3D11Texture2D;
typedef struct ID3D12Resource   ID3D12Resource;

// Forward declarations matching MS and VK specs
#ifdef VK_VERSION_1_0
using SL_VKResult = VkResult;
#else
using SL_VKResult = int;
#endif
using HRESULT = long;

namespace sl {

using CommandBuffer = void;
using Device = void;

//! Buffer types used for tagging
//! 
//! IMPORTANT: Each tag must use the unique id
//! 
using BufferType = uint32_t;

// Friend function declaration to access private members for ABI validation static_asserts
namespace test
{
constexpr void AbiValidation();
}

//! Depth buffer - IMPORTANT - Must be suitable to use with clipToPrevClip transformation (see Constants below)
constexpr BufferType kBufferTypeDepth = 0;
//! Object and optional camera motion vectors (see Constants below)
constexpr BufferType kBufferTypeMotionVectors = 1;
//! Color buffer with all post-processing effects applied but without any UI/HUD elements
constexpr BufferType kBufferTypeHUDLessColor = 2;
//! Color buffer containing jittered input data for the image scaling pass
constexpr BufferType kBufferTypeScalingInputColor = 3;
//! Color buffer containing results from the image scaling pass
constexpr BufferType kBufferTypeScalingOutputColor = 4;
//! Normals
constexpr BufferType kBufferTypeNormals = 5;
//! Roughness
constexpr BufferType kBufferTypeRoughness = 6;
//! Albedo
constexpr BufferType kBufferTypeAlbedo = 7;
//! Specular Albedo
constexpr BufferType kBufferTypeSpecularAlbedo = 8;
//! Indirect Albedo
constexpr BufferType kBufferTypeIndirectAlbedo = 9;
//! Specular Motion Vectors
constexpr BufferType kBufferTypeSpecularMotionVectors = 10;
//! Disocclusion Mask
constexpr BufferType kBufferTypeDisocclusionMask = 11;
//! Emissive
constexpr BufferType kBufferTypeEmissive = 12;
//! Exposure
constexpr BufferType kBufferTypeExposure = 13;
//! Buffer with normal and roughness in alpha channel
constexpr BufferType kBufferTypeNormalRoughness = 14;
//! Diffuse and camera ray length
constexpr BufferType kBufferTypeDiffuseHitNoisy = 15;
//! Diffuse denoised
constexpr BufferType kBufferTypeDiffuseHitDenoised = 16;
//! Specular and reflected ray length
constexpr BufferType kBufferTypeSpecularHitNoisy = 17;
//! Specular denoised
constexpr BufferType kBufferTypeSpecularHitDenoised = 18;
//! Shadow noisy
constexpr BufferType kBufferTypeShadowNoisy = 19;
//! Shadow denoised
constexpr BufferType kBufferTypeShadowDenoised = 20;
//! AO noisy
constexpr BufferType kBufferTypeAmbientOcclusionNoisy = 21;
//! AO denoised
constexpr BufferType kBufferTypeAmbientOcclusionDenoised = 22;
//! Optional - UI/HUD color and alpha
//! IMPORTANT: Please make sure that alpha channel has enough precision (for example do NOT use formats like R10G10B10A2)
constexpr BufferType kBufferTypeUIColorAndAlpha = 23;
//! Optional - Shadow pixels hint (set to 1 if a pixel belongs to the shadow area, 0 otherwise)
constexpr BufferType kBufferTypeShadowHint = 24;
//! Optional - Reflection pixels hint (set to 1 if a pixel belongs to the reflection area, 0 otherwise)
constexpr BufferType kBufferTypeReflectionHint = 25;
//! Optional - Particle pixels hint (set to 1 if a pixel represents a particle, 0 otherwise)
constexpr BufferType kBufferTypeParticleHint = 26;
//! Optional - Transparency pixels hint (set to 1 if a pixel belongs to the transparent area, 0 otherwise)
constexpr BufferType kBufferTypeTransparencyHint = 27;
//! Optional - Animated texture pixels hint (set to 1 if a pixel belongs to the animated texture area, 0 otherwise)
constexpr BufferType kBufferTypeAnimatedTextureHint = 28;
//! Optional - Bias for current color vs history hint - lerp(history, current, bias) (set to 1 to completely reject history)
constexpr BufferType kBufferTypeBiasCurrentColorHint = 29;
//! Optional - Ray-tracing distance (camera ray length)
constexpr BufferType kBufferTypeRaytracingDistance = 30;
//! Optional - Motion vectors for reflections
constexpr BufferType kBufferTypeReflectionMotionVectors = 31;
//! Optional - Position, in same space as eNormals
constexpr BufferType kBufferTypePosition = 32;
//! Optional - Indicates (via non-zero value) which pixels have motion/depth values that do not match the final color content at that pixel (e.g. overlaid, opaque Picture-in-Picture)
constexpr BufferType kBufferTypeInvalidDepthMotionHint = 33;
//! Alpha
constexpr BufferType kBufferTypeAlpha = 34;
//! Color buffer containing only opaque geometry
constexpr BufferType kBufferTypeOpaqueColor = 35;
//! Optional - Reduce reliance on history instead using current frame hint (0 if a pixel is not at all reactive and default composition should be used, 1 if fully reactive)
constexpr BufferType kBufferTypeReactiveMaskHint = 36;
//! Optional - Pixel lock adjustment hint (set to 1 if pixel lock should be completely removed, 0 otherwise)
constexpr BufferType kBufferTypeTransparencyAndCompositionMaskHint = 37;
//! Optional - Albedo of the reflection ray hit point. For multibounce reflections, this should be the albedo of the first non-specular bounce.
constexpr BufferType kBufferTypeReflectedAlbedo = 38;
//! Optional - Color buffer before particles are drawn.
constexpr BufferType kBufferTypeColorBeforeParticles = 39;
//! Optional - Color buffer before transparent objects are drawn.
constexpr BufferType kBufferTypeColorBeforeTransparency = 40;
//! Optional - Color buffer before fog is drawn.
constexpr BufferType kBufferTypeColorBeforeFog = 41;
//! Optional - Buffer containing the hit distance of a specular ray.
constexpr BufferType kBufferTypeSpecularHitDistance = 42;
//! Optional - Buffer that contains 3 components of a specular ray direction, and 1 component of specular hit distance.
constexpr BufferType kBufferTypeSpecularRayDirectionHitDistance = 43;
//! Optional - Buffer containing normalized direction of a specular ray.
constexpr BufferType kBufferTypeSpecularRayDirection = 44;
// !Optional - Buffer containing the hit distance of a diffuse ray.
constexpr BufferType kBufferTypeDiffuseHitDistance = 45;
//! Optional - Buffer that contains 3 components of a diffuse ray direction, and 1 component of diffuse hit distance.
constexpr BufferType kBufferTypeDiffuseRayDirectionHitDistance = 46;
//! Optional - Buffer containing normalized direction of a diffuse ray.
constexpr BufferType kBufferTypeDiffuseRayDirection = 47;
//! Optional - Buffer containing display resolution depth.
constexpr BufferType kBufferTypeHiResDepth = 48;
//! Required either this or kBufferTypeDepth - Buffer containing linear depth.
constexpr BufferType kBufferTypeLinearDepth = 49;
//! Optional - Bidirectional distortion field. 4 channels in normalized [0,1] pixel space. RG = distorted pixel to undistorted pixel displacement. BA = undistorted pixel to distorted pixel displacement.
constexpr BufferType kBufferTypeBidirectionalDistortionField = 50;
//!Optional - Buffer containing particles or other similar transparent effects rendered into it instead of passing it as part of the input color
constexpr BufferType kBufferTypeTransparencyLayer = 51;
//!Optional - Buffer to be used in addition to TransparencyLayer which allows 3-channels of Opacity versus 1-channel. 
//            In this case, TransparencyLayer represents Color (RcGcBc), TransparencyLayerOpacity represents alpha (RaGaBa)'
constexpr BufferType kBufferTypeTransparencyLayerOpacity = 52;
//! Optional - Swapchain buffer to be presented
constexpr BufferType kBufferTypeBackbuffer = 53;
//! Optional - Mask for pixels to skip warping
constexpr BufferType kBufferTypeNoWarpMask = 54;
//! Optional - Color buffer after particles are drawn (for research purposes)
constexpr BufferType kBufferTypeColorAfterParticles = 55;
//! Optional - Color buffer after transparent objects are drawn (for research purposes)
constexpr BufferType kBufferTypeColorAfterTransparency = 56;
//! Optional - Color buffer after fog is drawn (for research purposes)
constexpr BufferType kBufferTypeColorAfterFog = 57;
//! Optional - Subsurface scattering guide buffer
constexpr BufferType kBufferTypeScreenSpaceSubsurfaceScatteringGuide = 58;
//! Optional - Color buffer before subsurface scattering (for research purposes)
constexpr BufferType kBufferTypeColorBeforeScreenSpaceSubsurfaceScattering = 59;
//! Optional - Color buffer after subsurface scattering (for research purposes)
constexpr BufferType kBufferTypeColorAfterScreenSpaceSubsurfaceScattering = 60;
//! Optional - Refraction guide buffer (for research purposes)
constexpr BufferType kBufferTypeScreenSpaceRefractionGuide = 61;
//! Optional - Color buffer before refraction (for research purposes)
constexpr BufferType kBufferTypeColorBeforeScreenSpaceRefraction = 62;
//! Optional - Color buffer after refraction (for research purposes)
constexpr BufferType kBufferTypeColorAfterScreenSpaceRefraction = 63;
//! Optional - Depth of Field Buffer (for research purposes)
constexpr BufferType kBufferTypeDepthOfFieldGuide = 64;
//! Optional - Color buffer before Depth of Field (for research purposes)
constexpr BufferType kBufferTypeColorBeforeDepthOfField = 65;
//! Optional - Color buffer after Depth of Field (for research purposes)
constexpr BufferType kBufferTypeColorAfterDepthOfField = 66;
//! Optional - Color buffer that overrides the alpha channel of kBufferTypeScalingOutputColor
constexpr BufferType kBufferTypeScalingOutputAlpha  = 67;
//! Features supported with this SDK
//! 
//! IMPORTANT: Each feature must use a unique id
//! 
using Feature = uint32_t;

//! Deep Learning Super Sampling
constexpr Feature kFeatureDLSS = 0;

//! Real-Time Denoiser (removed)
constexpr Feature kFeatureNRD_INVALID = 1;

//! NVIDIA Image Scaling
constexpr Feature kFeatureNIS = 2;

//! Reflex
constexpr Feature kFeatureReflex = 3;

//! PC Latency
constexpr Feature kFeaturePCL = 4;

//! DeepDVC
constexpr Feature kFeatureDeepDVC = 5;

constexpr Feature kFeatureLatewarp = 6;

//! DLSS Frame Generation
constexpr Feature kFeatureDLSS_G = 1000;

//! DLSS Ray Reconstruction
constexpr Feature kFeatureDLSS_RR = 1001;

constexpr Feature kFeatureNvPerf = 1002;

constexpr Feature kFeatureDirectSR = 1003;

// ImGUI 
constexpr Feature kFeatureImGUI = 9999;

//! Common feature, NOT intended to be used directly
constexpr Feature kFeatureCommon = UINT_MAX;

//! Different levels for logging
enum class LogLevel : uint32_t
{
    //! No logging
    eOff,
    //! Default logging
    eDefault,
    //! Verbose logging
    eVerbose,
    //! Total count
    eCount
};

//! Resource types
enum class ResourceType : char
{
    eTex2d,
    eBuffer,
    eCommandQueue,
    eCommandBuffer,
    eCommandPool,
    eFence,
    eSwapchain,
    eHostFence,
    // this type means that the only thing we know for sure about this resource is that it's castable to IUnknown
    eUnknown,
    eCount
};

//! Resource allocate information
//!
SL_STRUCT_BEGIN(ResourceAllocationDesc, StructType({ 0xbb57e5, 0x49a2, 0x4c23, { 0xa5, 0x19, 0xab, 0x92, 0x86, 0xe7, 0x40, 0x14 } }), kStructVersion1)
    ResourceAllocationDesc(ResourceType _type, void* _desc, uint32_t _state, void* _heap) : BaseStructure(ResourceAllocationDesc::s_structType, kStructVersion1), type(_type),desc(_desc),state(_state),heap(_heap){};
    //! Indicates the type of resource
    ResourceType type = ResourceType::eTex2d;
    //! D3D12_RESOURCE_DESC/VkImageCreateInfo/VkBufferCreateInfo
    void* desc{};
    //! Initial state as D3D12_RESOURCE_STATES or VkMemoryPropertyFlags
    uint32_t state = 0;
    //! CD3DX12_HEAP_PROPERTIES or nullptr
    void* heap{};

    //! IMPORTANT: New members go here or if optional can be chained in a new struct, see sl_struct.h for details
SL_STRUCT_END()


//! Subresource range information, for Vulkan resources
//! 
//! {8D4C316C-D402-4524-89A7-14E79E638E3A}
SL_STRUCT_BEGIN(SubresourceRange, StructType({ 0x8d4c316c, 0xd402, 0x4524, { 0x89, 0xa7, 0x14, 0xe7, 0x9e, 0x63, 0x8e, 0x3a } }), kStructVersion1)
    //! Vulkan subresource aspectMask
    uint32_t aspectMask;
    //! Vulkan subresource baseMipLevel
    uint32_t baseMipLevel;
    //! Vulkan subresource levelCount
    uint32_t levelCount;
    //! Vulkan subresource baseArrayLayer
    uint32_t baseArrayLayer;
    //! Vulkan subresource layerCount
    uint32_t layerCount;
SL_STRUCT_END()

//! Native resource
//! 
//! {3A9D70CF-2418-4B72-8391-13F8721C7261}
SL_STRUCT_BEGIN(Resource, StructType({ 0x3a9d70cf, 0x2418, 0x4b72, { 0x83, 0x91, 0x13, 0xf8, 0x72, 0x1c, 0x72, 0x61 } }), kStructVersion1)
    //! Constructors
    //! 
    //! Resource type, native pointer are MANDATORY always
    //! Resource state is MANDATORY unless using D3D11
    //! Resource view, description etc. are MANDATORY only when using Vulkan
    //! 
    Resource(ResourceType _type, void* _native, void* _mem, void* _view, uint32_t _state = UINT_MAX) : BaseStructure(Resource::s_structType, kStructVersion1), type(_type), native(_native), memory(_mem), view(_view), state(_state){};
    Resource(ResourceType _type, void* _native, uint32_t _state = UINT_MAX) : BaseStructure(Resource::s_structType, kStructVersion1), type(_type), native(_native), state(_state) {};

    //! Conversion helpers for D3D
    inline operator ID3D12Resource* () { return reinterpret_cast<ID3D12Resource*>(native); }
    inline operator ID3D11Resource* () { return reinterpret_cast<ID3D11Resource*>(native); }
    inline operator ID3D11Buffer* () { return reinterpret_cast<ID3D11Buffer*>(native); }
    inline operator ID3D11Texture2D* () { return reinterpret_cast<ID3D11Texture2D*>(native); }

    //! Indicates the type of resource
    ResourceType type = ResourceType::eTex2d;
    //! ID3D11Resource/ID3D12Resource/VkBuffer/VkImage
    void* native{};
    //! vkDeviceMemory or nullptr
    void* memory{};
    //! VkImageView/VkBufferView or nullptr
    void* view{};
    //! State as D3D12_RESOURCE_STATES or VkImageLayout
    //! 
    //! IMPORTANT: State is MANDATORY and needs to be correct when tagged resources are actually used.
    //! 
    uint32_t state = UINT_MAX;
    //! Width in pixels
    uint32_t width{};
    //! Height in pixels
    uint32_t height{};
    //! Native format
    uint32_t nativeFormat{};
    //! Number of mip-map levels
    uint32_t mipLevels{};
    //! Number of arrays
    uint32_t arrayLayers{};
    //! Virtual address on GPU (if applicable)
    uint64_t gpuVirtualAddress{};
    //! VkImageCreateFlags
    uint32_t flags;
    //! VkImageUsageFlags
    uint32_t usage{};
    //! Reserved for internal use
    uint32_t reserved{};

    //! IMPORTANT: New members go here or if optional can be chained in a new struct, see sl_struct.h for details
SL_STRUCT_END()

//! Specifies life-cycle for the tagged resource
//! 
//! IMPORTANT: Use 'eOnlyValidNow' and 'eValidUntilEvaluate' ONLY when really needed since it can result in wasting VRAM if SL ends up making unnecessary copies.
//! 
//! If integrating features, like for example DLSS-G, which require tags to be 'eValidUntilPresent' please try to tag everything as 'eValidUntilPresent' first
//! and only make modifications if upon visual inspection you notice that tags are corrupted when used during the Present frame call.
enum ResourceLifecycle
{
    //! Resource can change, get destroyed or reused for other purposes after it is provided to SL
    eOnlyValidNow,
    //! Resource does NOT change, gets destroyed or reused for other purposes from the moment it is provided to SL until the frame is presented
    eValidUntilPresent,
    //! Resource does NOT change, gets destroyed or reused for other purposes from the moment it is provided to SL until after the slEvaluateFeature call has returned.
    eValidUntilEvaluate
};

//! Tagged resource
//! 
//! {4C6A5AAD-B445-496C-87FF-1AF3845BE653}
//! Extensions as part of the `next` ptr:
//!     PrecisionInfo
SL_STRUCT_BEGIN(ResourceTag, StructType({ 0x4c6a5aad, 0xb445, 0x496c, { 0x87, 0xff, 0x1a, 0xf3, 0x84, 0x5b, 0xe6, 0x53 } }), kStructVersion1)
    ResourceTag(Resource* r, BufferType t, ResourceLifecycle l, const Extent* e = nullptr)
        : BaseStructure(ResourceTag::s_structType, kStructVersion1), resource(r), type(t), lifecycle(l)
    {
        if (e) extent = *e;
    };

    //! Resource description
    Resource* resource{};
    //! Type of the tagged buffer
    BufferType type{};
    //! The life-cycle for the tag, if resource is volatile a valid command buffer must be specified
    ResourceLifecycle lifecycle{};
    //! The area of the tagged resource to use (if using the entire resource leave as null)
    Extent extent{};

    //! IMPORTANT: New members go here or if optional can be chained in a new struct, see sl_struct.h for details
SL_STRUCT_END()

// 
//! Precision info, optional extension for ResourceTag.
//! 
//! {98F6E9BA-8D16-4831-A802-4D3B52FF26BF}
//! Extensions as part of the `next` ptr:
//!     ResourceTag
SL_STRUCT_BEGIN(PrecisionInfo, StructType({ 0x98f6e9ba, 0x8d16, 0x4831, { 0xa8, 0x2, 0x4d, 0x3b, 0x52, 0xff, 0x26, 0xbf } }), kStructVersion1)
    // Formula used to convert the low-precision data to high-precision
    enum PrecisionFormula : uint32_t
    {
        eNoTransform = 0,           // hi = lo, essentially no conversion is done
        eLinearTransform,           // hi = lo * scale + bias
    };

    PrecisionInfo(PrecisionInfo::PrecisionFormula formula, float bias, float scale)
        : BaseStructure(PrecisionInfo::s_structType, kStructVersion1), conversionFormula(formula), bias(bias), scale(scale) {};
    
    static std::string getPrecisionFormulaAsStr(PrecisionFormula formula)
    {
        switch (formula)
        {
        case eNoTransform:
            return "eNoTransform";
        case eLinearTransform:
            return "eLinearTransform";
        default:
            assert("Invalid PrecisionFormula" && false);
            return "Unknown";
        }
    };

    PrecisionFormula conversionFormula{ eNoTransform };
    float bias{ 0.0f };
    float scale{ 1.0f };

    inline operator bool() const { return conversionFormula != eNoTransform; }
    inline bool operator==(const PrecisionInfo& rhs) const
    {
        return conversionFormula == rhs.conversionFormula && bias == rhs.bias && scale == rhs.scale;
    }
    inline bool operator!=(const PrecisionInfo& rhs) const
    {
        return !operator==(rhs);
    }
SL_STRUCT_END()

//! Resource allocation/deallocation callbacks
//!
//! Use these callbacks to gain full control over 
//! resource life cycle and memory allocation tracking.
//!
//! @param device - Device to be used (vkDevice or ID3D11Device or ID3D12Device)
//!
//! IMPORTANT: Textures must have the pixel shader resource
//! and the unordered access view flags set
using PFun_ResourceAllocateCallback = Resource(const ResourceAllocationDesc* desc, void* device);
using PFun_ResourceReleaseCallback = void(Resource* resource, void* device);

//! Log type
enum class LogType : uint32_t
{
    //! Controlled by LogLevel, SL can show more information in eLogLevelVerbose mode
    eInfo,
    //! Always shown regardless of LogLevel
    eWarn,
    eError,
    //! Total count
    eCount
};

//! Logging callback
//!
//! Use these callbacks to track messages posted in the log.
//! If any of the SL methods returns false use eLogTypeError
//! type to track down what went wrong and why.
using PFun_LogMessageCallback = void(LogType type, const char* msg);

struct APIError
{
    union
    {
        HRESULT hres;
        SL_VKResult vkRes;
    };
};
//! Returns an error returned by DXGI or Vulkan API calls 'vkQueuePresentKHR' and 'vkAcquireNextImageKHR'
using PFunOnAPIErrorCallback = void(const APIError& lastError);

//! Optional flags
enum class PreferenceFlags : uint64_t
{
    //! Set by default - Disables command list state tracking - Host application is responsible for restoring CL state correctly after each 'slEvaluateFeature' call
    eDisableCLStateTracking = 1 << 0,
    //! Optional - Disables debug text on screen in development builds
    eDisableDebugText = 1 << 1,
    //! Optional - IMPORTANT: Only to be used in the advanced integration mode, see the 'manual hooking' programming guide for more details
    eUseManualHooking = 1 << 2,
    //! Optional - Enables downloading of Over The Air (OTA) updates for SL and NGX
    //! This will invoke the OTA updater to look for new updates. A separate
    //! flag below is used to control whether or not OTA-downloaded SL Plugins are
    //! loaded.
    eAllowOTA = 1 << 3,
    //! Do not check OS version when deciding if feature is supported or not
    //! 
    //! IMPORTANT: ONLY SET THIS FLAG IF YOU KNOW WHAT YOU ARE DOING. 
    //! 
    //! VARIOUS WIN APIs INCLUDING BUT NOT LIMITED TO `IsWindowsXXX`, `GetVersionX`, `rtlGetVersion` ARE KNOWN FOR RETURNING INCORRECT RESULTS.
    eBypassOSVersionCheck = 1 << 4,
    //! Optional - If specified SL will create DXGI factory proxy rather than modifying the v-table for the base interface.
    //! 
    //! This can help with 3rd party overlays which are NOT integrated with the host application but rather operate via injection.
    eUseDXGIFactoryProxy = 1 << 5,
    //! Optional - Enables loading of plugins downloaded Over The Air (OTA), to
    //! be used in conjunction with the eAllowOTA flag.
    eLoadDownloadedPlugins = 1 << 6,

    //! Optional - allow tagging of resources for frame. This helps distinguish whether slEvaluateFeature needs to do frame-based tagging
    //! of resources which wasn't the case earlier.
    eUseFrameBasedResourceTagging = 1 << 7,

    //! All preference flags.  This isn't expected to be used directly by integrations, but may be useful for e.g. writing helpers.
    eAll = eDisableCLStateTracking | eDisableDebugText | eUseManualHooking | eAllowOTA | eBypassOSVersionCheck | eUseDXGIFactoryProxy | eLoadDownloadedPlugins | eUseFrameBasedResourceTagging
};

SL_ENUM_OPERATORS_64(PreferenceFlags)

//! Application preferences
//!
//! {1CA10965-BF8E-432B-8DA1-6716D879FB14}
SL_STRUCT_BEGIN(Preferences, StructType({ 0x1ca10965, 0xbf8e, 0x432b, { 0x8d, 0xa1, 0x67, 0x16, 0xd8, 0x79, 0xfb, 0x14 } }), kStructVersion1)
    //! Optional - In non-production builds it is useful to enable debugging console window
    bool showConsole = false;
    //! Optional - Various logging levels
    LogLevel logLevel = LogLevel::eDefault;
    //! Optional - Absolute paths to locations where to look for plugins, first path in the list has the highest priority
    const wchar_t** pathsToPlugins{};
    //! Optional - Number of paths to search
    uint32_t numPathsToPlugins = 0;
    //! Optional - Absolute path to location where logs and other data should be stored
    //! 
    //! NOTE: Set this to nullptr in order to disable logging to a file
    const wchar_t* pathToLogsAndData{};
    //! Optional - Allows resource allocation tracking on the host side
    PFun_ResourceAllocateCallback* allocateCallback{};
    //! Optional - Allows resource deallocation tracking on the host side
    PFun_ResourceReleaseCallback* releaseCallback{};
    //! Optional - Allows log message tracking including critical errors if they occur
    PFun_LogMessageCallback* logMessageCallback{};
    //! Optional - Flags used to enable or disable advanced options
    PreferenceFlags flags = PreferenceFlags::eDisableCLStateTracking | PreferenceFlags::eAllowOTA | PreferenceFlags::eLoadDownloadedPlugins;
    //! Required - Features to load (assuming appropriate plugins are found), if not specified NO features will be loaded by default
    const Feature* featuresToLoad{};
    //! Required - Number of features to load, only used when list is not a null pointer
    uint32_t numFeaturesToLoad{};
    //! Optional - Id provided by NVIDIA, if not specified then engine type and version are required
    uint32_t applicationId{};
    //! Optional - Type of the rendering engine used, if not specified then applicationId is required
    EngineType engine = EngineType::eCustom;
    //! Optional - Version of the rendering engine used
    const char* engineVersion{};
    //! Optional - GUID (like for example 'a0f57b54-1daf-4934-90ae-c4035c19df04')
    const char* projectId{};
    //! Optional - Which rendering API host is planning to use
    //! 
    //! NOTE: To ensure correct `slGetFeatureRequirements` behavior please specify if planning to use Vulkan.
    RenderAPI renderAPI = RenderAPI::eD3D12;

    //! IMPORTANT: New members go here or if optional can be chained in a new struct, see sl_struct.h for details
SL_STRUCT_END()

//! Frame tracking handle
//! 
//! IMPORTANT: Use slGetNewFrameToken to obtain unique instance
//! 
//! {830A0F35-DB84-4171-A804-59B206499B18}
SL_STRUCT_PROTECTED_BEGIN(FrameToken, StructType({ 0x830a0f35, 0xdb84, 0x4171, { 0xa8, 0x4, 0x59, 0xb2, 0x6, 0x49, 0x9b, 0x18 } }), kStructVersion1)
    //! Helper operator to obtain current frame index
    virtual operator uint32_t() const = 0;
SL_STRUCT_END()

//! Handle for the unique viewport
//! 
//! {171B6435-9B3C-4FC8-9994-FBE52569AAA4}
SL_STRUCT_BEGIN(ViewportHandle, StructType({ 0x171b6435, 0x9b3c, 0x4fc8, { 0x99, 0x94, 0xfb, 0xe5, 0x25, 0x69, 0xaa, 0xa4 } }), kStructVersion1)
    ViewportHandle(uint32_t v) : BaseStructure(ViewportHandle::s_structType, kStructVersion1), value(v) {}
    ViewportHandle(int32_t v) : BaseStructure(ViewportHandle::s_structType, kStructVersion1), value(v) {}
    operator uint32_t() const { return value; }
private:
    uint32_t value = UINT_MAX;
    friend constexpr void sl::test::AbiValidation();
SL_STRUCT_END()

//! Specifies feature requirement flags
//! 
enum class FeatureRequirementFlags : uint32_t
{
    //! Rendering APIs
    eD3D11Supported = 1 << 0,
    eD3D12Supported = 1 << 1,
    eVulkanSupported = 1 << 2,
    //! If set V-Sync must be disabled when feature is active
    eVSyncOffRequired = 1 << 3,
    //! If set GPU hardware scheduling OS feature must be turned on
    eHardwareSchedulingRequired = 1 << 4,

    //! All feature requirement flags.  This isn't expected to be used directly by integrations, but may be useful for e.g. writing helpers.
    eAll = eD3D11Supported | eD3D12Supported | eVulkanSupported | eVSyncOffRequired | eHardwareSchedulingRequired
};

SL_ENUM_OPERATORS_32(FeatureRequirementFlags);

//! Specifies feature requirements
//! 
//! {66714097-AC6D-4BC6-8915-1E0F55A6B61F}
SL_STRUCT_BEGIN(FeatureRequirements, StructType({ 0x66714097, 0xac6d, 0x4bc6, { 0x89, 0x15, 0x1e, 0xf, 0x55, 0xa6, 0xb6, 0x1f } }), kStructVersion2)
    //! Various Flags
    FeatureRequirementFlags flags {};
    
    //! Feature will create this many CPU threads
    uint32_t maxNumCPUThreads{};

    //! Feature supports only this many viewports
    uint32_t maxNumViewports{};
    
    //! Required buffer tags
    uint32_t numRequiredTags{};
    const BufferType* requiredTags{};

    //! OS and Driver versions
    Version osVersionDetected{};
    Version osVersionRequired{};
    Version driverVersionDetected{};
    Version driverVersionRequired{};

    //! Vulkan specific bits
    
    //! Command queues
    uint32_t vkNumComputeQueuesRequired{};
    uint32_t vkNumGraphicsQueuesRequired{};
    
    //! Device extensions
    uint32_t vkNumDeviceExtensions{};
    const char** vkDeviceExtensions{};
    //! Instance extensions
    uint32_t vkNumInstanceExtensions{};
    const char** vkInstanceExtensions{};
    //! 1.2 features
    //! 
    //! NOTE: Use getVkPhysicalDeviceVulkan12Features from sl_helpers_vk.h
    uint32_t vkNumFeatures12{};
    const char** vkFeatures12{};
    //! 1.3 features
    //! 
    //! NOTE: Use getVkPhysicalDeviceVulkan13Features from sl_helpers_vk.h
    uint32_t vkNumFeatures13{};
    const char** vkFeatures13{};

    //! Vulkan optical flow feature
    uint32_t vkNumOpticalFlowQueuesRequired{};

    //! IMPORTANT: New members go here or if optional can be chained in a new struct, see sl_struct.h for details
SL_STRUCT_END()

//! Specifies feature's version
//! 
//! {6D5B51F0-076B-486D-9995-5A561043F5C1}
SL_STRUCT_BEGIN(FeatureVersion, StructType({ 0x6d5b51f0, 0x76b, 0x486d, { 0x99, 0x95, 0x5a, 0x56, 0x10, 0x43, 0xf5, 0xc1 } }), kStructVersion1)
    //! SL version
    Version versionSL{};
    //! NGX version (if feature is using NGX, null otherwise)
    Version versionNGX{};

    //! IMPORTANT: New members go here or if optional can be chained in a new struct, see sl_struct.h for details
SL_STRUCT_END()

//! Specifies either DXGI adapter or VK physical device
//! 
//! {0677315F-A746-4492-9F42-CB6142C9C3D4}
SL_STRUCT_BEGIN(AdapterInfo, StructType({ 0x677315f, 0xa746, 0x4492, { 0x9f, 0x42, 0xcb, 0x61, 0x42, 0xc9, 0xc3, 0xd4 } }), kStructVersion1)
    //! Locally unique identifier
    uint8_t* deviceLUID {};
    //! Size in bytes
    uint32_t deviceLUIDSizeInBytes{};
    //! Vulkan Specific, if specified LUID will be ignored
    void* vkPhysicalDevice{};

    //! IMPORTANT: New members go here or if optional can be chained in a new struct, see sl_struct.h for details
SL_STRUCT_END()

//! A simple array wrapper designed for safe use across DLL boundaries.
//! Since different DLLs may use different memory allocators, this class
//! relies on an IAllocator pointer to ensure that memory is allocated
//! and freed consistently within the same runtime.
struct IAllocator
{
    virtual ~IAllocator() = default;
    virtual void *allocate(uint32_t nBytes) = 0;
    virtual void free(void *p) = 0;
};
template <class T>
struct Array
{
    inline Array() {}
    inline ~Array() { destroy(); }
    inline uint32_t size() const { return m_size; }
    inline void copyFrom(IAllocator *pAllocator, const std::vector<T>& src)
    {
        // if they give us data - we need the allocator
        assert(pAllocator || src.size() == 0);
        destroy();
        if (src.size() == 0) return;
        m_pAllocator = pAllocator;
        m_size = static_cast<uint32_t>(src.size());
        m_pData = (T*)m_pAllocator->allocate(m_size * sizeof(T));
        for (uint32_t i = 0; i < m_size; ++i)
            m_pData[i] = src[i];
    }
    inline void copyTo(std::vector<T>& dst)
    {
        dst.resize(m_size);
        for (uint32_t i = 0; i < m_size; ++i)
            dst[i] = m_pData[i];
    }
    inline T& operator[](uint32_t index)
    {
        assert(index < m_size);
        return m_pData[index];
    }
    inline const T& operator[](uint32_t index) const
    {
        assert(index < m_size);
        return m_pData[index];
    }
    inline void destroy()
    {
        if (m_pData) m_pAllocator->free(m_pData);
        m_pAllocator = nullptr;
        m_pData = nullptr;
        m_size = 0;
    }
    // Prevent copying
    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;
  private:
    T* m_pData{};
    uint32_t m_size{};
    // the allocator that was used to allocate memory
    IAllocator *m_pAllocator{};
};

}
