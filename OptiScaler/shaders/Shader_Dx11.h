#pragma once
#include "Shader_Common.h"
#include <d3d11.h>

#include <gpu_time/GpuTime_Dx11.h>

class Shader_Dx11
{
  protected:
    std::string _name = "";
    bool _init = false;
    int _counter = 0;

    std::unique_ptr<GpuTime_Dx11> GpuTime = nullptr;

    ID3D11Device* _device = nullptr;

    ID3D11ComputeShader* _computeShader = nullptr;
    ID3D11Buffer* _constantBuffer = nullptr;
    ID3D11Texture2D* _buffer = nullptr;
    ID3D11ShaderResourceView* _srvInput = nullptr;
    ID3D11UnorderedAccessView* _uavOutput = nullptr;

    ID3D11Texture2D* _currentInResource = nullptr;
    ID3D11Texture2D* _currentOutResource = nullptr;

    static DXGI_FORMAT TranslateTypelessFormats(DXGI_FORMAT format);

    bool CreateBufferResourceCommon(ID3D11Device* InDevice, ID3D11Resource* InResource, ID3D11Texture2D*& OutBuffer,
                                    std::function<void(D3D11_TEXTURE2D_DESC&)> modifyDescCallback);

    HRESULT CreateComputeShader(ID3D11Device* device, ID3D11ComputeShader*& computeShader, const void* bytecode,
                                size_t bytecodeSize, const char* shaderCode);

    bool InitializeSRV(ID3D11Texture2D* resource, ID3D11Texture2D*& currentResource,
                       ID3D11ShaderResourceView*& targetSrv);

    bool InitializeUAV(ID3D11Texture2D* resource, ID3D11Texture2D*& currentResource,
                       ID3D11UnorderedAccessView*& targetUav);

  public:
    ID3D11Texture2D* Buffer() { return _buffer; }
    bool IsInit() const { return _init; }
    bool CanRender() const { return _init && _buffer != nullptr; }

    std::string Name() const { return _name; }
    std::optional<double> ReadGpuTime(ID3D11DeviceContext* deviceContext)
    {
        return GpuTime->ReadGpuTime(deviceContext);
    }

    Shader_Dx11(std::string InName, ID3D11Device* InDevice);

    ~Shader_Dx11();
};
