#include "pch.h"
#include "RUI_Dx12.h"

#include "RUI_Common.h"
#include "precompile/render_ui_PShader.h"
#include "precompile/render_ui_VShader.h"
#include "precompile/render_ui_pm_PShader.h"
#include "precompile/render_ui_pm_VShader.h"

#include <Config.h>

using Microsoft::WRL::ComPtr;

bool RUI_Dx12::CreateBufferResource(UINT index, ID3D12Device* InDevice, ID3D12Resource* InSource,
                                    D3D12_RESOURCE_STATES InState)
{
    LOG_DEBUG("[{0}] Start!", _name);

    auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    auto result = Shader_Dx12::CreateBufferResource(InDevice, InSource, InState, &_buffer[index], resourceFlags);

    if (result)
    {
        _buffer[index]->SetName(L"RUI_Buffer");
        _bufferState[index] = InState;
    }

    return result;
}

void RUI_Dx12::ResourceBarrier(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource,
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

void RUI_Dx12::SetBufferState(UINT index, ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
{
    if (_bufferState[index] == InState)
        return;

    ResourceBarrier(InCommandList, _buffer[index], _bufferState[index], InState);

    _bufferState[index] = InState;
}

RUI_Dx12::RUI_Dx12(std::string InName, ID3D12Device* InDevice, bool preMultipliedAlpha) : Shader_Dx12(InName, InDevice)
{
    _pm = preMultipliedAlpha;

    DXGI_SWAP_CHAIN_DESC scDesc {};
    if (State::Instance().currentSwapchain->GetDesc(&scDesc) != S_OK)
    {
        LOG_ERROR("Can't get swapchain desc!");
        return;
    }

    CD3DX12_STATIC_SAMPLER_DESC sampler(0);
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    if (!SetupRootSignature(InDevice, 2, 0, 0, 1, 0, 1, &sampler))
    {
        LOG_ERROR("Failed to setup root signature");
        return;
    }

    // Compile shaders
    UINT cflags = 0;
    ID3DBlob *vs, *ps;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPsoDesc {};
    graphicsPsoDesc.pRootSignature = _rootSignature;

    if (Config::Instance()->UsePrecompiledShaders.value_or_default())
    {
        if (!preMultipliedAlpha)
        {
            graphicsPsoDesc.VS =
                CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(render_ui_VS_cso), sizeof(render_ui_VS_cso));
            graphicsPsoDesc.PS =
                CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(render_ui_PS_cso), sizeof(render_ui_PS_cso));
        }
        else
        {
            graphicsPsoDesc.VS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(render_ui_pm_VS_cso),
                                                         sizeof(render_ui_pm_VS_cso));
            graphicsPsoDesc.PS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(render_ui_pm_PS_cso),
                                                         sizeof(render_ui_pm_PS_cso));
        }
    }
    else
    {
        if (!preMultipliedAlpha)
        {
            vs = CompileShader(ruiCode.c_str(), "VSMain", "vs_5_1");
            if (vs != nullptr)
                graphicsPsoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
            else
                graphicsPsoDesc.VS =
                    CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(render_ui_VS_cso), sizeof(render_ui_VS_cso));

            ps = CompileShader(ruiCode.c_str(), "PSMain", "ps_5_1");
            if (ps != nullptr)
                graphicsPsoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
            else
                graphicsPsoDesc.PS =
                    CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(render_ui_PS_cso), sizeof(render_ui_PS_cso));
        }
        else
        {
            vs = CompileShader(ruipmCode.c_str(), "VSMain", "vs_5_1");
            if (vs != nullptr)
                graphicsPsoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
            else
                graphicsPsoDesc.VS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(render_ui_pm_VS_cso),
                                                             sizeof(render_ui_pm_VS_cso));

            ps = CompileShader(ruipmCode.c_str(), "PSMain", "ps_5_1");
            if (ps != nullptr)
                graphicsPsoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
            else
                graphicsPsoDesc.PS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(render_ui_pm_PS_cso),
                                                             sizeof(render_ui_pm_PS_cso));
        }
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

    auto result = InDevice->CreateGraphicsPipelineState(&graphicsPsoDesc, IID_PPV_ARGS(&_pipelineState));
    if (result != S_OK)
    {
        LOG_ERROR("CreateGraphicsPipelineState error: {:X}", (unsigned long) result);
        return;
    }

    _init = InitHeaps(InDevice, _frameHeaps, HC_NUM_OF_HEAPS);
}

bool RUI_Dx12::Dispatch(IDXGISwapChain3* sc, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* hudless,
                        D3D12_RESOURCE_STATES state)
{
    if (sc == nullptr || hudless == nullptr || !_init)
        return false;

    ScopedGpuTime_Dx12 scopedGpuTime(GpuTime.get(), cmdList);

    DXGI_SWAP_CHAIN_DESC scDesc {};
    if (sc->GetDesc(&scDesc) != S_OK)
    {
        LOG_WARN("Can't get swapchain desc!");
        return false;
    }

    // Get SwapChain Buffer
    ComPtr<ID3D12Resource> scBuffer;
    auto scIndex = sc->GetCurrentBackBufferIndex();
    auto result = sc->GetBuffer(scIndex, IID_PPV_ARGS(&scBuffer));

    if (result != S_OK)
    {
        LOG_ERROR("sc->GetBuffer({}) error: {:X}", scIndex, (unsigned long) result);
        return false;
    }

    // Check Hudless Buffer
    D3D12_RESOURCE_DESC hudlessDesc = hudless->GetDesc();

    if (/*hudlessDesc.Format != scDesc.BufferDesc.Format ||*/ hudlessDesc.Width != scDesc.BufferDesc.Width ||
        hudlessDesc.Height != scDesc.BufferDesc.Height)
    {
        return false;
    }

    _counter++;
    _counter = _counter % HC_NUM_OF_HEAPS;

    if (!CreateBufferResource(_counter, _device, scBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST))
    {
        LOG_ERROR("CreateBufferResource error!");
        return false;
    }

    // Copy Swapchain Buffer to read buffer
    SetBufferState(_counter, cmdList, D3D12_RESOURCE_STATE_COPY_DEST);
    ResourceBarrier(cmdList, scBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);

    if (_buffer[_counter] != nullptr)
        cmdList->CopyResource(_buffer[_counter], scBuffer.Get());

    ResourceBarrier(cmdList, scBuffer.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    SetBufferState(_counter, cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    if (state != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
        ResourceBarrier(cmdList, hudless, state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // Start setting pipeline
    UINT outWidth = scDesc.BufferDesc.Width;
    UINT outHeight = scDesc.BufferDesc.Height;

    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];

    // Create views
    CreateShaderResourceView(_device, hudless, currentHeap.GetSrvCPU(0));
    CreateShaderResourceView(_device, _buffer[_counter], currentHeap.GetSrvCPU(1));
    CreateRenderTargetView(_device, scBuffer.Get(), currentHeap.GetRtvCPU(0), 0);

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

    ResourceBarrier(cmdList, scBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    if (state != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
        ResourceBarrier(cmdList, hudless, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, state);

    return true;
}

RUI_Dx12::~RUI_Dx12()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    SAFE_RELEASE(_rootSignature);
    SAFE_RELEASE(_constantBuffer);

    for (int i = 0; i < HC_NUM_OF_HEAPS; i++)
    {
        _frameHeaps[i].ReleaseHeaps();
    }
}
