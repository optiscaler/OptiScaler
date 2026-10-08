#include "pch.h"
#include "input_antilag2.h"
#include "input_common.h"
#include <proxies/FfxApi_Proxy.h>

HRESULT STDMETHODCALLTYPE AmdExtAntiLagApi::UpdateAntiLagState(VOID* pData)
{
    // AntiLag 2 never passes its device, without one the output can't start
    if (device == nullptr)
        device = State::Instance().currentD3D12Device;

    // That's the mark to insert a delay...
    if (!pData)
    {
        auto result = InputCommon::sleep(inputContext, device);

        if (result == InputResult::UsingDifferentInput)
            return S_OK;

        if (result != InputResult::Ok)
        {
            LOG_ERROR("sleep result: {}", magic_enum::enum_name(result));
            return E_FAIL;
        }

        MarkerParams markerParams {};
        markerParams.frame_id = 0;
        markerParams.marker_type = MarkerType::SIMULATION_START;

        result = InputCommon::set_marker(inputContext, device, markerParams);

        if (result != InputResult::Ok)
        {
            LOG_ERROR("set_marker result: {}", magic_enum::enum_name(result));
            return E_FAIL;
        }

        return S_OK;
    }

    auto ver1Struct = (AMD::AntiLag2DX12::APIData_v1*) pData;
    AMD::AntiLag2DX12::APIData_v2* ver2Struct = nullptr;

    const uint32_t structVersion = ver1Struct->uiVersion;

    if (structVersion < 1 || structVersion > 2)
    {
        LOG_ERROR("Unsupported struct version");
        return E_INVALIDARG;
    }

    if (structVersion == 1)
    {
        if (ver1Struct->uiSize != sizeof(AMD::AntiLag2DX12::APIData_v1))
        {
            LOG_ERROR("Wrong v1 struct size");
            return E_INVALIDARG;
        }

        // No validation
        AntiLag2eMode eMode = (AntiLag2eMode) ver1Struct->eMode;

        // sControlStr = ver1Struct->sControlStr;
        // uiControlStrLength = ver1Struct->uiControlStrLength;

        // TODO: not fully filled out
        SleepMode sleepMode {};
        sleepMode.low_latency_enabled = eMode == AntiLag2eMode::AntiLag2Mode_On;

        if (ver1Struct->maxFPS == 0)
            sleepMode.minimum_interval_us = 0;
        else
            sleepMode.minimum_interval_us = static_cast<uint32_t>(std::round(1'000'000 / ver1Struct->maxFPS));

        auto result = InputCommon::set_sleep_mode(inputContext, device, &sleepMode);

        if (result == InputResult::Ok || result == InputResult::UsingDifferentInput)
            return S_OK;
        else
            LOG_ERROR("set_sleep_mode result: {}", magic_enum::enum_name(result));

        return E_FAIL;
    }
    else if (structVersion == 2)
    {
        ver1Struct = nullptr;
        ver2Struct = (AMD::AntiLag2DX12::APIData_v2*) pData;

        if (ver2Struct->uiSize != sizeof(AMD::AntiLag2DX12::APIData_v2))
        {
            LOG_ERROR("Wrong v2 struct size");
            return E_INVALIDARG;
        }

        // TODO: This doesn't get unset if the game stops sending FG markers
        inputContext.markerMode = InputMarkerMode::SimStartAndPresentStart;
        const uint64_t frameId = ver2Struct->iiFrameIdx; // Usually not used

        if (ver2Struct->flags.signalEndOfFrameIdx == 1)
        {
            // MarkEndOfFrameRendering
            // TODO: fakenvapi calls it on PRESENT_START

            MarkerParams markerParams {};
            markerParams.frame_id = pseudoFrameId + 1; // This is wrong, could also just send 0
            markerParams.marker_type = MarkerType::PRESENT_START;

            auto result = InputCommon::set_marker(inputContext, device, markerParams);

            if (result == InputResult::Ok || result == InputResult::UsingDifferentInput)
                return S_OK;
            else
                LOG_ERROR("set_marker result: {}", magic_enum::enum_name(result));

            return E_FAIL;
        }
        else if (ver2Struct->flags.signalFgFrameType == 1)
        {
            // SetFrameGenFrameType
            // TODO: fakenvapi calls it on OUT_OF_BAND_PRESENT_START

            MarkerParams markerParams {};
            markerParams.marker_type = MarkerType::OUT_OF_BAND_PRESENT_START;
            markerParams.frame_id = pseudoFrameId;

            // The frame type is known, the pseudo frame ids are only for the other outputs
            const bool fakeFrame = ver2Struct->flags.isInterpolatedFrame == 1;
            markerParams.interpolated = fakeFrame;

            if (!fakeFrame)
                pseudoFrameId++;

            ID3D12CommandQueue* d3d12AppQueue = nullptr; // AntiLag 2 doesn't provide that

            auto result = InputCommon::set_async_marker(inputContext, d3d12AppQueue, markerParams);

            if (result == InputResult::Ok || result == InputResult::UsingDifferentInput)
                return S_OK;
            else
                LOG_ERROR("set_async_marker result: {}", magic_enum::enum_name(result));

            return E_FAIL;
        }

        return S_OK;
    }

    return E_INVALIDARG;
}

// Inform AntiLag 2 when present of interpolated frames starts
void InputAntiLag2::injectAl2Context(IDXGISwapChain* pSwapChain, bool fg_state)
{
    if (State::Instance().activeFgOutput != FGOutput::FSRFG)
        return;

    if (fg_state)
    {
        // Starting with FSR 3.1.1 we can provide an AntiLag 2 context to FSR FG
        // and it will call SetFrameGenFrameType for us
        auto static ffxApiVersion = FfxApiProxy::VersionDx12();
        constexpr feature_version requiredVersion = { 3, 1, 1 };
        if (ffxApiVersion >= requiredVersion)
        {
            if (!antiLag2DataForFG.context)
                antiLag2DataForFG.context = new AMD::AntiLag2DX12::Context();

            if (antiLag2DataForFG.context && !antiLag2DataForFG.context->m_pAntiLagAPI)
            {
                AmdExtAntiLagApi* context = new AmdExtAntiLagApi();
                context->inputContext.localContext = true;

                antiLag2DataForFG.context->m_pAntiLagAPI = context;
                antiLag2DataForFG.enabled = true; // Should be fine?
            }

            pSwapChain->SetPrivateData(IID_IFfxAntiLag2Data, sizeof(antiLag2DataForFG), &antiLag2DataForFG);
        }
        // No fallbacks for older versions
    }
    else
    {
        // Remove it or other FG mods won't work with AL2
        pSwapChain->SetPrivateData(IID_IFfxAntiLag2Data, 0, nullptr);
    }
}