#include "HC_Dx12.h"

#include "HC_Common.h"
#include "precompile/hudless_compare_PShader.h"
#include "precompile/hudless_compare_VShader.h"

#include <Config.h>

DXGI_FORMAT HC_Dx12::ToSRGB(DXGI_FORMAT f)
{
    return f;

    // switch (f)
    //{
    // case DXGI_FORMAT_R8G8B8A8_UNORM:
    //     return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    // case DXGI_FORMAT_B8G8R8A8_UNORM:
    //     return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    // default:
    //     return f;
    // }
}

bool HC_Dx12::CreateBufferResource(UINT index, ID3D12Device* InDevice, ID3D12Resource* InSource,
                                   D3D12_RESOURCE_STATES InState)
{
    if (InDevice == nullptr || InSource == nullptr)
        return false;

    D3D12_RESOURCE_DESC texDesc = InSource->GetDesc();

    if (_buffer[index] != nullptr)
    {
        auto bufDesc = _buffer[index]->GetDesc();

        if (bufDesc.Width != (UINT64) (texDesc.Width) || bufDesc.Height != (UINT) (texDesc.Height) ||
            bufDesc.Format != texDesc.Format)
        {
            _buffer[index]->Release();
            _buffer[index] = nullptr;
        }
        else
        {
            return true;
        }
    }

    LOG_DEBUG("[{0}] Start!", _name);

    D3D12_HEAP_PROPERTIES heapProperties;
    D3D12_HEAP_FLAGS heapFlags;
    HRESULT hr = InSource->GetHeapProperties(&heapProperties, &heapFlags);

    if (hr != S_OK)
    {
        LOG_ERROR("[{0}] GetHeapProperties result: {1:x}", _name.c_str(), hr);
        return false;
    }

    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    texDesc.Width = texDesc.Width;
    texDesc.Height = texDesc.Height;

    hr = InDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &texDesc, InState, nullptr,
                                           IID_PPV_ARGS(&_buffer[index]));

    if (hr != S_OK)
    {
        LOG_ERROR("[{0}] CreateCommittedResource result: {1:x}", _name, hr);
        return false;
    }

    _buffer[index]->SetName(L"HC_Buffer");
    _bufferState[index] = InState;

    return true;
}

void HC_Dx12::ResourceBarrier(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource,
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

void HC_Dx12::SetBufferState(UINT index, ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
{
    if (_bufferState[index] == InState)
        return;

    ResourceBarrier(InCommandList, _buffer[index], _bufferState[index], InState);

    _bufferState[index] = InState;
}

HC_Dx12::HC_Dx12(std::string InName, ID3D12Device* InDevice)
{
    _name = InName;

    DXGI_SWAP_CHAIN_DESC scDesc {};
    if (State::Instance().currentSwapchain->GetDesc(&scDesc) != S_OK)
    {
        LOG_ERROR("Can't get swapchain desc!");
        return;
    }

    // Root signature
    CD3DX12_DESCRIPTOR_RANGE1 rangeSRV0(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
    CD3DX12_DESCRIPTOR_RANGE1 rangeSRV1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0);
    CD3DX12_DESCRIPTOR_RANGE1 rangeCBV0(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0);

    CD3DX12_ROOT_PARAMETER1 params[3] {};
    params[0].InitAsDescriptorTable(1, &rangeSRV0, D3D12_SHADER_VISIBILITY_PIXEL);
    params[1].InitAsDescriptorTable(1, &rangeSRV1, D3D12_SHADER_VISIBILITY_PIXEL);
    params[2].InitAsDescriptorTable(1, &rangeCBV0, D3D12_SHADER_VISIBILITY_PIXEL);

    D3D12_STATIC_SAMPLER_DESC samp {};
    samp.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.MaxLOD = D3D12_FLOAT32_MAX;
    samp.ShaderRegister = 0;
    samp.RegisterSpace = 0;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc;
    rsDesc.Init_1_1(_countof(params), params, 1, &samp, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ID3DBlob *sig, *err;
    auto result = D3D12SerializeVersionedRootSignature(&rsDesc, &sig, &err);
    if (result != S_OK)
    {
        LOG_ERROR("D3D12SerializeVersionedRootSignature error: {:X}", (unsigned long) result);
        return;
    }

    result =
        InDevice->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&_rootSignature));
    if (result != S_OK)
    {
        LOG_ERROR("CreateRootSignature error: {:X}", (unsigned long) result);
        return;
    }

    // Compile shaders
    UINT cflags = 0;
    ID3DBlob *vs, *ps;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso {};
    pso.pRootSignature = _rootSignature;

    if (Config::Instance()->UsePrecompiledShaders.value_or_default())
    {
        pso.VS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(hudless_compare_VS_cso),
                                         sizeof(hudless_compare_VS_cso));
        pso.PS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(hudless_compare_PS_cso),
                                         sizeof(hudless_compare_PS_cso));
    }
    else
    {
        vs = HC_CompileShader(hcCode.c_str(), "VSMain", "vs_5_1");
        if (vs != nullptr)
            pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };

        ps = HC_CompileShader(hcCode.c_str(), "PSMain", "ps_5_1");
        if (ps != nullptr)
            pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    }

    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = ShaderDx12Utils::TranslateTypelessFormats(
        ToSRGB(scDesc.BufferDesc.Format)); // match swapchain RTV format (can be *_SRGB)
    pso.SampleDesc = { 1, 0 };

    result = InDevice->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&_pipelineState));
    if (result != S_OK)
    {
        LOG_ERROR("CreateGraphicsPipelineState error: {:X}", (unsigned long) result);
        return;
    }

    // Create Constant Buffer
    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(InternalCompareParams));
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    result =
        InDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                          nullptr, IID_PPV_ARGS(&_constantBuffer));

    if (result != S_OK)
    {
        LOG_ERROR("CreateCommittedResource error {:X}", (unsigned int) result);
        return;
    }

    // Command List
    // for (size_t i = 0; i < 2; i++)
    //{
    //    result = InDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
    //    IID_PPV_ARGS(&_commandAllocator[i]));

    //    if (result != S_OK)
    //    {
    //        LOG_ERROR("CreateCommandAllocator error: {:X}", (unsigned long) result);
    //        return;
    //    }

    //    result = InDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _commandAllocator[i], NULL,
    //                                         IID_PPV_ARGS(&_commandList[i]));
    //    if (result != S_OK)
    //    {
    //        LOG_ERROR("CreateCommandList error: {:X}", (unsigned long) result);
    //        return;
    //    }

    //    result = _commandList[i]->Close();
    //    if (result != S_OK)
    //    {
    //        LOG_ERROR("_commandList->Close error: {:X}", (unsigned long) result);
    //        return;
    //    }
    //}

    // Create Heaps
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 6; // SRV + UAV + CBV
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    State::Instance().skipHeapCapture = true;

    auto hr = InDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_srvHeap[0]));

    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] CreateDescriptorHeap[0] error {1:x}", _name, hr);
        return;
    }

    heapDesc.NumDescriptors = 2; // RTV
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hr = InDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_srvHeap[1]));

    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] CreateDescriptorHeap[1] error {1:x}", _name, hr);
        return;
    }

    _init = _srvHeap[1] != nullptr;

    if (_init)
        _device = InDevice;
}

bool HC_Dx12::Dispatch(IDXGISwapChain3* sc, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* hudless,
                       D3D12_RESOURCE_STATES state)
{
    if (sc == nullptr || hudless == nullptr || !_init)
        return false;

    DXGI_SWAP_CHAIN_DESC scDesc {};
    if (sc->GetDesc(&scDesc) != S_OK)
    {
        LOG_WARN("Can't get swapchain desc!");
        return false;
    }

    // Get SwapChain Buffer
    ID3D12Resource* scBuffer = nullptr;
    auto scIndex = sc->GetCurrentBackBufferIndex();
    auto result = sc->GetBuffer(scIndex, IID_PPV_ARGS(&scBuffer));

    if (result != S_OK)
    {
        LOG_ERROR("sc->GetBuffer({}) error: {:X}", scIndex, (unsigned long) result);
        return false;
    }

    scBuffer->Release();

    // Check Hudless Buffer
    D3D12_RESOURCE_DESC hudlessDesc = hudless->GetDesc();

    if (/*hudlessDesc.Format != scDesc.BufferDesc.Format ||*/ hudlessDesc.Width != scDesc.BufferDesc.Width ||
        hudlessDesc.Height != scDesc.BufferDesc.Height)
    {
        return false;
    }

    _counter++;
    _counter = _counter % 2;

    // Check existing buffer
    D3D12_RESOURCE_DESC bufferDesc {};
    if (_buffer[_counter] != nullptr)
        bufferDesc = _buffer[_counter]->GetDesc();

    if (!CreateBufferResource(_counter, _device, scBuffer, D3D12_RESOURCE_STATE_COPY_DEST))
    {
        LOG_ERROR("CreateBufferResource error!");
        return false;
    }

    // Reset CommandList
    // result = _commandAllocator[_counter]->Reset();
    // if (result != S_OK)
    //{
    //    LOG_ERROR("_commandAllocator->Reset() error: {:X}", (UINT) result);
    //    return false;
    //}

    // result = _commandList[_counter]->Reset(_commandAllocator[_counter], nullptr);
    // if (result != S_OK)
    //{
    //     LOG_ERROR("_commandList->Reset error: {:X}", (UINT) result);
    //     return false;
    // }

    // Copy Swapchain Buffer to read buffer
    SetBufferState(_counter, cmdList, D3D12_RESOURCE_STATE_COPY_DEST);
    ResourceBarrier(cmdList, scBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);

    if (_buffer != nullptr)
        cmdList->CopyResource(_buffer[_counter], scBuffer);

    ResourceBarrier(cmdList, scBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    SetBufferState(_counter, cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    if (state != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
        ResourceBarrier(cmdList, hudless, state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // Start setting pipeline
    UINT outWidth = scDesc.BufferDesc.Width;
    UINT outHeight = scDesc.BufferDesc.Height;

    // Create heap handles
    if (_cpuSrv0Handle[0].ptr == NULL)
    {
        auto size = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        _cpuSrv0Handle[0] = _srvHeap[0]->GetCPUDescriptorHandleForHeapStart();
        _cpuSrv0Handle[1] = _cpuSrv0Handle[0];
        _cpuSrv0Handle[1].ptr += size;
        _cpuSrv1Handle[0] = _cpuSrv0Handle[1];
        _cpuSrv1Handle[0].ptr += size;
        _cpuSrv1Handle[1] = _cpuSrv1Handle[0];
        _cpuSrv1Handle[1].ptr += size;
        _cpuCbv0Handle[0] = _cpuSrv1Handle[1];
        _cpuCbv0Handle[0].ptr += size;
        _cpuCbv0Handle[1] = _cpuCbv0Handle[0];
        _cpuCbv0Handle[1].ptr += size;
    }

    if (_cpuRtv0Handle[0].ptr == NULL)
    {
        auto size = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        _cpuRtv0Handle[0] = _srvHeap[1]->GetCPUDescriptorHandleForHeapStart();
        _cpuRtv0Handle[1] = _cpuRtv0Handle[0];
        _cpuRtv0Handle[1].ptr += size;
    }

    if (_gpuSrv0Handle[0].ptr == NULL)
    {
        auto size = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        _gpuSrv0Handle[0] = _srvHeap[0]->GetGPUDescriptorHandleForHeapStart();
        _gpuSrv0Handle[1] = _gpuSrv0Handle[0];
        _gpuSrv0Handle[1].ptr += size;
        _gpuSrv1Handle[0] = _gpuSrv0Handle[1];
        _gpuSrv1Handle[0].ptr += size;
        _gpuSrv1Handle[1] = _gpuSrv1Handle[0];
        _gpuSrv1Handle[1].ptr += size;
        _gpuCbv0Handle[0] = _gpuSrv1Handle[1];
        _gpuCbv0Handle[0].ptr += size;
        _gpuCbv0Handle[1] = _gpuCbv0Handle[0];
        _gpuCbv0Handle[1].ptr += size;
    }

    // Create views
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv {};
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        srv.Format = ShaderDx12Utils::TranslateTypelessFormats(ToSRGB(hudlessDesc.Format));
        _device->CreateShaderResourceView(hudless, &srv, _cpuSrv0Handle[_counter]);
    }

    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv {};
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        srv.Format = ShaderDx12Utils::TranslateTypelessFormats(ToSRGB(scDesc.BufferDesc.Format));
        _device->CreateShaderResourceView(_buffer[_counter], &srv, _cpuSrv1Handle[_counter]);
    }

    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv {};
        cbv.BufferLocation = _constantBuffer->GetGPUVirtualAddress();
        cbv.SizeInBytes = sizeof(InternalCompareParams);
        _device->CreateConstantBufferView(&cbv, _cpuCbv0Handle[_counter]);
    }

    {
        D3D12_RENDER_TARGET_VIEW_DESC rtv {};
        rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtv.Format = ShaderDx12Utils::TranslateTypelessFormats(ToSRGB(scDesc.BufferDesc.Format));
        _device->CreateRenderTargetView(scBuffer, &rtv, _cpuRtv0Handle[_counter]);
    }

    InternalCompareParams constants {};
    constants.DiffThreshold = 0.003f;
    constants.PinkAmount = 0.6f;

    BYTE* pCBDataBegin;
    CD3DX12_RANGE readRange(0, 0);
    result = _constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pCBDataBegin));
    if (result != S_OK)
    {
        LOG_ERROR("_constantBuffer->Map error {:X}", (unsigned int) result);
        return false;
    }

    if (pCBDataBegin == nullptr)
    {
        _constantBuffer->Unmap(0, nullptr);
        LOG_ERROR("pCBDataBegin is null!");
        return false;
    }

    memcpy(pCBDataBegin, &constants, sizeof(constants));
    _constantBuffer->Unmap(0, nullptr);

    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = _constantBuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = sizeof(constants);
        _device->CreateConstantBufferView(&cbvDesc, _cpuCbv0Handle[_counter]);
    }

    // Pipeline
    // Bind pipeline + heap
    cmdList->SetGraphicsRootSignature(_rootSignature);
    cmdList->SetPipelineState(_pipelineState);

    cmdList->SetDescriptorHeaps(1, &_srvHeap[0]);

    cmdList->SetGraphicsRootDescriptorTable(0, _gpuSrv0Handle[_counter]); // t0
    cmdList->SetGraphicsRootDescriptorTable(1, _gpuSrv1Handle[_counter]); // t1
    cmdList->SetGraphicsRootDescriptorTable(2, _gpuCbv0Handle[_counter]); // b0

    // Set RTV, viewport, scissor
    cmdList->OMSetRenderTargets(1, &_cpuRtv0Handle[_counter], FALSE, nullptr);

    D3D12_VIEWPORT vp {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<FLOAT>(outWidth);
    vp.Height = static_cast<FLOAT>(outHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    cmdList->RSSetViewports(1, &vp);

    D3D12_RECT rect { 0, 0, (LONG) outWidth, (LONG) outHeight };
    cmdList->RSSetScissorRects(1, &rect);

    // Fullscreen triangle
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);

    ResourceBarrier(cmdList, scBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    if (state != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
        ResourceBarrier(cmdList, hudless, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, state);

    // result = cmdList->Close();

    // if (result != S_OK)
    //{
    //     LOG_ERROR("_commandList[{}]->Close() error: {:X}", _counter, (UINT) result);
    //     return false;
    // }

    // queue->ExecuteCommandLists(1, (ID3D12CommandList**) &_commandList[_counter]);

    return true;
}

HC_Dx12::~HC_Dx12()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    // for (size_t i = 0; i < 2; i++)
    //{
    //     if (_commandAllocator[i] != nullptr)
    //         _commandAllocator[i]->Release();

    //    if (_commandList[i] != nullptr)
    //        _commandList[i]->Release();

    //    if (_buffer[i] != nullptr)
    //    {
    //        _buffer[i]->Release();
    //        _buffer[i] = nullptr;
    //    }
    //}

    if (_rootSignature != nullptr)
    {
        _rootSignature->Release();
        _rootSignature = nullptr;
    }

    if (_srvHeap[0] != nullptr)
    {
        _srvHeap[0]->Release();
        _srvHeap[0] = nullptr;
    }

    if (_srvHeap[1] != nullptr)
    {
        _srvHeap[1]->Release();
        _srvHeap[1] = nullptr;
    }

    if (_constantBuffer != nullptr)
    {
        _constantBuffer->Release();
        _constantBuffer = nullptr;
    }
}
