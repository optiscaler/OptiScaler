#pragma once
#include "MenuBlur_Common.h"

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <shaders/Shader_Dx12.h>
#include <imgui/imgui_impl_dx12.h>

// Blurs the swapchain image for the background of the overlay menu
class MenuBlur_Dx12 : public Shader_Dx12, public MenuBlur_Common
{
  private:
    static constexpr UINT MaxBackBuffers = 8;

    // Descriptor layout, every pass uses a SRV + UAV pair
    // [2 * i]: back buffer i SRV, [2 * i + 1]: blur A UAV
    static constexpr UINT HorizontalTable = MaxBackBuffers * 2; // blur A SRV, blur B UAV
    static constexpr UINT VerticalTable = HorizontalTable + 2;  // blur B SRV, blur A UAV
    static constexpr UINT DescriptorCount = VerticalTable + 2;

    ID3D12DescriptorHeap* _heap = nullptr;
    UINT _descriptorSize = 0;

    // Final result is always in A
    ID3D12Resource* _blurA = nullptr;
    ID3D12Resource* _blurB = nullptr;
    D3D12_RESOURCE_STATES _blurAState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES _blurBState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    // SRV of blur A in the ImGui heap
    DescriptorHeapAllocator* _imguiHeapAlloc = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE _imguiSrvCpu {};
    D3D12_GPU_DESCRIPTOR_HANDLE _imguiSrvGpu {};

    // Part of the blurred texture covered by the back buffer
    ImVec2 _uvScale { 1.0f, 1.0f };

    D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle(UINT index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(UINT index) const;
    bool CreateRootSignature(ID3D12Device* InDevice);
    void DispatchPass(ID3D12GraphicsCommandList* InCmdList, UINT InTable, BlurMode InMode, float InSpacing,
                      UINT InWidth, UINT InHeight);
    void ReleaseTextures();

    static DXGI_FORMAT BackBufferSrvFormat(DXGI_FORMAT InFormat);

  public:
    // Makes sure blur textures match the back buffer, false if the back buffer can't be blurred
    bool Prepare(ID3D12Resource* InBackBuffer);

    // InBackBuffer needs to be in NON_PIXEL_SHADER_RESOURCE state, blurred texture ends in PIXEL_SHADER_RESOURCE
    bool Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InBackBuffer, UINT InBackBufferIndex);

    ImTextureID TextureId() const { return (ImTextureID) _imguiSrvGpu.ptr; }
    ImVec2 UVScale() const { return _uvScale; }

    MenuBlur_Dx12(std::string InName, ID3D12Device* InDevice, DescriptorHeapAllocator* InImGuiHeapAlloc);

    ~MenuBlur_Dx12();
};
