#include <pch.h>

#include "DLSSFeature_Dx11On12.h"
#include <upscalers/dlss/DLSSFeature_Dx12.h>

DLSSFeatureDx11On12::DLSSFeatureDx11On12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : IFeature_Dx11wDx12(InHandleId, InParameters), IFeature_Dx11(InHandleId, InParameters),
      IFeature(InHandleId, InParameters)
{
    dx12Feature = std::make_unique<DLSSFeatureDx12>(InHandleId, InParameters);
}
