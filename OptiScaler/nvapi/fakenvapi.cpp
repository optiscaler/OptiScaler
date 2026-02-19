#include "pch.h"
#include "fakenvapi.h"
#include "proxies/FfxApi_Proxy.h"

void fakenvapi::Init(PFN_NvApi_QueryInterface& queryInterface)
{
    if (_inited)
        return;

    LOG_DEBUG("Trying to get fakenvapi-specific functions");

    Fake_InformFGState = static_cast<decltype(Fake_InformFGState)>(queryInterface(GET_ID(Fake_InformFGState)));
    Fake_InformPresentFG = static_cast<decltype(Fake_InformPresentFG)>(queryInterface(GET_ID(Fake_InformPresentFG)));
    Fake_GetAntiLagCtx = static_cast<decltype(Fake_GetAntiLagCtx)>(queryInterface(GET_ID(Fake_GetAntiLagCtx)));
    Fake_GetLowLatencyCtx = static_cast<decltype(Fake_GetLowLatencyCtx)>(queryInterface(GET_ID(Fake_GetLowLatencyCtx)));
    Fake_SetLowLatencyCtx = static_cast<decltype(Fake_SetLowLatencyCtx)>(queryInterface(GET_ID(Fake_SetLowLatencyCtx)));

    if (Fake_InformFGState != nullptr)
        LOG_DEBUG("Got InformFGState");

    if (Fake_InformPresentFG != nullptr)
        LOG_DEBUG("Got InformPresentFG");

    if (Fake_GetAntiLagCtx != nullptr)
        LOG_DEBUG("Got GetAntiLagCtx");

    if (Fake_GetLowLatencyCtx != nullptr)
        LOG_DEBUG("Got GetLowLatencyCtx");

    if (Fake_SetLowLatencyCtx != nullptr)
        LOG_DEBUG("Got SetLowLatencyCtx");

    _inited = Fake_InformFGState || Fake_InformPresentFG;

    if (_inited)
        LOG_INFO("fakenvapi initialized successfully");
    else
        LOG_INFO("Failed to initialize fakenvapi");
}

// Inform AntiLag 2 when present of interpolated frames starts
void fakenvapi::reportFGPresent(IDXGISwapChain* pSwapChain, bool fg_state, bool frame_interpolated)
{
    if (!isUsingFakenvapi())
        return;

    // Lets fakenvapi log and reset correctly
    Fake_InformFGState(fg_state);

    if (fg_state)
    {
        if (State::Instance().activeFgOutput == FGOutput::FSRFG)
        {
            // Starting with FSR 3.1.1 we can provide an AntiLag 2 context to FSR FG
            // and it will call SetFrameGenFrameType for us
            auto static ffxApiVersion = FfxApiProxy::VersionDx12();
            constexpr feature_version requiredVersion = { 3, 1, 1 };
            if (ffxApiVersion >= requiredVersion && updateModeAndContext())
            {
                antilag2_data.enabled = _lowLatencyContext != nullptr && _lowLatencyMode == Mode::AntiLag2;
                antilag2_data.context = antilag2_data.enabled ? _lowLatencyContext : nullptr;

                pSwapChain->SetPrivateData(IID_IFfxAntiLag2Data, sizeof(antilag2_data), &antilag2_data);
            }
            else
            {
                // Tell fakenvapi to call SetFrameGenFrameType by itself
                // Reflex frame id might get used in the future
                LOG_TRACE("Fake_InformPresentFG: {}", frame_interpolated);
                Fake_InformPresentFG(frame_interpolated, 0);
            }
        }
        else if (State::Instance().activeFgOutput == FGOutput::XeFG ||
                 State::Instance().activeFgOutput == FGOutput::DLSSG)
        {
            if (updateModeAndContext())
            {
                // Tell fakenvapi to call SetFrameGenFrameType by itself
                // Reflex frame id might get used in the future
                LOG_TRACE("Fake_InformPresentFG: {}", frame_interpolated);
                Fake_InformPresentFG(frame_interpolated, 0);
            }
        }
    }
    else
    {
        // Remove it or other FG mods won't work with AL2
        pSwapChain->SetPrivateData(IID_IFfxAntiLag2Data, 0, nullptr);
    }
}

bool fakenvapi::updateModeAndContext()
{
    if (!isUsingFakenvapi() &&
        (State::Instance().activeFgOutput == FGOutput::XeFG || State::Instance().activeFgOutput == FGOutput::DLSSG))
        auto loaded = fakenvapi::loadForNvidia();

    if (!isUsingFakenvapi() && !isUsingFakenvapiOnNvidia())
        return false;

    LOG_FUNC();

    if (Fake_GetLowLatencyCtx)
    {
        auto result = Fake_GetLowLatencyCtx(&_lowLatencyContext, &_lowLatencyMode);

        if (result != NVAPI_OK)
            LOG_ERROR("Can't get Low Latency context from fakenvapi");

        return result == NVAPI_OK;
    }

    // fallback for older fakenvapi builds
    if (Fake_GetAntiLagCtx)
    {
        auto result = Fake_GetAntiLagCtx(&_lowLatencyContext);

        if (result != NVAPI_OK)
            _lowLatencyMode = Mode::LatencyFlex;
        else
            _lowLatencyMode = Mode::AntiLag2;

        return result == NVAPI_OK;
    }

    _lowLatencyContext = nullptr;
    _lowLatencyMode = Mode::LatencyFlex;

    return false;
}

bool fakenvapi::setModeAndContext(void* context, Mode mode)
{
    if (!isUsingFakenvapi() &&
        (State::Instance().activeFgOutput == FGOutput::XeFG || State::Instance().activeFgOutput == FGOutput::DLSSG))
        auto loaded = fakenvapi::loadForNvidia();

    if (!isUsingFakenvapi() && !isUsingFakenvapiOnNvidia())
        return false;

    LOG_FUNC();

    if (Fake_SetLowLatencyCtx)
    {
        auto result = Fake_SetLowLatencyCtx(context, mode);

        if (result != NVAPI_OK)
            LOG_ERROR("Can't set Low Latency context from fakenvapi");

        return result == NVAPI_OK;
    }

    return false;
}

bool fakenvapi::loadForNvidia()
{
    if (!State::Instance().isRunningOnNvidia)
        return false;

    if (_dllForNvidia != nullptr)
        return true;

    _dllForNvidia = NtdllProxy::LoadLibraryExW_Ldr(L"fakenvapi.dll", NULL, 0);

    if (!_dllForNvidia)
        return false;

    auto queryInterface =
        (PFN_NvApi_QueryInterface) KernelBaseProxy::GetProcAddress_()(_dllForNvidia, "nvapi_QueryInterface");

    if (queryInterface == nullptr)
    {
        _dllForNvidia = nullptr;
        return false;
    }

    ForNvidia_SetSleepMode = GET_INTERFACE(NvAPI_D3D_SetSleepMode, queryInterface);
    ForNvidia_Sleep = GET_INTERFACE(NvAPI_D3D_Sleep, queryInterface);
    ForNvidia_GetLatency = GET_INTERFACE(NvAPI_D3D_GetLatency, queryInterface);
    ForNvidia_SetLatencyMarker = GET_INTERFACE(NvAPI_D3D_SetLatencyMarker, queryInterface);
    ForNvidia_SetAsyncFrameMarker = GET_INTERFACE(NvAPI_D3D12_SetAsyncFrameMarker, queryInterface);

    Fake_InformFGState = static_cast<decltype(Fake_InformFGState)>(queryInterface(GET_ID(Fake_InformFGState)));
    Fake_InformPresentFG = static_cast<decltype(Fake_InformPresentFG)>(queryInterface(GET_ID(Fake_InformPresentFG)));
    Fake_GetAntiLagCtx = static_cast<decltype(Fake_GetAntiLagCtx)>(queryInterface(GET_ID(Fake_GetAntiLagCtx)));
    Fake_GetLowLatencyCtx = static_cast<decltype(Fake_GetLowLatencyCtx)>(queryInterface(GET_ID(Fake_GetLowLatencyCtx)));
    Fake_SetLowLatencyCtx = static_cast<decltype(Fake_SetLowLatencyCtx)>(queryInterface(GET_ID(Fake_SetLowLatencyCtx)));

    if (Fake_SetLowLatencyCtx)
    {
        _initedForNvidia = true;
        LOG_INFO("fakenvapi initialized for Nvidia");
        return true;
    }

    LOG_INFO("Failed to initialize fakenvapi for Nvidia");
    return false;
}

// updateModeAndContext needs to be called before that
Mode fakenvapi::getCurrentMode() { return _lowLatencyMode; }

bool fakenvapi::isUsingFakenvapi() { return _inited; }

bool fakenvapi::isUsingFakenvapiOnNvidia() { return _initedForNvidia; }
