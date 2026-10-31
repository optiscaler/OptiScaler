#include <pch.h>
#include "FeatureProvider_Dx11.h"

#include "Util.h"
#include "Config.h"

#include "NVNGX_Parameter.h"

#include "upscalers/dlss/DLSSFeature_Dx11.h"
#include "upscalers/dlssd/DLSSDFeature_Dx11.h"
#include "upscalers/fsr2/FSR2Feature_Dx11.h"
#include "upscalers/fsr2/FSR2Feature_Dx11On12.h"
#include "upscalers/fsr2_212/FSR2Feature_Dx11On12_212.h"
#include "upscalers/fsr31/FSR31Feature_Dx11.h"
#include "upscalers/fsr31/FSR31Feature_Dx11On12.h"
#include "upscalers/xess/XeSSFeature_Dx11.h"
#include "upscalers/xess/XeSSFeature_Dx11on12.h"

bool FeatureProvider_Dx11::GetFeature(std::string upscalerName, UINT handleId, NVSDK_NGX_Parameter* parameters,
                                      std::unique_ptr<IFeature_Dx11>* feature)
{
    do
    {
        if (upscalerName == "xess")
        {
            *feature = std::make_unique<XeSSFeature_Dx11>(handleId, parameters);
            break;
        }
        else if (upscalerName == "xess_12")
        {
            *feature = std::make_unique<XeSSFeatureDx11on12>(handleId, parameters);
            break;
        }
        else if (upscalerName == "fsr21_12")
        {
            *feature = std::make_unique<FSR2FeatureDx11on12_212>(handleId, parameters);
            break;
        }
        else if (upscalerName == "fsr22")
        {
            *feature = std::make_unique<FSR2FeatureDx11>(handleId, parameters);
            break;
        }
        else if (upscalerName == "fsr22_12")
        {
            *feature = std::make_unique<FSR2FeatureDx11on12>(handleId, parameters);
            break;
        }
        else if (upscalerName == "fsr31")
        {
            *feature = std::make_unique<FSR31FeatureDx11>(handleId, parameters);
            break;
        }
        else if (upscalerName == "fsr31_12")
        {
            *feature = std::make_unique<FSR31FeatureDx11on12>(handleId, parameters);
            break;
        }

        if (Config::Instance()->DLSSEnabled.value_or_default())
        {
            if (upscalerName == "dlss" && State::Instance().NVNGX_DLSS_Path.has_value())
            {
                *feature = std::make_unique<DLSSFeatureDx11>(handleId, parameters);
                break;
            }
            else if (upscalerName == "dlssd" && State::Instance().NVNGX_DLSSD_Path.has_value())
            {
                *feature = std::make_unique<DLSSDFeatureDx11>(handleId, parameters);
                break;
            }
            else
            {
                *feature = std::make_unique<FSR2FeatureDx11>(handleId, parameters);
            }
        }
        else
        {
            *feature = std::make_unique<FSR2FeatureDx11>(handleId, parameters);
        }

    } while (false);

    if (!(*feature)->ModuleLoaded())
    {
        (*feature).reset();
        *feature = std::make_unique<FSR2FeatureDx11>(handleId, parameters);
        upscalerName = "fsr22";
    }
    else
    {
        Config::Instance()->Dx11Upscaler = upscalerName;
    }

    auto result = (*feature)->ModuleLoaded();

    if (result)
    {
        if (upscalerName == "dlssd")
            upscalerName = "dlss";

        Config::Instance()->Dx11Upscaler = upscalerName;
    }

    return result;
}

bool FeatureProvider_Dx11::ChangeFeature(std::string upscalerName, ID3D11Device* device,
                                         ID3D11DeviceContext* devContext, UINT handleId,
                                         NVSDK_NGX_Parameter* parameters, ContextData<IFeature_Dx11>* contextData)
{
    if (State::Instance().newBackend == "" ||
        (!Config::Instance()->DLSSEnabled.value_or_default() && State::Instance().newBackend == "dlss"))
        State::Instance().newBackend = Config::Instance()->Dx11Upscaler.value_or_default();

    contextData->changeBackendCounter++;

    // first release everything
    if (contextData->changeBackendCounter == 1)
    {
        if (contextData->feature != nullptr)
        {
            LOG_INFO("changing backend to {0}", State::Instance().newBackend);

            auto dc = contextData->feature.get();

            if (State::Instance().newBackend != "dlssd" && State::Instance().newBackend != "dlss")
                contextData->createParams = GetNGXParameters("OptiDx11");
            else
                contextData->createParams = parameters;

            contextData->createParams->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, dc->GetFeatureFlags());
            contextData->createParams->Set(NVSDK_NGX_Parameter_Width, dc->RenderWidth());
            contextData->createParams->Set(NVSDK_NGX_Parameter_Height, dc->RenderHeight());
            contextData->createParams->Set(NVSDK_NGX_Parameter_OutWidth, dc->DisplayWidth());
            contextData->createParams->Set(NVSDK_NGX_Parameter_OutHeight, dc->DisplayHeight());
            contextData->createParams->Set(NVSDK_NGX_Parameter_PerfQualityValue, dc->PerfQualityValue());

            State::Instance().currentFeature = nullptr;

            LOG_TRACE("sleeping before reset of current feature for 1000ms");
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            contextData->feature.reset();
            contextData->feature = nullptr;
        }
        else
        {
            LOG_ERROR("can't find handle {0} in Dx11Contexts!", handleId);

            State::Instance().newBackend = "";
            State::Instance().changeBackend[handleId] = false;

            int optiParam = 0;
            if (contextData->createParams != nullptr &&
                contextData->createParams->Get("OptiScaler", &optiParam) == NVSDK_NGX_Result_Success && optiParam == 1)
            {
                delete static_cast<NVNGX_Parameters*>(contextData->createParams);
            }

            contextData->createParams = nullptr;

            contextData->changeBackendCounter = 0;
        }

        return true;
    }

    if (contextData->changeBackendCounter == 2)
    {
        LOG_INFO("Creating new {} upscaler", State::Instance().newBackend);

        contextData->feature.reset();

        if (!GetFeature(State::Instance().newBackend, handleId, contextData->createParams, &contextData->feature))
        {
            LOG_ERROR("Upscaler can't created");
            return false;
        }

        return true;
    }

    if (contextData->changeBackendCounter == 3)
    {
        // then init and continue
        auto initResult = contextData->feature->Init(device, devContext, contextData->createParams);

        if (Config::Instance()->Dx11DelayedInit.value_or_default())
        {
            LOG_TRACE("sleeping after new Init of new feature for 1000ms");
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        contextData->changeBackendCounter = 0;

        if (!initResult || !contextData->feature->ModuleLoaded())
        {
            LOG_ERROR("init failed with {0} feature", State::Instance().newBackend);

            if (State::Instance().newBackend != "dlssd")
            {
                State::Instance().newBackend = "fsr22";
                State::Instance().changeBackend[handleId] = true;
            }
            else
            {
                State::Instance().newBackend = "";
                State::Instance().changeBackend[handleId] = false;
                return false;
            }
        }
        else
        {
            LOG_INFO("init successful for {0}, upscaler changed", State::Instance().newBackend);

            State::Instance().newBackend = "";
            State::Instance().changeBackend[handleId] = false;
        }

        // if opti nvparam release it
        int optiParam = 0;
        if (contextData->createParams->Get("OptiScaler", &optiParam) == NVSDK_NGX_Result_Success && optiParam == 1)
        {
            delete static_cast<NVNGX_Parameters*>(contextData->createParams);
            contextData->createParams = nullptr;
        }
    }

    // if initial feature can't be inited
    State::Instance().currentFeature = contextData->feature.get();

    return true;
}
