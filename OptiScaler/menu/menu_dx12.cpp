#include "pch.h"
#include "menu_dx12.h"

#include <d3dx/d3dx12.h>

#include "Config.h"
#include "menu_common.h"
#include <misc/DeferredRelease.h>
#include <imgui/imgui_impl_dx12.h>
#include <imgui/imgui_impl_win32.h>

long frameCounter = 0;
static int const SRV_HEAP_SIZE = 64;

struct ImGui_ImplDX12_Texture
{
    ID3D12Resource* pTextureResource;
    D3D12_CPU_DESCRIPTOR_HANDLE hFontSrvCpuDescHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE hFontSrvGpuDescHandle;

    ImGui_ImplDX12_Texture() { memset((void*) this, 0, sizeof(*this)); }
};

struct ImGui_ImplDX12_Data
{
    ImGui_ImplDX12_InitInfo InitInfo;
    ID3D12Device* pd3dDevice;
    ID3D12RootSignature* pRootSignature;
    ID3D12PipelineState* pPipelineState;
    ID3D12CommandQueue* pCommandQueue;
    bool commandQueueOwned;
    DXGI_FORMAT RTVFormat;
    DXGI_FORMAT DSVFormat;
    ID3D12DescriptorHeap* pd3dSrvDescHeap;
    UINT numFramesInFlight;
    ImGui_ImplDX12_Texture FontTexture;
    bool LegacySingleDescriptorUsed;

    ImGui_ImplDX12_Data() { memset((void*) this, 0, sizeof(*this)); }
};

static DescriptorHeapAllocator g_pd3dSrvDescHeapAlloc;

bool Menu_Dx12::Render(ID3D12GraphicsCommandList* pCmdList, ID3D12Resource* outTexture)
{
    if (Config::Instance()->OverlayMenu.value_or_default())
        return false;

    if (pCmdList == nullptr || outTexture == nullptr)
        return false;

    frameCounter++;

    // if (!IsVisible())
    //	return true;

    auto outDesc = outTexture->GetDesc();

    CreateRenderTarget(outDesc);

    ImGuiIO& io = ImGui::GetIO();
    (void) io;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    if (!_dx12Init && io.BackendRendererUserData == nullptr)
    {
        ImGui_ImplDX12_InitInfo initInfo {};
        initInfo.Device = _device;

        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.NodeMask = 1;
        HRESULT hr = _device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&initInfo.CommandQueue));
        IM_ASSERT(SUCCEEDED(hr));

        initInfo.NumFramesInFlight = 2;
        initInfo.RTVFormat = TranslateTypelessFormats(outDesc.Format);
        initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
        initInfo.SrvDescriptorHeap = _srvDescHeap;
        initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle,
                                           D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle)
        { return g_pd3dSrvDescHeapAlloc.Alloc(out_cpu_handle, out_gpu_handle); };
        initInfo.SrvDescriptorFreeFn =
            [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle)
        { return g_pd3dSrvDescHeapAlloc.Free(cpu_handle, gpu_handle); };

        _dx12Init = ImGui_ImplDX12_Init(&initInfo);

        ImGui_ImplDX12_Data* bd =
            ImGui::GetCurrentContext() ? (ImGui_ImplDX12_Data*) ImGui::GetIO().BackendRendererUserData : nullptr;
        if (bd)
            bd->commandQueueOwned = true;
    }

    if (!_dx12Init)
        return false;

    frameCounter++;

    auto backbuf = frameCounter % 2;

    D3D12_RENDER_TARGET_VIEW_DESC rtDesc = {};
    rtDesc.Format = TranslateTypelessFormats(outDesc.Format);
    rtDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    if ((outDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) > 0)
    {
        D3D12_RESOURCE_BARRIER outBarrier = {};
        outBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        outBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        outBarrier.Transition.pResource = outTexture;
        outBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        outBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        outBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        pCmdList->ResourceBarrier(1, &outBarrier);

        // Create RTV for out
        pCmdList->SetDescriptorHeaps(1, &_srvDescHeap);

        _device->CreateRenderTargetView(outTexture, &rtDesc, _renderTargetDescriptor[backbuf]);
        pCmdList->OMSetRenderTargets(1, &_renderTargetDescriptor[backbuf], FALSE, NULL);

        ImGui_ImplDX12_NewFrame();
        // ImGui_ImplWin32_NewFrame();

        // Render
        if (MenuDxBase::RenderMenu())
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pCmdList);

        outBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        outBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        pCmdList->ResourceBarrier(1, &outBarrier);

        return true;
    }

    D3D12_RESOURCE_BARRIER outBarrier = {};
    outBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    outBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    outBarrier.Transition.pResource = outTexture;
    outBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    outBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    outBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;

    D3D12_RESOURCE_BARRIER bufferBarrier = {};
    bufferBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bufferBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    bufferBarrier.Transition.pResource = _renderTargetResource[backbuf];
    bufferBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    bufferBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    bufferBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;

    D3D12_RESOURCE_BARRIER barriers[] = { bufferBarrier, outBarrier };
    pCmdList->ResourceBarrier(2, barriers);

    // Copy out to buffer
    pCmdList->CopyResource(_renderTargetResource[backbuf], outTexture);

    // Set as render target
    bufferBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    bufferBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    pCmdList->ResourceBarrier(1, &bufferBarrier);

    // Create RTV for buffer
    pCmdList->SetDescriptorHeaps(1, &_srvDescHeap);

    _device->CreateRenderTargetView(_renderTargetResource[backbuf], &rtDesc, _renderTargetDescriptor[backbuf]);
    pCmdList->OMSetRenderTargets(1, &_renderTargetDescriptor[backbuf], FALSE, nullptr);

    ImGui_ImplDX12_NewFrame();
    // ImGui_ImplWin32_NewFrame();

    // Render to buffer
    if (MenuDxBase::RenderMenu())
    {
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pCmdList);

        outBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        outBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;

        bufferBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        bufferBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        pCmdList->ResourceBarrier(2, barriers);

        // Copy back buffer to out
        pCmdList->CopyResource(outTexture, _renderTargetResource[backbuf]);

        // fix states
        bufferBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        bufferBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

        outBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        outBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        pCmdList->ResourceBarrier(2, barriers);
    }

    return true;
}

Menu_Dx12::Menu_Dx12(HWND handle, ID3D12Device* pDevice) : MenuDxBase(handle), _device(pDevice)
{
    if (Config::Instance()->OverlayMenu.value_or_default())
        return;

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = 2;
    rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvDesc.NodeMask = 1;

    if (pDevice->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&_rtvDescHeap)) != S_OK)
        return;

    _rtvDescHeap->SetName(L"Imgui_Dx12_rtvHeap");

    SIZE_T rtvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = _rtvDescHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < 2; ++i)
    {
        _renderTargetDescriptor[i] = rtvHandle;
        rtvHandle.ptr += rtvDescriptorSize;
    }

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = SRV_HEAP_SIZE;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    {
        ScopedSkipHeapCapture skipHeapCapture {};

        if (pDevice->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&_srvDescHeap)) != S_OK)
            return;
    }

    g_pd3dSrvDescHeapAlloc.Destroy();
    g_pd3dSrvDescHeapAlloc.Create(pDevice, _srvDescHeap);

    Dx12Ready();

    _srvDescHeap->SetName(L"Imgui_Dx12_srvDescHeap");
}

Menu_Dx12::~Menu_Dx12()
{
    // g_pd3dSrvDescHeapAlloc.Destroy(); // Can cause a crash on app close, unsure why

    if (!_dx12Init)
        return;

    // On shutting down don't invalidate device objects
    ImGui_ImplDX12_Shutdown(true, !State::Instance().isShuttingDown);
    MenuCommon::Shutdown();

    // Last menu render can still be running
    DeferredRelease::Release(_rtvDescHeap);
    DeferredRelease::Release(_srvDescHeap);
    DeferredRelease::Release(_renderTargetResource[0]);
    DeferredRelease::Release(_renderTargetResource[1]);
}

void Menu_Dx12::CreateRenderTarget(const D3D12_RESOURCE_DESC& InDesc)
{
    if (_renderTargetResource[0] != nullptr)
    {
        auto rtDesc = _renderTargetResource[0]->GetDesc();

        if (InDesc.Width != rtDesc.Width || InDesc.Height != rtDesc.Height ||
            TranslateTypelessFormats(InDesc.Format) != rtDesc.Format)
        {
            // Last frames' copies can still be running
            DeferredRelease::Release(_renderTargetResource[0]);
            DeferredRelease::Release(_renderTargetResource[1]);
        }
        else
            return;
    }

    for (UINT i = 0; i < 2; ++i)
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = InDesc.Width;
        desc.Height = InDesc.Height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = TranslateTypelessFormats(InDesc.Format);
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        ID3D12Resource* renderTarget;
        auto result =
            _device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                             D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&renderTarget));

        if (result == S_OK)
        {
            renderTarget->SetName(L"Imgui_Dx12_renderTarget");

            D3D12_RENDER_TARGET_VIEW_DESC rtDesc = {};
            rtDesc.Format = desc.Format;
            rtDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

            _device->CreateRenderTargetView(renderTarget, &rtDesc, _renderTargetDescriptor[i]);
            _renderTargetResource[i] = renderTarget;
        }
    }
}
