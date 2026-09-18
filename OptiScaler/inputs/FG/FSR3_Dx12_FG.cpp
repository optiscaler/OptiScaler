#include "pch.h"
#include "FSR3_Dx12_FG.h"

#include "Config.h"
#include "Util.h"
#include "MathUtils.h"

#include "resource.h"
#include "NVNGX_Parameter.h"

#include <framegen/ffx/FSRFG_Dx12.h>
#include <framegen/xefg/XeFG_Dx12.h>

#include "shaders/depth_scale/DS_Dx12.h"

#include <proxies/Ntdll_Proxy.h>
#include <proxies/KernelBase_Proxy.h>

#include <scanner/scanner.h>
#include <detours/detours.h>

#include "fsr3/ffx_fsr3.h"
#include "fsr3/dx12/ffx_dx12.h"
#include "fsr3/ffx_frameinterpolation.h"

const UINT fgContext = 0x1337;
using namespace OptiMath;

// Swapchain create
typedef FFX_API
    Fsr3::FfxErrorCode (*PFN_ffxReplaceSwapchainForFrameinterpolationDX12)(Fsr3::FfxCommandQueue gameQueue,
                                                                           Fsr3::FfxSwapchain& gameSwapChain);

typedef FFX_API Fsr3::FfxErrorCode (*PFN_ffxCreateFrameinterpolationSwapchainDX12)(
    DXGI_SWAP_CHAIN_DESC* desc, ID3D12CommandQueue* queue, IDXGIFactory* dxgiFactory,
    Fsr3::FfxSwapchain& outGameSwapChain);

typedef FFX_API Fsr3::FfxErrorCode (*PFN_ffxCreateFrameinterpolationSwapchainForHwndDX12)(
    HWND hWnd, DXGI_SWAP_CHAIN_DESC1* desc1, DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc, ID3D12CommandQueue* queue,
    IDXGIFactory* dxgiFactory, Fsr3::FfxSwapchain& outGameSwapChain);

typedef FFX_API Fsr3::FfxErrorCode (*PFN_ffxWaitForPresents)(Fsr3::FfxSwapchain gameSwapChain);

typedef FFX_API Fsr3::FfxErrorCode (*PFN_ffxRegisterFrameinterpolationUiResourceDX12)(Fsr3::FfxSwapchain gameSwapChain,
                                                                                      Fsr3::FfxResource uiResource);

typedef FFX_API
    Fsr3::FfxErrorCode (*PFN_ffxGetFrameinterpolationCommandlistDX12)(Fsr3::FfxSwapchain gameSwapChain,
                                                                      Fsr3::FfxCommandList& gameCommandlist);

typedef FFX_API Fsr3::FfxResource (*PFN_ffxGetFrameinterpolationTextureDX12)(Fsr3::FfxSwapchain gameSwapChain);

// Context
typedef FFX_API Fsr3::FfxErrorCode (*PFN_ffxFrameInterpolationContextCreate)(
    FfxFrameInterpolationContext* context, FfxFrameInterpolationContextDescription* contextDescription);

typedef FFX_API
    Fsr3::FfxErrorCode (*PFN_ffxFrameInterpolationDispatch)(FfxFrameInterpolationContext* context,
                                                            FfxFrameInterpolationDispatchDescription* params);

typedef FFX_API Fsr3::FfxErrorCode (*PFN_ffxFrameInterpolationContextDestroy)(FfxFrameInterpolationContext* context);

typedef FFX_API Fsr3::FfxErrorCode (*PFN_ffxFsr3ConfigureFrameGeneration)(void* context,
                                                                          Fsr3::FfxFrameGenerationConfig* config);

typedef FFX_API
    Fsr3::FfxErrorCode (*PFN_ffxSetFrameGenerationConfigToSwapchainDX12)(Fsr3::FfxFrameGenerationConfig const* config);

// Swapchain
static PFN_ffxReplaceSwapchainForFrameinterpolationDX12 o_ffxReplaceSwapchainForFrameinterpolationDX12 = nullptr;
static PFN_ffxCreateFrameinterpolationSwapchainDX12 o_ffxCreateFrameinterpolationSwapchainDX12 = nullptr;
static PFN_ffxCreateFrameinterpolationSwapchainForHwndDX12 o_ffxCreateFrameinterpolationSwapchainForHwndDX12 = nullptr;
static PFN_ffxWaitForPresents o_ffxWaitForPresents = nullptr;
static PFN_ffxRegisterFrameinterpolationUiResourceDX12 o_ffxRegisterFrameinterpolationUiResourceDX12 = nullptr;
static PFN_ffxGetFrameinterpolationCommandlistDX12 o_ffxGetFrameinterpolationCommandlistDX12 = nullptr;
static PFN_ffxGetFrameinterpolationTextureDX12 o_ffxGetFrameinterpolationTextureDX12 = nullptr;
static PFN_ffxSetFrameGenerationConfigToSwapchainDX12 o_ffxSetFrameGenerationConfigToSwapchainDX12 = nullptr;

// Context
static PFN_ffxFrameInterpolationContextCreate o_ffxFrameInterpolationContextCreate = nullptr;
static PFN_ffxFrameInterpolationDispatch o_ffxFrameInterpolationDispatch = nullptr;
static PFN_ffxFrameInterpolationContextDestroy o_ffxFrameInterpolationContextDestroy = nullptr;
static PFN_ffxFsr3ConfigureFrameGeneration o_ffxFsr3ConfigureFrameGeneration = nullptr;

static ID3D12Device* _device = nullptr;
static FG_Constants _fgConst {};

static Fsr3::FfxPresentCallbackFunc _presentCallback = nullptr;
static Fsr3::FfxFrameGenerationDispatchFunc _fgCallback = nullptr;

static std::mutex _newFrameMutex;

static ID3D12Resource* _hudless[BUFFER_COUNT] = {};
static ID3D12Resource* _interpolation[BUFFER_COUNT] = {};
static Dx12Resource _uiRes[BUFFER_COUNT] = {};
static bool _uiIndex[BUFFER_COUNT] = {};

static DS_Dx12* DepthScale = nullptr;

static Fsr3::FfxResourceStates GetFfxApiState(D3D12_RESOURCE_STATES state)
{
    switch (state)
    {
    case D3D12_RESOURCE_STATE_COMMON:
        return Fsr3::FFX_RESOURCE_STATE_COMMON;
    case D3D12_RESOURCE_STATE_UNORDERED_ACCESS:
        return Fsr3::FFX_RESOURCE_STATE_UNORDERED_ACCESS;
    case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:
        return Fsr3::FFX_RESOURCE_STATE_COMPUTE_READ;
    case D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:
        return Fsr3::FFX_RESOURCE_STATE_PIXEL_READ;
    case (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE):
        return Fsr3::FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ;
    case D3D12_RESOURCE_STATE_COPY_SOURCE:
        return Fsr3::FFX_RESOURCE_STATE_COPY_SRC;
    case D3D12_RESOURCE_STATE_COPY_DEST:
        return Fsr3::FFX_RESOURCE_STATE_COPY_DEST;
    case D3D12_RESOURCE_STATE_GENERIC_READ:
        return Fsr3::FFX_RESOURCE_STATE_GENERIC_READ;
    case D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT:
        return Fsr3::FFX_RESOURCE_STATE_INDIRECT_ARGUMENT;
    case D3D12_RESOURCE_STATE_RENDER_TARGET:
        return Fsr3::FFX_RESOURCE_STATE_RENDER_TARGET;
    default:
        return Fsr3::FFX_RESOURCE_STATE_COMMON;
    }
}

static D3D12_RESOURCE_STATES GetD3D12State(Fsr3::FfxResourceStates state)
{
    switch (state)
    {
    case Fsr3::FFX_RESOURCE_STATE_COMMON:
        return D3D12_RESOURCE_STATE_COMMON;
    case Fsr3::FFX_RESOURCE_STATE_UNORDERED_ACCESS:
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case Fsr3::FFX_RESOURCE_STATE_COMPUTE_READ:
        return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case Fsr3::FFX_RESOURCE_STATE_PIXEL_READ:
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case Fsr3::FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ:
        return (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    case Fsr3::FFX_RESOURCE_STATE_COPY_SRC:
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case Fsr3::FFX_RESOURCE_STATE_COPY_DEST:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    case Fsr3::FFX_RESOURCE_STATE_GENERIC_READ:
        return D3D12_RESOURCE_STATE_GENERIC_READ;
    case Fsr3::FFX_RESOURCE_STATE_INDIRECT_ARGUMENT:
        return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    case Fsr3::FFX_RESOURCE_STATE_RENDER_TARGET:
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    default:
        return D3D12_RESOURCE_STATE_COMMON;
    }
}

Fsr3::FfxSurfaceFormat ffxGetSurfaceFormatDX12Local(DXGI_FORMAT format)
{
    switch (format)
    {

    case (DXGI_FORMAT_R32G32B32A32_TYPELESS):
        return Fsr3::FFX_SURFACE_FORMAT_R32G32B32A32_TYPELESS;
    case (DXGI_FORMAT_R32G32B32A32_FLOAT):
        return Fsr3::FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R32G32B32A32_UINT:
        return Fsr3::FFX_SURFACE_FORMAT_R32G32B32A32_UINT;

    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case (DXGI_FORMAT_R16G16B16A16_FLOAT):
        return Fsr3::FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;

    case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G32_FLOAT:
        return Fsr3::FFX_SURFACE_FORMAT_R32G32_FLOAT;

    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        return Fsr3::FFX_SURFACE_FORMAT_R32_FLOAT;

    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        return Fsr3::FFX_SURFACE_FORMAT_R32_UINT;

    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
        return Fsr3::FFX_SURFACE_FORMAT_R8_UINT;

    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return Fsr3::FFX_SURFACE_FORMAT_R10G10B10A2_UNORM;

    case (DXGI_FORMAT_R11G11B10_FLOAT):
        return Fsr3::FFX_SURFACE_FORMAT_R11G11B10_FLOAT;

    case (DXGI_FORMAT_R8G8B8A8_TYPELESS):
        return Fsr3::FFX_SURFACE_FORMAT_R8G8B8A8_TYPELESS;

    case (DXGI_FORMAT_R8G8B8A8_UNORM):
        return Fsr3::FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;

    case (DXGI_FORMAT_R8G8B8A8_UNORM_SRGB):
        return Fsr3::FFX_SURFACE_FORMAT_R8G8B8A8_SRGB;

    case DXGI_FORMAT_R8G8B8A8_SNORM:
        return Fsr3::FFX_SURFACE_FORMAT_R8G8B8A8_SNORM;

    case DXGI_FORMAT_R16G16_TYPELESS:
    case (DXGI_FORMAT_R16G16_FLOAT):
        return Fsr3::FFX_SURFACE_FORMAT_R16G16_FLOAT;

    case (DXGI_FORMAT_R16G16_UINT):
        return Fsr3::FFX_SURFACE_FORMAT_R16G16_UINT;

    case DXGI_FORMAT_R32_UINT:
        return Fsr3::FFX_SURFACE_FORMAT_R32_UINT;

    case DXGI_FORMAT_R32_TYPELESS:
    case (DXGI_FORMAT_D32_FLOAT):
    case (DXGI_FORMAT_R32_FLOAT):
        return Fsr3::FFX_SURFACE_FORMAT_R32_FLOAT;

    case DXGI_FORMAT_R8G8_TYPELESS:
    case (DXGI_FORMAT_R8G8_UINT):
        return Fsr3::FFX_SURFACE_FORMAT_R8G8_UINT;

    case DXGI_FORMAT_R16_TYPELESS:
    case (DXGI_FORMAT_R16_FLOAT):
        return Fsr3::FFX_SURFACE_FORMAT_R16_FLOAT;

    case (DXGI_FORMAT_R16_UINT):
        return Fsr3::FFX_SURFACE_FORMAT_R16_UINT;

    case DXGI_FORMAT_D16_UNORM:
    case (DXGI_FORMAT_R16_UNORM):
        return Fsr3::FFX_SURFACE_FORMAT_R16_UNORM;
    case (DXGI_FORMAT_R16_SNORM):
        return Fsr3::FFX_SURFACE_FORMAT_R16_SNORM;

    case DXGI_FORMAT_R8_TYPELESS:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_A8_UNORM:
        return Fsr3::FFX_SURFACE_FORMAT_R8_UNORM;

    case DXGI_FORMAT_R8_UINT:
        return Fsr3::FFX_SURFACE_FORMAT_R8_UINT;

    case (DXGI_FORMAT_UNKNOWN):
        return Fsr3::FFX_SURFACE_FORMAT_UNKNOWN;

    default:
        return Fsr3::FFX_SURFACE_FORMAT_UNKNOWN;
    }
}

Fsr3::FfxResourceDescription GetFfxResourceDescriptionDX12Local(ID3D12Resource* pResource)
{
    Fsr3::FfxResourceDescription resourceDescription = {};

    // This is valid
    if (!pResource)
        return resourceDescription;

    if (pResource)
    {

        // Set flags properly for resource registration
        D3D12_RESOURCE_DESC desc = pResource->GetDesc();

        resourceDescription.flags = Fsr3::FFX_RESOURCE_FLAGS_NONE;
        resourceDescription.usage = Fsr3::FFX_RESOURCE_USAGE_READ_ONLY;

        if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
            resourceDescription.usage =
                (Fsr3::FfxResourceUsage) (resourceDescription.usage | Fsr3::FFX_RESOURCE_USAGE_UAV);

        resourceDescription.width = (uint32_t) desc.Width;
        resourceDescription.height = (uint32_t) desc.Height;
        resourceDescription.depth = desc.DepthOrArraySize;
        resourceDescription.mipCount = desc.MipLevels;
        resourceDescription.format = ffxGetSurfaceFormatDX12Local(desc.Format);

        resourceDescription.type = Fsr3::FFX_RESOURCE_TYPE_TEXTURE2D;
    }

    return resourceDescription;
}

Fsr3::FfxResource ffxGetResourceDX12Local(const ID3D12Resource* dx12Resource,
                                          Fsr3::FfxResourceDescription ffxResDescription, Fsr3::FfxResourceStates state)
{
    Fsr3::FfxResource resource = {};
    resource.resource = reinterpret_cast<void*>(const_cast<ID3D12Resource*>(dx12Resource));
    resource.state = state;
    resource.description = ffxResDescription;

    return resource;
}

static bool CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InResource, D3D12_RESOURCE_STATES InState,
                                 ID3D12Resource** OutResource)
{
    if (InDevice == nullptr || InResource == nullptr)
        return false;

    auto inDesc = InResource->GetDesc();

    if (*OutResource != nullptr)
    {
        auto bufDesc = (*OutResource)->GetDesc();

        if (bufDesc.Width != inDesc.Width || bufDesc.Height != inDesc.Height || bufDesc.Format != inDesc.Format)
        {
            (*OutResource)->Release();
            (*OutResource) = nullptr;
            LOG_WARN("Release {}x{}, new one: {}x{}", bufDesc.Width, bufDesc.Height, inDesc.Width, inDesc.Height);
        }
        else
        {
            return true;
        }
    }

    HRESULT hr;
    CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
    inDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    hr = InDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &inDesc, InState, nullptr,
                                           IID_PPV_ARGS(OutResource));

    if (hr != S_OK)
    {
        LOG_ERROR("CreateCommittedResource result: {:X}", (UINT64) hr);
        return false;
    }

    LOG_DEBUG("Created new one: {}x{}", inDesc.Width, inDesc.Height);
    return true;
}

static void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                            D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState)
{
    if (InBeforeState == InAfterState)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = InResource;
    barrier.Transition.StateBefore = InBeforeState;
    barrier.Transition.StateAfter = InAfterState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    InCommandList->ResourceBarrier(1, &barrier);
}

// Hook Methods
static Fsr3::FfxErrorCode hkffxReplaceSwapchainForFrameinterpolationDX12(Fsr3::FfxCommandQueue gameQueue,
                                                                         Fsr3::FfxSwapchain& gameSwapChain)
{
    if (State::Instance().currentFG != nullptr && State::Instance().currentFGSwapchain != nullptr)
    {
        gameSwapChain = State::Instance().currentFGSwapchain;
        return Fsr3::FFX_OK;
    }
    else
    {
        LOG_ERROR("currentFG: {:X}, currentFGSwapchain: {:X}", (size_t) State::Instance().currentFG,
                  (size_t) State::Instance().currentFGSwapchain);

        return Fsr3::FFX_ERROR_INVALID_ALIGNMENT;
    }
}

static Fsr3::FfxErrorCode hkffxCreateFrameinterpolationSwapchainDX12(DXGI_SWAP_CHAIN_DESC* desc,
                                                                     ID3D12CommandQueue* queue,
                                                                     IDXGIFactory* dxgiFactory,
                                                                     Fsr3::FfxSwapchain& outGameSwapChain)
{
    if (State::Instance().currentWrappedSwapchain != nullptr &&
        State::Instance().currentSwapchainDesc.OutputWindow == desc->OutputWindow)
    {
        auto refCount = State::Instance().currentWrappedSwapchain->Release();

        while (refCount > 0 && refCount < 0xffffff00)
        {
            refCount = State::Instance().currentWrappedSwapchain->Release();
        }

        State::Instance().currentWrappedSwapchain = nullptr;
    }

    auto result = dxgiFactory->CreateSwapChain(queue, desc, (IDXGISwapChain**) &outGameSwapChain);

    return result == S_OK ? Fsr3::FFX_OK : Fsr3::FFX_ERROR_BACKEND_API_ERROR;
}

static Fsr3::FfxErrorCode hkffxCreateFrameinterpolationSwapchainForHwndDX12(
    HWND hWnd, DXGI_SWAP_CHAIN_DESC1* desc1, DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc, ID3D12CommandQueue* queue,
    IDXGIFactory* dxgiFactory, Fsr3::FfxSwapchain& outGameSwapChain)
{
    IDXGIFactory2* df = nullptr;
    HRESULT result = E_ABORT;

    if (dxgiFactory->QueryInterface(IID_PPV_ARGS(&df)) == S_OK)
    {
        df->Release();

        if (State::Instance().currentWrappedSwapchain != nullptr &&
            State::Instance().currentSwapchainDesc.OutputWindow == hWnd)
        {
            auto refCount = State::Instance().currentWrappedSwapchain->Release();

            while (refCount > 0 && refCount < 0xffffff00)
            {
                refCount = State::Instance().currentWrappedSwapchain->Release();
            }

            State::Instance().currentWrappedSwapchain = nullptr;
        }

        result = df->CreateSwapChainForHwnd(queue, hWnd, desc1, fullscreenDesc, nullptr,
                                            (IDXGISwapChain1**) &outGameSwapChain);
    }

    return result == S_OK ? Fsr3::FFX_OK : Fsr3::FFX_ERROR_BACKEND_API_ERROR;
}

static Fsr3::FfxErrorCode hkffxWaitForPresents(Fsr3::FfxSwapchain gameSwapChain) { return Fsr3::FFX_OK; }

static Fsr3::FfxErrorCode hkffxRegisterFrameinterpolationUiResourceDX12(Fsr3::FfxSwapchain gameSwapChain,
                                                                        Fsr3::FfxResource uiResource)
{
    LOG_DEBUG("UiResource found 1: {:X}", (size_t) uiResource.resource);

    auto& s = State::Instance();
    auto fg = s.currentFG;

    if (fg->FrameGenerationContext() != nullptr && uiResource.resource != nullptr)
    {
        auto fIndex = fg->GetIndexWillBeDispatched();
        if (fIndex < 0)
            fIndex = fg->GetIndex();

        UINT width = 0;
        UINT height = 0;

        width = s.currentSwapchainDesc.BufferDesc.Width;
        height = s.currentSwapchainDesc.BufferDesc.Height;

        Dx12Resource ui {};
        ui.cmdList = nullptr; // Not sure about this
        ui.height = height;
        ui.resource = (ID3D12Resource*) uiResource.resource;
        ui.state = GetD3D12State((Fsr3::FfxResourceStates) uiResource.state);
        ui.type = FG_ResourceType::UIColor;
        ui.validity = FG_ResourceValidity::UntilPresent;
        ui.width = width;
        ui.left = 0;
        ui.top = 0;

        _uiRes[fIndex] = ui;
        _uiIndex[fIndex] = true;

        fg->SetResource(&ui);
    }

    return Fsr3::FFX_OK;
}

static Fsr3::FfxErrorCode hkffxGetFrameinterpolationCommandlistDX12(Fsr3::FfxSwapchain gameSwapChain,
                                                                    Fsr3::FfxCommandList& gameCommandlist)
{
    auto fg = State::Instance().currentFG;

    if (fg != nullptr)
    {
        gameCommandlist = fg->GetUICommandList(fg->GetIndexWillBeDispatched());
        LOG_DEBUG("Returning cmdList: {:X}", (size_t) gameCommandlist);
    }

    return Fsr3::FFX_OK;
}

static Fsr3::FfxResource hkffxGetFrameinterpolationTextureDX12(Fsr3::FfxSwapchain gameSwapChain)
{
    LOG_FUNC();

    auto fg = State::Instance().currentFG;

    if (fg == nullptr)
        return {};

    auto fIndex = fg->GetIndexWillBeDispatched();
    if (fIndex < 0)
        fIndex = fg->GetIndex();

    IDXGISwapChain3* sc = (IDXGISwapChain3*) State::Instance().currentFGSwapchain;
    auto scIndex = sc->GetCurrentBackBufferIndex();

    ID3D12Resource* currentBuffer = nullptr;
    auto hr = sc->GetBuffer(scIndex, IID_PPV_ARGS(&currentBuffer));
    if (hr != S_OK)
    {
        LOG_ERROR("sc->GetBuffer error: {:X}", (UINT) hr);
        return {};
    }

    if (currentBuffer == nullptr)
    {
        LOG_ERROR("currentBuffer is nullptr!");
        return {};
    }

    currentBuffer->SetName(std::format(L"currentBuffer[{}]", scIndex).c_str());
    currentBuffer->Release();

    if (CreateBufferResource(_device, currentBuffer, D3D12_RESOURCE_STATE_COMMON, &_interpolation[fIndex]))
        _interpolation[fIndex]->SetName(std::format(L"_interpolation[{}]", fIndex).c_str());

    return ffxGetResourceDX12Local(_interpolation[fIndex], GetFfxResourceDescriptionDX12Local(_interpolation[fIndex]),
                                   Fsr3::FFX_RESOURCE_STATE_COMMON);
}

static Fsr3::FfxErrorCode
hkffxFrameInterpolationContextCreate(FfxFrameInterpolationContext* context,
                                     FfxFrameInterpolationContextDescription* contextDescription)
{
    if (context == nullptr || contextDescription == nullptr)
        return Fsr3::FFX_ERROR_INVALID_ARGUMENT;

    auto fg = State::Instance().currentFG;

    if (fg == nullptr)
    {
        LOG_ERROR("No FG Feature!");
        return Fsr3::FFX_ERROR_NULL_DEVICE;
    }

    if (State::Instance().currentFG->FrameGenerationContext() != nullptr)
    {
        LOG_INFO("There is already an active FG context: {:X}, destroying it.",
                 (size_t) State::Instance().currentFG->FrameGenerationContext());

        State::Instance().currentFG->DestroyFGContext();
    }

    _fgConst = {};

    _fgConst.displayHeight = contextDescription->displaySize.height;
    _fgConst.displayWidth = contextDescription->displaySize.width;

    auto uf = State::Instance().currentFeature;

    if (uf != nullptr && !uf->LowResMV())
        _fgConst.flags |= FG_Flags::DisplayResolutionMVs;

    if (uf != nullptr && !uf->JitteredMV())
        _fgConst.flags |= FG_Flags::JitteredMVs;

    if ((contextDescription->flags & FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INVERTED) > 0)
        _fgConst.flags |= FG_Flags::InvertedDepth;

    if ((contextDescription->flags & FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INFINITE) > 0)
        _fgConst.flags |= FG_Flags::InfiniteDepth;

    if ((contextDescription->flags & FFX_FRAMEINTERPOLATION_ENABLE_HDR_COLOR_INPUT) > 0)
        _fgConst.flags |= FG_Flags::Hdr;

    Config::Instance()->FGXeFGDepthInverted = _fgConst.flags[FG_Flags::InvertedDepth];
    Config::Instance()->FGXeFGJitteredMV = _fgConst.flags[FG_Flags::JitteredMVs];
    Config::Instance()->FGXeFGHighResMV = _fgConst.flags[FG_Flags::DisplayResolutionMVs];
    LOG_DEBUG("XeFG DepthInverted: {}", Config::Instance()->FGXeFGDepthInverted.value_or_default());
    LOG_DEBUG("XeFG JitteredMV: {}", Config::Instance()->FGXeFGJitteredMV.value_or_default());
    LOG_DEBUG("XeFG HighResMV: {}", Config::Instance()->FGXeFGHighResMV.value_or_default());
    Config::Instance()->SaveXeFG();

    _device = State::Instance().currentD3D12Device;

    State::Instance().currentFG->CreateContext(_device, _fgConst);

    *context = {};
    context->data[0] = fgContext;

    return Fsr3::FFX_OK;
}

static Fsr3::FfxErrorCode hkffxFrameInterpolationDispatch(FfxFrameInterpolationContext* context,
                                                          FfxFrameInterpolationDispatchDescription* params)
{
    if (context == nullptr || params == nullptr)
        return Fsr3::FFX_ERROR_INVALID_ARGUMENT;

    auto& s = State::Instance();
    auto fg = s.currentFG;

    if (fg == nullptr)
    {
        LOG_ERROR("No FG Feature!");
        return Fsr3::FFX_ERROR_NULL_DEVICE;
    }

    float ratio = (float) params->interpolationRect.width / (float) params->interpolationRect.height;
    fg->SetCameraValues(params->cameraNear, params->cameraFar, params->cameraFovAngleVertical, ratio,
                        params->viewSpaceToMetersFactor);
    fg->SetInterpolationPos(params->interpolationRect.left, params->interpolationRect.top);
    fg->SetInterpolationRect(params->interpolationRect.width, params->interpolationRect.height);
    fg->SetFrameTimeDelta(params->frameTimeDelta);
    fg->SetReset(params->reset ? 1 : 0);

    if (params->currentBackBuffer_HUDLess.resource != nullptr && !fg->GetResource(FG_ResourceType::HudlessColor))
    {
        UINT width = params->interpolationRect.width;
        UINT height = params->interpolationRect.height;
        UINT left = params->interpolationRect.left;
        UINT top = params->interpolationRect.top;

        if (width == 0)
        {
            width = s.currentSwapchainDesc.BufferDesc.Width;
            height = s.currentSwapchainDesc.BufferDesc.Height;
            top = 0;
            left = 0;
        }

        Dx12Resource hudless {};
        hudless.cmdList = (ID3D12GraphicsCommandList*) params->commandList;
        hudless.height = height;
        hudless.resource = (ID3D12Resource*) params->currentBackBuffer_HUDLess.resource;
        hudless.state = GetD3D12State((Fsr3::FfxResourceStates) params->currentBackBuffer_HUDLess.state);
        hudless.type = FG_ResourceType::HudlessColor;
        hudless.validity = FG_ResourceValidity::ValidNow;
        hudless.width = width;
        hudless.top = top;
        hudless.left = left;
        fg->SetResource(&hudless);
    }

    if (_presentCallback != nullptr && params->currentBackBuffer.resource != nullptr &&
        !fg->GetResource(FG_ResourceType::HudlessColor))
    {
        UINT width = params->interpolationRect.width;
        UINT height = params->interpolationRect.height;
        UINT left = params->interpolationRect.left;
        UINT top = params->interpolationRect.top;

        if (width == 0)
        {
            width = s.currentSwapchainDesc.BufferDesc.Width;
            height = s.currentSwapchainDesc.BufferDesc.Height;
            top = 0;
            left = 0;
        }

        Dx12Resource hudless {};
        hudless.cmdList = (ID3D12GraphicsCommandList*) params->commandList;
        hudless.height = height;
        hudless.resource = (ID3D12Resource*) params->currentBackBuffer.resource;
        hudless.state = GetD3D12State((Fsr3::FfxResourceStates) params->currentBackBuffer.state);
        hudless.type = FG_ResourceType::HudlessColor;
        hudless.validity = FG_ResourceValidity::ValidNow;
        hudless.width = width;
        hudless.top = top;
        hudless.left = left;
        fg->SetResource(&hudless);
    }

    return Fsr3::FFX_OK;
}

static Fsr3::FfxErrorCode hkffxFrameInterpolationContextDestroy(FfxFrameInterpolationContext* context)
{
    if (context == nullptr)
        return Fsr3::FFX_ERROR_INVALID_ARGUMENT;

    LOG_DEBUG("");

    if (State::Instance().currentFG != nullptr && fgContext == context->data[0])
    {
        LOG_INFO("Destroying FG Context: {:X}", (size_t) State::Instance().currentFG);
        State::Instance().currentFG->DestroyFGContext();
    }

    return Fsr3::FFX_OK;
}

static Fsr3::FfxErrorCode hkffxFsr3ConfigureFrameGeneration(void* context, Fsr3::FfxFrameGenerationConfig* config)
{
    if (context == nullptr || config == nullptr)
        return Fsr3::FFX_ERROR_INVALID_ARGUMENT;

    auto& s = State::Instance();
    auto fg = s.currentFG;

    if (fg == nullptr)
    {
        LOG_ERROR("No FG Feature!");
        return Fsr3::FFX_ERROR_NULL_DEVICE;
    }

    if (fg->FrameGenerationContext() != nullptr)
    {
        LOG_DEBUG("frameGenerationEnabled: {} ", config->frameGenerationEnabled);

        s.fsrfgInputActive = config->frameGenerationEnabled;

        if (config->frameGenerationEnabled && !fg->IsActive() && Config::Instance()->FGEnabled.value_or_default())
        {
            if (!fg->IsPaused())
            {
                fg->Activate();
                fg->ResetCounters();
            }
        }
        else if (!config->frameGenerationEnabled && fg->IsActive())
        {
            fg->Deactivate();
            fg->ResetCounters();
        }

        UINT64 width = 0;
        UINT height = 0;
        UINT left = 0;
        UINT top = 0;

        fg->GetInterpolationPos(left, top);
        fg->GetInterpolationRect(width, height);

        if (width == 0)
        {
            width = s.currentSwapchainDesc.BufferDesc.Width;
            height = s.currentSwapchainDesc.BufferDesc.Height;
            top = 0;
            left = 0;
        }

        if (config->HUDLessColor.resource != nullptr)
        {
            Dx12Resource ui {};
            ui.cmdList = nullptr; // Not sure about this
            ui.height = height;
            ui.resource = (ID3D12Resource*) config->HUDLessColor.resource;
            ui.state = GetD3D12State((Fsr3::FfxResourceStates) config->HUDLessColor.state);
            ui.type = FG_ResourceType::HudlessColor;
            ui.validity = FG_ResourceValidity::UntilPresent;
            ui.width = width;
            ui.left = left;
            ui.top = top;

            _uiRes[fg->GetIndex()] = ui;

            fg->SetResource(&ui);
        }

        if (config->frameGenerationCallback != nullptr)
        {
            LOG_DEBUG("frameGenerationCallback exist");
            _fgCallback = config->frameGenerationCallback;
        }

        if (config->presentCallback != nullptr)
        {
            LOG_DEBUG("presentCallback exist");
            _presentCallback = config->presentCallback;
        }
    }

    return Fsr3::FFX_OK;
}

static Fsr3::FfxErrorCode hkffxSetFrameGenerationConfigToSwapchainDX12(Fsr3::FfxFrameGenerationConfig* config)
{
    if (config == nullptr)
        return Fsr3::FFX_ERROR_INVALID_ARGUMENT;

    auto& s = State::Instance();
    auto fg = s.currentFG;

    if (fg == nullptr)
    {
        LOG_ERROR("No FG Feature!");
        return Fsr3::FFX_ERROR_NULL_DEVICE;
    }

    if (fg->FrameGenerationContext() != nullptr)
    {
        LOG_DEBUG("frameGenerationEnabled: {} ", config->frameGenerationEnabled);

        s.fsrfgInputActive = config->frameGenerationEnabled;

        if (config->frameGenerationEnabled && !fg->IsActive() && Config::Instance()->FGEnabled.value_or_default())
        {
            if (!fg->IsPaused())
            {
                fg->Activate();
                fg->ResetCounters();
            }
        }
        else if (!config->frameGenerationEnabled && fg->IsActive())
        {
            fg->Deactivate();
            fg->ResetCounters();
        }

        UINT64 width = 0;
        UINT height = 0;
        UINT left = 0;
        UINT top = 0;

        fg->GetInterpolationPos(left, top);
        fg->GetInterpolationRect(width, height);

        if (width == 0)
        {
            width = s.currentSwapchainDesc.BufferDesc.Width;
            height = s.currentSwapchainDesc.BufferDesc.Height;
            top = 0;
            left = 0;
        }

        if (config->HUDLessColor.resource != nullptr && !fg->GetResource(FG_ResourceType::HudlessColor))
        {
            Dx12Resource ui {};
            ui.cmdList = nullptr; // Not sure about this
            ui.height = height;
            ui.resource = (ID3D12Resource*) config->HUDLessColor.resource;
            ui.state = GetD3D12State((Fsr3::FfxResourceStates) config->HUDLessColor.state);
            ui.type = FG_ResourceType::HudlessColor;
            ui.validity = FG_ResourceValidity::UntilPresent;
            ui.width = width;
            ui.left = left;
            ui.top = top;

            _uiRes[fg->GetIndex()] = ui;

            fg->SetResource(&ui);
        }

        if (config->frameGenerationCallback != nullptr)
        {
            LOG_DEBUG("frameGenerationCallback exist");
        }

        if (config->presentCallback != nullptr)
        {
            LOG_DEBUG("presentCallback exist");
            _presentCallback = config->presentCallback;
        }
    }

    return Fsr3::FFX_OK;
}

void FSR3FG::HookFSR3FGExeInputs()
{
    if (o_ffxReplaceSwapchainForFrameinterpolationDX12 != nullptr)
        return;

    LOG_INFO("Trying to hook FSR3-FG exe methods");

    // Swapchain
    o_ffxReplaceSwapchainForFrameinterpolationDX12 =
        (PFN_ffxReplaceSwapchainForFrameinterpolationDX12) KernelBaseProxy::GetProcAddress_()(
            exeModule, "ffxReplaceSwapchainForFrameinterpolationDX12");
    o_ffxCreateFrameinterpolationSwapchainDX12 =
        (PFN_ffxCreateFrameinterpolationSwapchainDX12) KernelBaseProxy::GetProcAddress_()(
            exeModule, "ffxCreateFrameinterpolationSwapchainDX12");
    o_ffxCreateFrameinterpolationSwapchainForHwndDX12 =
        (PFN_ffxCreateFrameinterpolationSwapchainForHwndDX12) KernelBaseProxy::GetProcAddress_()(
            exeModule, "ffxCreateFrameinterpolationSwapchainForHwndDX12");
    o_ffxWaitForPresents = (PFN_ffxWaitForPresents) KernelBaseProxy::GetProcAddress_()(exeModule, "ffxWaitForPresents");
    o_ffxRegisterFrameinterpolationUiResourceDX12 =
        (PFN_ffxRegisterFrameinterpolationUiResourceDX12) KernelBaseProxy::GetProcAddress_()(
            exeModule, "ffxRegisterFrameinterpolationUiResourceDX12");
    o_ffxGetFrameinterpolationCommandlistDX12 =
        (PFN_ffxGetFrameinterpolationCommandlistDX12) KernelBaseProxy::GetProcAddress_()(
            exeModule, "ffxGetFrameinterpolationCommandlistDX12");
    o_ffxGetFrameinterpolationTextureDX12 =
        (PFN_ffxGetFrameinterpolationTextureDX12) KernelBaseProxy::GetProcAddress_()(
            exeModule, "ffxGetFrameinterpolationTextureDX12");
    o_ffxSetFrameGenerationConfigToSwapchainDX12 =
        (PFN_ffxSetFrameGenerationConfigToSwapchainDX12) KernelBaseProxy::GetProcAddress_()(
            exeModule, "ffxSetFrameGenerationConfigToSwapchainDX12");

    // Context
    o_ffxFrameInterpolationContextCreate = (PFN_ffxFrameInterpolationContextCreate) KernelBaseProxy::GetProcAddress_()(
        exeModule, "ffxFrameInterpolationContextCreate");
    o_ffxFrameInterpolationDispatch = (PFN_ffxFrameInterpolationDispatch) KernelBaseProxy::GetProcAddress_()(
        exeModule, "ffxFrameInterpolationDispatch");
    o_ffxFrameInterpolationContextDestroy =
        (PFN_ffxFrameInterpolationContextDestroy) KernelBaseProxy::GetProcAddress_()(
            exeModule, "ffxFrameInterpolationContextDestroy");
    o_ffxFsr3ConfigureFrameGeneration = (PFN_ffxFsr3ConfigureFrameGeneration) KernelBaseProxy::GetProcAddress_()(
        exeModule, "ffxFsr3ConfigureFrameGeneration");

    LOG_DEBUG("ffxReplaceSwapchainForFrameinterpolationDX12: {:X}",
              (size_t) o_ffxReplaceSwapchainForFrameinterpolationDX12);
    LOG_DEBUG("ffxCreateFrameinterpolationSwapchainDX12: {:X}", (size_t) o_ffxCreateFrameinterpolationSwapchainDX12);
    LOG_DEBUG("ffxCreateFrameinterpolationSwapchainForHwndDX12: {:X}",
              (size_t) o_ffxCreateFrameinterpolationSwapchainForHwndDX12);
    LOG_DEBUG("ffxWaitForPresents: {:X}", (size_t) o_ffxWaitForPresents);
    LOG_DEBUG("ffxRegisterFrameinterpolationUiResourceDX12: {:X}",
              (size_t) o_ffxRegisterFrameinterpolationUiResourceDX12);
    LOG_DEBUG("ffxGetFrameinterpolationCommandlistDX12: {:X}", (size_t) o_ffxGetFrameinterpolationCommandlistDX12);
    LOG_DEBUG("ffxGetFrameinterpolationTextureDX12: {:X}", (size_t) o_ffxGetFrameinterpolationTextureDX12);
    LOG_DEBUG("ffxFrameInterpolationContextCreate: {:X}", (size_t) o_ffxFrameInterpolationContextCreate);
    LOG_DEBUG("ffxFrameInterpolationDispatch: {:X}", (size_t) o_ffxFrameInterpolationDispatch);
    LOG_DEBUG("ffxFrameInterpolationContextDestroy: {:X}", (size_t) o_ffxFrameInterpolationContextDestroy);
    LOG_DEBUG("ffxFsr3ConfigureFrameGeneration: {:X}", (size_t) o_ffxFsr3ConfigureFrameGeneration);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_ffxReplaceSwapchainForFrameinterpolationDX12 != nullptr)
        DetourAttach(&(PVOID&) o_ffxReplaceSwapchainForFrameinterpolationDX12,
                     hkffxReplaceSwapchainForFrameinterpolationDX12);
    if (o_ffxCreateFrameinterpolationSwapchainDX12 != nullptr)
        DetourAttach(&(PVOID&) o_ffxCreateFrameinterpolationSwapchainDX12, hkffxCreateFrameinterpolationSwapchainDX12);
    if (o_ffxCreateFrameinterpolationSwapchainForHwndDX12 != nullptr)
        DetourAttach(&(PVOID&) o_ffxCreateFrameinterpolationSwapchainForHwndDX12,
                     hkffxCreateFrameinterpolationSwapchainForHwndDX12);
    if (o_ffxWaitForPresents != nullptr)
        DetourAttach(&(PVOID&) o_ffxWaitForPresents, hkffxWaitForPresents);
    if (o_ffxRegisterFrameinterpolationUiResourceDX12 != nullptr)
        DetourAttach(&(PVOID&) o_ffxRegisterFrameinterpolationUiResourceDX12,
                     hkffxRegisterFrameinterpolationUiResourceDX12);
    if (o_ffxGetFrameinterpolationCommandlistDX12 != nullptr)
        DetourAttach(&(PVOID&) o_ffxGetFrameinterpolationCommandlistDX12, hkffxGetFrameinterpolationCommandlistDX12);
    if (o_ffxGetFrameinterpolationTextureDX12 != nullptr)
        DetourAttach(&(PVOID&) o_ffxGetFrameinterpolationTextureDX12, hkffxGetFrameinterpolationTextureDX12);
    if (o_ffxFrameInterpolationContextCreate != nullptr)
        DetourAttach(&(PVOID&) o_ffxFrameInterpolationContextCreate, hkffxFrameInterpolationContextCreate);
    if (o_ffxFrameInterpolationDispatch != nullptr)
        DetourAttach(&(PVOID&) o_ffxFrameInterpolationDispatch, hkffxFrameInterpolationDispatch);
    if (o_ffxFrameInterpolationContextDestroy != nullptr)
        DetourAttach(&(PVOID&) o_ffxFrameInterpolationContextDestroy, hkffxFrameInterpolationContextDestroy);
    if (o_ffxFsr3ConfigureFrameGeneration != nullptr)
        DetourAttach(&(PVOID&) o_ffxFsr3ConfigureFrameGeneration, hkffxFsr3ConfigureFrameGeneration);
    if (o_ffxSetFrameGenerationConfigToSwapchainDX12 != nullptr)
        DetourAttach(&(PVOID&) o_ffxSetFrameGenerationConfigToSwapchainDX12,
                     hkffxSetFrameGenerationConfigToSwapchainDX12);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to hook FSR3-FG exe methods, DetourTransactionCommit result: {:X}", detourResult);
        o_ffxReplaceSwapchainForFrameinterpolationDX12 = nullptr;
        o_ffxCreateFrameinterpolationSwapchainDX12 = nullptr;
        o_ffxCreateFrameinterpolationSwapchainForHwndDX12 = nullptr;
        o_ffxWaitForPresents = nullptr;
        o_ffxRegisterFrameinterpolationUiResourceDX12 = nullptr;
        o_ffxGetFrameinterpolationCommandlistDX12 = nullptr;
        o_ffxGetFrameinterpolationTextureDX12 = nullptr;
        o_ffxFrameInterpolationContextCreate = nullptr;
        o_ffxFrameInterpolationDispatch = nullptr;
        o_ffxFrameInterpolationContextDestroy = nullptr;
        o_ffxFsr3ConfigureFrameGeneration = nullptr;
        o_ffxSetFrameGenerationConfigToSwapchainDX12 = nullptr;
    }
    else
    {
        State::Instance().fsrHooks = o_ffxReplaceSwapchainForFrameinterpolationDX12 != nullptr;
    }
}

void FSR3FG::HookFSR3FGInputs()
{
    if (o_ffxReplaceSwapchainForFrameinterpolationDX12 != nullptr)
        return;

    LOG_INFO("Trying to hook FSR3-FG dll methods");

    auto backend = KernelBaseProxy::GetModuleHandleW_()(L"ffx_backend_dx12_x64.dll");
    if (backend == nullptr)
        backend = NtdllProxy::LoadLibraryExW_Ldr(L"ffx_backend_dx12_x64.dll", NULL, 0);

    auto fg = KernelBaseProxy::GetModuleHandleW_()(L"ffx_frameinterpolation_x64.dll");
    if (fg == nullptr)
        fg = NtdllProxy::LoadLibraryExW_Ldr(L"ffx_frameinterpolation_x64.dll", NULL, 0);

    if (backend == nullptr || fg == nullptr)
    {
        LOG_DEBUG("Exitting backend: {:X}, fg: {:X}", (size_t) backend, (size_t) fg);
        return;
    }

    // Swapchain
    o_ffxReplaceSwapchainForFrameinterpolationDX12 =
        (PFN_ffxReplaceSwapchainForFrameinterpolationDX12) KernelBaseProxy::GetProcAddress_()(
            backend, "ffxReplaceSwapchainForFrameinterpolationDX12");
    o_ffxCreateFrameinterpolationSwapchainDX12 =
        (PFN_ffxCreateFrameinterpolationSwapchainDX12) KernelBaseProxy::GetProcAddress_()(
            backend, "ffxCreateFrameinterpolationSwapchainDX12");
    o_ffxCreateFrameinterpolationSwapchainForHwndDX12 =
        (PFN_ffxCreateFrameinterpolationSwapchainForHwndDX12) KernelBaseProxy::GetProcAddress_()(
            backend, "ffxCreateFrameinterpolationSwapchainForHwndDX12");
    o_ffxWaitForPresents = (PFN_ffxWaitForPresents) KernelBaseProxy::GetProcAddress_()(backend, "ffxWaitForPresents");
    o_ffxRegisterFrameinterpolationUiResourceDX12 =
        (PFN_ffxRegisterFrameinterpolationUiResourceDX12) KernelBaseProxy::GetProcAddress_()(
            backend, "ffxRegisterFrameinterpolationUiResourceDX12");
    o_ffxGetFrameinterpolationCommandlistDX12 =
        (PFN_ffxGetFrameinterpolationCommandlistDX12) KernelBaseProxy::GetProcAddress_()(
            backend, "ffxGetFrameinterpolationCommandlistDX12");
    o_ffxGetFrameinterpolationTextureDX12 =
        (PFN_ffxGetFrameinterpolationTextureDX12) KernelBaseProxy::GetProcAddress_()(
            backend, "ffxGetFrameinterpolationTextureDX12");
    o_ffxSetFrameGenerationConfigToSwapchainDX12 =
        (PFN_ffxSetFrameGenerationConfigToSwapchainDX12) KernelBaseProxy::GetProcAddress_()(
            backend, "ffxSetFrameGenerationConfigToSwapchainDX12");

    // Context
    o_ffxFrameInterpolationContextCreate = (PFN_ffxFrameInterpolationContextCreate) KernelBaseProxy::GetProcAddress_()(
        fg, "ffxFrameInterpolationContextCreate");
    o_ffxFrameInterpolationDispatch =
        (PFN_ffxFrameInterpolationDispatch) KernelBaseProxy::GetProcAddress_()(fg, "ffxFrameInterpolationDispatch");
    o_ffxFrameInterpolationContextDestroy =
        (PFN_ffxFrameInterpolationContextDestroy) KernelBaseProxy::GetProcAddress_()(
            fg, "ffxFrameInterpolationContextDestroy");
    o_ffxFsr3ConfigureFrameGeneration =
        (PFN_ffxFsr3ConfigureFrameGeneration) KernelBaseProxy::GetProcAddress_()(fg, "ffxFsr3ConfigureFrameGeneration");

    LOG_DEBUG("ffxReplaceSwapchainForFrameinterpolationDX12: {:X}",
              (size_t) o_ffxReplaceSwapchainForFrameinterpolationDX12);
    LOG_DEBUG("ffxCreateFrameinterpolationSwapchainDX12: {:X}", (size_t) o_ffxCreateFrameinterpolationSwapchainDX12);
    LOG_DEBUG("ffxCreateFrameinterpolationSwapchainForHwndDX12: {:X}",
              (size_t) o_ffxCreateFrameinterpolationSwapchainForHwndDX12);
    LOG_DEBUG("ffxWaitForPresents: {:X}", (size_t) o_ffxWaitForPresents);
    LOG_DEBUG("ffxRegisterFrameinterpolationUiResourceDX12: {:X}",
              (size_t) o_ffxRegisterFrameinterpolationUiResourceDX12);
    LOG_DEBUG("ffxGetFrameinterpolationCommandlistDX12: {:X}", (size_t) o_ffxGetFrameinterpolationCommandlistDX12);
    LOG_DEBUG("ffxGetFrameinterpolationTextureDX12: {:X}", (size_t) o_ffxGetFrameinterpolationTextureDX12);
    LOG_DEBUG("ffxFrameInterpolationContextCreate: {:X}", (size_t) o_ffxFrameInterpolationContextCreate);
    LOG_DEBUG("ffxFrameInterpolationDispatch: {:X}", (size_t) o_ffxFrameInterpolationDispatch);
    LOG_DEBUG("ffxFrameInterpolationContextDestroy: {:X}", (size_t) o_ffxFrameInterpolationContextDestroy);
    LOG_DEBUG("ffxFsr3ConfigureFrameGeneration: {:X}", (size_t) o_ffxFsr3ConfigureFrameGeneration);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_ffxReplaceSwapchainForFrameinterpolationDX12 != nullptr)
        DetourAttach(&(PVOID&) o_ffxReplaceSwapchainForFrameinterpolationDX12,
                     hkffxReplaceSwapchainForFrameinterpolationDX12);
    if (o_ffxCreateFrameinterpolationSwapchainDX12 != nullptr)
        DetourAttach(&(PVOID&) o_ffxCreateFrameinterpolationSwapchainDX12, hkffxCreateFrameinterpolationSwapchainDX12);
    if (o_ffxCreateFrameinterpolationSwapchainForHwndDX12 != nullptr)
        DetourAttach(&(PVOID&) o_ffxCreateFrameinterpolationSwapchainForHwndDX12,
                     hkffxCreateFrameinterpolationSwapchainForHwndDX12);
    if (o_ffxWaitForPresents != nullptr)
        DetourAttach(&(PVOID&) o_ffxWaitForPresents, hkffxWaitForPresents);
    if (o_ffxRegisterFrameinterpolationUiResourceDX12 != nullptr)
        DetourAttach(&(PVOID&) o_ffxRegisterFrameinterpolationUiResourceDX12,
                     hkffxRegisterFrameinterpolationUiResourceDX12);
    if (o_ffxGetFrameinterpolationCommandlistDX12 != nullptr)
        DetourAttach(&(PVOID&) o_ffxGetFrameinterpolationCommandlistDX12, hkffxGetFrameinterpolationCommandlistDX12);
    if (o_ffxGetFrameinterpolationTextureDX12 != nullptr)
        DetourAttach(&(PVOID&) o_ffxGetFrameinterpolationTextureDX12, hkffxGetFrameinterpolationTextureDX12);
    if (o_ffxFrameInterpolationContextCreate != nullptr)
        DetourAttach(&(PVOID&) o_ffxFrameInterpolationContextCreate, hkffxFrameInterpolationContextCreate);
    if (o_ffxFrameInterpolationDispatch != nullptr)
        DetourAttach(&(PVOID&) o_ffxFrameInterpolationDispatch, hkffxFrameInterpolationDispatch);
    if (o_ffxFrameInterpolationContextDestroy != nullptr)
        DetourAttach(&(PVOID&) o_ffxFrameInterpolationContextDestroy, hkffxFrameInterpolationContextDestroy);
    if (o_ffxFsr3ConfigureFrameGeneration != nullptr)
        DetourAttach(&(PVOID&) o_ffxFsr3ConfigureFrameGeneration, hkffxFsr3ConfigureFrameGeneration);
    if (o_ffxSetFrameGenerationConfigToSwapchainDX12 != nullptr)
        DetourAttach(&(PVOID&) o_ffxSetFrameGenerationConfigToSwapchainDX12,
                     hkffxSetFrameGenerationConfigToSwapchainDX12);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to hook FSR3-FG dll methods, DetourTransactionCommit result: {:X}", detourResult);
        o_ffxReplaceSwapchainForFrameinterpolationDX12 = nullptr;
        o_ffxCreateFrameinterpolationSwapchainDX12 = nullptr;
        o_ffxCreateFrameinterpolationSwapchainForHwndDX12 = nullptr;
        o_ffxWaitForPresents = nullptr;
        o_ffxRegisterFrameinterpolationUiResourceDX12 = nullptr;
        o_ffxGetFrameinterpolationCommandlistDX12 = nullptr;
        o_ffxGetFrameinterpolationTextureDX12 = nullptr;
        o_ffxFrameInterpolationContextCreate = nullptr;
        o_ffxFrameInterpolationDispatch = nullptr;
        o_ffxFrameInterpolationContextDestroy = nullptr;
        o_ffxFsr3ConfigureFrameGeneration = nullptr;
        o_ffxSetFrameGenerationConfigToSwapchainDX12 = nullptr;
    }
    else
    {
        State::Instance().fsrHooks = o_ffxReplaceSwapchainForFrameinterpolationDX12 != nullptr;
    }
}

void FSR3FG::ffxPresentCallback()
{
    LOG_DEBUG("");

    if (_presentCallback == nullptr && _fgCallback == nullptr)
        return;

    auto fg = State::Instance().currentFG;

    if (fg == nullptr)
        return;

    auto fIndex = fg->GetIndexWillBeDispatched();

    ID3D12GraphicsCommandList* cmdList = nullptr;

    ID3D12Resource* currentBuffer = nullptr;

    if (_fgCallback != nullptr)
    {
        Fsr3::FfxFrameGenerationDispatchDescription ddfg {};

        ddfg.numInterpolatedFrames = 1;

        if (currentBuffer == nullptr)
        {
            IDXGISwapChain3* sc = (IDXGISwapChain3*) State::Instance().currentFGSwapchain;
            auto scIndex = sc->GetCurrentBackBufferIndex();

            auto hr = sc->GetBuffer(scIndex, IID_PPV_ARGS(&currentBuffer));
            if (hr != S_OK)
            {
                LOG_ERROR("sc->GetBuffer error: {:X}", (UINT) hr);
                return;
            }

            if (currentBuffer == nullptr)
            {
                LOG_ERROR("currentBuffer is nullptr!");
                return;
            }

            currentBuffer->Release();
            currentBuffer->SetName(std::format(L"currentBuffer[{}]", scIndex).c_str());
        }

        if (CreateBufferResource(_device, currentBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &_hudless[fIndex]))
            _hudless[fIndex]->SetName(std::format(L"_hudless[{}]", fIndex).c_str());
        else
            return;

        if (cmdList == nullptr)
            cmdList = fg->GetUICommandList(fIndex);

        ddfg.commandList = cmdList;

        ResourceBarrier(cmdList, currentBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ResourceBarrier(cmdList, _hudless[fIndex], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_COPY_DEST);

        cmdList->CopyResource(_hudless[fIndex], currentBuffer);

        ResourceBarrier(cmdList, currentBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE,
                        D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrier(cmdList, _hudless[fIndex], D3D12_RESOURCE_STATE_COPY_DEST,
                        D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        ddfg.outputs[0] = ffxGetResourceDX12Local(currentBuffer, GetFfxResourceDescriptionDX12Local(currentBuffer),
                                                  Fsr3::FFX_RESOURCE_STATE_GENERIC_READ);
        ddfg.presentColor =
            ffxGetResourceDX12Local(_hudless[fIndex], GetFfxResourceDescriptionDX12Local(_hudless[fIndex]),
                                    Fsr3::FFX_RESOURCE_STATE_GENERIC_READ);
        ddfg.reset = false;

        auto result = _fgCallback(&ddfg);

        ResourceBarrier(cmdList, currentBuffer,
                        D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_PRESENT);
        ResourceBarrier(cmdList, _hudless[fIndex],
                        D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        if (result == FFX_API_RETURN_OK)
        {
            if (!fg->GetResource(FG_ResourceType::HudlessColor, fIndex))
            {
                auto hDesc = _hudless[fIndex]->GetDesc();
                Dx12Resource hudless {};
                hudless.cmdList = cmdList;
                hudless.height = hDesc.Height;
                hudless.resource = _hudless[fIndex];
                hudless.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                hudless.type = FG_ResourceType::HudlessColor;
                hudless.validity = FG_ResourceValidity::ValidNow;
                hudless.width = hDesc.Width;
                fg->SetResource(&hudless);
            }
        }
        else
        {
            LOG_ERROR("Frame Generation callback failed: {:X}", (UINT) result);
        }

        _fgCallback = nullptr;
    }

    if (_presentCallback != nullptr)
    {
        if (fIndex < 0)
        {
            if (!fg->IsActive() || fg->IsPaused())
                fIndex = fg->GetIndex();
            else
                return;
        }

        Fsr3::FfxPresentCallbackDescription cdfgp {};
        cdfgp.device = _device;
        cdfgp.isInterpolatedFrame = false;

        if (currentBuffer == nullptr)
        {
            IDXGISwapChain3* sc = (IDXGISwapChain3*) State::Instance().currentFGSwapchain;
            auto scIndex = sc->GetCurrentBackBufferIndex();

            auto hr = sc->GetBuffer(scIndex, IID_PPV_ARGS(&currentBuffer));
            if (hr != S_OK)
            {
                LOG_ERROR("sc->GetBuffer error: {:X}", (UINT) hr);
                return;
            }

            if (currentBuffer == nullptr)
            {
                LOG_ERROR("currentBuffer is nullptr!");
                return;
            }

            currentBuffer->Release();
            currentBuffer->SetName(std::format(L"currentBuffer[{}]", scIndex).c_str());
        }

        if (CreateBufferResource(_device, currentBuffer, D3D12_RESOURCE_STATE_COMMON, &_hudless[fIndex]))
            _hudless[fIndex]->SetName(std::format(L"_hudless[{}]", fIndex).c_str());
        else
            return;

        if (cmdList == nullptr)
            cmdList = fg->GetUICommandList(fIndex);

        cdfgp.commandList = cmdList;

        ResourceBarrier(cmdList, currentBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ResourceBarrier(cmdList, _hudless[fIndex], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_COPY_DEST);

        cmdList->CopyResource(_hudless[fIndex], currentBuffer);

        ResourceBarrier(cmdList, currentBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
        ResourceBarrier(cmdList, _hudless[fIndex], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);

        cdfgp.outputSwapChainBuffer = ffxGetResourceDX12Local(
            currentBuffer, GetFfxResourceDescriptionDX12Local(currentBuffer), Fsr3::FFX_RESOURCE_STATE_PRESENT);
        cdfgp.currentBackBuffer = ffxGetResourceDX12Local(
            _hudless[fIndex], GetFfxResourceDescriptionDX12Local(_hudless[fIndex]), Fsr3::FFX_RESOURCE_STATE_PRESENT);

        if (_uiRes[fIndex].resource != nullptr && _uiIndex[fIndex])
            cdfgp.currentUI = ffxGetResourceDX12Local(_uiRes[fIndex].resource,
                                                      GetFfxResourceDescriptionDX12Local(_uiRes[fIndex].resource),
                                                      Fsr3::FFX_RESOURCE_STATE_PRESENT);

        auto result = _presentCallback(&cdfgp);

        ResourceBarrier(cmdList, _hudless[fIndex], D3D12_RESOURCE_STATE_COMMON,
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        if (result == FFX_API_RETURN_OK)
        {
            if (!fg->GetResource(FG_ResourceType::HudlessColor, fIndex))
            {
                auto hDesc = _hudless[fIndex]->GetDesc();
                Dx12Resource hudless {};
                hudless.cmdList = cmdList;
                hudless.height = hDesc.Height;
                hudless.resource = _hudless[fIndex];
                hudless.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                hudless.type = FG_ResourceType::HudlessColor;
                hudless.validity = Config::Instance()->FGHudlessValidNow.value_or_default()
                                       ? FG_ResourceValidity::ValidNow
                                       : FG_ResourceValidity::UntilPresent;
                hudless.width = hDesc.Width;
                fg->SetResource(&hudless);
            }
        }
        else
        {
            LOG_ERROR("Present callback failed: {:X}", (UINT) result);
        }

        _uiIndex[fIndex] = false;
        _presentCallback = nullptr;
    }
}

void FSR3FG::SetUpscalerInputs(ID3D12GraphicsCommandList* InCmdList, NVSDK_NGX_Parameter* InParameters,
                               IFeature_Dx12* feature)
{
    auto fg = State::Instance().currentFG;

    if (State::Instance().activeFgInput != FGInput::FSRFG30 || fg == nullptr || _device == nullptr)
        return;

    {
        std::lock_guard<std::mutex> lock(_newFrameMutex);
        fg->StartNewFrame();
        _uiRes[fg->GetIndex()] = {};
    }

    fg->EvaluateState(_device, _fgConst);

    // FSR Camera values
    float cameraNear = 0.0f;
    float cameraFar = 0.0f;
    float cameraVFov = 0.0f;
    float meterFactor = 0.0f;
    float mvScaleX = 0.0f;
    float mvScaleY = 0.0f;
    float jitterX = 0.0f;
    float jitterY = 0.0f;

    float tempCameraNear = 0.0f;
    float tempCameraFar = 0.0f;

    auto& state = State::Instance();
    auto& cfg = *Config::Instance();
    const auto& ngxParams = *InParameters;

    ngxParams.Get(OptiKeys::FSR_NearPlane, &tempCameraNear);
    ngxParams.Get(OptiKeys::FSR_FarPlane, &tempCameraFar);

    if (!cfg.FsrUseFsrInputValues.value_or_default() || (tempCameraNear == 0.0f && tempCameraFar == 0.0f))
    {
        if (feature->DepthInverted())
        {
            cameraFar = cfg.FsrCameraNear.value_or_default();
            cameraNear = cfg.FsrCameraFar.value_or_default();
        }
        else
        {
            cameraFar = cfg.FsrCameraFar.value_or_default();
            cameraNear = cfg.FsrCameraNear.value_or_default();
        }
    }
    else
    {
        cameraNear = tempCameraNear;
        cameraFar = tempCameraFar;
    }

    if (!cfg.FsrUseFsrInputValues.value_or_default() ||
        ngxParams.Get(OptiKeys::FSR_CameraFovVertical, &cameraVFov) != NVSDK_NGX_Result_Success)
    {
        if (cfg.FsrVerticalFov.has_value())
            cameraVFov = GetRadiansFromDeg(cfg.FsrVerticalFov.value());
        else if (cfg.FsrHorizontalFov.value_or_default() > 0.0f)
        {
            const float hFovRad = GetRadiansFromDeg(cfg.FsrHorizontalFov.value());
            cameraVFov =
                GetVerticalFovFromHorizontal(hFovRad, (float) feature->TargetWidth(), (float) feature->TargetHeight());
        }
        else
            cameraVFov = GetRadiansFromDeg(60);
    }

    if (!cfg.FsrUseFsrInputValues.value_or_default())
        ngxParams.Get(OptiKeys::FSR_ViewSpaceToMetersFactor, &meterFactor);

    State::Instance().lastFsrCameraFar = cameraFar;
    State::Instance().lastFsrCameraNear = cameraNear;

    int reset = 0;
    InParameters->Get(NVSDK_NGX_Parameter_Reset, &reset);

    InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &mvScaleX);
    InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &mvScaleY);
    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &jitterX);
    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &jitterY);

    auto aspectRatio = (float) feature->DisplayWidth() / (float) feature->DisplayHeight();
    fg->SetCameraValues(cameraNear, cameraFar, cameraVFov, aspectRatio, meterFactor);
    fg->SetFrameTimeDelta(State::Instance().lastFGFrameTime);
    fg->SetMVScale(mvScaleX, mvScaleY);
    fg->SetJitter(jitterX, jitterY);
    fg->SetReset(reset);
    fg->SetInterpolationRect(feature->DisplayWidth(), feature->DisplayHeight());

    // FG Prepare
    UINT frameIndex;
    if (!State::Instance().isShuttingDown && fg->IsActive() && Config::Instance()->FGEnabled.value_or_default() &&
        !fg->IsPaused() && State::Instance().currentSwapchain != nullptr)
    {
        //// Wait for present
        // if (fg->Mutex.getOwner() == 2)
        //{
        //     LOG_TRACE("Waiting for present!");
        //     fg->Mutex.lock(4);
        //     fg->Mutex.unlockThis(4);
        // }

        bool allocatorReset = false;
        frameIndex = fg->GetIndex();

        ID3D12GraphicsCommandList* commandList = nullptr;
        commandList = InCmdList;

        LOG_DEBUG("(FG) copy buffers for fgUpscaledImage[{}], frame: {}", frameIndex, fg->FrameCount());

        ID3D12Resource* paramVelocity = nullptr;
        if (InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, &paramVelocity) != NVSDK_NGX_Result_Success)
            InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &paramVelocity);

        if (paramVelocity != nullptr)
        {
            Dx12Resource setResource {};
            setResource.type = FG_ResourceType::Velocity;
            setResource.cmdList = commandList;
            setResource.resource = paramVelocity;
            setResource.state = (D3D12_RESOURCE_STATES) Config::Instance()->MVResourceBarrier.value_or(
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            setResource.validity = FG_ResourceValidity::ValidNow;

            if (feature->LowResMV())
            {
                setResource.width = feature->RenderWidth();
                setResource.height = feature->RenderHeight();
            }
            else
            {
                setResource.width = feature->TargetWidth();
                setResource.height = feature->TargetHeight();
            }

            fg->SetResource(&setResource);
        }

        ID3D12Resource* paramDepth = nullptr;
        if (InParameters->Get(NVSDK_NGX_Parameter_Depth, &paramDepth) != NVSDK_NGX_Result_Success)
            InParameters->Get(NVSDK_NGX_Parameter_Depth, (void**) &paramDepth);

        if (paramDepth != nullptr)
        {
            auto done = false;

            if (Config::Instance()->FGEnableDepthScale.value_or_default())
            {
                if (DepthScale == nullptr)
                    DepthScale = new DS_Dx12("Depth Scale", _device);

                if (DepthScale->CreateBufferResource(_device, paramDepth, feature->DisplayWidth(),
                                                     feature->DisplayHeight(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS) &&
                    DepthScale->Buffer() != nullptr)
                {
                    DepthScale->SetBufferState(InCmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                    if (DepthScale->Dispatch(InCmdList, paramDepth, DepthScale->Buffer()))
                    {
                        Dx12Resource setResource {};
                        setResource.type = FG_ResourceType::Depth;
                        setResource.cmdList = commandList;
                        setResource.resource = DepthScale->Buffer();
                        setResource.width = feature->RenderWidth();
                        setResource.height = feature->RenderHeight();
                        setResource.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                        setResource.validity = FG_ResourceValidity::UntilPresent;

                        fg->SetResource(&setResource);

                        done = true;
                    }
                }
            }

            if (!done)
            {
                Dx12Resource setResource {};
                setResource.type = FG_ResourceType::Depth;
                setResource.cmdList = commandList;
                setResource.resource = paramDepth;
                setResource.width = feature->RenderWidth();
                setResource.height = feature->RenderHeight();
                setResource.state = (D3D12_RESOURCE_STATES) Config::Instance()->DepthResourceBarrier.value_or(
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                setResource.validity = FG_ResourceValidity::ValidNow;

                fg->SetResource(&setResource);
            }
        }

        LOG_DEBUG("(FG) copy buffers done, frame: {0}", fg->FrameCount());
    }
}
