#include "HC_Dx12.h"

#include "HC_Common.h"
#include "precompile/hudless_compare_PShader.h"
#include "precompile/hudless_compare_VShader.h"

#include <Config.h>

bool HC_Dx12::CreateBufferResource(UINT index, ID3D12Device* InDevice, ID3D12Resource* InSource,
                                   D3D12_RESOURCE_STATES InState)
{
    LOG_DEBUG("[{0}] Start!", _name);

    auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    auto result = Shader_Dx12::CreateBufferResource(InDevice, InSource, InState, &_buffer[index], resourceFlags);

    if (result)
    {
        _buffer[index]->SetName(L"HC_Buffer");
        _bufferState[index] = InState;
    }

    return result;
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

HC_Dx12::HC_Dx12(std::string InName, ID3D12Device* InDevice) : Shader_Dx12(InName, InDevice)
{
    DXGI_SWAP_CHAIN_DESC scDesc {};
    if (State::Instance().currentSwapchain->GetDesc(&scDesc) != S_OK)
    {
        LOG_ERROR("Can't get swapchain desc!");
        return;
    }

    CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[] = {
        // 2 SRVs starting at register t0, space 0
        CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0),

        // 1 CBV starting at register b0, space 0
        CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0)
    };

    CD3DX12_ROOT_PARAMETER1 rootParameter {};
    rootParameter.InitAsDescriptorTable(std::size(descriptorRanges), descriptorRanges);

    D3D12_STATIC_SAMPLER_DESC sampler {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(1, &rootParameter, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ID3DBlob* signatureBlob;
    ID3DBlob* errorBlob;

    auto result = D3D12SerializeVersionedRootSignature(&rootSigDesc, &signatureBlob, &errorBlob);
    if (result != S_OK)
    {
        LOG_ERROR("D3D12SerializeVersionedRootSignature error: {:X}", (unsigned long) result);
        return;
    }

    result = InDevice->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
                                           IID_PPV_ARGS(&_rootSignature));
    if (result != S_OK)
    {
        LOG_ERROR("CreateRootSignature error: {:X}", (unsigned long) result);
        return;
    }

    if (errorBlob != nullptr)
    {
        errorBlob->Release();
        errorBlob = nullptr;
    }

    if (signatureBlob != nullptr)
    {
        signatureBlob->Release();
        signatureBlob = nullptr;
    }

    if (_rootSignature == nullptr)
    {
        LOG_ERROR("[{0}] _rootSignature is null!", _name);
        return;
    }

    // Compile shaders
    UINT cflags = 0;
    ID3DBlob *vs, *ps;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPsoDesc {};
    graphicsPsoDesc.pRootSignature = _rootSignature;

    if (Config::Instance()->UsePrecompiledShaders.value_or_default())
    {
        graphicsPsoDesc.VS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(hudless_compare_VS_cso),
                                                     sizeof(hudless_compare_VS_cso));
        graphicsPsoDesc.PS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(hudless_compare_PS_cso),
                                                     sizeof(hudless_compare_PS_cso));
    }
    else
    {
        vs = HC_CompileShader(hcCode.c_str(), "VSMain", "vs_5_1");
        if (vs != nullptr)
            graphicsPsoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };

        ps = HC_CompileShader(hcCode.c_str(), "PSMain", "ps_5_1");
        if (ps != nullptr)
            graphicsPsoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    }

    graphicsPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    graphicsPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    graphicsPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    graphicsPsoDesc.DepthStencilState.DepthEnable = FALSE;
    graphicsPsoDesc.SampleMask = UINT_MAX;
    graphicsPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    graphicsPsoDesc.NumRenderTargets = 1;
    graphicsPsoDesc.RTVFormats[0] =
        Shader_Dx12::TranslateTypelessFormats(scDesc.BufferDesc.Format); // match swapchain RTV format (can be *_SRGB)
    graphicsPsoDesc.SampleDesc = { 1, 0 };

    result = InDevice->CreateGraphicsPipelineState(&graphicsPsoDesc, IID_PPV_ARGS(&_pipelineState));
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

    State::Instance().skipHeapCapture = true;

    for (int i = 0; i < HC_NUM_OF_HEAPS; i++)
    {
        if (!_frameHeaps[i].Initialize(InDevice, 2, 0, 1, 1))
        {
            LOG_ERROR("[{0}] Failed to init heap", _name);
            _init = false;
            State::Instance().skipHeapCapture = false;
            return;
        }
    }

    State::Instance().skipHeapCapture = false;

    _init = true;
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
    _counter = _counter % HC_NUM_OF_HEAPS;

    // Check existing buffer
    D3D12_RESOURCE_DESC bufferDesc {};
    if (_buffer[_counter] != nullptr)
        bufferDesc = _buffer[_counter]->GetDesc();

    if (!CreateBufferResource(_counter, _device, scBuffer, D3D12_RESOURCE_STATE_COPY_DEST))
    {
        LOG_ERROR("CreateBufferResource error!");
        return false;
    }

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

    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];

    // Create views
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv {};
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        srv.Format = Shader_Dx12::TranslateTypelessFormats(hudlessDesc.Format);
        _device->CreateShaderResourceView(hudless, &srv, currentHeap.GetSrvCPU(0));
    }

    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv {};
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        srv.Format = Shader_Dx12::TranslateTypelessFormats(scDesc.BufferDesc.Format);
        _device->CreateShaderResourceView(_buffer[_counter], &srv, currentHeap.GetSrvCPU(1));
    }

    {
        D3D12_RENDER_TARGET_VIEW_DESC rtv {};
        rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtv.Format = Shader_Dx12::TranslateTypelessFormats(scDesc.BufferDesc.Format);
        _device->CreateRenderTargetView(scBuffer, &rtv, currentHeap.GetRtvCPU(0));
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
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv {};
        cbv.BufferLocation = _constantBuffer->GetGPUVirtualAddress();
        cbv.SizeInBytes = sizeof(constants);
        _device->CreateConstantBufferView(&cbv, currentHeap.GetCbvCPU(0));
    }

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    cmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    cmdList->SetGraphicsRootSignature(_rootSignature);
    cmdList->SetPipelineState(_pipelineState);

    cmdList->SetGraphicsRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    // Set RTV, viewport, scissor
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[] = { currentHeap.GetRtvCPU(0) };
    cmdList->OMSetRenderTargets(_countof(rtvHandles), rtvHandles, true, nullptr);

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

    return true;
}

HC_Dx12::~HC_Dx12()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    if (_rootSignature != nullptr)
    {
        _rootSignature->Release();
        _rootSignature = nullptr;
    }

    for (int i = 0; i < HC_NUM_OF_HEAPS; i++)
    {
        _frameHeaps[i].ReleaseHeaps();
    }

    if (_constantBuffer != nullptr)
    {
        _constantBuffer->Release();
        _constantBuffer = nullptr;
    }
}
