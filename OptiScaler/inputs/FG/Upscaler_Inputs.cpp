#include "pch.h"
#include "Upscaler_Inputs.h"

#include <State.h>
#include <Config.h>
#include <MathUtils.h>

using namespace OptiMath;

UpscalerInputs::CameraValues UpscalerInputs::GetCameraValues(const NVSDK_NGX_Parameter& parameters, IFeature* feature)
{
    auto& cfg = *Config::Instance();
    const bool useFsrInputs = cfg.FsrUseFsrInputValues.value_or_default();

    CameraValues values {};

    parameters.Get(OptiKeys::FSR_NearPlane, &values.cameraNear);
    parameters.Get(OptiKeys::FSR_FarPlane, &values.cameraFar);

    if (!useFsrInputs || (values.cameraNear == 0.0f && values.cameraFar == 0.0f))
    {
        values.cameraNear = cfg.FsrCameraNear.value_or_default();
        values.cameraFar = cfg.FsrCameraFar.value_or_default();

        if (feature->DepthInverted())
            std::swap(values.cameraNear, values.cameraFar);
    }

    if (!useFsrInputs ||
        parameters.Get(OptiKeys::FSR_CameraFovVertical, &values.cameraVFov) != NVSDK_NGX_Result_Success)
    {
        if (cfg.FsrVerticalFov.has_value())
            values.cameraVFov = GetRadiansFromDeg(cfg.FsrVerticalFov.value());
        else if (cfg.FsrHorizontalFov.value_or_default() > 0.0f)
            values.cameraVFov =
                GetVerticalFovFromHorizontal(GetRadiansFromDeg(cfg.FsrHorizontalFov.value()),
                                             (float) feature->TargetWidth(), (float) feature->TargetHeight());
        else
            values.cameraVFov = GetRadiansFromDeg(60);
    }

    if (!useFsrInputs)
        parameters.Get(OptiKeys::FSR_ViewSpaceToMetersFactor, &values.meterFactor);

    State::Instance().lastFsrCameraNear = values.cameraNear;
    State::Instance().lastFsrCameraFar = values.cameraFar;

    return values;
}

FG_Constants UpscalerInputs::GetFGConstants(IFeature* feature)
{
    FG_Constants constants {};
    constants.displayWidth = feature->DisplayWidth();
    constants.displayHeight = feature->DisplayHeight();

    if (feature->IsHdr())
        constants.flags |= FG_Flags::Hdr;

    if (feature->DepthInverted())
        constants.flags |= FG_Flags::InvertedDepth;

    if (feature->JitteredMV())
        constants.flags |= FG_Flags::JitteredMVs;

    if (!feature->LowResMV())
        constants.flags |= FG_Flags::DisplayResolutionMVs;

    if (Config::Instance()->FGAsync.value_or_default())
        constants.flags |= FG_Flags::Async;

    return constants;
}
