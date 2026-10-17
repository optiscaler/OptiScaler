#include "pch.h"
#include <Config.h>

#include "Reproject_Dx12.h"
#include "Reproject_Common.h"
#include "precompile/reproject_Shader.h"
#include "precompile/reproject_mask_h_Shader.h"
#include "precompile/reproject_mask_v_Shader.h"
#include "precompile/reproject_latch_Shader.h"
#include "precompile/reproject_cache_Shader.h"
#include "precompile/reproject_stats_Shader.h"

#include <magic_enum.hpp>
#include "mouseInputs/RawInputHook.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

inline static int GetFormatGroup(DXGI_FORMAT format)
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

inline static bool CompareResourceFormats(DXGI_FORMAT sc, DXGI_FORMAT hudless)
{
    if (sc == hudless)
        return true;

    auto scGroup = GetFormatGroup(sc);
    auto hudlessGroup = GetFormatGroup(hudless);
    return scGroup == hudlessGroup;
}

bool Reproject_Dx12::CreateStatsResources()
{
    auto defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto outputDesc =
        CD3DX12_RESOURCE_DESC::Buffer(StatsCount * sizeof(uint32_t), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    auto result =
        _device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &outputDesc,
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&_statsOutput));

    if (result != S_OK)
    {
        LOG_ERROR("[{0}] CreateCommittedResource (stats) error {1:x}", _name, (unsigned int) result);
        return false;
    }

    _statsOutput->SetName(L"Reproject_Stats");

    auto readbackHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    auto readbackDesc = CD3DX12_RESOURCE_DESC::Buffer(StatsCount * sizeof(uint32_t));

    for (uint32_t i = 0; i < StatsLatency; i++)
    {
        result =
            _device->CreateCommittedResource(&readbackHeapProps, D3D12_HEAP_FLAG_NONE, &readbackDesc,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&_statsReadback[i]));

        if (result != S_OK)
        {
            LOG_ERROR("[{0}] CreateCommittedResource (stats readback) error {1:x}", _name, (unsigned int) result);
            return false;
        }

        _statsReadback[i]->SetName(L"Reproject_StatsReadback");

        // Readback heaps can stay mapped, a slot is only read once the GPU is done with it
        void* mapped = nullptr;
        CD3DX12_RANGE readRange(0, StatsCount * sizeof(uint32_t));
        if (_statsReadback[i]->Map(0, &readRange, &mapped) != S_OK)
        {
            LOG_ERROR("[{0}] Failed to map stats readback", _name);
            return false;
        }

        _statsMapped[i] = static_cast<const uint32_t*>(mapped);
    }

    return true;
}

bool Reproject_Dx12::CreateCacheResources(uint32_t size, DXGI_FORMAT colorFormat)
{
    if (_cacheColor[0] != nullptr)
    {
        auto desc = _cacheColor[0]->GetDesc();

        if (desc.Width == size && desc.Format == colorFormat)
            return true;

        LOG_WARN("[{0}] Release cache {1} ({2}), new one: {3} ({4})", _name, desc.Width, (uint32_t) desc.Format, size,
                 (uint32_t) colorFormat);
        ReleaseCacheResources();
    }

    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    for (int i = 0; i < 2; i++)
    {
        auto colorDesc = CD3DX12_RESOURCE_DESC::Tex2D(colorFormat, size, size, 6, 1, 1, 0,
                                                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        auto geoDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16_FLOAT, size, size, 6, 1, 1, 0,
                                                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        auto result = _device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &colorDesc,
                                                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
                                                       IID_PPV_ARGS(&_cacheColor[i]));

        if (result == S_OK)
        {
            result = _device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &geoDesc,
                                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
                                                      IID_PPV_ARGS(&_cacheGeo[i]));
        }

        if (result != S_OK)
        {
            LOG_ERROR("[{0}] CreateCommittedResource (cache) error {1:x}", _name, (unsigned int) result);
            ReleaseCacheResources();
            return false;
        }

        _cacheColor[i]->SetName(L"Reproject_CacheColor");
        _cacheGeo[i]->SetName(L"Reproject_CacheGeo");
    }

    // Contents are undefined until the first update
    _cacheValid = false;
    return true;
}

void Reproject_Dx12::ReleaseCacheResources()
{
    for (int i = 0; i < 2; i++)
    {
        DeferredRelease::Release(_cacheColor[i]);
        DeferredRelease::Release(_cacheGeo[i]);
    }

    _cacheValid = false;
}

void Reproject_Dx12::ResourceBarrier(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource,
                                     D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState)
{
    if (beforeState == afterState)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = beforeState;
    barrier.Transition.StateAfter = afterState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
}

bool Reproject_Dx12::CreateMaskResource(ID3D12Resource** resource, uint32_t width, uint32_t height, const wchar_t* name)
{
    if (*resource != nullptr)
    {
        auto desc = (*resource)->GetDesc();

        if (desc.Width == width && desc.Height == height)
            return true;

        LOG_WARN("[{0}] Release {1}x{2}, new one: {3}x{4}", _name, desc.Width, desc.Height, width, height);
        DeferredRelease::Release(*resource);
    }

    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8_UNORM, width, height, 1, 1, 1, 0,
                                             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    auto result =
        _device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                         nullptr, IID_PPV_ARGS(resource));

    if (result != S_OK)
    {
        LOG_ERROR("[{0}] CreateCommittedResource error {1:x}", _name, (unsigned int) result);
        return false;
    }

    (*resource)->SetName(name);
    return true;
}

Reproject_Dx12::Reproject_Dx12(std::string InName, ID3D12Device* InDevice) : Shader_Dx12(InName, InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    CD3DX12_STATIC_SAMPLER_DESC samplers[2] = { CD3DX12_STATIC_SAMPLER_DESC(0), CD3DX12_STATIC_SAMPLER_DESC(1) };
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;

    for (auto& sampler : samplers)
    {
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    }

    // SRV: Hudless, PresentCopy, Depth, FakePresent, DepthMask, DepthMaskTemp,
    //      PrevCacheColor, PrevCacheGeo, CacheColor, CacheGeo, Velocity
    // UAV: Present, DepthMaskTemp, DepthMask, Latched, CacheColorOut, CacheGeoOut, Stats
    // CBV: Params, LiveInput
    if (!SetupRootSignature(InDevice, 11, 7, 2, 0, 0, 2, samplers))
    {
        LOG_ERROR("Failed to setup root signature");
        return;
    }

    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ReprojectionParams));
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    for (int i = 0; i < Reproject_NUM_OF_HEAPS; i++)
    {
        auto result = InDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                        IID_PPV_ARGS(&_constantBuffers[i]));

        if (result != S_OK)
        {
            LOG_ERROR("[{0}] CreateCommittedResource error {1:x}", _name, (unsigned int) result);
            return;
        }
    }

    if (!CreateComputePipeline(InDevice, &_pipelineState, reproject_cso, sizeof(reproject_cso), shaderCode.c_str()))
    {
        LOG_ERROR("[{0}] Failed to create compute pipeline", _name);
        return;
    }

    if (!CreateComputePipeline(InDevice, &_maskPipelineH, reproject_mask_h_cso, sizeof(reproject_mask_h_cso),
                               maskHShaderCode.c_str()))
    {
        LOG_ERROR("[{0}] Failed to create mask H compute pipeline", _name);
        return;
    }

    if (!CreateComputePipeline(InDevice, &_maskPipelineV, reproject_mask_v_cso, sizeof(reproject_mask_v_cso),
                               maskVShaderCode.c_str()))
    {
        LOG_ERROR("[{0}] Failed to create mask V compute pipeline", _name);
        return;
    }

    if (!CreateComputePipeline(InDevice, &_latchPipeline, reproject_latch_cso, sizeof(reproject_latch_cso),
                               latchShaderCode.c_str()))
    {
        LOG_ERROR("[{0}] Failed to create latch compute pipeline", _name);
        return;
    }

    if (!CreateComputePipeline(InDevice, &_cachePipeline, reproject_cache_cso, sizeof(reproject_cache_cso),
                               cacheShaderCode.c_str()))
    {
        LOG_ERROR("[{0}] Failed to create cache compute pipeline", _name);
        return;
    }

    if (!CreateComputePipeline(InDevice, &_statsPipeline, reproject_stats_cso, sizeof(reproject_stats_cso),
                               statsShaderCode.c_str()))
    {
        LOG_ERROR("[{0}] Failed to create stats compute pipeline", _name);
        return;
    }

    if (!CreateStatsResources())
        return;

    // Live mouse input, stays mapped for the whole lifetime so the input thread can write into it at any time
    auto liveDesc = CD3DX12_RESOURCE_DESC::Buffer(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    auto result =
        InDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &liveDesc,
                                          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&_liveInput));

    if (result != S_OK)
    {
        LOG_ERROR("[{0}] CreateCommittedResource (live input) error {1:x}", _name, (unsigned int) result);
        return;
    }

    _liveInput->SetName(L"Reproject_LiveInput");

    CD3DX12_RANGE readRange(0, 0);
    if (_liveInput->Map(0, &readRange, &_liveInputMapped) != S_OK)
    {
        LOG_ERROR("[{0}] Failed to map live input", _name);
        _liveInputMapped = nullptr;
        return;
    }

    std::memset(_liveInputMapped, 0, sizeof(ReprojectionLiveInput));

    // Result of the latch pass, 3 rotation rows + used mouse delta
    auto latchedDesc = CD3DX12_RESOURCE_DESC::Buffer(4 * sizeof(XMFLOAT4), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    result = InDevice->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &latchedDesc,
                                               D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&_latchedOutput));

    if (result != S_OK)
    {
        LOG_ERROR("[{0}] CreateCommittedResource (latched output) error {1:x}", _name, (unsigned int) result);
        return;
    }

    _latchedOutput->SetName(L"Reproject_LatchedOutput");

    _init = InitHeaps(InDevice, _frameHeaps, Reproject_NUM_OF_HEAPS);

    if (_init)
        InputCollection::getInstance().registerLiveSink(_liveInputMapped);

    // TODO: make this owned or something, so that hooks stay in place if there are
    // two instances of this shader and one gets destroyed
    RawInputHook::getInstance().start();
}

bool Reproject_Dx12::Dispatch(ID3D12GraphicsCommandList* cmdList, ReprojectionParams& params,
                              ID3D12Resource* realPresent, D3D12_RESOURCE_STATES realPresentState,
                              ID3D12Resource* fakePresent, D3D12_RESOURCE_STATES fakePresentState,
                              ID3D12Resource* hudless, D3D12_RESOURCE_STATES hudlessState, ID3D12Resource* depth,
                              D3D12_RESOURCE_STATES depthState, ID3D12Resource* velocity,
                              D3D12_RESOURCE_STATES velocityState)
{
    if (!_init || !_device || !realPresent || !hudless || !cmdList || !depth)
        return false;

    ScopedGpuTime_Dx12 scopedGpuTime(GpuTime.get(), cmdList);

    _counter++;
    _counter = _counter % Reproject_NUM_OF_HEAPS;
    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];
    auto& currentBuffer = _buffer[_counter];

    // Recreated when the present size or format changes, kept in COPY_SOURCE between dispatches
    auto previousBuffer = currentBuffer;
    auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

    if (!Shader_Dx12::CreateBufferResource(_device, realPresent, D3D12_RESOURCE_STATE_COPY_SOURCE, &currentBuffer,
                                           resourceFlags))
    {
        return false;
    }

    if (currentBuffer != previousBuffer)
        currentBuffer->SetName(L"Reproject_Buffer");

    // Not every caller provides depth size
    if (params.DepthWidth == 0 || params.DepthHeight == 0)
    {
        auto depthDesc = depth->GetDesc();
        params.DepthWidth = static_cast<uint32_t>(depthDesc.Width);
        params.DepthHeight = depthDesc.Height;
    }

    if (!CreateMaskResource(&_depthMaskTemp, params.DepthWidth, params.DepthHeight, L"Reproject_DepthMaskTemp") ||
        !CreateMaskResource(&_depthMask, params.DepthWidth, params.DepthHeight, L"Reproject_DepthMask"))
    {
        return false;
    }

    auto presentFormat = realPresent->GetDesc().Format;
    auto hudlessFormat = hudless->GetDesc().Format;

    if (presentFormat != _loggedPresentFormat || hudlessFormat != _loggedHudlessFormat)
    {
        LOG_INFO("[{0}] Present format: {1} ({2}), hudless format: {3} ({4})", _name,
                 magic_enum::enum_name(presentFormat), (uint32_t) presentFormat, magic_enum::enum_name(hudlessFormat),
                 (uint32_t) hudlessFormat);

        _loggedPresentFormat = presentFormat;
        _loggedHudlessFormat = hudlessFormat;
    }

    // R11G11B10 can't store negative values (scRGB) and loses precision every frame on the carried over texels,
    // as they get premultiplied by a decaying confidence
    constexpr auto cacheColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    bool cacheReady = params.CacheEnabled && CreateCacheResources(params.CacheSize, cacheColorFormat);

    // Keyed on the option, the third person bypass toggles too often to free on it
    if (!Config::Instance()->ReprojectionDirectionCache.value_or_default() && _cacheColor[0] != nullptr)
        ReleaseCacheResources();
    auto plan = PlanDispatch(params, cacheReady, velocity != nullptr);

    ResourceBarrier(cmdList, currentBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    ResourceBarrier(cmdList, realPresent, realPresentState, D3D12_RESOURCE_STATE_COPY_SOURCE);

    cmdList->CopyResource(currentBuffer, realPresent);

    // Make sure present is in D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
    if (fakePresent)
        ResourceBarrier(cmdList, fakePresent, fakePresentState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ResourceBarrier(cmdList, realPresent, D3D12_RESOURCE_STATE_COPY_SOURCE,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ResourceBarrier(cmdList, hudless, hudlessState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ResourceBarrier(cmdList, depth, depthState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (plan.statsUpdate)
        ResourceBarrier(cmdList, velocity, velocityState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ResourceBarrier(cmdList, currentBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // Create views
    auto presentDesc = realPresent->GetDesc();

    // Hudless is often a typeless version of the present format (Cyberpunk: R8G8B8A8_TYPELESS vs R8G8B8A8_UNORM),
    // viewing it as present keeps the same encoding, anything else gets its own format as it can't be cast
    auto hudlessViewFormat =
        CompareResourceFormats(presentDesc.Format, hudlessFormat) ? presentDesc.Format : hudlessFormat;
    CreateShaderResourceView(_device, hudless, currentHeap.GetSrvCPU(0), hudlessViewFormat);
    CreateShaderResourceView(_device, realPresent, currentHeap.GetSrvCPU(1));
    CreateShaderResourceView(_device, depth, currentHeap.GetSrvCPU(2));
    if (fakePresent)
        CreateShaderResourceView(_device, fakePresent, currentHeap.GetSrvCPU(3));
    CreateShaderResourceView(_device, _depthMask, currentHeap.GetSrvCPU(4));
    CreateShaderResourceView(_device, _depthMaskTemp, currentHeap.GetSrvCPU(5));

    CreateUnorderedAccessView(_device, currentBuffer, currentHeap.GetUavCPU(0), 0);
    CreateUnorderedAccessView(_device, _depthMaskTemp, currentHeap.GetUavCPU(1), 0);
    CreateUnorderedAccessView(_device, _depthMask, currentHeap.GetUavCPU(2), 0);

    if (plan.statsUpdate)
    {
        CreateShaderResourceView(_device, velocity, currentHeap.GetSrvCPU(10));
    }
    else
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC nullSrvDesc {};
        nullSrvDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
        nullSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        nullSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        nullSrvDesc.Texture2D.MipLevels = 1;
        _device->CreateShaderResourceView(nullptr, &nullSrvDesc, currentHeap.GetSrvCPU(10));
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC statsUavDesc {};
    statsUavDesc.Format = DXGI_FORMAT_UNKNOWN;
    statsUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    statsUavDesc.Buffer.NumElements = StatsCount;
    statsUavDesc.Buffer.StructureByteStride = sizeof(uint32_t);
    _device->CreateUnorderedAccessView(_statsOutput, nullptr, &statsUavDesc, currentHeap.GetUavCPU(6));

    // Cache views, null descriptors when there is no cache
    auto createCubeSrv = [&](ID3D12Resource* resource, DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE handle)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc {};
        srvDesc.Format = format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.TextureCube.MipLevels = 1;
        _device->CreateShaderResourceView(resource, &srvDesc, handle);
    };

    auto createArrayUav = [&](ID3D12Resource* resource, DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE handle)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc {};
        uavDesc.Format = format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uavDesc.Texture2DArray.ArraySize = 6;
        _device->CreateUnorderedAccessView(resource, nullptr, &uavDesc, handle);
    };

    constexpr auto cacheGeoFormat = DXGI_FORMAT_R16G16_FLOAT;

    createCubeSrv(cacheReady ? _cacheColor[plan.prevCache] : nullptr, cacheColorFormat, currentHeap.GetSrvCPU(6));
    createCubeSrv(cacheReady ? _cacheGeo[plan.prevCache] : nullptr, cacheGeoFormat, currentHeap.GetSrvCPU(7));
    createCubeSrv(cacheReady ? _cacheColor[plan.newestCache] : nullptr, cacheColorFormat, currentHeap.GetSrvCPU(8));
    createCubeSrv(cacheReady ? _cacheGeo[plan.newestCache] : nullptr, cacheGeoFormat, currentHeap.GetSrvCPU(9));
    createArrayUav(plan.cacheUpdate ? _cacheColor[plan.newestCache] : nullptr, cacheColorFormat,
                   currentHeap.GetUavCPU(4));
    createArrayUav(plan.cacheUpdate ? _cacheGeo[plan.newestCache] : nullptr, cacheGeoFormat, currentHeap.GetUavCPU(5));

    D3D12_UNORDERED_ACCESS_VIEW_DESC latchedUavDesc {};
    latchedUavDesc.Format = DXGI_FORMAT_UNKNOWN;
    latchedUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    latchedUavDesc.Buffer.NumElements = 4;
    latchedUavDesc.Buffer.StructureByteStride = sizeof(XMFLOAT4);
    _device->CreateUnorderedAccessView(_latchedOutput, nullptr, &latchedUavDesc, currentHeap.GetUavCPU(3));

    if (!CreateConstantsBuffer(_device, _constantBuffers[_counter], params, currentHeap.GetCbvCPU(0)))
    {
        LOG_ERROR("[{0}] Failed to create a constants buffer", _name);
        return false;
    }

    D3D12_CONSTANT_BUFFER_VIEW_DESC liveCbvDesc {};
    liveCbvDesc.BufferLocation = _liveInput->GetGPUVirtualAddress();
    liveCbvDesc.SizeInBytes = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    _device->CreateConstantBufferView(&liveCbvDesc, currentHeap.GetCbvCPU(1));

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    cmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    cmdList->SetComputeRootSignature(_rootSignature);
    cmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    // Depth mask prepass
    UINT maskDispatchWidth = (params.DepthWidth + InNumThreadsX - 1) / InNumThreadsX;
    UINT maskDispatchHeight = (params.DepthHeight + InNumThreadsY - 1) / InNumThreadsY;

    // Writes straight to _depthMask when CutoffExpandPx == 0
    cmdList->SetPipelineState(_maskPipelineH);
    cmdList->Dispatch(maskDispatchWidth, maskDispatchHeight, 1);

    if (params.CutoffExpandPx > 0)
    {
        ResourceBarrier(cmdList, _depthMaskTemp, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        cmdList->SetPipelineState(_maskPipelineV);
        cmdList->Dispatch(maskDispatchWidth, maskDispatchHeight, 1);

        ResourceBarrier(cmdList, _depthMaskTemp, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    ResourceBarrier(cmdList, _depthMask, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // Motion vector statistics, a single group
    if (plan.statsUpdate)
    {
        cmdList->SetPipelineState(_statsPipeline);
        cmdList->Dispatch(1, 1, 1);

        ResourceBarrier(cmdList, _statsOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyResource(_statsReadback[plan.statsSlot], _statsOutput);
        ResourceBarrier(cmdList, _statsOutput, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    // Direction cache update, once per rendered frame
    if (plan.cacheUpdate)
    {
        ResourceBarrier(cmdList, _cacheColor[plan.newestCache], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ResourceBarrier(cmdList, _cacheGeo[plan.newestCache], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        UINT cacheDispatchSize = (params.CacheSize + 7) / 8;

        cmdList->SetPipelineState(_cachePipeline);
        cmdList->Dispatch(cacheDispatchSize, cacheDispatchSize, 6);

        ResourceBarrier(cmdList, _cacheColor[plan.newestCache], D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrier(cmdList, _cacheGeo[plan.newestCache], D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    PlanRecorded(plan);

    // Late latch, as close as possible to the reprojection itself
    cmdList->SetPipelineState(_latchPipeline);
    cmdList->Dispatch(1, 1, 1);

    auto latchBarrier = CD3DX12_RESOURCE_BARRIER::UAV(_latchedOutput);
    cmdList->ResourceBarrier(1, &latchBarrier);

    cmdList->SetPipelineState(_pipelineState);

    UINT dispatchWidth = static_cast<UINT>((presentDesc.Width + InNumThreadsX - 1) / InNumThreadsX);
    UINT dispatchHeight = (presentDesc.Height + InNumThreadsY - 1) / InNumThreadsY;

    cmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    if (fakePresent)
        ResourceBarrier(cmdList, fakePresent, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, fakePresentState);
    ResourceBarrier(cmdList, realPresent, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, realPresentState);
    ResourceBarrier(cmdList, hudless, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, hudlessState);
    ResourceBarrier(cmdList, depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, depthState);
    if (plan.statsUpdate)
        ResourceBarrier(cmdList, velocity, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, velocityState);
    ResourceBarrier(cmdList, currentBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ResourceBarrier(cmdList, _depthMask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    return true;
}

bool Reproject_Dx12::Dispatch(IDXGISwapChain3* sc, ID3D12GraphicsCommandList* cmdList, ReprojectionParams& params,
                              ID3D12Resource* hudless, D3D12_RESOURCE_STATES hudlessState, ID3D12Resource* depth,
                              D3D12_RESOURCE_STATES depthState, ID3D12Resource* velocity,
                              D3D12_RESOURCE_STATES velocityState)
{
    if (!_init || !sc)
        return false;

    DXGI_SWAP_CHAIN_DESC scDesc {};
    if (sc->GetDesc(&scDesc) != S_OK)
    {
        LOG_WARN("Can't get swapchain desc!");
        return false;
    }

    // Get SwapChain Buffer
    ComPtr<ID3D12Resource> scBuffer;
    auto scIndex = sc->GetCurrentBackBufferIndex();
    if (auto result = sc->GetBuffer(scIndex, IID_PPV_ARGS(&scBuffer)); result != S_OK)
    {
        LOG_ERROR("sc->GetBuffer({}) error: {:X}", scIndex, (unsigned long) result);
        return false;
    }

    auto present = scBuffer.Get();
    auto presentState = D3D12_RESOURCE_STATE_PRESENT;
    auto result = Dispatch(cmdList, params, present, presentState, nullptr, D3D12_RESOURCE_STATE_COMMON, hudless,
                           hudlessState, depth, depthState, velocity, velocityState);

    if (result)
    {
        // Already in D3D12_RESOURCE_STATE_COPY_SOURCE
        auto currentBuffer = GetCurrentBuffer();

        ResourceBarrier(cmdList, present, presentState, D3D12_RESOURCE_STATE_COPY_DEST);

        cmdList->CopyResource(present, currentBuffer);

        ResourceBarrier(cmdList, present, D3D12_RESOURCE_STATE_COPY_DEST, presentState);
    }

    return result;
}

ID3D12Resource* Reproject_Dx12::GetCurrentBuffer() { return _buffer[_counter]; }

Reproject_Dx12::~Reproject_Dx12()
{
    // Always stop the input thread from writing into the mapped memory
    InputCollection::getInstance().unregisterLiveSink(_liveInputMapped);

    if (State::Instance().isShuttingDown)
        return;

    DeferredRelease::Release(_rootSignature);
    DeferredRelease::Release(_constantBuffer);
    DeferredRelease::Release(_maskPipelineH);
    DeferredRelease::Release(_maskPipelineV);
    DeferredRelease::Release(_latchPipeline);
    DeferredRelease::Release(_cachePipeline);
    DeferredRelease::Release(_statsPipeline);
    DeferredRelease::Release(_statsOutput);

    for (uint32_t i = 0; i < StatsLatency; i++)
    {
        if (_statsMapped[i])
            _statsReadback[i]->Unmap(0, nullptr);

        DeferredRelease::Release(_statsReadback[i]);
    }

    ReleaseCacheResources();
    DeferredRelease::Release(_liveInput);
    DeferredRelease::Release(_latchedOutput);
    DeferredRelease::Release(_depthMaskTemp);
    DeferredRelease::Release(_depthMask);

    for (int i = 0; i < Reproject_NUM_OF_HEAPS; i++)
    {
        _frameHeaps[i].ReleaseHeaps();
        DeferredRelease::Release(_buffer[i]);
        DeferredRelease::Release(_constantBuffers[i]);
    }

    RawInputHook::getInstance().stop();
}
