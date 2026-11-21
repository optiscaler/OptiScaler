#include <pch.h>
#include "DLSSFeature_Dx11.h"
#include <Config.h>
#include <imgui/ImGuiNotify.hpp>

#include <dxgi.h>

PFN_D3D11_CreateFeature DLSSFeatureDx11::CreateFeature() const
{
    if (_custom != nullptr)
        return _custom->dx11Inited ? _custom->module.D3D11_CreateFeature : nullptr;

    return NVNGXProxy::D3D11_CreateFeature();
}

PFN_D3D11_EvaluateFeature DLSSFeatureDx11::EvaluateFeature() const
{
    if (_custom != nullptr)
        return _custom->dx11Inited ? _custom->module.D3D11_EvaluateFeature : nullptr;

    return NVNGXProxy::D3D11_EvaluateFeature();
}

PFN_D3D11_ReleaseFeature DLSSFeatureDx11::ReleaseFeature() const
{
    if (_custom != nullptr)
        return _custom->dx11Inited ? _custom->module.D3D11_ReleaseFeature : nullptr;

    return NVNGXProxy::D3D11_ReleaseFeature();
}

bool DLSSFeatureDx11::InitInternal(ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters)
{
    if (_custom == nullptr && NVNGXProxy::NVNGXModule() == nullptr)
    {
        LOG_ERROR("nvngx.dll not loaded!");

        SetInit(false);
        return false;
    }

    NVSDK_NGX_Result nvResult;
    bool initResult = false;

    do
    {
        if (!_dlssInited)
        {
            _dlssInited =
                _custom != nullptr ? NVNGXProxy::InitCustomDx11(*_custom, Device) : NVNGXProxy::InitDx11(Device);

            if (!_dlssInited)
                return false;

            // LoadCustom already checked the functions of a custom runtime
            if (_custom == nullptr)
            {
                _moduleLoaded =
                    (NVNGXProxy::D3D11_Init_ProjectID() != nullptr || NVNGXProxy::D3D11_Init_Ext() != nullptr) &&
                    (NVNGXProxy::D3D11_Shutdown() != nullptr || NVNGXProxy::D3D11_Shutdown1() != nullptr) &&
                    (NVNGXProxy::D3D11_GetParameters() != nullptr ||
                     NVNGXProxy::D3D11_AllocateParameters() != nullptr) &&
                    NVNGXProxy::D3D11_DestroyParameters() != nullptr && NVNGXProxy::D3D11_CreateFeature() != nullptr &&
                    NVNGXProxy::D3D11_ReleaseFeature() != nullptr && NVNGXProxy::D3D11_EvaluateFeature() != nullptr;
            }

            // delay between init and create feature
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        LOG_INFO("Creating DLSS feature");

        if (CreateFeature() != nullptr)
        {
            ProcessInitParams(InParameters);

            _p_dlssHandle = &_dlssHandle;
            nvResult = CreateFeature()(InContext, NVSDK_NGX_Feature_SuperSampling, InParameters, &_p_dlssHandle);

            if (nvResult != NVSDK_NGX_Result_Success)
            {
                LOG_ERROR("D3D11_CreateFeature result: {0:X}", (unsigned int) nvResult);
                _p_dlssHandle = nullptr; // NGX never issued it
                break;
            }
        }
        else
        {
            LOG_ERROR("D3D11_CreateFeature is nullptr");
            break;
        }

        ReadVersion(_custom != nullptr ? _custom->module.filePath : L"");

        initResult = true;

    } while (false);

    SetInit(initResult);

    return initResult;
}

bool DLSSFeatureDx11::EvaluateInternal(ID3D11DeviceContext* InDeviceContext, NVSDK_NGX_Parameter* InParameters)
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

        nvResult = EvaluateFeature()(InDeviceContext, _p_dlssHandle, InParameters, NULL);

        if (nvResult != NVSDK_NGX_Result_Success)
        {
            LOG_ERROR("_EvaluateFeature result: {0:X}", (unsigned int) nvResult);
            return false;
        }

        LOG_TRACE("_EvaluateFeature ok!");
    }
    else
    {
        LOG_ERROR("_EvaluateFeature is nullptr");
        return false;
    }

    return true;
}

void DLSSFeatureDx11::Shutdown(ID3D11Device* InDevice)
{
    if (_dlssInited && _custom != nullptr)
        NVNGXProxy::ShutdownCustomDx11(*_custom, InDevice);

    DLSSFeature::Shutdown();
}

DLSSFeatureDx11::DLSSFeatureDx11(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters,
                                 const std::string& customUpscaler)
    : IFeature(InHandleId, InParameters), IFeature_Dx11(InHandleId, InParameters), DLSSFeature(InHandleId, InParameters)
{
    if (!customUpscaler.empty())
    {
        _custom = NVNGXProxy::LoadCustom(customUpscaler, API::DX11);
        _moduleLoaded = _custom != nullptr;

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

DLSSFeatureDx11::~DLSSFeatureDx11()
{
    if (State::Instance().isShuttingDown)
        return;

    if (ReleaseFeature() != nullptr && _p_dlssHandle != nullptr)
        ReleaseFeature()(_p_dlssHandle);
}
