#include "pch.h"
#include "DlssNr_Dx12_State.h"

auto DlssNr_Dx12::State::ParkNrResource(ID3D12Resource*& resource) -> void
{
    if (!resource) return;
    auto* retired = resource;
    resource = nullptr;
    lifetime.Retire([retired] { retired->Release(); });
}

auto DlssNr_Dx12::State::TickNrRetired([[maybe_unused]] uint64_t epoch) -> void
{ lifetime.Collect(); }

auto DlssNr_Dx12::State::ReleaseSurfacesIfFormatChanged(DXGI_FORMAT needed) -> void
{
    if (nr.output == nullptr || nr.output->GetDesc().Format == needed)
        return;

    LOG_INFO("DLSS-NR rebuilding surfaces: format {} -> {} (inject point changed)",
             (int) nr.output->GetDesc().Format, (int) needed);

    ForgetCalibration();

    for (auto& model : nr.models)
        model.RetryAfterFailure();
    std::fill(std::begin(nr.passCreateFailed), std::end(nr.passCreateFailed), false);
    modelRunning = false;

    for (ID3D12Resource** r : { &nr.output, &nr.passScratch, &nr.passClamp, &nr.colorCopy, &nr.hdrCopy, &nr.colorSmall,
                                &nr.outputNative, &nr.activeColor })
        ParkNrResource(*r);

    nr.passScratchFailed = false;

    nr.reset = true;
}

auto DlssNr_Dx12::State::CreateScratch(ID3D12Device* device, DXGI_FORMAT format, unsigned int width, unsigned int height) -> ID3D12Resource*
{
    D3D12_HEAP_PROPERTIES heap {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    // The model writes its result, so the destination has to be a UAV.
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    ID3D12Resource* res = nullptr;
    device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                    nullptr, IID_PPV_ARGS(&res));
    return res;
}

auto DlssNr_Dx12::State::Barrier(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* res, D3D12_RESOURCE_STATES from,
                 D3D12_RESOURCE_STATES to) -> void
{
    if (from == to)
        return;
    D3D12_RESOURCE_BARRIER b {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    cmdList->ResourceBarrier(1, &b);
}

auto DlssNr_Dx12::State::TypedGuideFormat(DXGI_FORMAT f) -> DXGI_FORMAT
{
    switch (f)
    {
    case DXGI_FORMAT_R32_TYPELESS:
        return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R16_TYPELESS:
        return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_R24G8_TYPELESS:
        return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_R32G8X24_TYPELESS:
        return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case DXGI_FORMAT_R32G32_TYPELESS:
        return DXGI_FORMAT_R32G32_FLOAT;
    case DXGI_FORMAT_R16G16_TYPELESS:
        return DXGI_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default:
        return f;
    }
}

auto DlssNr_Dx12::State::IsTypeless(DXGI_FORMAT f) -> bool
{ return TypedGuideFormat(f) != f; }

auto DlssNr_Dx12::State::CreateGuideClone(ID3D12Device* device, ID3D12Resource* source) -> ID3D12Resource*
{
    D3D12_RESOURCE_DESC desc = source->GetDesc();
    desc.Format = TypedGuideFormat(desc.Format);
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heap {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    ID3D12Resource* res = nullptr;
    device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                    IID_PPV_ARGS(&res));
    return res;
}

auto DlssNr_Dx12::State::ReadableGuide(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* source,
                                  ID3D12Resource** clone) -> ID3D12Resource*
{
    if (source == nullptr || !IsTypeless(source->GetDesc().Format))
        return source;

    // A dynamic-resolution game reallocates its depth and motion vectors as the render size moves, so
    // the clone made for the old size no longer matches -- and CopyResource demands identical
    // dimensions. Copying a 1970x1108 source into a 984x554 clone is undefined and removes the device,
    // which is the DRS crash. Rebuild the clone whenever the source's shape has changed under it.
    if (*clone != nullptr)
    {
        const D3D12_RESOURCE_DESC have = (*clone)->GetDesc();
        const D3D12_RESOURCE_DESC want = source->GetDesc();

        if (have.Width != want.Width || have.Height != want.Height || have.Format != TypedGuideFormat(want.Format))
        {
            // Retired, not released: the previous copy may still be in flight on the game's queue.
            ParkNrResource(*clone);
        }
    }

    if (*clone == nullptr)
    {
        *clone = CreateGuideClone(device, source);

        if (*clone == nullptr)
            return nullptr;

        LOG_DEBUG("DLSS-NR cloned a typeless guide as format {}", (int) TypedGuideFormat(source->GetDesc().Format));
    }

    Barrier(cmdList, source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmdList->CopyResource(*clone, source);
    Barrier(cmdList, source, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Barrier(cmdList, *clone, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    return *clone;
}

auto DlssNr_Dx12::State::FormatCanHoldLinearHdr(DXGI_FORMAT format) -> bool
{
    switch (format)
    {
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
        return true;
    default:
        return false;
    }
}

auto DlssNr_Dx12::State::GetResource(NVSDK_NGX_Parameter* params, const char* a, const char* b) -> ID3D12Resource*
{
    ID3D12Resource* res = nullptr;

    if (params->Get(a, &res) == NVSDK_NGX_Result_Success && res != nullptr)
        return res;

    res = nullptr;

    if (params->Get(b, &res) == NVSDK_NGX_Result_Success && res != nullptr)
        return res;

    // The same key again, as a plain pointer.
    //
    // NVSDK_NGX_Parameter has a typed setter per resource kind and an untyped one, and on a real NGX
    // parameter block those are separate slots: what goes in through Set(name, void*) does not come
    // back out of Get(name, ID3D12Resource**). A game running its own D3D12 upscaler sets these
    // typed, so the typed read above is enough and always was.
    //
    // Both of OptiScaler's bridges write them untyped. IFeature_Dx11wDx12 and IFeature_VkwDx12 turn
    // the game's D3D11 textures or Vulkan images into D3D12 resources and hand them over with
    // Set(name, (void*) resource) -- so the typed read came back null a few lines after the resource
    // had been written, and the pass quietly did nothing. That is the whole reason this never ran in
    // a DirectX 11 or Vulkan game.
    void* untyped = nullptr;

    if (params->Get(a, &untyped) == NVSDK_NGX_Result_Success && untyped != nullptr)
        return static_cast<ID3D12Resource*>(untyped);

    untyped = nullptr;

    if (params->Get(b, &untyped) == NVSDK_NGX_Result_Success && untyped != nullptr)
        return static_cast<ID3D12Resource*>(untyped);

    return nullptr;
}

auto DlssNr_Dx12::State::ReleaseResources() -> void
{
    std::lock_guard<std::recursive_mutex> nrLock(mutex);
    ReleaseEnlarger();
    enlargementStatus.clear();
    deferredSr.ReleaseResources();

    lifetime.Collect();

    for (auto& model : nr.models)
        model.Release();
    std::fill(std::begin(nr.passCreateFailed), std::end(nr.passCreateFailed), false);
    modelRunning = false;

    if (nr.output != nullptr)
    {
        ParkNrResource(nr.output);
    }

    ParkNrResource(nr.passScratch);
    ParkNrResource(nr.passClamp);
    nr.passScratchFailed = false;

    if (nr.colorCopy != nullptr)
    {
        ParkNrResource(nr.colorCopy);
    }

    if (nr.hdrCopy != nullptr)
    {
        ParkNrResource(nr.hdrCopy);
    }

    if (nr.activeColor != nullptr)
    {
        ParkNrResource(nr.activeColor);
    }

    if (nr.colorSmall != nullptr)
    {
        ParkNrResource(nr.colorSmall);
    }

    if (nr.superUp != nullptr)
    {
        lifetime.Retire([up = nr.superUp] { delete up; });
        nr.superUp = nullptr;
    }

    if (nr.superDown != nullptr)
    {
        lifetime.Retire([down = nr.superDown] { delete down; });
        nr.superDown = nullptr;
    }

    if (nr.outputNative != nullptr)
    {
        ParkNrResource(nr.outputNative);
    }

    if (nr.heldColor != nullptr)
    {
        ParkNrResource(nr.heldColor);
    }
    nr.heldActive = false;

    if (nr.meter != nullptr)
    {
        ParkNrResource(nr.meter);
    }

    if (nr.calib != nullptr)
    {
        ParkNrResource(nr.calib);
    }

    for (auto& r : nr.calibReadback)
    {
        if (r != nullptr)
        {
            ParkNrResource(r);
        }
    }

    nr.calibFrames = 0;
    nr.calibCount = 0;
    nr.calibSuggestion = 0.0f;
    nr.calibSteadiness = 0.0f;
    nr.calibUsable = false;
    nr.calibWhy = "measuring...";

    for (auto& rb : nr.meterReadback)
    {
        if (rb != nullptr)
        {
            ParkNrResource(rb);
        }
    }

    // The slots these flags describe have just been released, so nothing may vouch for what the next
    // buffers happen to contain. gameExposure is deliberately NOT cleared here: a recreate is a
    // transition within the same scene, and dropping to the slider for a few frames would be the
    // flicker the held value exists to prevent. The user switching the option off is the case where
    // the held value has to go, and that is handled at the edge in Dispatch.
    for (bool& valid : nr.meterExposureValid)
        valid = false;

    nr.meterFrames = 0;

    if (nr.depthClone != nullptr)
    {
        ParkNrResource(nr.depthClone);
    }

    if (nr.motionClone != nullptr)
    {
        ParkNrResource(nr.motionClone);
    }

    captureFrames.release();
    if (auto* timer = gpuTime.release()) lifetime.Retire([timer] { delete timer; });
    if (auto* timer = ngxTime.release()) lifetime.Retire([timer] { delete timer; });
    lastNgxTime.reset();
    lastGpuTime.reset();
}
