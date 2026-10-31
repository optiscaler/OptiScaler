#pragma once
#include "IFeature_Dx11.h"

#include <menu/menu_dx11.h>

#include <shaders/rcas/RCAS_Dx12.h>
#include <shaders/bias/Bias_Dx12.h>
#include <shaders/output_scaling/OS_Dx12.h>
#include <shaders/depth_transfer/DT_Dx11.h>

#include <d3d12.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>

class IFeature_Dx11wDx12 : public virtual IFeature_Dx11
{
  protected:
    // Dx11w12 part
    using D3D11_TEXTURE2D_DESC_C = struct D3D11_TEXTURE2D_DESC_C
    {
        UINT Width = 0;
        UINT Height = 0;
        DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
        UINT BindFlags = 0;
        UINT MiscFlags = 0;
    };

    using D3D11_TEXTURE2D_RESOURCE_C = struct D3D11_TEXTURE2D_RESOURCE_C
    {
        D3D11_TEXTURE2D_DESC_C Desc = {};
        ID3D11Texture2D* SourceTexture = nullptr;
        ID3D11Texture2D* SharedTexture = nullptr;
        ID3D12Resource* Dx12Resource = nullptr;
        HANDLE Dx11Handle = NULL;
        HANDLE Dx12Handle = NULL;
        bool NtHandle = false;

        // NT handles have to be closed, legacy shared handles can't be. A new handle can get the same value
        void CloseNtHandle()
        {
            if (!NtHandle)
                return;

            if (Dx11Handle != NULL)
                CloseHandle(Dx11Handle);

            Dx11Handle = NULL;
            Dx12Handle = NULL;
            NtHandle = false;
        }
    };

    // D3D11
    ID3D11Device5* Dx11Device = nullptr;
    ID3D11DeviceContext4* Dx11DeviceContext = nullptr;

    D3D12_COMMAND_LIST_TYPE Dx12CommandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ID3D12CommandQueue* Dx12CommandQueue = nullptr;
    ID3D12CommandAllocator* Dx12CommandAllocator[2] = { nullptr, nullptr };
    ID3D12GraphicsCommandList* Dx12CommandList[2] = { nullptr, nullptr };
    ID3D12Fence* Dx12Fence = nullptr;
    HANDLE Dx12FenceEvent = nullptr;

    D3D11_TEXTURE2D_RESOURCE_C dx11Color = {};
    D3D11_TEXTURE2D_RESOURCE_C dx11Mv = {};
    D3D11_TEXTURE2D_RESOURCE_C dx11Depth = {};
    D3D11_TEXTURE2D_RESOURCE_C dx11Reactive = {};
    D3D11_TEXTURE2D_RESOURCE_C dx11Exp = {};
    D3D11_TEXTURE2D_RESOURCE_C dx11Out = {};

    ID3D11Resource* paramOutput[2] = { nullptr, nullptr };

    ID3D11Fence* dx11FenceTextureCopy = nullptr;
    ID3D12Fence* dx12FenceTextureCopy = nullptr;
    HANDLE dx11SHForTextureCopy = nullptr;
    ULONG _fenceValue = 1;

    std::unique_ptr<OS_Dx12> OutputScaler = nullptr;
    std::unique_ptr<RCAS_Dx12> RCAS = nullptr;
    std::unique_ptr<Bias_Dx12> Bias = nullptr;
    std::unique_ptr<DepthTransfer_Dx11> DT = nullptr;

    HRESULT CreateDx12Device(D3D_FEATURE_LEVEL InFeatureLevel);
    void GetHardwareAdapter(IDXGIFactory1* InFactory, IDXGIAdapter** InAdapter, D3D_FEATURE_LEVEL InFeatureLevel,
                            bool InRequestHighPerformanceAdapter);

    bool CopyTextureFrom11To12(ID3D11Resource* InResource, D3D11_TEXTURE2D_RESOURCE_C* OutResource, bool InCopy,
                               bool InDepth, bool InDontUseNTShared);
    bool ProcessDx11Textures(const NVSDK_NGX_Parameter* InParameters);
    bool CopyBackOutput();

    void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                         D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);

    void ReleaseSharedResources();
    void ReleaseSyncResources();

  public:
    virtual bool Init(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters) = 0;
    virtual bool Evaluate(ID3D11DeviceContext* DeviceContext, NVSDK_NGX_Parameter* InParameters) = 0;

    bool BaseInit(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters);

    IFeature_Dx11wDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);

    ~IFeature_Dx11wDx12();
};
