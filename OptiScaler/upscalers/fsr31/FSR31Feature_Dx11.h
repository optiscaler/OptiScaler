#pragma once
#include "FSR31Feature.h"
#include <upscalers/IFeature_Dx11.h>

#include <fsr31/dx11/ffx_dx11.h>
#include <fsr31/ffx_fsr3.h>
#include <fsr31/ffx_types.h>
#include <fsr31/ffx_error.h>

class FSR31FeatureDx11 : public FSR31Feature, public IFeature_Dx11
{

  private:
    inline static std::string ResultToString(Fsr31::FfxErrorCode result)
    {
        switch (result)
        {
        case Fsr31::FFX_OK:
            return "The operation completed successfully.";
        case Fsr31::FFX_ERROR_INVALID_POINTER:
            return "The operation failed due to an invalid pointer.";
        case Fsr31::FFX_ERROR_INVALID_ALIGNMENT:
            return "The operation failed due to an invalid alignment.";
        case Fsr31::FFX_ERROR_INVALID_SIZE:
            return "The operation failed due to an invalid size.";
        case Fsr31::FFX_EOF:
            return "The end of the file was encountered.";
        case Fsr31::FFX_ERROR_INVALID_PATH:
            return "The operation failed because the specified path was invalid.";
        case Fsr31::FFX_ERROR_EOF:
            return "The operation failed because end of file was reached.";
        case Fsr31::FFX_ERROR_MALFORMED_DATA:
            return "The operation failed because of some malformed data.";
        case Fsr31::FFX_ERROR_OUT_OF_MEMORY:
            return "The operation failed because it ran out memory.";
        case Fsr31::FFX_ERROR_INCOMPLETE_INTERFACE:
            return "The operation failed because the interface was not fully configured.";
        case Fsr31::FFX_ERROR_INVALID_ENUM:
            return "The operation failed because of an invalid enumeration value.";
        case Fsr31::FFX_ERROR_INVALID_ARGUMENT:
            return "The operation failed because an argument was invalid.";
        case Fsr31::FFX_ERROR_OUT_OF_RANGE:
            return "The operation failed because a value was out of range.";
        case Fsr31::FFX_ERROR_NULL_DEVICE:
            return "The operation failed because a device was null.";
        case Fsr31::FFX_ERROR_BACKEND_API_ERROR:
            return "The operation failed because the backend API returned an error code.";
        case Fsr31::FFX_ERROR_INSUFFICIENT_MEMORY:
            return "The operation failed because there was not enough memory.";
        default:
            return "Unknown";
        }
    }

    inline static void FfxLogCallback(Fsr31::FfxMsgType type, const wchar_t* message)
    {
        std::wstring string(message);

        // if (type == FFX_API_MESSAGE_TYPE_ERROR)
        //	LOG_ERROR("FSR Runtime: {0}", str);
        // else if (type == FFX_API_MESSAGE_TYPE_WARNING)
        //	LOG_WARN("FSR Runtime: {0}", str);
        // else
        LOG_DEBUG("FSR Runtime: {0}", wstring_to_string(string));
    }

    using D3D11_TEXTURE2D_DESC_C = struct D3D11_TEXTURE2D_DESC_C
    {
        UINT Width;
        UINT Height;
        DXGI_FORMAT Format;
        UINT BindFlags;
    };

    using D3D11_TEXTURE2D_RESOURCE_C = struct D3D11_TEXTURE2D_RESOURCE_C
    {
        D3D11_TEXTURE2D_DESC_C Desc = {};
        ID3D11Texture2D* Texture = nullptr;
        bool usingOriginal = false;
    };

    D3D11_TEXTURE2D_RESOURCE_C bufferColor = {};
    D3D11_TEXTURE2D_RESOURCE_C bufferDepth = {};
    D3D11_TEXTURE2D_RESOURCE_C bufferVelocity = {};

    bool CopyTexture(ID3D11Resource* InResource, D3D11_TEXTURE2D_RESOURCE_C* OutTextureDesc, UINT bindFlags,
                     bool InCopy);
    void ReleaseResources();

  protected:
    Fsr31::FfxFsr3Context _upscalerContext = {};
    Fsr31::FfxFsr3ContextDescription _upscalerContextDesc = {};
    bool InitFSR3(const NVSDK_NGX_Parameter* InParameters);

  public:
    FSR31FeatureDx11(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);

    bool InitInternal(ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters) override;
    bool EvaluateInternal(ID3D11DeviceContext* DeviceContext, NVSDK_NGX_Parameter* InParameters) override;

    feature_version Version() override { return feature_version { 3, 1, 2 }; }
    Upscaler GetUpscalerType() const final { return Upscaler::FSR31; }
    API Api() const override { return IFeature_Dx11::Api(); }
    bool CallsUpscalerEndByItself() override { return IFeature_Dx11::CallsUpscalerEndByItself(); }

    bool IsWithDx12() final { return false; }

    ~FSR31FeatureDx11();
};
