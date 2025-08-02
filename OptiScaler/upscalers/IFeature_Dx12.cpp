#include "IFeature_Dx12.h"
#include <pch.h>

#include "State.h"

void IFeature_Dx12::ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                    D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState) const
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = InResource;
    barrier.Transition.StateBefore = InBeforeState;
    barrier.Transition.StateAfter = InAfterState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    InCommandList->ResourceBarrier(1, &barrier);
}

IFeature_Dx12::IFeature_Dx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters) {}

void IFeature_Dx12::Shutdown() {}

IFeature_Dx12::~IFeature_Dx12()
{
    if (State::Instance().isShuttingDown)
        return;

    if (Imgui != nullptr && Imgui.get() != nullptr)
        Imgui.reset();

    if (OutputScaler != nullptr && OutputScaler.get() != nullptr)
        OutputScaler.reset();

    if (RCAS != nullptr && RCAS.get() != nullptr)
        RCAS.reset();

    if (Bias != nullptr && Bias.get() != nullptr)
        Bias.reset();
}
