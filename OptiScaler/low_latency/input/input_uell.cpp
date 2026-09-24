#include "pch.h"
#include "input_uell.h"

#include <DirectXMath.h>

using namespace DirectX;

void InputUeLowLatency::init()
{
    if (inited)
        return;

    // if (auto dx11device = State::Instance().currentD3D11Device; dx11device)
    //{
    //     device = dx11device;
    //     inited = true;
    // }

    if (auto dx12device = State::Instance().currentD3D12Device; !inited && dx12device)
    {
        device = dx12device;
        inited = true;
    }

    if (inited)
    {
        LOG_DEBUG("Inited UeLowLatency");

        // TODO: not fully filled out
        SleepMode sleepMode {};
        sleepMode.low_latency_enabled = true;
        sleepMode.minimum_interval_us = 0;

        auto result = InputCommon::set_sleep_mode(inputContext, device, &sleepMode);

        if (result == InputResult::Ok || result == InputResult::UsingDifferentInput)
            return;
        else
            LOG_ERROR("set_sleep_mode result: {}", magic_enum::enum_name(result));
    }
}

void InputUeLowLatency::tickStart(int64_t frameId, float DeltaSeconds, bool bIdleMode)
{
    if (!inited)
        InputUeLowLatency::init();

    auto result = InputCommon::sleep(inputContext, device, frameId);

    if (result == InputResult::UsingDifferentInput)
        return;

    if (result != InputResult::Ok)
    {
        LOG_ERROR("sleep result: {}", magic_enum::enum_name(result));
        return;
    }

    MarkerParams markerParams {};
    markerParams.frame_id = frameId;
    markerParams.marker_type = MarkerType::SIMULATION_START;

    result = InputCommon::set_marker(inputContext, device, markerParams);

    if (result != InputResult::Ok)
    {
        LOG_ERROR("set_marker result: {}", magic_enum::enum_name(result));
        return;
    }
}

void InputUeLowLatency::tickEnd(int64_t frameId, float DeltaSeconds, bool bIdleMode)
{
    MarkerParams markerParams {};
    markerParams.frame_id = frameId;
    markerParams.marker_type = MarkerType::SIMULATION_END;

    auto result = InputCommon::set_marker(inputContext, device, markerParams);

    if (result != InputResult::Ok)
    {
        LOG_ERROR("set_marker result: {}", magic_enum::enum_name(result));
        return;
    }
}

static void GetCameraBasis(const float cameraRotation[3], float cameraUp[3], float cameraRight[3],
                           float cameraForward[3])
{
    float pitch = DirectX::XMConvertToRadians(cameraRotation[0]);
    float yaw = DirectX::XMConvertToRadians(cameraRotation[1]);
    float roll = DirectX::XMConvertToRadians(cameraRotation[2]);

    float SP, CP, SY, CY, SR, CR;
    DirectX::XMScalarSinCos(&SP, &CP, pitch);
    DirectX::XMScalarSinCos(&SY, &CY, yaw);
    DirectX::XMScalarSinCos(&SR, &CR, roll);

    cameraForward[0] = CP * CY;
    cameraForward[1] = CP * SY;
    cameraForward[2] = SP;

    cameraRight[0] = SR * SP * CY - CR * SY;
    cameraRight[1] = SR * SP * SY + CR * CY;
    cameraRight[2] = -SR * CP;

    cameraUp[0] = -(CR * SP * CY + SR * SY);
    cameraUp[1] = CY * SR - CR * SP * SY;
    cameraUp[2] = CR * CP;
}

void InputUeLowLatency::cameraUpdate(int64_t frameId, float cameraPosition[3], float cameraRotation[3], float fovAngle)
{
    // Technically FSRFG input might have the camera data but not guaranteed
    // if (State::Instance().activeFgInput == FGInput::DLSSG)
    //    return;

    if (auto fg = State::Instance().currentFG)
    {
        auto index = frameId % BUFFER_COUNT;

        float cameraUp[3];
        float cameraRight[3];
        float cameraForward[3];

        GetCameraBasis(cameraRotation, cameraUp, cameraRight, cameraForward);

        fg->SetCameraData(cameraPosition, cameraUp, cameraRight, cameraForward, index);

        UINT64 width = 0;
        UINT height = 0;
        fg->GetInterpolationRect(width, height, index);
        float aspectRatio = (float) width / (float) height;

        // TODO: camera near and far are placeholders
        fg->SetCameraValues(0.01f, 10000.f, DirectX::XMConvertToRadians(fovAngle), aspectRatio, 0.0f, index);
    }
}
