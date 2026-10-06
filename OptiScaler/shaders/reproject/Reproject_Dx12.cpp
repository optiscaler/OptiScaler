#include "pch.h"
#include <Config.h>

#include "Reproject_Dx12.h"
#include "Reproject_Common.h"
#include "precompile/reproject_Shader.h"
#include "precompile/reproject_mask_h_Shader.h"
#include "precompile/reproject_mask_v_Shader.h"
#include "precompile/reproject_latch_Shader.h"

#include <numbers>
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

void Reproject_Dx12::FilloutStruct(const FilloutData& data, ReprojectionParams& params)
{
    params.ScreenWidth = data.screenWidth;
    params.ScreenHeight = data.screenHeight;
    params.InvScreenWidth = 1.0f / data.screenWidth;
    params.InvScreenHeight = 1.0f / data.screenHeight;

    params.DepthWidth = data.depthWidth;
    params.DepthHeight = data.depthHeight;

    params.UiDiffThreshold = data.diffThreshold;
    params.DepthCutoff = Config::Instance()->ReprojectionDepthCutoff.value_or_default();
    params.DitherWidthPx = data.screenHeight / 16.0f; // TODO: configurable
    params.CutoffExpandPx = Config::Instance()->ReprojectionCutoffExpand.value_or_default();

    params.InvertedDepth = data.invertedDepth;
    params.ShowStaticElements = State::Instance().fgHudlessCompare;
    params.FakeFrame = data.fakeFrame;

    auto fillMode = Config::Instance()->ReprojectionFillMode.value_or_default();
    if (fillMode == ReprojectionFill::Debug)
        params.EdgeMode = 0;
    else if (fillMode == ReprojectionFill::StrechEdge)
        params.EdgeMode = 1;
    else if (fillMode == ReprojectionFill::Dithering)
        params.EdgeMode = 2;
    else if (fillMode == ReprojectionFill::Noise)
        params.EdgeMode = 3;

    const float tanHalfFovY = std::tan(data.cameraVFov * 0.5f);
    const float pixelAngle = 2.0f * std::atan(tanHalfFovY / data.screenHeight);

    params.TanHalfFovY = tanHalfFovY;
    params.TanHalfFovX = tanHalfFovY * data.cameraAspectRatio;
    params.InvTanHalfFovY = 1.0f / tanHalfFovY;
    params.InvTanHalfFovX = 1.0f / params.TanHalfFovX;

    // Compute actual camera rotation deltas between frames
    if (!_isFirstFrame && !data.fakeFrame)
    {
        XMVECTOR prevFwd = XMVector3Normalize(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(data.cameraPrevForward)));
        XMVECTOR currFwd = XMVector3Normalize(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(data.cameraForward)));

        XMFLOAT3 p, c;
        XMStoreFloat3(&p, prevFwd);
        XMStoreFloat3(&c, currFwd);

        // Yaw delta (wrapped to [-pi, pi])
        float camYawDelta = std::atan2(c.y, c.x) - std::atan2(p.y, p.x);

        if (camYawDelta > std::numbers::pi_v<float>)
            camYawDelta -= 2.0f * std::numbers::pi_v<float>;

        if (camYawDelta < -std::numbers::pi_v<float>)
            camYawDelta += 2.0f * std::numbers::pi_v<float>;

        // Pitch delta
        const float camPitchDelta = std::asin(std::clamp(c.z, -1.0f, 1.0f)) - std::asin(std::clamp(p.z, -1.0f, 1.0f));

        const float mouseX = data.mouseDeltaSimToSim.x * pixelAngle;
        const float mouseY = data.mouseDeltaSimToSim.y * pixelAngle;

        _calibration.Update(mouseX, mouseY, camYawDelta, camPitchDelta);
    }
    else
    {
        _isFirstFrame = false;
    }

    // Compute camera basis
    XMVECTOR camRight = XMVector3Normalize(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(data.cameraRight)));
    XMVECTOR camUp = XMVector3Normalize(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(data.cameraUp)));
    XMVECTOR camForward = XMVector3Normalize(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(data.cameraForward)));

    constexpr float TEST_ANGLE = 0.001f;

    // Find which matrix rotation direction corresponds to positive camera motion.
    XMVECTOR testPitchForward =
        XMVector3Normalize(XMVector3TransformNormal(camForward, XMMatrixRotationAxis(camRight, TEST_ANGLE)));

    float pitchTest = std::asin(std::clamp(XMVectorGetZ(testPitchForward), -1.0f, 1.0f)) -
                      std::asin(std::clamp(XMVectorGetZ(camForward), -1.0f, 1.0f));

    int pitchSign = (pitchTest >= 0.0f) ? 1 : -1;

    XMVECTOR testYawForward = XMVector3Normalize(
        XMVector3TransformNormal(camForward, XMMatrixRotationAxis(XMVectorSet(0, 0, 1, 0), TEST_ANGLE)));

    float yawTest = std::atan2(XMVectorGetY(testYawForward), XMVectorGetX(testYawForward)) -
                    std::atan2(XMVectorGetY(camForward), XMVectorGetX(camForward));

    if (yawTest > std::numbers::pi_v<float>)
        yawTest -= 2.0f * std::numbers::pi_v<float>;

    if (yawTest < -std::numbers::pi_v<float>)
        yawTest += 2.0f * std::numbers::pi_v<float>;

    int yawSign = (std::abs(yawTest) >= 1e-8f && yawTest >= 0.0f) ? 1 : -1;

    // The rotation itself is built by the latch pass on the GPU, see reproject_latch.hlsl
    XMStoreFloat4(&params.CameraRight, camRight);
    XMStoreFloat4(&params.CameraUp, camUp);
    XMStoreFloat4(&params.CameraForward, camForward);
    params.Calibration = { _calibration.yawFromX, _calibration.yawFromY, _calibration.pitchFromX,
                           _calibration.pitchFromY };

    params.PixelAngle = pixelAngle;
    params.YawSign = (float) yawSign;
    params.PitchSign = (float) pitchSign;

    params.LateLatch = data.lateLatch ? 1 : 0;
    params.MouseDelta = data.mouseDeltaSinceSim;
    params.SimStartMouse = data.simStartMouse;
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
        (*resource)->Release();
        (*resource) = nullptr;
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

    CD3DX12_STATIC_SAMPLER_DESC sampler(0);
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    // SRV: Hudless, PresentCopy, Depth, FakePresent, DepthMask, DepthMaskTemp
    // UAV: Present, DepthMaskTemp, DepthMask, Latched
    // CBV: Params, LiveInput
    if (!SetupRootSignature(InDevice, 6, 4, 2, 0, 0, 1, &sampler))
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
                              D3D12_RESOURCE_STATES depthState)
{
    if (!_init || !_device || !realPresent || !hudless || !cmdList || !depth)
        return false;

    ScopedGpuTime_Dx12 scopedGpuTime(GpuTime.get(), cmdList);

    _counter++;
    _counter = _counter % Reproject_NUM_OF_HEAPS;
    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];
    auto& currentBuffer = _buffer[_counter];

    if (!currentBuffer)
    {
        LOG_DEBUG("[{0}] Start!", _name);

        auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                             D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

        auto result = Shader_Dx12::CreateBufferResource(_device, realPresent, D3D12_RESOURCE_STATE_COPY_DEST,
                                                        &currentBuffer, resourceFlags);

        if (result)
            currentBuffer->SetName(L"Reproject_Buffer");

        return result;
    }

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
    ResourceBarrier(cmdList, currentBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // Create views
    auto presentDesc = realPresent->GetDesc();

    // Because of HudlessTransfer, the format of hudless is the same as present
    CreateShaderResourceView(_device, hudless, currentHeap.GetSrvCPU(0), presentDesc.Format);
    CreateShaderResourceView(_device, realPresent, currentHeap.GetSrvCPU(1));
    CreateShaderResourceView(_device, depth, currentHeap.GetSrvCPU(2));
    if (fakePresent)
        CreateShaderResourceView(_device, fakePresent, currentHeap.GetSrvCPU(3));
    CreateShaderResourceView(_device, _depthMask, currentHeap.GetSrvCPU(4));
    CreateShaderResourceView(_device, _depthMaskTemp, currentHeap.GetSrvCPU(5));

    CreateUnorderedAccessView(_device, currentBuffer, currentHeap.GetUavCPU(0), 0);
    CreateUnorderedAccessView(_device, _depthMaskTemp, currentHeap.GetUavCPU(1), 0);
    CreateUnorderedAccessView(_device, _depthMask, currentHeap.GetUavCPU(2), 0);

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
    ResourceBarrier(cmdList, currentBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ResourceBarrier(cmdList, _depthMask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    return true;
}

bool Reproject_Dx12::Dispatch(IDXGISwapChain3* sc, ID3D12GraphicsCommandList* cmdList, ReprojectionParams& params,
                              ID3D12Resource* hudless, D3D12_RESOURCE_STATES hudlessState, ID3D12Resource* depth,
                              D3D12_RESOURCE_STATES depthState)
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
                           hudlessState, depth, depthState);

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

    if (!_init || State::Instance().isShuttingDown)
        return;

    SAFE_RELEASE(_rootSignature);
    SAFE_RELEASE(_constantBuffer);
    SAFE_RELEASE(_maskPipelineH);
    SAFE_RELEASE(_maskPipelineV);
    SAFE_RELEASE(_latchPipeline);
    SAFE_RELEASE(_liveInput);
    SAFE_RELEASE(_latchedOutput);
    SAFE_RELEASE(_depthMaskTemp);
    SAFE_RELEASE(_depthMask);

    for (int i = 0; i < Reproject_NUM_OF_HEAPS; i++)
    {
        _frameHeaps[i].ReleaseHeaps();
        SAFE_RELEASE(_constantBuffers[i]);
    }

    RawInputHook::getInstance().stop();
}
