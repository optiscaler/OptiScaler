#pragma once
#include "SysUtils.h"
#include <NVNGX_Parameter.h>
#include <upscalers/IFeature.h>
#include <framegen/IFGFeature.h>

namespace UpscalerInputs
{
struct CameraValues
{
    float cameraNear = 0.0f;
    float cameraFar = 0.0f;
    float cameraVFov = 0.0f;
    float meterFactor = 0.0f;
};

// Also updates State::lastFsrCameraNear/Far
CameraValues GetCameraValues(const NVSDK_NGX_Parameter& parameters, IFeature* feature);
FG_Constants GetFGConstants(IFeature* feature);
} // namespace UpscalerInputs
