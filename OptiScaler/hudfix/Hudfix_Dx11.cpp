#include "pch.h"
#include "Hudfix_Dx11.h"

#include <Config.h>
#include <State.h>

#include <framegen/IFGFeature_Dx12.h>
#include <resource_tracking/ResTrack_dx11.h>
#include <with_dx12/with_dx12.h>

#include <algorithm>

namespace
{
int GetFormatGroup(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
        return 1;

    case DXGI_FORMAT_R32G32B32_TYPELESS:
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
        return 2;

    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
        return 3;

    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
        return 4;

    case DXGI_FORMAT_R11G11B10_FLOAT:
        return 5;

    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
        return 6;

    case DXGI_FORMAT_B5G6R5_UNORM:
        return 7;

    case DXGI_FORMAT_B5G5R5A1_UNORM:
        return 8;

    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return 9;

    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
        return 10;

    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return 11;

    default:
        return -1;
    }
}

static DXGI_FORMAT GetDx12InteropFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM;

    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM;

    default:
        return format;
    }
}

bool CompareResourceFormats(DXGI_FORMAT a, DXGI_FORMAT b)
{
    if (a == b)
        return true;

    const auto groupA = GetFormatGroup(a);
    const auto groupB = GetFormatGroup(b);
    return groupA >= 0 && groupA == groupB;
}

bool CaptureDescMatches(ID3D11Texture2D* texture, const D3D11_TEXTURE2D_DESC& wanted)
{
    if (texture == nullptr)
        return false;

    D3D11_TEXTURE2D_DESC current = {};
    texture->GetDesc(&current);

    return current.Width == wanted.Width && current.Height == wanted.Height && current.MipLevels == wanted.MipLevels &&
           current.ArraySize == wanted.ArraySize && current.Format == wanted.Format &&
           current.SampleDesc.Count == wanted.SampleDesc.Count &&
           current.SampleDesc.Quality == wanted.SampleDesc.Quality && current.Usage == wanted.Usage &&
           current.BindFlags == wanted.BindFlags && current.CPUAccessFlags == wanted.CPUAccessFlags &&
           current.MiscFlags == wanted.MiscFlags;
}

void TransitionResource(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource,
                        D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState)
{
    if (commandList == nullptr || resource == nullptr || beforeState == afterState)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = beforeState;
    barrier.Transition.StateAfter = afterState;
    commandList->ResourceBarrier(1, &barrier);
}

const char* SourceString(UINT source)
{
    switch (source & 0xFF)
    {
    case Dx11CaptureInfo::CreateRTV:
        return "RTV";
    case Dx11CaptureInfo::CreateSRV:
        return "SRV";
    case Dx11CaptureInfo::CreateUAV:
        return "UAV";
    case Dx11CaptureInfo::OMSetRTV:
        return "OM";
    case Dx11CaptureInfo::Upscaler:
        return "Ups";
    case Dx11CaptureInfo::SetCR:
        return "CR";
    case Dx11CaptureInfo::SetGR:
        return "GR";
    case Dx11CaptureInfo::SetOMUAV:
        return "OMUAV";
    default:
        return "?";
    }
}

const char* DispatchString(UINT source)
{
    if ((source & Dx11CaptureInfo::DrawIndexedInstanced) != 0)
        return "DII";
    if ((source & Dx11CaptureInfo::DrawInstanced) != 0)
        return "DI";
    if ((source & Dx11CaptureInfo::Dispatch) != 0)
        return "Disp";
    return "?";
}

const GUID kHudfixDx11IdentityGuid = { 0x3f9744d1, 0x5d18, 0x4d60, { 0xb4, 0x2e, 0x8b, 0x86, 0x8e, 0x43, 0x76, 0xa9 } };
} // namespace

int Hudfix_Dx11::GetIndex() { return static_cast<int>(_upscaleCounter % BUFFER_COUNT); }

UINT64 Hudfix_Dx11::GetResourceIdentity(ID3D11Texture2D* texture)
{
    if (texture == nullptr)
        return 0;

    UINT64 identity = 0;
    UINT size = sizeof(identity);
    if (SUCCEEDED(texture->GetPrivateData(kHudfixDx11IdentityGuid, &size, &identity)) && size == sizeof(identity) &&
        identity != 0)
    {
        return identity;
    }

    identity = _nextIdentity.fetch_add(1, std::memory_order_relaxed);
    if (identity == 0)
        identity = _nextIdentity.fetch_add(1, std::memory_order_relaxed);

    if (SUCCEEDED(texture->SetPrivateData(kHudfixDx11IdentityGuid, sizeof(identity), &identity)))
        return identity;

    if (!_identityFallbackWarning.exchange(true))
    {
        LOG_WARN("DX11 Hudfix resource private-data identity is unavailable; using pointer identity fallback. "
                 "Captured-resource persistence is less robust for this D3D11 wrapper/device.");
    }

    return static_cast<UINT64>(reinterpret_cast<uintptr_t>(texture)) | (1ull << 63);
}

void Hudfix_Dx11::ValidateCapturedIdentity(ID3D11Texture2D* texture, UINT64 identity)
{
    if (texture == nullptr || identity == 0)
        return;

    void* key = texture;
    if (auto it = _capturedIdentity.find(key); it != _capturedIdentity.end() && it->second != identity)
    {
        const auto previousIdentity = it->second;
        LOG_DEBUG("DX11 Hudfix resource address reused: {:X}, identity {} -> {}", (size_t) texture, previousIdentity,
                  identity);

        State::Instance().capturedHudlesses.erase(key);
        _hudlessList.erase(previousIdentity);

        {
            std::lock_guard<std::mutex> captureLock(_captureMutex);
            _captureList.erase(previousIdentity);
            State::Instance().fgCapturedResourceCount = _captureList.size();
        }

        _capturedIdentity.erase(it);
    }
}

bool Hudfix_Dx11::FillResourceInfo(ID3D11Texture2D* texture, Dx11ResourceType type, UINT captureInfo,
                                   Dx11ResourceInfo* outInfo)
{
    if (texture == nullptr || outInfo == nullptr)
        return false;

    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);

    outInfo->texture = texture;
    outInfo->width = desc.Width;
    outInfo->height = desc.Height;
    outInfo->arraySize = desc.ArraySize;
    outInfo->format = desc.Format;
    outInfo->bindFlags = desc.BindFlags;
    outInfo->miscFlags = desc.MiscFlags;
    outInfo->sampleCount = desc.SampleDesc.Count;
    outInfo->type = type;
    outInfo->extended = false;
    outInfo->captureInfo = captureInfo;
    return true;
}

bool Hudfix_Dx11::CheckCapture()
{
    const auto index = GetIndex();

    std::lock_guard<std::mutex> lock(_counterMutex);
    if (_captureCounter[index] > 999)
        return false;

    ++_captureCounter[index];

    if (_captureCounter[index] < Config::Instance()->FGHUDLimit.value_or_default())
        return false;

    return true;
}

void Hudfix_Dx11::HudlessFound()
{
    std::lock_guard<std::mutex> lock(_counterMutex);

    const auto index = GetIndex();
    if (_captureCounter[index] > 1000)
        return;

    _captureCounter[index] = 9999;
    _fgCounter = _upscaleCounter;
    _skipHudlessChecks = false;
}

void Hudfix_Dx11::UpscaleStart()
{
    auto& state = State::Instance();

    if (state.fgResetCapturedResources)
    {
        std::lock_guard<std::mutex> lock(_captureMutex);

        _captureList.clear();
        state.fgCapturedResourceCount = 0;
        state.fgResetCapturedResources = false;
    }

    if (state.clearCapturedHudlesses)
    {
        std::lock_guard<std::mutex> lock(_checkMutex);

        state.clearCapturedHudlesses = false;
        state.capturedHudlesses.clear();
        _capturedIdentity.clear();
    }
}

void Hudfix_Dx11::UpscaleEnd(UINT64 frameId, double lastFGFrameTime)
{
    UNREFERENCED_PARAMETER(frameId);

    std::lock_guard<std::mutex> lock(_counterMutex);

    ++_upscaleCounter;
    _frameTime = lastFGFrameTime;

    const auto index = GetIndex();
    _captureCounter[index] = 0;
    _skipHudlessChecks = false;
}

void Hudfix_Dx11::PresentStart()
{
    std::lock_guard<std::mutex> lock(_counterMutex);
    _fgCounter = _upscaleCounter;
}

void Hudfix_Dx11::PresentEnd() {}

UINT64 Hudfix_Dx11::ActiveUpscaleFrame()
{
    std::lock_guard<std::mutex> lock(_counterMutex);
    return _upscaleCounter;
}

UINT64 Hudfix_Dx11::ActivePresentFrame()
{
    std::lock_guard<std::mutex> lock(_counterMutex);
    return _fgCounter;
}

bool Hudfix_Dx11::IsResourceCheckActive()
{
    auto& state = State::Instance();

    if (_skipTracking || state.isShuttingDown)
        return false;

    {
        std::lock_guard<std::mutex> lock(_counterMutex);
        if (_upscaleCounter <= _fgCounter)
            return false;
    }

    if (!Config::Instance()->FGEnabled.value_or_default() || !Config::Instance()->FGHUDFix.value_or_default())
        return false;

    if (state.activeFgInput != FGInput::Upscaler || state.swapchainInteropApi != SwapchainInteropApi::Dx11wDx12)
        return false;

    if (state.currentFeature == nullptr || state.currentFG == nullptr || state.fgChanged)
        return false;

    if (!state.currentFG->IsActive())
        return false;

    return true;
}

bool Hudfix_Dx11::SkipHudlessChecks() { return _skipHudlessChecks; }

bool Hudfix_Dx11::CheckResource(Dx11ResourceInfo* resource)
{
    if (resource == nullptr || resource->texture == nullptr || State::Instance().isShuttingDown)
        return false;

    if (State::Instance().fgOnlyUseCapturedResources)
    {
        const auto identity = GetResourceIdentity(resource->texture);
        std::lock_guard<std::mutex> lock(_captureMutex);
        return identity != 0 && _captureList.find(identity) != _captureList.end();
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = resource->width;
    desc.Height = resource->height;
    desc.ArraySize = resource->arraySize;
    desc.Format = resource->format;
    desc.SampleDesc.Count = resource->sampleCount;
    desc.BindFlags = resource->bindFlags;
    desc.MiscFlags = resource->miscFlags;

    if (desc.Width == 0 || desc.Height == 0 || desc.ArraySize == 0)
        return false;

    if (desc.SampleDesc.Count != 1)
    {
        if (!_msaaWarning.exchange(true))
            LOG_WARN("DX11 Hudfix encountered an MSAA HUDless candidate; MSAA candidates are currently skipped.");

        return false;
    }

    if ((desc.BindFlags & (D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_DECODER | D3D11_BIND_VIDEO_ENCODER)) != 0)
        return false;

    auto& state = State::Instance();
    const UINT width = state.currentSwapchainDesc.BufferDesc.Width;
    const UINT height = state.currentSwapchainDesc.BufferDesc.Height;

    if (width == 0 || height == 0)
        return false;

    if (desc.Width != width || desc.Height != height)
    {
        const UINT toleranceX = width / 8;
        const UINT toleranceY = height / 8;

        if (resource->captureInfo != Dx11CaptureInfo::Upscaler &&
            !(Config::Instance()->FGRelaxedResolutionCheck.value_or_default() &&
              desc.Width >= width - std::min(width, toleranceX) && desc.Width <= width + toleranceX &&
              desc.Height >= height - std::min(height, toleranceY) && desc.Height <= height + toleranceY))
        {
            return false;
        }

        resource->extended = true;
    }

    if (CompareResourceFormats(desc.Format, state.currentSwapchainDesc.BufferDesc.Format))
        return true;

    return Config::Instance()->FGHUDFixExtended.value_or_default();
}

bool Hudfix_Dx11::CreateCaptureResource(ID3D11DeviceContext* context, const Dx11ResourceInfo& source, int index)
{
    if (context == nullptr || source.texture == nullptr || index < 0 || index >= BUFFER_COUNT)
        return false;

    auto& state = State::Instance();

    D3D11_TEXTURE2D_DESC wanted = {};
    wanted.Width = source.extended ? state.currentSwapchainDesc.BufferDesc.Width : source.width;
    wanted.Height = source.extended ? state.currentSwapchainDesc.BufferDesc.Height : source.height;
    wanted.MipLevels = 1;
    wanted.ArraySize = 1;
    wanted.Format = GetDx12InteropFormat(source.format);
    wanted.SampleDesc.Count = 1;
    wanted.SampleDesc.Quality = 0;
    wanted.Usage = D3D11_USAGE_DEFAULT;
    wanted.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    wanted.CPUAccessFlags = 0;

    if (Config::Instance()->DontUseNTShared.value_or_default())
        wanted.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    else
        wanted.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

    if (CaptureDescMatches(_captureBuffer[index], wanted))
        return true;

    Dx11WithDx12::ReleaseSharedResource(&_sharedCapture[index]);
    SAFE_RELEASE(_captureBuffer[index]);

    ID3D11Device* device = nullptr;
    context->GetDevice(&device);
    if (device == nullptr)
        return false;

    const HRESULT result = device->CreateTexture2D(&wanted, nullptr, &_captureBuffer[index]);
    device->Release();

    if (FAILED(result) || _captureBuffer[index] == nullptr)
    {
        LOG_WARN("DX11 Hudfix CreateTexture2D failed: {:X}, {}x{}, format {}", (UINT) result, wanted.Width,
                 wanted.Height, (UINT) wanted.Format);

        return false;
    }

    return true;
}

bool Hudfix_Dx11::CopyCaptureResource(ID3D11DeviceContext* context, Dx11ResourceInfo* source, int index)
{
    if (context == nullptr || source == nullptr || source->texture == nullptr)
        return false;

    if (!CreateCaptureResource(context, *source, index))
        return false;

    const UINT scWidth = State::Instance().currentSwapchainDesc.BufferDesc.Width;
    const UINT scHeight = State::Instance().currentSwapchainDesc.BufferDesc.Height;

    if (!source->extended)
    {
        context->CopySubresourceRegion(_captureBuffer[index], 0, 0, 0, 0, source->texture, 0, nullptr);
        return true;
    }

    D3D11_BOX box = {};
    box.left = 0;
    box.top = 0;
    box.front = 0;
    box.right = std::min(scWidth, source->width);
    box.bottom = std::min(scHeight, source->height);
    box.back = 1;

    const UINT dstX = scWidth > source->width ? (scWidth - source->width) / 2 : 0;
    const UINT dstY = scHeight > source->height ? (scHeight - source->height) / 2 : 0;

    context->CopySubresourceRegion(_captureBuffer[index], 0, dstX, dstY, 0, source->texture, 0, &box);
    return true;
}

bool Hudfix_Dx11::PublishCaptureToFg(ID3D11DeviceContext* context, Dx11ResourceInfo* source, int index)
{
    if (context == nullptr || source == nullptr || _captureBuffer[index] == nullptr)
        return false;

    auto& state = State::Instance();
    auto* fg = state.currentFG;
    if (fg == nullptr)
        return false;

    const auto activeUpscaleFrame = ActiveUpscaleFrame();
    const auto frameId = activeUpscaleFrame == 0 ? 1 : activeUpscaleFrame;
    const bool dontUseNTShared = Config::Instance()->DontUseNTShared.value_or_default();

    if (!Dx11WithDx12::PrepareTextureFrom11To12("HudlessColor", WithDx12::GetD3D12Device(), _captureBuffer[index],
                                                &_sharedCapture[index], false, false, dontUseNTShared, frameId))
    {
        LOG_WARN("DX11 Hudfix failed to prepare shared capture");
        return false;
    }

    if (!Dx11WithDx12::SyncDx11ToDx12())
    {
        LOG_WARN("DX11 Hudfix failed to synchronize D3D11 capture to D3D12");
        return false;
    }

    auto* shared = _sharedCapture[index].Dx12Resource;
    if (shared == nullptr)
        return false;

    auto* cmdList = fg->GetUICommandList();
    if (cmdList == nullptr)
        return false;

    Dx12Resource setResource = {};
    setResource.type = FG_ResourceType::HudlessColor;
    setResource.left = 0;
    setResource.top = 0;
    setResource.width = state.currentSwapchainDesc.BufferDesc.Width;
    setResource.height = state.currentSwapchainDesc.BufferDesc.Height;
    setResource.frameIndex = fg->GetIndexWillBeDispatched();

    if (!CompareResourceFormats(source->format, state.currentSwapchainDesc.BufferDesc.Format))
    {
        auto* device = WithDx12::GetD3D12Device();
        if (device == nullptr)
            return false;

        if (_formatTransfer[index] == nullptr ||
            !_formatTransfer[index]->IsFormatCompatible(state.currentSwapchainDesc.BufferDesc.Format))
        {
            delete _formatTransfer[index];
            _formatTransfer[index] =
                new FT_Dx12("FormatTransfer DX11 Hudfix", device, state.currentSwapchainDesc.BufferDesc.Format);
        }

        if (_formatTransfer[index] == nullptr ||
            !_formatTransfer[index]->CreateBufferResource(device, shared, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) ||
            _formatTransfer[index]->Buffer() == nullptr)
        {
            return false;
        }

        TransitionResource(cmdList, shared, D3D12_RESOURCE_STATE_COMMON,
                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        _formatTransfer[index]->SetBufferState(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        if (!_formatTransfer[index]->Dispatch(cmdList, shared, _formatTransfer[index]->Buffer()))
        {
            TransitionResource(cmdList, shared, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                               D3D12_RESOURCE_STATE_COMMON);

            return false;
        }

        TransitionResource(cmdList, shared, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                           D3D12_RESOURCE_STATE_COMMON);

        setResource.cmdList = cmdList;
        setResource.resource = _formatTransfer[index]->Buffer();
        setResource.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        setResource.validity = FG_ResourceValidity::ValidNow;
    }
    else
    {
        setResource.cmdList = cmdList;
        setResource.resource = shared;
        setResource.state = D3D12_RESOURCE_STATE_COMMON;
        setResource.validity = FG_ResourceValidity::ValidNow;
    }

    return fg->SetResource(&setResource);
}

bool Hudfix_Dx11::CheckForHudless(ID3D11DeviceContext* context, Dx11ResourceInfo* resource, bool ignoreBlocked)
{
    auto& state = State::Instance();

    if (context == nullptr || resource == nullptr || resource->texture == nullptr || !IsResourceCheckActive())
        return false;

    if (!CheckResource(resource))
        return false;

    const auto identity = GetResourceIdentity(resource->texture);
    if (identity == 0)
        return false;

    std::lock_guard<std::mutex> lock(_checkMutex);

    ValidateCapturedIdentity(resource->texture, identity);

    const auto upscaleFrame = ActiveUpscaleFrame();

    CapturedHudlessInfo* capturedHudlessInfo = nullptr;
    if (auto it = state.capturedHudlesses.find(resource->texture); it != state.capturedHudlesses.end())
    {
        capturedHudlessInfo = &it->second;
        _capturedIdentity[resource->texture] = identity;

        if (!capturedHudlessInfo->enabled)
            return false;
    }

    if (!ignoreBlocked && Config::Instance()->FGResourceBlocking.value_or_default())
    {
        if (auto it = _hudlessList.find(identity); it != _hudlessList.end())
        {
            auto& info = it->second;

            if (info.ignore && !info.dontReuse && info.lastTriedFrame != upscaleFrame)
            {
                if (info.retryStartFrame == 0)
                {
                    info.retryStartFrame = upscaleFrame;
                    info.lastTriedFrame = upscaleFrame;
                    info.retryCount = 0;
                    return false;
                }

                ++info.retryCount;
                info.lastTriedFrame = upscaleFrame;

                if ((upscaleFrame - info.retryStartFrame) < 69)
                {
                    if (info.retryCount > 19)
                    {
                        info.lastUsedFrame = upscaleFrame;
                        info.retryStartFrame = 0;
                        info.useCount = 0;
                        info.retryCount = 0;
                        info.ignore = false;
                        ++info.reuseCount;
                    }
                }
                else
                {
                    info.useCount = 0;
                    info.retryCount = 0;
                    info.retryStartFrame = 0;
                }
            }

            if (info.ignore)
                return false;

            if ((upscaleFrame - info.lastUsedFrame) > 6 && info.useCount < 100)
            {
                info.ignore = true;
                info.retryCount = 0;
                info.lastTriedFrame = 0;
                info.retryStartFrame = 0;
                info.lastUsedFrame = upscaleFrame;
                if (info.reuseCount > 1)
                    info.dontReuse = true;
                return false;
            }

            info.lastUsedFrame = upscaleFrame;
            ++info.useCount;
        }
        else
        {
            _hudlessList[identity] = { upscaleFrame, 0, 0, 0, 0, 1, false, false };
        }
    }

    if (!CheckCapture())
        return false;

    int index = 0;
    {
        std::lock_guard<std::mutex> counterLock(_counterMutex);
        index = GetIndex();
    }

    LOG_DEBUG("DX11 Hudfix capture {}->{}, resource {:X}, {}x{}, format {}", SourceString(resource->captureInfo),
              DispatchString(resource->captureInfo), (size_t) resource->texture, resource->width, resource->height,
              (UINT) resource->format);

    ScopedSkipDx11HudfixTracking skipTracking;
    _skipHudlessChecks = true;

    if (!CopyCaptureResource(context, resource, index) || !PublishCaptureToFg(context, resource, index))
    {
        _skipHudlessChecks = false;
        return false;
    }

    if (state.fgCaptureResources)
    {
        std::lock_guard<std::mutex> captureLock(_captureMutex);
        _captureList.insert(identity);
        state.fgCapturedResourceCount = _captureList.size();
    }

    HudlessFound();

    if (capturedHudlessInfo != nullptr)
    {
        ++capturedHudlessInfo->usageCount;
        capturedHudlessInfo->captureInfo = resource->captureInfo;
    }
    else
    {
        state.capturedHudlesses.insert_or_assign(resource->texture,
                                                 CapturedHudlessInfo { 1, resource->captureInfo, true });
    }

    _capturedIdentity[resource->texture] = identity;

    return true;
}

void Hudfix_Dx11::ResetCounters()
{
    std::lock_guard<std::mutex> checkLock(_checkMutex);
    std::lock_guard<std::mutex> counterLock(_counterMutex);

    _fgCounter = 0;
    _upscaleCounter = 0;
    _frameTime = 0.0;
    _skipTracking = false;
    _skipHudlessChecks = false;

    _hudlessList.clear();

    for (auto& counter : _captureCounter)
        counter = 0;
}

void Hudfix_Dx11::ReleaseResources()
{
    std::lock_guard<std::mutex> lock(_checkMutex);

    for (size_t i = 0; i < BUFFER_COUNT; ++i)
    {
        Dx11WithDx12::ReleaseSharedResource(&_sharedCapture[i]);
        SAFE_RELEASE(_captureBuffer[i]);
        delete _formatTransfer[i];
        _formatTransfer[i] = nullptr;
    }

    {
        std::lock_guard<std::mutex> captureLock(_captureMutex);
        _captureList.clear();
    }

    auto& capturedHudlesses = State::Instance().capturedHudlesses;
    for (const auto& [resource, identity] : _capturedIdentity)
    {
        UNREFERENCED_PARAMETER(identity);
        capturedHudlesses.erase(resource);
    }
    _capturedIdentity.clear();

    _hudlessList.clear();
    _identityFallbackWarning.store(false);
    _msaaWarning.store(false);
    State::Instance().fgCapturedResourceCount = 0;
}
