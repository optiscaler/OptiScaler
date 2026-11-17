#include "pch.h"
#include "Config.h"
#include "proxies/Nvngx_Proxy.h"

#pragma region Out of NVNGX Scope

// #include "backends/dlss/nvapi.h"
//
//// Implementation of
/// https://github.com/Nukem9/dlssg-to-fsr3/blob/89ddc8c1cce4593fb420e633a06605c3c4b9c3cf/source/maindll/NGX/NvNGXExports.cpp
//
// NVSDK_NGX_API uint32_t NVSDK_NGX_GetAPIVersion()
//{
//	LOG_FUNC();
//	return 19;
//}
//
// NVSDK_NGX_API uint32_t NVSDK_NGX_GetApplicationId()
//{
//	LOG_FUNC();
//	return State::Instance().NVNGX_ApplicationId;
//}
//
// NVSDK_NGX_API uint32_t NVSDK_NGX_GetDriverVersion()
//{
//	LOG_FUNC();
//	return 0x23A0000; // NGXMinimumDriverVersion 570.00
//}
//
// NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_GetDriverVersionEx(uint32_t* Versions, uint32_t InputVersionCount, uint32_t*
// TotalDriverVersionCount)
//{
//	LOG_FUNC();
//
//	if (!Versions && !TotalDriverVersionCount)
//	{
//		LOG_ERROR("invalid parameter!");
//		return NVSDK_NGX_Result_FAIL_InvalidParameter;
//	}
//
//	if (TotalDriverVersionCount)
//		*TotalDriverVersionCount = 2;
//
//	if (Versions && InputVersionCount >= 1)
//	{
//		Versions[0] = 0x23A; // 570
//
//		if (InputVersionCount >= 2)
//			Versions[1] = 0;
//	}
//
//	return NVSDK_NGX_Result_Success;
//}
//
// NVSDK_NGX_API uint32_t NVSDK_NGX_GetGPUArchitecture()
//{
//	LOG_FUNC();
//	return NV_GPU_ARCHITECTURE_TU100;
//}
//
// NVSDK_NGX_API uint32_t NVSDK_NGX_GetSnippetVersion()
//{
//	LOG_FUNC();
//	return 0x30500;
//}
//
// NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_ProcessCommand(const char* Command, const char* Value, void* Unknown)
//{
//	LOG_DEBUG("NVSDK_NGX_ProcessCommand({0}, {1}, void)", std::string(Command), std::string(Value));
//	return NVSDK_NGX_Result_Success;
//}
//
// NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_SetInfoCallback(void* Callback)
//{
//	LOG_FUNC();
//	return NVSDK_NGX_Result_Success;
//}
//
// NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_SetTelemetryEvaluateCallback(void* Callback)
//{
//	LOG_FUNC();
//	return NVSDK_NGX_Result_Success;
//}

#pragma endregion

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_UpdateFeature(const NVSDK_NGX_Application_Identifier* ApplicationId,
                                                       const NVSDK_NGX_Feature FeatureID)
{
    LOG_DEBUG("FeatureId: {0}", (UINT) FeatureID);

    // To test with puredark mods
    // if (FeatureID != NVSDK_NGX_Feature_SuperSampling)
    //{
    //	if (NVNGXProxy::UpdateFeature() != nullptr)
    //	{
    //		auto result = NVNGXProxy::UpdateFeature()(ApplicationId, FeatureID);
    //		LOG_ERROR("NVNGXProxy result for feature ({0}): {1:X}", (int)FeatureID, (UINT)result);
    //		return result;
    //	}
    //	else
    //	{
    //		LOG_ERROR("Can't update this feature ({0})!", (int)FeatureID);
    //		return NVSDK_NGX_Result_Fail;
    //	}
    //}

    if (ApplicationId != nullptr)
    {
        if (ApplicationId->IdentifierType == NVSDK_NGX_Application_Identifier_Type_Application_Id)
        {
            auto appId = Config::Instance()->UseGenericAppIdWithDlss.value_or_default()
                             ? app_id_override
                             : ApplicationId->v.ApplicationId;
            LOG_INFO("Update ApplicationId: {0:X}", appId);
            State::Instance().NVNGX_ApplicationId = appId;
        }
        else if (ApplicationId->IdentifierType == NVSDK_NGX_Application_Identifier_Type_Project_Id)
        {
            auto projectId = Config::Instance()->UseGenericAppIdWithDlss.value_or_default()
                                 ? project_id_override
                                 : std::string(ApplicationId->v.ProjectDesc.ProjectId);
            State::Instance().NVNGX_ProjectId = projectId;
            State::Instance().NVNGX_Engine = ApplicationId->v.ProjectDesc.EngineType;
            State::Instance().NVNGX_EngineVersion = std::string(ApplicationId->v.ProjectDesc.EngineVersion);

            LOG_INFO("Update InProjectId: {0}", projectId);
            LOG_INFO("Update InEngineType: {0}", (int) State::Instance().NVNGX_Engine);
            LOG_INFO("Update InEngineVersion: {0}", State::Instance().NVNGX_EngineVersion);
        }
        else
        {
            LOG_ERROR("Unknown IdentifierType ({0})!", (int) ApplicationId->IdentifierType);
        }
    }

    // To test with puredark mods
    LOG_DEBUG("FeatureId finished, returning NVSDK_NGX_Result_Success");
    return NVSDK_NGX_Result_Success;
}
