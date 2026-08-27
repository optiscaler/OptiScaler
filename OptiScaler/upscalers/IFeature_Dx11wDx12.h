#pragma once
#include "IFeature_Dx11.h"

#include "IFeature_Dx12.h"

#include <with_dx12/dx11_with_dx12.h>

#include <d3d12.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>

#define DX11WDX12_NUM_OF_BUFFERS 2

class IFeature_Dx11wDx12 : public virtual IFeature_Dx11
{
  private:
    template <typename F, typename Default> auto CallFeature(F&& f, Default&& def)
    {
        if (auto feature = dx12Feature.get(); feature)
            return f(feature);
        return def;
    }

  protected:
    std::unique_ptr<IFeature_Dx12> dx12Feature = nullptr;

    // D3D11
    ID3D11Device5* Dx11Device = nullptr;
    ID3D11DeviceContext4* Dx11DeviceContext = nullptr;

    D3D12_COMMAND_LIST_TYPE Dx12CommandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ID3D12CommandQueue* Dx12CommandQueue = nullptr;
    ID3D12CommandAllocator* Dx12CommandAllocator[DX11WDX12_NUM_OF_BUFFERS] {};
    ID3D12GraphicsCommandList* Dx12CommandList[DX11WDX12_NUM_OF_BUFFERS] {};
    ID3D12Fence* Dx12Fence = nullptr;
    HANDLE Dx12FenceEvent = nullptr;
    UINT64 Dx12FenceValue = 0;
    UINT64 Dx12CommandAllocatorFenceValue[DX11WDX12_NUM_OF_BUFFERS] = {};

    Dx11WithDx12::D3D11_TEXTURE2D_RESOURCE_C dx11Color = {};
    Dx11WithDx12::D3D11_TEXTURE2D_RESOURCE_C dx11Mv = {};
    Dx11WithDx12::D3D11_TEXTURE2D_RESOURCE_C dx11Depth = {};
    Dx11WithDx12::D3D11_TEXTURE2D_RESOURCE_C dx11Reactive = {};
    Dx11WithDx12::D3D11_TEXTURE2D_RESOURCE_C dx11Exp = {};
    Dx11WithDx12::D3D11_TEXTURE2D_RESOURCE_C dx11Out = {};

    ID3D11Resource* paramOutput[DX11WDX12_NUM_OF_BUFFERS] = {};

    bool CreateD3D12Objects();
    bool ProcessDx11Textures(const NVSDK_NGX_Parameter* InParameters);
    bool CopyBackOutput();

    void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                         D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);

    void ReleaseSharedResources();

    bool BaseInit(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters);

  public:
    // dx11 w/dx12 can't be wrapped but the IFeature_Dx11, needs its own Init and Evaluate
    bool InitInternal(ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters) final { return false; };
    bool EvaluateInternal(ID3D11DeviceContext* DeviceContext, NVSDK_NGX_Parameter* InParameters) final
    {
        return false;
    };

    bool Init(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters) final;
    bool Evaluate(ID3D11DeviceContext* DeviceContext, NVSDK_NGX_Parameter* InParameters) final;
    bool IsWithDx12() final { return true; }
    API Api() const override { return API::DX12; }

    std::optional<double> ReadUpscalerTime(void* deviceContextVoid) override
    {
        if (auto feature = dx12Feature.get(); feature && Dx12CommandQueue)
            return feature->ReadUpscalerTime(Dx12CommandQueue);

        return std::nullopt;
    };
    void ReadDetailedGpuTimes(void* deviceContextVoid, std::vector<DetailedGpuTime>& detailedGpuTimes) override
    {
        if (auto feature = dx12Feature.get(); feature && Dx12CommandQueue)
            return feature->ReadDetailedGpuTimes(Dx12CommandQueue, detailedGpuTimes);
    };

    feature_version Version() final
    {
        return CallFeature([](auto f) { return f->Version(); }, feature_version {});
    }

    size_t JitterCount() override
    {
        return CallFeature([](auto f) { return f->JitterCount(); }, size_t {});
    }

    void TickFrozenCheck() override
    {
        if (auto feature = dx12Feature.get(); feature)
            return feature->TickFrozenCheck();
    };

    bool IsFrozen() override
    {
        return CallFeature([](auto f) { return f->IsFrozen(); }, bool {});
    };
    bool UpdateOutputResolution(const NVSDK_NGX_Parameter* InParameters) override
    {
        return CallFeature([&](auto f) { return f->UpdateOutputResolution(InParameters); }, bool {});
    };
    uint32_t DisplayWidth() override
    {
        return CallFeature([](auto f) { return f->DisplayWidth(); }, uint32_t {});
    };
    uint32_t DisplayHeight() override
    {
        return CallFeature([](auto f) { return f->DisplayHeight(); }, uint32_t {});
    };
    uint32_t TargetWidth() override
    {
        return CallFeature([](auto f) { return f->TargetWidth(); }, uint32_t {});
    };
    uint32_t TargetHeight() override
    {
        return CallFeature([](auto f) { return f->TargetHeight(); }, uint32_t {});
    };
    uint32_t RenderWidth() override
    {
        return CallFeature([](auto f) { return f->RenderWidth(); }, uint32_t {});
    };
    uint32_t RenderHeight() override
    {
        return CallFeature([](auto f) { return f->RenderHeight(); }, uint32_t {});
    };
    NVSDK_NGX_PerfQuality_Value PerfQualityValue() override
    {
        return CallFeature([](auto f) { return f->PerfQualityValue(); }, NVSDK_NGX_PerfQuality_Value {});
    }
    bool IsInitParameters() override
    {
        return CallFeature([](auto f) { return f->IsInitParameters(); }, bool {});
    };
    bool IsInited() override
    {
        return CallFeature([](auto f) { return f->IsInited(); }, bool {});
    }
    float Sharpness() override
    {
        return CallFeature([](auto f) { return f->Sharpness(); }, float {});
    }
    bool HasColor() override
    {
        return CallFeature([](auto f) { return f->HasColor(); }, bool {});
    }
    bool HasDepth() override
    {
        return CallFeature([](auto f) { return f->HasDepth(); }, bool {});
    }
    bool HasMV() override
    {
        return CallFeature([](auto f) { return f->HasMV(); }, bool {});
    }
    bool HasTM() override
    {
        return CallFeature([](auto f) { return f->HasTM(); }, bool {});
    }
    bool AccessToReactiveMask() override
    {
        return CallFeature([](auto f) { return f->AccessToReactiveMask(); }, bool {});
    }
    bool HasExposure() override
    {
        return CallFeature([](auto f) { return f->HasExposure(); }, bool {});
    }
    bool HasOutput() override
    {
        return CallFeature([](auto f) { return f->HasOutput(); }, bool {});
    }
    bool ModuleLoaded() override
    {
        return CallFeature([](auto f) { return f->ModuleLoaded(); }, bool {});
    }
    long FrameCount() override
    {
        return CallFeature([](auto f) { return f->FrameCount(); }, long {});
    }
    bool DepthLinear() override
    {
        return CallFeature([](auto f) { return f->DepthLinear(); }, bool {});
    }
    bool AutoExposure() override
    {
        return CallFeature([](auto f) { return f->AutoExposure(); }, bool {});
    }
    bool DepthInverted() override
    {
        return CallFeature([](auto f) { return f->DepthInverted(); }, bool {});
    }
    bool IsHdr() override
    {
        return CallFeature([](auto f) { return f->IsHdr(); }, bool {});
    }
    bool JitteredMV() override
    {
        return CallFeature([](auto f) { return f->JitteredMV(); }, bool {});
    }
    bool LowResMV() override
    {
        return CallFeature([](auto f) { return f->LowResMV(); }, bool {});
    }
    bool SharpenEnabled() override
    {
        return CallFeature([](auto f) { return f->SharpenEnabled(); }, bool {});
    }

    IFeature_Dx11wDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);

    ~IFeature_Dx11wDx12();
};
