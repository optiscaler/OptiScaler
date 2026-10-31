#include <pch.h>
#include "DLSSFeature_Dx12.h"
#include <dxgi1_4.h>
#include <Config.h>
#include <imgui/ImGuiNotify.hpp>

PFN_D3D12_CreateFeature DLSSFeatureDx12::CreateFeature() const
{
    if (_custom != nullptr)
        return _custom->dx12Inited ? _custom->module.D3D12_CreateFeature : nullptr;

    return NVNGXProxy::D3D12_CreateFeature();
}

PFN_D3D12_EvaluateFeature DLSSFeatureDx12::EvaluateFeature() const
{
    if (_custom != nullptr)
        return _custom->dx12Inited ? _custom->module.D3D12_EvaluateFeature : nullptr;

    return NVNGXProxy::D3D12_EvaluateFeature();
}

PFN_D3D12_ReleaseFeature DLSSFeatureDx12::ReleaseFeature() const
{
    if (_custom != nullptr)
        return _custom->dx12Inited ? _custom->module.D3D12_ReleaseFeature : nullptr;

    return NVNGXProxy::D3D12_ReleaseFeature();
}

bool DLSSFeatureDx12::InitInternal(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters)
{
    if (IsInited())
        return true;

    return InitDLSS(InCommandList, InParameters);
}

bool DLSSFeatureDx12::InitDLSS(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters)
{
    if (_custom == nullptr && NVNGXProxy::NVNGXModule() == nullptr)
    {
        LOG_ERROR("nvngx.dll not loaded!");
        return false;
    }

    if (!_dlssInited)
    {
        _dlssInited = _custom != nullptr ? NVNGXProxy::InitCustomDx12(*_custom, Device) : NVNGXProxy::InitDx12(Device);

        if (!_dlssInited)
            return false;

        // LoadCustomDx12 already checked the functions of a custom runtime
        if (_custom == nullptr)
        {
            _moduleLoaded =
                (NVNGXProxy::D3D12_Init_ProjectID() != nullptr || NVNGXProxy::D3D12_Init_Ext() != nullptr) &&
                (NVNGXProxy::D3D12_Shutdown() != nullptr || NVNGXProxy::D3D12_Shutdown1() != nullptr) &&
                (NVNGXProxy::D3D12_GetParameters() != nullptr || NVNGXProxy::D3D12_AllocateParameters() != nullptr) &&
                NVNGXProxy::D3D12_DestroyParameters() != nullptr && NVNGXProxy::D3D12_CreateFeature() != nullptr &&
                NVNGXProxy::D3D12_ReleaseFeature() != nullptr && NVNGXProxy::D3D12_EvaluateFeature() != nullptr;
        }

        // delay between init and create feature
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    LOG_INFO("Creating DLSS feature");

    if (CreateFeature() != nullptr)
    {
        ProcessInitParams(InParameters);

        _p_dlssHandle = &_dlssHandle;

        NVSDK_NGX_Result nvResult;
        {
            ScopedSkipHeapCapture skipHeapCapture {};

            nvResult = CreateFeature()(InCommandList, NVSDK_NGX_Feature_SuperSampling, InParameters, &_p_dlssHandle);
        }

        if (nvResult != NVSDK_NGX_Result_Success)
        {
            LOG_ERROR("_CreateFeature result: {0:X}", (unsigned int) nvResult);
            return false;
        }
        else
        {
            LOG_INFO("_CreateFeature result: NVSDK_NGX_Result_Success");
        }
    }
    else
    {
        LOG_ERROR("_CreateFeature is nullptr");
        return false;
    }

    ReadVersion(_custom != nullptr ? _custom->module.filePath : L"");

    SetInit(true);
    return true;
}

bool DLSSFeatureDx12::EvaluateInternal(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters)
{
    if (!_moduleLoaded)
    {
        LOG_ERROR("nvngx.dll or _nvngx.dll is not loaded!");
        return false;
    }

    NVSDK_NGX_Result nvResult;

    if (EvaluateFeature() != nullptr)
    {
        ProcessEvaluateParams(InParameters);

        // HelixSR reads which upscaler API the game's inputs came from (0 FidelityFX, 1 NGX, 2 XeSS) and whether the
        // reactive mask is disabled
        if (_helixSRInput)
        {
            const auto api = State::Instance().currentInputApiName;
            unsigned int inputApi = 0;

            if (api == ApiUpscalerInput::DLSS_DX11 || api == ApiUpscalerInput::DLSS_DX12 ||
                api == ApiUpscalerInput::DLSS_VK)
                inputApi = 1;
            else if (api == ApiUpscalerInput::XeSS_DX11 || api == ApiUpscalerInput::XeSS_DX12 ||
                     api == ApiUpscalerInput::XeSS_VK)
                inputApi = 2;

            InParameters->Set("HelixSR.Upscale.Input.API", inputApi);
            InParameters->Set("HelixSR.Upscale.MaskOff",
                              Config::Instance()->DisableReactiveMask.value_or(false) ? 1u : 0u);
        }

        nvResult = EvaluateFeature()(InCommandList, _p_dlssHandle, InParameters, NULL);

        if (nvResult != NVSDK_NGX_Result_Success)
        {
            LOG_ERROR("_EvaluateFeature result: {0:X}", (unsigned int) nvResult);
            return false;
        }
    }
    else
    {
        LOG_ERROR("_EvaluateFeature is nullptr");
        return false;
    }

    _frameCount++;

    return true;
}

void DLSSFeatureDx12::Shutdown(ID3D12Device* InDevice)
{
    if (_dlssInited)
    {
        if (_custom != nullptr)
            NVNGXProxy::ShutdownCustomDx12(*_custom, InDevice);
        else if (NVNGXProxy::D3D12_Shutdown() != nullptr)
            NVNGXProxy::D3D12_Shutdown()();
        else if (NVNGXProxy::D3D12_Shutdown1() != nullptr)
            NVNGXProxy::D3D12_Shutdown1()(InDevice);
    }

    DLSSFeature::Shutdown();
}

DLSSFeatureDx12::DLSSFeatureDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters,
                                 const std::string& customUpscaler)
    : IFeature(InHandleId, InParameters), IFeature_Dx12(InHandleId, InParameters), DLSSFeature(InHandleId, InParameters)
{
    if (!customUpscaler.empty())
    {
        _custom = NVNGXProxy::LoadCustomDx12(customUpscaler);
        _moduleLoaded = _custom != nullptr;

        if (_custom != nullptr)
        {
            typedef unsigned int (*PFN_HelixSR_UpscaleInputVersion)();
            auto inputVersion = (PFN_HelixSR_UpscaleInputVersion) KernelBaseProxy::GetProcAddress_()(
                _custom->module.dll, "HelixSR_UpscaleInputVersion");

            _helixSRInput = inputVersion != nullptr && inputVersion() >= 1;
        }

        if (!_moduleLoaded)
            ImGui::InsertNotification(
                { ImGuiToastType::Error, 10000, "Can't load custom upscaler %s", customUpscaler.c_str() });

        return;
    }

    if (NVNGXProxy::NVNGXModule() == nullptr)
    {
        LOG_INFO("nvngx.dll not loaded, now loading");
        NVNGXProxy::InitNVNGX();
    }

    LOG_INFO("binding complete!");
}

DLSSFeatureDx12::~DLSSFeatureDx12()
{
    if (State::Instance().isShuttingDown)
        return;

    if (ReleaseFeature() != nullptr && _p_dlssHandle != nullptr)
        ReleaseFeature()(_p_dlssHandle);
}
