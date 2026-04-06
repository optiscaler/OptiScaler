#pragma once

#include "SysUtils.h"
#include "RCAS_Common.h"

#include <d3d11.h>

class RCAS_Dx11
{
  private:
    struct alignas(256) InternalConstants
    {
        float Sharpness;
        float Contrast;

        // Motion Vector Stuff
        int DynamicSharpenEnabled;
        int DisplaySizeMV;
        int Debug;

        float MotionSharpness;
        float MotionTextureScale;
        float MvScaleX;
        float MvScaleY;
        float Threshold;
        float ScaleLimit;
        int DisplayWidth;
        int DisplayHeight;
        int RenderWidth;
        int RenderHeight;
        int DepthEnabled;
        int DepthInverted;
        float DepthSharpness;
        float DepthEdgeThreshold;
    };

    std::string _name = "";
    bool _init = false;
    int _counter = 0;

    ID3D11Device* _device = nullptr;

    ID3D11ComputeShader* _computeShader = nullptr;
    ID3D11Buffer* _constantBuffer = nullptr;
    ID3D11Texture2D* _buffer = nullptr;
    ID3D11ShaderResourceView* _srvInput = nullptr;
    ID3D11ShaderResourceView* _srvMotionVectors = nullptr;
    ID3D11ShaderResourceView* _srvDepth = nullptr;
    ID3D11UnorderedAccessView* _uavOutput = nullptr;

    ID3D11Texture2D* _currentInResource = nullptr;
    ID3D11Texture2D* _currentMotionVectors = nullptr;
    ID3D11Texture2D* _currentDepth = nullptr;
    ID3D11Texture2D* _currentOutResource = nullptr;

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

    bool InitializeViews(ID3D11Texture2D* InResource, ID3D11Texture2D* InMotionVectors, ID3D11Texture2D* InDepth,
                         ID3D11Texture2D* OutResource);

  public:
    bool CreateBufferResource(ID3D11Device* InDevice, ID3D11Resource* InSource);
    bool Dispatch(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, ID3D11Texture2D* InResource,
                  ID3D11Texture2D* InMotionVectors, ID3D11Texture2D* InDepth, RcasConstants InConstants,
                  ID3D11Texture2D* OutResource);

    ID3D11Texture2D* Buffer() { return _buffer; }
    bool IsInit() const { return _init; }
    bool CanRender() const { return _init && _buffer != nullptr; }

    RCAS_Dx11(std::string InName, ID3D11Device* InDevice);

    ~RCAS_Dx11();
};
