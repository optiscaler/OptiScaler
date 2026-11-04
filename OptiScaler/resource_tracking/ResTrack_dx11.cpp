#include "pch.h"
#include "ResTrack_dx11.h"

#include <Config.h>
#include <State.h>

#include <detours/detours.h>

#include <algorithm>
#include <array>

namespace
{
typedef void(STDMETHODCALLTYPE* PFN_SetShaderResources)(ID3D11DeviceContext* This, UINT StartSlot, UINT NumViews,
                                                        ID3D11ShaderResourceView* const* ppShaderResourceViews);
typedef void(STDMETHODCALLTYPE* PFN_CSSetUnorderedAccessViews)(ID3D11DeviceContext* This, UINT StartSlot, UINT NumUAVs,
                                                               ID3D11UnorderedAccessView* const* ppUnorderedAccessViews,
                                                               const UINT* pUAVInitialCounts);
typedef void(STDMETHODCALLTYPE* PFN_OMSetRenderTargets)(ID3D11DeviceContext* This, UINT NumViews,
                                                        ID3D11RenderTargetView* const* ppRenderTargetViews,
                                                        ID3D11DepthStencilView* pDepthStencilView);
typedef void(STDMETHODCALLTYPE* PFN_OMSetRenderTargetsAndUnorderedAccessViews)(
    ID3D11DeviceContext* This, UINT NumRTVs, ID3D11RenderTargetView* const* ppRenderTargetViews,
    ID3D11DepthStencilView* pDepthStencilView, UINT UAVStartSlot, UINT NumUAVs,
    ID3D11UnorderedAccessView* const* ppUnorderedAccessViews, const UINT* pUAVInitialCounts);

typedef void(STDMETHODCALLTYPE* PFN_Draw)(ID3D11DeviceContext* This, UINT VertexCount, UINT StartVertexLocation);
typedef void(STDMETHODCALLTYPE* PFN_DrawIndexed)(ID3D11DeviceContext* This, UINT IndexCount, UINT StartIndexLocation,
                                                 INT BaseVertexLocation);
typedef void(STDMETHODCALLTYPE* PFN_DrawInstanced)(ID3D11DeviceContext* This, UINT VertexCountPerInstance,
                                                   UINT InstanceCount, UINT StartVertexLocation,
                                                   UINT StartInstanceLocation);
typedef void(STDMETHODCALLTYPE* PFN_DrawIndexedInstanced)(ID3D11DeviceContext* This, UINT IndexCountPerInstance,
                                                          UINT InstanceCount, UINT StartIndexLocation,
                                                          INT BaseVertexLocation, UINT StartInstanceLocation);
typedef void(STDMETHODCALLTYPE* PFN_DrawAuto)(ID3D11DeviceContext* This);
typedef void(STDMETHODCALLTYPE* PFN_DrawIndexedInstancedIndirect)(ID3D11DeviceContext* This,
                                                                  ID3D11Buffer* pBufferForArgs,
                                                                  UINT AlignedByteOffsetForArgs);
typedef void(STDMETHODCALLTYPE* PFN_DrawInstancedIndirect)(ID3D11DeviceContext* This, ID3D11Buffer* pBufferForArgs,
                                                           UINT AlignedByteOffsetForArgs);
typedef void(STDMETHODCALLTYPE* PFN_Dispatch)(ID3D11DeviceContext* This, UINT ThreadGroupCountX, UINT ThreadGroupCountY,
                                              UINT ThreadGroupCountZ);
typedef void(STDMETHODCALLTYPE* PFN_DispatchIndirect)(ID3D11DeviceContext* This, ID3D11Buffer* pBufferForArgs,
                                                      UINT AlignedByteOffsetForArgs);

PFN_SetShaderResources o_PSSetShaderResources = nullptr;
PFN_SetShaderResources o_VSSetShaderResources = nullptr;
PFN_SetShaderResources o_GSSetShaderResources = nullptr;
PFN_SetShaderResources o_HSSetShaderResources = nullptr;
PFN_SetShaderResources o_DSSetShaderResources = nullptr;
PFN_SetShaderResources o_CSSetShaderResources = nullptr;
PFN_CSSetUnorderedAccessViews o_CSSetUnorderedAccessViews = nullptr;
PFN_OMSetRenderTargets o_OMSetRenderTargets = nullptr;
PFN_OMSetRenderTargetsAndUnorderedAccessViews o_OMSetRenderTargetsAndUnorderedAccessViews = nullptr;
PFN_Draw o_Draw = nullptr;
PFN_DrawIndexed o_DrawIndexed = nullptr;
PFN_DrawInstanced o_DrawInstanced = nullptr;
PFN_DrawIndexedInstanced o_DrawIndexedInstanced = nullptr;
PFN_DrawAuto o_DrawAuto = nullptr;
PFN_DrawIndexedInstancedIndirect o_DrawIndexedInstancedIndirect = nullptr;
PFN_DrawInstancedIndirect o_DrawInstancedIndirect = nullptr;
PFN_Dispatch o_Dispatch = nullptr;
PFN_DispatchIndirect o_DispatchIndirect = nullptr;

PVOID g_hookedOMSetRenderTargetsAddress = nullptr;

constexpr UINT BASELINE_PS_SRV_SLOTS = 16;

size_t StageIndex(Dx11ShaderStage stage) { return static_cast<size_t>(stage); }

void GetGraphicsShaderResource(ID3D11DeviceContext* context, Dx11ShaderStage stage, UINT slot,
                               ID3D11ShaderResourceView** view)
{
    if (context == nullptr || view == nullptr)
        return;

    switch (stage)
    {
    case Dx11ShaderStage::PS:
        context->PSGetShaderResources(slot, 1, view);
        break;
    case Dx11ShaderStage::VS:
        context->VSGetShaderResources(slot, 1, view);
        break;
    case Dx11ShaderStage::GS:
        context->GSGetShaderResources(slot, 1, view);
        break;
    case Dx11ShaderStage::HS:
        context->HSGetShaderResources(slot, 1, view);
        break;
    case Dx11ShaderStage::DS:
        context->DSGetShaderResources(slot, 1, view);
        break;
    default:
        break;
    }
}

void STDMETHODCALLTYPE hkPSSetShaderResources(ID3D11DeviceContext* This, UINT StartSlot, UINT NumViews,
                                              ID3D11ShaderResourceView* const* ppShaderResourceViews)
{
    o_PSSetShaderResources(This, StartSlot, NumViews, ppShaderResourceViews);
    ResTrack_Dx11::NoteGraphicsShaderResources(This, Dx11ShaderStage::PS, StartSlot, NumViews, ppShaderResourceViews);
}

void STDMETHODCALLTYPE hkVSSetShaderResources(ID3D11DeviceContext* This, UINT StartSlot, UINT NumViews,
                                              ID3D11ShaderResourceView* const* ppShaderResourceViews)
{
    o_VSSetShaderResources(This, StartSlot, NumViews, ppShaderResourceViews);
    ResTrack_Dx11::NoteGraphicsShaderResources(This, Dx11ShaderStage::VS, StartSlot, NumViews, ppShaderResourceViews);
}

void STDMETHODCALLTYPE hkGSSetShaderResources(ID3D11DeviceContext* This, UINT StartSlot, UINT NumViews,
                                              ID3D11ShaderResourceView* const* ppShaderResourceViews)
{
    o_GSSetShaderResources(This, StartSlot, NumViews, ppShaderResourceViews);
    ResTrack_Dx11::NoteGraphicsShaderResources(This, Dx11ShaderStage::GS, StartSlot, NumViews, ppShaderResourceViews);
}

void STDMETHODCALLTYPE hkHSSetShaderResources(ID3D11DeviceContext* This, UINT StartSlot, UINT NumViews,
                                              ID3D11ShaderResourceView* const* ppShaderResourceViews)
{
    o_HSSetShaderResources(This, StartSlot, NumViews, ppShaderResourceViews);
    ResTrack_Dx11::NoteGraphicsShaderResources(This, Dx11ShaderStage::HS, StartSlot, NumViews, ppShaderResourceViews);
}

void STDMETHODCALLTYPE hkDSSetShaderResources(ID3D11DeviceContext* This, UINT StartSlot, UINT NumViews,
                                              ID3D11ShaderResourceView* const* ppShaderResourceViews)
{
    o_DSSetShaderResources(This, StartSlot, NumViews, ppShaderResourceViews);
    ResTrack_Dx11::NoteGraphicsShaderResources(This, Dx11ShaderStage::DS, StartSlot, NumViews, ppShaderResourceViews);
}

void STDMETHODCALLTYPE hkCSSetShaderResources(ID3D11DeviceContext* This, UINT StartSlot, UINT NumViews,
                                              ID3D11ShaderResourceView* const* ppShaderResourceViews)
{
    o_CSSetShaderResources(This, StartSlot, NumViews, ppShaderResourceViews);
    ResTrack_Dx11::NoteCSShaderResources(This, StartSlot, NumViews, ppShaderResourceViews);
}

void STDMETHODCALLTYPE hkCSSetUnorderedAccessViews(ID3D11DeviceContext* This, UINT StartSlot, UINT NumUAVs,
                                                   ID3D11UnorderedAccessView* const* ppUnorderedAccessViews,
                                                   const UINT* pUAVInitialCounts)
{
    o_CSSetUnorderedAccessViews(This, StartSlot, NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);
    ResTrack_Dx11::NoteCSUnorderedAccessViews(This, StartSlot, NumUAVs, ppUnorderedAccessViews);
}

void STDMETHODCALLTYPE hkOMSetRenderTargets(ID3D11DeviceContext* This, UINT NumViews,
                                            ID3D11RenderTargetView* const* ppRenderTargetViews,
                                            ID3D11DepthStencilView* pDepthStencilView)
{
    o_OMSetRenderTargets(This, NumViews, ppRenderTargetViews, pDepthStencilView);
    ResTrack_Dx11::NoteOMRenderTargets(This, NumViews, ppRenderTargetViews);
}

void STDMETHODCALLTYPE hkOMSetRenderTargetsAndUnorderedAccessViews(
    ID3D11DeviceContext* This, UINT NumRTVs, ID3D11RenderTargetView* const* ppRenderTargetViews,
    ID3D11DepthStencilView* pDepthStencilView, UINT UAVStartSlot, UINT NumUAVs,
    ID3D11UnorderedAccessView* const* ppUnorderedAccessViews, const UINT* pUAVInitialCounts)
{
    o_OMSetRenderTargetsAndUnorderedAccessViews(This, NumRTVs, ppRenderTargetViews, pDepthStencilView, UAVStartSlot,
                                                NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);
    ResTrack_Dx11::NoteOMRenderTargetsAndUAVs(This, NumRTVs, ppRenderTargetViews, UAVStartSlot, NumUAVs,
                                              ppUnorderedAccessViews);
}

void STDMETHODCALLTYPE hkDraw(ID3D11DeviceContext* This, UINT VertexCount, UINT StartVertexLocation)
{
    o_Draw(This, VertexCount, StartVertexLocation);
    ResTrack_Dx11::ProcessCandidates(This, Dx11CaptureInfo::DrawInstanced);
}

void STDMETHODCALLTYPE hkDrawIndexed(ID3D11DeviceContext* This, UINT IndexCount, UINT StartIndexLocation,
                                     INT BaseVertexLocation)
{
    o_DrawIndexed(This, IndexCount, StartIndexLocation, BaseVertexLocation);
    ResTrack_Dx11::ProcessCandidates(This, Dx11CaptureInfo::DrawIndexedInstanced);
}

void STDMETHODCALLTYPE hkDrawInstanced(ID3D11DeviceContext* This, UINT VertexCountPerInstance, UINT InstanceCount,
                                       UINT StartVertexLocation, UINT StartInstanceLocation)
{
    o_DrawInstanced(This, VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
    ResTrack_Dx11::ProcessCandidates(This, Dx11CaptureInfo::DrawInstanced);
}

void STDMETHODCALLTYPE hkDrawIndexedInstanced(ID3D11DeviceContext* This, UINT IndexCountPerInstance, UINT InstanceCount,
                                              UINT StartIndexLocation, INT BaseVertexLocation,
                                              UINT StartInstanceLocation)
{
    o_DrawIndexedInstanced(This, IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation,
                           StartInstanceLocation);
    ResTrack_Dx11::ProcessCandidates(This, Dx11CaptureInfo::DrawIndexedInstanced);
}

void STDMETHODCALLTYPE hkDrawAuto(ID3D11DeviceContext* This)
{
    o_DrawAuto(This);
    ResTrack_Dx11::ProcessCandidates(This, Dx11CaptureInfo::DrawInstanced);
}

void STDMETHODCALLTYPE hkDrawIndexedInstancedIndirect(ID3D11DeviceContext* This, ID3D11Buffer* pBufferForArgs,
                                                      UINT AlignedByteOffsetForArgs)
{
    o_DrawIndexedInstancedIndirect(This, pBufferForArgs, AlignedByteOffsetForArgs);
    ResTrack_Dx11::ProcessCandidates(This, Dx11CaptureInfo::DrawIndexedInstanced);
}

void STDMETHODCALLTYPE hkDrawInstancedIndirect(ID3D11DeviceContext* This, ID3D11Buffer* pBufferForArgs,
                                               UINT AlignedByteOffsetForArgs)
{
    o_DrawInstancedIndirect(This, pBufferForArgs, AlignedByteOffsetForArgs);
    ResTrack_Dx11::ProcessCandidates(This, Dx11CaptureInfo::DrawInstanced);
}

void STDMETHODCALLTYPE hkDispatch(ID3D11DeviceContext* This, UINT ThreadGroupCountX, UINT ThreadGroupCountY,
                                  UINT ThreadGroupCountZ)
{
    o_Dispatch(This, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
    ResTrack_Dx11::ProcessCandidates(This, Dx11CaptureInfo::Dispatch);
}

void STDMETHODCALLTYPE hkDispatchIndirect(ID3D11DeviceContext* This, ID3D11Buffer* pBufferForArgs,
                                          UINT AlignedByteOffsetForArgs)
{
    o_DispatchIndirect(This, pBufferForArgs, AlignedByteOffsetForArgs);
    ResTrack_Dx11::ProcessCandidates(This, Dx11CaptureInfo::Dispatch);
}
} // namespace

bool ResTrack_Dx11::IsHudFixActive(ID3D11DeviceContext* context)
{
    if (context == nullptr || InternalCallActive())
        return false;

    if (context != _immediateContext)
    {
        auto& state = State::Instance();
        if (!_deferredContextWarning.load(std::memory_order_relaxed) &&
            Config::Instance()->FGHUDFix.value_or_default() &&
            state.swapchainInteropApi == SwapchainInteropApi::Dx11wDx12 &&
            context->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED && !_deferredContextWarning.exchange(true))
        {
            LOG_WARN("DX11 deferred command-list HUDless capture is not supported!");
        }

        return false;
    }

    return !Hudfix_Dx11::SkipHudlessChecks() && Hudfix_Dx11::IsResourceCheckActive();
}

void ResTrack_Dx11::RememberSlots(std::vector<UINT>& slots, UINT startSlot, UINT count, UINT maxSlots)
{
    if (startSlot >= maxSlots || count == 0)
        return;

    const UINT safeCount = std::min(count, maxSlots - startSlot);
    const UINT endSlot = startSlot + safeCount;
    for (UINT slot = startSlot; slot < endSlot; ++slot)
    {
        if (std::find(slots.begin(), slots.end(), slot) == slots.end())
            slots.push_back(slot);
    }
}

bool ResTrack_Dx11::CheckView(ID3D11DeviceContext* context, ID3D11View* view, Dx11ResourceType type, UINT captureInfo)
{
    if (!IsHudFixActive(context) || view == nullptr)
        return false;

    auto& config = *Config::Instance();

    if ((type == Dx11ResourceType::RTV && config.FGHudfixDisableRTV.value_or_default()) ||
        (type == Dx11ResourceType::SRV && config.FGHudfixDisableSRV.value_or_default()) ||
        (type == Dx11ResourceType::UAV && config.FGHudfixDisableUAV.value_or_default()))
    {
        return false;
    }

    ID3D11Resource* resource = nullptr;
    view->GetResource(&resource);

    if (resource == nullptr)
        return false;

    ID3D11Texture2D* texture = nullptr;
    const auto qiResult = resource->QueryInterface(IID_PPV_ARGS(&texture));
    resource->Release();

    if (FAILED(qiResult) || texture == nullptr)
        return false;

    Dx11ResourceInfo info = {};
    const bool filled = Hudfix_Dx11::FillResourceInfo(texture, type, captureInfo, &info);
    const bool captured = filled && Hudfix_Dx11::CheckForHudless(context, &info);

    texture->Release();
    return captured;
}

void ResTrack_Dx11::NoteGraphicsShaderResources(ID3D11DeviceContext* context, Dx11ShaderStage stage, UINT startSlot,
                                                UINT numViews, ID3D11ShaderResourceView* const* views)
{
    if (!IsHudFixActive(context) || Config::Instance()->FGHudfixDisableSGR.value_or_default() ||
        Config::Instance()->FGHudfixDisableSRV.value_or_default())
        return;

    const auto stageIndex = StageIndex(stage);
    if (stageIndex >= _graphicsSrvSlots.size())
        return;

    {
        std::lock_guard<std::mutex> lock(_stateMutex);
        RememberSlots(_graphicsSrvSlots[stageIndex], startSlot, numViews, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
    }

    if (!Config::Instance()->FGImmediateCapture.value_or_default() || views == nullptr)
        return;

    const UINT safeCount = startSlot < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT
                               ? std::min(numViews, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - startSlot)
                               : 0;

    for (UINT i = 0; i < safeCount; ++i)
    {
        if (CheckView(context, views[i], Dx11ResourceType::SRV, Dx11CaptureInfo::SetGR))
            break;
    }
}

void ResTrack_Dx11::NoteCSShaderResources(ID3D11DeviceContext* context, UINT startSlot, UINT numViews,
                                          ID3D11ShaderResourceView* const* views)
{
    if (!IsHudFixActive(context) || Config::Instance()->FGHudfixDisableSCR.value_or_default() ||
        Config::Instance()->FGHudfixDisableSRV.value_or_default())
        return;

    {
        std::lock_guard<std::mutex> lock(_stateMutex);
        RememberSlots(_csSrvSlots, startSlot, numViews, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
    }

    if (!Config::Instance()->FGImmediateCapture.value_or_default() || views == nullptr)
        return;

    const UINT safeCount = startSlot < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT
                               ? std::min(numViews, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - startSlot)
                               : 0;

    for (UINT i = 0; i < safeCount; ++i)
    {
        if (CheckView(context, views[i], Dx11ResourceType::SRV, Dx11CaptureInfo::SetCR))
            break;
    }
}

void ResTrack_Dx11::NoteCSUnorderedAccessViews(ID3D11DeviceContext* context, UINT startSlot, UINT numViews,
                                               ID3D11UnorderedAccessView* const* views)
{
    if (!IsHudFixActive(context) || Config::Instance()->FGHudfixDisableSCR.value_or_default() ||
        Config::Instance()->FGHudfixDisableUAV.value_or_default() ||
        !Config::Instance()->FGImmediateCapture.value_or_default() || views == nullptr)
        return;

    const UINT safeCount = startSlot < _uavSlotCount ? std::min(numViews, _uavSlotCount - startSlot) : 0;

    for (UINT i = 0; i < safeCount; ++i)
    {
        if (CheckView(context, views[i], Dx11ResourceType::UAV, Dx11CaptureInfo::SetCR))
            break;
    }
}

void ResTrack_Dx11::NoteOMRenderTargets(ID3D11DeviceContext* context, UINT numViews,
                                        ID3D11RenderTargetView* const* views)
{
    if (context == _immediateContext && !InternalCallActive())
        _omRtvCount.store(std::min<UINT>(numViews, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT), std::memory_order_relaxed);

    if (!IsHudFixActive(context) || Config::Instance()->FGHudfixDisableOM.value_or_default() ||
        Config::Instance()->FGHudfixDisableRTV.value_or_default() ||
        !Config::Instance()->FGImmediateCapture.value_or_default() || views == nullptr)
        return;

    const UINT safeCount = std::min<UINT>(numViews, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT);

    for (UINT i = 0; i < safeCount; ++i)
    {
        if (CheckView(context, views[i], Dx11ResourceType::RTV, Dx11CaptureInfo::OMSetRTV))
            break;
    }
}

void ResTrack_Dx11::NoteOMRenderTargetsAndUAVs(ID3D11DeviceContext* context, UINT numRTVs,
                                               ID3D11RenderTargetView* const* rtvs, UINT uavStartSlot, UINT numUAVs,
                                               ID3D11UnorderedAccessView* const* uavs)
{
    if (context == _immediateContext && !InternalCallActive() && numRTVs != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)
        _omRtvCount.store(std::min<UINT>(numRTVs, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT), std::memory_order_relaxed);

    if (!IsHudFixActive(context) || Config::Instance()->FGHudfixDisableOM.value_or_default() ||
        !Config::Instance()->FGImmediateCapture.value_or_default())
        return;

    if (!Config::Instance()->FGHudfixDisableRTV.value_or_default() && rtvs != nullptr &&
        numRTVs != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)
    {
        const UINT safeRtvCount = std::min<UINT>(numRTVs, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT);
        for (UINT i = 0; i < safeRtvCount; ++i)
        {
            if (CheckView(context, rtvs[i], Dx11ResourceType::RTV, Dx11CaptureInfo::OMSetRTV))
                return;
        }
    }

    if (!Config::Instance()->FGHudfixDisableUAV.value_or_default() && uavs != nullptr &&
        numUAVs != D3D11_KEEP_UNORDERED_ACCESS_VIEWS)
    {
        const UINT safeUavCount = uavStartSlot < _uavSlotCount ? std::min(numUAVs, _uavSlotCount - uavStartSlot) : 0;

        for (UINT i = 0; i < safeUavCount; ++i)
        {
            if (CheckView(context, uavs[i], Dx11ResourceType::UAV, Dx11CaptureInfo::SetOMUAV))
                return;
        }
    }
}

void ResTrack_Dx11::ProcessCandidates(ID3D11DeviceContext* context, UINT dispatchInfo)
{
    if (!IsHudFixActive(context))
        return;

    auto& config = *Config::Instance();

    if ((dispatchInfo == Dx11CaptureInfo::Dispatch && config.FGHudfixDisableDispatch.value_or_default()) ||
        (dispatchInfo == Dx11CaptureInfo::DrawInstanced && config.FGHudfixDisableDI.value_or_default()) ||
        (dispatchInfo == Dx11CaptureInfo::DrawIndexedInstanced && config.FGHudfixDisableDII.value_or_default()))
    {
        return;
    }

    if (dispatchInfo == Dx11CaptureInfo::Dispatch)
    {
        if (!config.FGHudfixDisableSCR.value_or_default() && !config.FGHudfixDisableSRV.value_or_default())
        {
            std::array<bool, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> srvSlots = {};
            const UINT baselineCount =
                std::min<UINT>(BASELINE_PS_SRV_SLOTS, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);

            for (UINT i = 0; i < baselineCount; ++i)
                srvSlots[i] = true;

            {
                std::lock_guard<std::mutex> lock(_stateMutex);
                for (const UINT slot : _csSrvSlots)
                {
                    if (slot < srvSlots.size())
                        srvSlots[slot] = true;
                }
            }

            for (UINT slot = 0; slot < srvSlots.size(); ++slot)
            {
                if (!srvSlots[slot])
                    continue;

                ID3D11ShaderResourceView* view = nullptr;
                context->CSGetShaderResources(slot, 1, &view);

                if (view != nullptr)
                {
                    const bool captured =
                        CheckView(context, view, Dx11ResourceType::SRV, Dx11CaptureInfo::SetCR | dispatchInfo);

                    view->Release();

                    if (captured)
                        return;
                }
            }
        }

        if (!config.FGHudfixDisableSCR.value_or_default() && !config.FGHudfixDisableUAV.value_or_default())
        {
            for (UINT slot = 0; slot < _uavSlotCount; ++slot)
            {
                ID3D11UnorderedAccessView* view = nullptr;
                context->CSGetUnorderedAccessViews(slot, 1, &view);

                if (view != nullptr)
                {
                    const bool captured =
                        CheckView(context, view, Dx11ResourceType::UAV, Dx11CaptureInfo::SetCR | dispatchInfo);

                    view->Release();

                    if (captured)
                        return;
                }
            }
        }

        return;
    }

    if (!config.FGHudfixDisableSGR.value_or_default() && !config.FGHudfixDisableSRV.value_or_default())
    {
        std::array<std::vector<UINT>, static_cast<size_t>(Dx11ShaderStage::Count)> remembered;

        {
            std::lock_guard<std::mutex> lock(_stateMutex);
            remembered = _graphicsSrvSlots;
        }

        const std::array<Dx11ShaderStage, 5> stages = { Dx11ShaderStage::PS, Dx11ShaderStage::VS, Dx11ShaderStage::GS,
                                                        Dx11ShaderStage::HS, Dx11ShaderStage::DS };

        for (const auto stage : stages)
        {
            std::array<bool, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> srvSlots = {};

            if (stage == Dx11ShaderStage::PS)
            {
                const UINT baselineCount =
                    std::min<UINT>(BASELINE_PS_SRV_SLOTS, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
                for (UINT i = 0; i < baselineCount; ++i)
                    srvSlots[i] = true;
            }

            for (const UINT slot : remembered[StageIndex(stage)])
            {
                if (slot < srvSlots.size())
                    srvSlots[slot] = true;
            }

            for (UINT slot = 0; slot < srvSlots.size(); ++slot)
            {
                if (!srvSlots[slot])
                    continue;

                ID3D11ShaderResourceView* view = nullptr;
                GetGraphicsShaderResource(context, stage, slot, &view);

                if (view != nullptr)
                {
                    const bool captured =
                        CheckView(context, view, Dx11ResourceType::SRV, Dx11CaptureInfo::SetGR | dispatchInfo);
                    view->Release();
                    if (captured)
                        return;
                }
            }
        }
    }

    if (config.FGHudfixDisableOM.value_or_default())
        return;

    // try OM UAVs, then RTVs
    const UINT omRtvCount = _omRtvCount.load(std::memory_order_relaxed);

    if (!config.FGHudfixDisableUAV.value_or_default() && omRtvCount < _uavSlotCount)
    {
        const UINT numUavs = _uavSlotCount - omRtvCount;
        ID3D11UnorderedAccessView* uavs[D3D11_1_UAV_SLOT_COUNT] = {};

        context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, omRtvCount, numUavs, uavs);

        for (UINT i = 0; i < numUavs; ++i)
        {
            auto* view = uavs[i];

            if (view == nullptr)
                continue;

            const bool captured =
                CheckView(context, view, Dx11ResourceType::UAV, Dx11CaptureInfo::SetOMUAV | dispatchInfo);
            view->Release();
            uavs[i] = nullptr;

            if (captured)
            {
                for (UINT j = i + 1; j < numUavs; ++j)
                {
                    if (uavs[j] != nullptr)
                    {
                        uavs[j]->Release();
                        uavs[j] = nullptr;
                    }
                }

                return;
            }
        }
    }

    if (config.FGHudfixDisableRTV.value_or_default())
        return;

    ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, nullptr);

    for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        auto* view = rtvs[i];

        if (view == nullptr)
            continue;

        const bool captured = CheckView(context, view, Dx11ResourceType::RTV, Dx11CaptureInfo::OMSetRTV | dispatchInfo);
        view->Release();
        rtvs[i] = nullptr;

        if (captured)
        {
            for (UINT j = i + 1; j < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++j)
            {
                if (rtvs[j] != nullptr)
                {
                    rtvs[j]->Release();
                    rtvs[j] = nullptr;
                }
            }

            return;
        }
    }
}

void ResTrack_Dx11::HookDevice(ID3D11Device* device)
{
    if (device == nullptr)
        return;

    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);

    if (context == nullptr)
        return;

    std::lock_guard<std::mutex> hookLock(_hookMutex);

    if (_device != nullptr && _device != device)
    {
        ClearPossibleHudless();
        Hudfix_Dx11::ReleaseResources();
        Hudfix_Dx11::ResetCounters();
        _deferredContextWarning.store(false);
    }

    _device = device;
    _immediateContext = context;
    _uavSlotCount =
        device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1 ? D3D11_1_UAV_SLOT_COUNT : D3D11_PS_CS_UAV_REGISTER_COUNT;

    PVOID* vtable = *(PVOID**) context;

    if (o_OMSetRenderTargets != nullptr)
    {
        if (g_hookedOMSetRenderTargetsAddress != nullptr && g_hookedOMSetRenderTargetsAddress != vtable[33])
        {
            LOG_WARN("DX11 Hudfix encountered a different ID3D11DeviceContext vtable while hooks are already active; "
                     "the new context implementation will not be hooked.");
        }

        context->Release();
        return;
    }

    for (auto& slots : _graphicsSrvSlots)
        slots.reserve(32);
    _csSrvSlots.reserve(32);

    // ID3D11DeviceContext
    o_PSSetShaderResources = (PFN_SetShaderResources) vtable[8];
    o_DrawIndexed = (PFN_DrawIndexed) vtable[12];
    o_Draw = (PFN_Draw) vtable[13];
    o_DrawIndexedInstanced = (PFN_DrawIndexedInstanced) vtable[20];
    o_DrawInstanced = (PFN_DrawInstanced) vtable[21];
    o_VSSetShaderResources = (PFN_SetShaderResources) vtable[25];
    o_GSSetShaderResources = (PFN_SetShaderResources) vtable[31];
    o_OMSetRenderTargets = (PFN_OMSetRenderTargets) vtable[33];
    o_OMSetRenderTargetsAndUnorderedAccessViews = (PFN_OMSetRenderTargetsAndUnorderedAccessViews) vtable[34];
    o_DrawAuto = (PFN_DrawAuto) vtable[38];
    o_DrawIndexedInstancedIndirect = (PFN_DrawIndexedInstancedIndirect) vtable[39];
    o_DrawInstancedIndirect = (PFN_DrawInstancedIndirect) vtable[40];
    o_Dispatch = (PFN_Dispatch) vtable[41];
    o_DispatchIndirect = (PFN_DispatchIndirect) vtable[42];
    o_HSSetShaderResources = (PFN_SetShaderResources) vtable[59];
    o_DSSetShaderResources = (PFN_SetShaderResources) vtable[63];
    o_CSSetShaderResources = (PFN_SetShaderResources) vtable[67];
    o_CSSetUnorderedAccessViews = (PFN_CSSetUnorderedAccessViews) vtable[68];

    g_hookedOMSetRenderTargetsAddress = vtable[33];

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    DetourAttach(&(PVOID&) o_PSSetShaderResources, hkPSSetShaderResources);
    DetourAttach(&(PVOID&) o_VSSetShaderResources, hkVSSetShaderResources);
    DetourAttach(&(PVOID&) o_GSSetShaderResources, hkGSSetShaderResources);
    DetourAttach(&(PVOID&) o_HSSetShaderResources, hkHSSetShaderResources);
    DetourAttach(&(PVOID&) o_DSSetShaderResources, hkDSSetShaderResources);
    DetourAttach(&(PVOID&) o_CSSetShaderResources, hkCSSetShaderResources);
    DetourAttach(&(PVOID&) o_CSSetUnorderedAccessViews, hkCSSetUnorderedAccessViews);
    DetourAttach(&(PVOID&) o_OMSetRenderTargets, hkOMSetRenderTargets);
    DetourAttach(&(PVOID&) o_OMSetRenderTargetsAndUnorderedAccessViews, hkOMSetRenderTargetsAndUnorderedAccessViews);
    DetourAttach(&(PVOID&) o_Draw, hkDraw);
    DetourAttach(&(PVOID&) o_DrawIndexed, hkDrawIndexed);
    DetourAttach(&(PVOID&) o_DrawInstanced, hkDrawInstanced);
    DetourAttach(&(PVOID&) o_DrawIndexedInstanced, hkDrawIndexedInstanced);
    DetourAttach(&(PVOID&) o_DrawAuto, hkDrawAuto);
    DetourAttach(&(PVOID&) o_DrawIndexedInstancedIndirect, hkDrawIndexedInstancedIndirect);
    DetourAttach(&(PVOID&) o_DrawInstancedIndirect, hkDrawInstancedIndirect);
    DetourAttach(&(PVOID&) o_Dispatch, hkDispatch);
    DetourAttach(&(PVOID&) o_DispatchIndirect, hkDispatchIndirect);

    const auto result = DetourTransactionCommit();
    if (result != NO_ERROR)
    {
        LOG_ERROR("Failed to hook ID3D11DeviceContext for Hudfix: {:X}", result);

        o_PSSetShaderResources = nullptr;
        o_VSSetShaderResources = nullptr;
        o_GSSetShaderResources = nullptr;
        o_HSSetShaderResources = nullptr;
        o_DSSetShaderResources = nullptr;
        o_CSSetShaderResources = nullptr;
        o_CSSetUnorderedAccessViews = nullptr;
        o_OMSetRenderTargets = nullptr;
        o_OMSetRenderTargetsAndUnorderedAccessViews = nullptr;
        o_Draw = nullptr;
        o_DrawIndexed = nullptr;
        o_DrawInstanced = nullptr;
        o_DrawIndexedInstanced = nullptr;
        o_DrawAuto = nullptr;
        o_DrawIndexedInstancedIndirect = nullptr;
        o_DrawInstancedIndirect = nullptr;
        o_Dispatch = nullptr;
        o_DispatchIndirect = nullptr;
        g_hookedOMSetRenderTargetsAddress = nullptr;
    }
    else
    {
        LOG_INFO("DX11 Hudfix resource tracking enabled for context {:X}", (size_t) context);
    }

    context->Release();
}

void ResTrack_Dx11::OnDeviceReleased(ID3D11Device* device)
{
    if (device == nullptr || device != _device)
        return;

    ClearPossibleHudless();
    Hudfix_Dx11::ReleaseResources();
    Hudfix_Dx11::ResetCounters();

    {
        std::lock_guard<std::mutex> lock(_stateMutex);
        _omRtvCount = 0;
        _uavSlotCount = D3D11_PS_CS_UAV_REGISTER_COUNT;
    }

    _deferredContextWarning.store(false);
    _immediateContext = nullptr;
    _device = nullptr;
}

void ResTrack_Dx11::ReleaseHooks()
{
    std::lock_guard<std::mutex> hookLock(_hookMutex);

    if (o_OMSetRenderTargets != nullptr)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_PSSetShaderResources != nullptr)
            DetourDetach(&(PVOID&) o_PSSetShaderResources, hkPSSetShaderResources);

        if (o_VSSetShaderResources != nullptr)
            DetourDetach(&(PVOID&) o_VSSetShaderResources, hkVSSetShaderResources);

        if (o_GSSetShaderResources != nullptr)
            DetourDetach(&(PVOID&) o_GSSetShaderResources, hkGSSetShaderResources);

        if (o_HSSetShaderResources != nullptr)
            DetourDetach(&(PVOID&) o_HSSetShaderResources, hkHSSetShaderResources);

        if (o_DSSetShaderResources != nullptr)
            DetourDetach(&(PVOID&) o_DSSetShaderResources, hkDSSetShaderResources);

        if (o_CSSetShaderResources != nullptr)
            DetourDetach(&(PVOID&) o_CSSetShaderResources, hkCSSetShaderResources);

        if (o_CSSetUnorderedAccessViews != nullptr)
            DetourDetach(&(PVOID&) o_CSSetUnorderedAccessViews, hkCSSetUnorderedAccessViews);

        if (o_OMSetRenderTargets != nullptr)
            DetourDetach(&(PVOID&) o_OMSetRenderTargets, hkOMSetRenderTargets);

        if (o_OMSetRenderTargetsAndUnorderedAccessViews != nullptr)
            DetourDetach(&(PVOID&) o_OMSetRenderTargetsAndUnorderedAccessViews,
                         hkOMSetRenderTargetsAndUnorderedAccessViews);

        if (o_Draw != nullptr)
            DetourDetach(&(PVOID&) o_Draw, hkDraw);

        if (o_DrawIndexed != nullptr)
            DetourDetach(&(PVOID&) o_DrawIndexed, hkDrawIndexed);

        if (o_DrawInstanced != nullptr)
            DetourDetach(&(PVOID&) o_DrawInstanced, hkDrawInstanced);

        if (o_DrawIndexedInstanced != nullptr)
            DetourDetach(&(PVOID&) o_DrawIndexedInstanced, hkDrawIndexedInstanced);

        if (o_DrawAuto != nullptr)
            DetourDetach(&(PVOID&) o_DrawAuto, hkDrawAuto);

        if (o_DrawIndexedInstancedIndirect != nullptr)
            DetourDetach(&(PVOID&) o_DrawIndexedInstancedIndirect, hkDrawIndexedInstancedIndirect);

        if (o_DrawInstancedIndirect != nullptr)
            DetourDetach(&(PVOID&) o_DrawInstancedIndirect, hkDrawInstancedIndirect);

        if (o_Dispatch != nullptr)
            DetourDetach(&(PVOID&) o_Dispatch, hkDispatch);

        if (o_DispatchIndirect != nullptr)
            DetourDetach(&(PVOID&) o_DispatchIndirect, hkDispatchIndirect);

        const auto result = DetourTransactionCommit();

        if (result != NO_ERROR)
            LOG_ERROR("Failed to unhook ID3D11DeviceContext Hudfix hooks: {:X}", result);
    }

    o_PSSetShaderResources = nullptr;
    o_VSSetShaderResources = nullptr;
    o_GSSetShaderResources = nullptr;
    o_HSSetShaderResources = nullptr;
    o_DSSetShaderResources = nullptr;
    o_CSSetShaderResources = nullptr;
    o_CSSetUnorderedAccessViews = nullptr;
    o_OMSetRenderTargets = nullptr;
    o_OMSetRenderTargetsAndUnorderedAccessViews = nullptr;
    o_Draw = nullptr;
    o_DrawIndexed = nullptr;
    o_DrawInstanced = nullptr;
    o_DrawIndexedInstanced = nullptr;
    o_DrawAuto = nullptr;
    o_DrawIndexedInstancedIndirect = nullptr;
    o_DrawInstancedIndirect = nullptr;
    o_Dispatch = nullptr;
    o_DispatchIndirect = nullptr;
    g_hookedOMSetRenderTargetsAddress = nullptr;

    ClearPossibleHudless();
    Hudfix_Dx11::ReleaseResources();

    {
        std::lock_guard<std::mutex> lock(_stateMutex);
        _omRtvCount = 0;
        _uavSlotCount = D3D11_PS_CS_UAV_REGISTER_COUNT;
    }

    _deferredContextWarning.store(false);
    _immediateContext = nullptr;
    _device = nullptr;
}

void ResTrack_Dx11::ClearPossibleHudless()
{
    std::lock_guard<std::mutex> lock(_stateMutex);

    for (auto& slots : _graphicsSrvSlots)
    {
        slots.clear();
    }

    _csSrvSlots.clear();
}
