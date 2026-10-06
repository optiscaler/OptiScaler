#include "pch.h"
#include "MenuBlur_Dx12.h"

#include <Config.h>
#include <State.h>
#include "precompile/MenuBlur_Shader.h"

using Microsoft::WRL::ComPtr;

D3D12_CPU_DESCRIPTOR_HANDLE MenuBlur_Dx12::CpuHandle(UINT index) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = _heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += (SIZE_T) index * _descriptorSize;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE MenuBlur_Dx12::GpuHandle(UINT index) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = _heap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += (UINT64) index * _descriptorSize;
    return handle;
}

DXGI_FORMAT MenuBlur_Dx12::BackBufferSrvFormat(DXGI_FORMAT InFormat)
{
    // Read raw values, the menu is rendered with a non-sRGB RTV
    switch (InFormat)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM;

    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM;

    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8X8_UNORM;

    default:
        return TranslateTypelessFormats(InFormat);
    }
}

bool MenuBlur_Dx12::CreateRootSignature(ID3D12Device* InDevice)
{
    CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

    CD3DX12_ROOT_PARAMETER1 rootParameters[2];
    rootParameters[0].InitAsConstants(sizeof(InternalMenuBlurParams) / sizeof(uint32_t), 0);
    rootParameters[1].InitAsDescriptorTable(_countof(ranges), ranges);

    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc {};
    rootSigDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler);

    ComPtr<ID3DBlob> errorBlob;
    ComPtr<ID3DBlob> signatureBlob;

    auto hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, &signatureBlob, &errorBlob);

    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] D3D12SerializeVersionedRootSignature error {1:x}", _name, hr);
        return false;
    }

    hr = InDevice->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
                                       IID_PPV_ARGS(&_rootSignature));

    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] CreateRootSignature error {1:x}", _name, hr);
        return false;
    }

    return true;
}

void MenuBlur_Dx12::ReleaseTextures()
{
    SAFE_RELEASE(_blurA);
    SAFE_RELEASE(_blurB);
}

bool MenuBlur_Dx12::Prepare(ID3D12Resource* InBackBuffer)
{
    if (!_init || InBackBuffer == nullptr)
        return false;

    auto bbDesc = InBackBuffer->GetDesc();

    if (bbDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || bbDesc.SampleDesc.Count > 1 ||
        (bbDesc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0)
    {
        return false;
    }

    UINT width = (UINT) ((bbDesc.Width + DownsampleFactor - 1) / DownsampleFactor);
    UINT height = (bbDesc.Height + DownsampleFactor - 1) / DownsampleFactor;

    if (_blurA != nullptr)
    {
        auto blurDesc = _blurA->GetDesc();

        if (blurDesc.Width == width && blurDesc.Height == height)
            return true;

        // Back buffer size only changes after ResizeBuffers so these are not in use anymore
        ReleaseTextures();
    }

    auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, 1, 1, 1, 0,
                                                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    for (auto texture : { &_blurA, &_blurB })
    {
        auto hr =
            _device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(texture));

        if (hr != S_OK)
        {
            LOG_ERROR("[{0}] CreateCommittedResource error {1:x}", _name, (unsigned int) hr);
            ReleaseTextures();
            return false;
        }
    }

    _blurA->SetName(L"MenuBlur_A");
    _blurB->SetName(L"MenuBlur_B");
    _blurAState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _blurBState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    for (UINT i = 0; i < MaxBackBuffers; i++)
        CreateUnorderedAccessView(_device, _blurA, CpuHandle(i * 2 + 1), 0);

    CreateShaderResourceView(_device, _blurA, CpuHandle(HorizontalTable));
    CreateUnorderedAccessView(_device, _blurB, CpuHandle(HorizontalTable + 1), 0);
    CreateShaderResourceView(_device, _blurB, CpuHandle(VerticalTable));
    CreateUnorderedAccessView(_device, _blurA, CpuHandle(VerticalTable + 1), 0);

    CreateShaderResourceView(_device, _blurA, _imguiSrvCpu);

    _uvScale = ImVec2((float) bbDesc.Width / (float) (width * DownsampleFactor),
                      (float) bbDesc.Height / (float) (height * DownsampleFactor));

    LOG_DEBUG("[{0}] Created blur textures {1}x{2}", _name, width, height);

    return true;
}

void MenuBlur_Dx12::DispatchPass(ID3D12GraphicsCommandList* InCmdList, UINT InTable, BlurMode InMode, float InSpacing,
                                 UINT InWidth, UINT InHeight)
{
    InternalMenuBlurParams params {};
    params.Mode = (uint32_t) InMode;
    params.Spacing = InSpacing;

    InCmdList->SetComputeRoot32BitConstants(0, sizeof(params) / sizeof(uint32_t), &params, 0);
    InCmdList->SetComputeRootDescriptorTable(1, GpuHandle(InTable));
    InCmdList->Dispatch((InWidth + 7) / 8, (InHeight + 7) / 8, 1);
}

bool MenuBlur_Dx12::Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InBackBuffer, UINT InBackBufferIndex)
{
    if (!_init || _blurA == nullptr || InCmdList == nullptr || InBackBuffer == nullptr ||
        InBackBufferIndex >= MaxBackBuffers)
    {
        return false;
    }

    // Each back buffer has its own slot so the descriptor isn't changed while a previous frame uses it
    CreateShaderResourceView(_device, InBackBuffer, CpuHandle(InBackBufferIndex * 2),
                             BackBufferSrvFormat(InBackBuffer->GetDesc().Format), false);

    auto blurDesc = _blurA->GetDesc();
    UINT width = (UINT) blurDesc.Width;
    UINT height = blurDesc.Height;

    // Strength scales the distance between taps, 1.0 is a regular 9 tap gaussian
    float spacing = Config::Instance()->MenuBlurStrength.value_or_default();

    InCmdList->SetDescriptorHeaps(1, &_heap);
    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineState);

    SetBufferState(InCmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, _blurA, &_blurAState);
    DispatchPass(InCmdList, InBackBufferIndex * 2, BlurMode::Downsample, spacing, width, height);

    if (spacing > 0.0f)
    {
        for (uint32_t i = 0; i < BlurIterations; i++)
        {
            SetBufferState(InCmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, _blurA, &_blurAState);
            SetBufferState(InCmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, _blurB, &_blurBState);
            DispatchPass(InCmdList, HorizontalTable, BlurMode::Horizontal, spacing, width, height);

            SetBufferState(InCmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, _blurB, &_blurBState);
            SetBufferState(InCmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, _blurA, &_blurAState);
            DispatchPass(InCmdList, VerticalTable, BlurMode::Vertical, spacing, width, height);
        }
    }

    SetBufferState(InCmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, _blurA, &_blurAState);

    return true;
}

MenuBlur_Dx12::MenuBlur_Dx12(std::string InName, ID3D12Device* InDevice, DescriptorHeapAllocator* InImGuiHeapAlloc)
    : Shader_Dx12(InName, InDevice), _imguiHeapAlloc(InImGuiHeapAlloc)
{
    if (InDevice == nullptr || InImGuiHeapAlloc == nullptr || InImGuiHeapAlloc->FreeIndices.empty())
    {
        LOG_ERROR("InDevice or InImGuiHeapAlloc is not usable!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    if (!CreateRootSignature(InDevice))
    {
        LOG_ERROR("[{0}] Failed to setup root signature", _name);
        return;
    }

    if (!CreateComputePipeline(InDevice, &_pipelineState, MenuBlur_cso, sizeof(MenuBlur_cso), shaderCode.c_str()))
    {
        LOG_ERROR("[{0}] Failed to create compute pipeline", _name);
        SAFE_RELEASE(_rootSignature);
        return;
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = DescriptorCount;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        ScopedSkipHeapCapture skipHeapCapture {};
        auto hr = InDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&_heap));

        if (hr != S_OK)
        {
            LOG_ERROR("[{0}] CreateDescriptorHeap error {1:x}", _name, (unsigned int) hr);
            SAFE_RELEASE(_pipelineState);
            SAFE_RELEASE(_rootSignature);
            return;
        }
    }

    _descriptorSize = InDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    _imguiHeapAlloc->Alloc(&_imguiSrvCpu, &_imguiSrvGpu);

    _init = true;
}

MenuBlur_Dx12::~MenuBlur_Dx12()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    if (_imguiHeapAlloc->Heap != nullptr)
        _imguiHeapAlloc->Free(_imguiSrvCpu, _imguiSrvGpu);

    ReleaseTextures();
    SAFE_RELEASE(_heap);
}
