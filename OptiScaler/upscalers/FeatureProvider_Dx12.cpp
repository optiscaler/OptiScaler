#include <pch.h>
#include "FeatureProvider_Dx12.h"

#include "Util.h"
#include "Config.h"

#include "NVNGX_Parameter.h"

#include "upscalers/dlss/DLSSFeature_Dx12.h"
#include "upscalers/dlssd/DLSSDFeature_Dx12.h"
#include "upscalers/fsr2/FSR2Feature_Dx12.h"
#include "upscalers/fsr2_212/FSR2Feature_Dx12_212.h"
#include "upscalers/fsr31/FSR31Feature_Dx12.h"
#include "upscalers/xess/XeSSFeature_Dx12.h"
#include "FeatureProvider_Dx11.h"

bool FeatureProvider_Dx12::GetFeature(std::string upscalerName, UINT handleId, NVSDK_NGX_Parameter* parameters,
                                      std::unique_ptr<IFeature_Dx12>* feature)
{
    ScopedSkipHeapCapture skipHeapCapture {};

    do
    {
        if (upscalerName == "xess")
        {
            *feature = std::make_unique<XeSSFeatureDx12>(handleId, parameters);
            break;
        }
        else if (upscalerName == "fsr21")
        {
            *feature = std::make_unique<FSR2FeatureDx12_212>(handleId, parameters);
            break;
        }
        else if (upscalerName == "fsr22")
        {
            *feature = std::make_unique<FSR2FeatureDx12>(handleId, parameters);
            break;
        }
        else if (upscalerName == "fsr31")
        {
            *feature = std::make_unique<FSR31FeatureDx12>(handleId, parameters);
            break;
        }

        if (Config::Instance()->DLSSEnabled.value_or_default())
        {
            if (upscalerName == "dlss" && State::Instance().NVNGX_DLSS_Path.has_value())
            {
                *feature = std::make_unique<DLSSFeatureDx12>(handleId, parameters);
                break;
            }
            else if (upscalerName == "dlssd" && State::Instance().NVNGX_DLSSD_Path.has_value())
            {
                *feature = std::make_unique<DLSSDFeatureDx12>(handleId, parameters);
                break;
            }
            else
            {
                *feature = std::make_unique<FSR2FeatureDx12_212>(handleId, parameters);
            }
        }
        else
        {
            *feature = std::make_unique<FSR2FeatureDx12_212>(handleId, parameters);
        }

    } while (false);

    if (!(*feature)->ModuleLoaded())
    {
        (*feature).reset();
        *feature = std::make_unique<FSR2FeatureDx12_212>(handleId, parameters);
        upscalerName = "fsr21";
    }
    else
    {
        Config::Instance()->Dx12Upscaler = upscalerName;
    }

    auto result = (*feature)->ModuleLoaded();

    if (result)
    {
        if (upscalerName == "dlssd")
            upscalerName = "dlss";

        Config::Instance()->Dx12Upscaler = upscalerName;
    }

    return result;
}

bool FeatureProvider_Dx12::ChangeFeature(std::string upscalerName, ID3D12Device* device,
                                         ID3D12GraphicsCommandList* cmdList, UINT handleId,
                                         NVSDK_NGX_Parameter* parameters, ContextData<IFeature_Dx12>* contextData)
{
    if (!State::Instance().changeBackend[handleId])
        return false;

    if (State::Instance().newBackend == "" ||
        (!Config::Instance()->DLSSEnabled.value_or_default() && State::Instance().newBackend == "dlss"))
        State::Instance().newBackend = Config::Instance()->Dx12Upscaler.value_or_default();

    contextData->changeBackendCounter++;

    LOG_INFO("changeBackend is true, counter: {0}", contextData->changeBackendCounter);

    // first release everything
    if (contextData->changeBackendCounter == 1)
    {
        if (State::Instance().currentFG != nullptr && State::Instance().currentFG->IsActive() &&
            State::Instance().activeFgInput == FGInput::Upscaler)
        {
            State::Instance().currentFG->DestroyFGContext();
            State::Instance().FGchanged = true;
            State::Instance().ClearCapturedHudlesses = true;
        }

        if (contextData->feature != nullptr)
        {
            LOG_INFO("changing backend to {}", State::Instance().newBackend);

            auto dc = contextData->feature.get();

            if (State::Instance().newBackend != "dlssd" && State::Instance().newBackend != "dlss")
                contextData->createParams = GetNGXParameters("OptiDx12");
            else
                contextData->createParams = parameters;

            contextData->createParams->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, dc->GetFeatureFlags());
            contextData->createParams->Set(NVSDK_NGX_Parameter_Width, dc->RenderWidth());
            contextData->createParams->Set(NVSDK_NGX_Parameter_Height, dc->RenderHeight());
            contextData->createParams->Set(NVSDK_NGX_Parameter_OutWidth, dc->DisplayWidth());
            contextData->createParams->Set(NVSDK_NGX_Parameter_OutHeight, dc->DisplayHeight());
            contextData->createParams->Set(NVSDK_NGX_Parameter_PerfQualityValue, dc->PerfQualityValue());

            dc = nullptr;

            State::Instance().currentFeature = nullptr;

            if (State::Instance().gameQuirks & GameQuirk::FastFeatureReset)
            {
                LOG_DEBUG("sleeping before reset of current feature for 100ms (Fast Feature Reset)");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            else
            {
                LOG_DEBUG("sleeping before reset of current feature for 1000ms");
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }

            contextData->feature.reset();
            contextData->feature = nullptr;
        }
        else
        {
            LOG_ERROR("can't find handle {0} in Dx12Contexts!", handleId);

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

    // create new feature
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

    // init feature
    if (contextData->changeBackendCounter == 3)
    {
        auto initResult = contextData->feature->Init(device, cmdList, contextData->createParams);

        contextData->changeBackendCounter = 0;

        if (!initResult)
        {
            LOG_ERROR("init failed with {0} feature", State::Instance().newBackend);

            if (State::Instance().newBackend != "dlssd")
            {
                if (Config::Instance()->Dx12Upscaler == "dlss")
                    State::Instance().newBackend = "xess";
                else
                    State::Instance().newBackend = "fsr21";
            }
            else
            {
                // Retry DLSSD
                State::Instance().newBackend = "dlssd";
            }

            State::Instance().changeBackend[handleId] = true;
            return NVSDK_NGX_Result_Success;
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
    if (State::Instance().currentFG != nullptr && State::Instance().activeFgInput == FGInput::Upscaler)
        State::Instance().currentFG->UpdateTarget();

    return true;
}
