#include "pch.h"

#include "RCAS_Dx11.h"
#include "../Shader_Common.h"

#include "precompile/RCAS_Shader_Dx11.h"
#include "precompile/da_das_sharpen_Shader_Dx11.h"
#include "precompile/da_rcas_sharpen_Shader_Dx11.h"

#include "precompile/baba_neural_sharpen_Shader_Dx11.h"
#include "precompile/opti_sharpen_fp32_crisp_Shader_Dx11.h"
#include "precompile/opti_sharpen_fp32_natural_Shader_Dx11.h"

#include "shaders/OPTI_SHARPEN_FP32_Shader.h"

#include <Config.h>

bool RCAS_Dx11::CreateBufferResource(ID3D11Device* InDevice, ID3D11Resource* InResource)
{
    return CreateBufferResourceCommon(InDevice, InResource, _buffer, [](D3D11_TEXTURE2D_DESC& desc)
                                      { desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS; });
}

bool RCAS_Dx11::InitializeViews(ID3D11Texture2D* InResource, ID3D11Texture2D* InMotionVectors,
                                ID3D11Texture2D* OutResource)
{
    auto resultInput = InitializeSRV(InResource, _currentInResource, _srvInput);
    auto resultMvs = InitializeSRV(InMotionVectors, _currentMotionVectors, _srvMotionVectors);
    auto resultOutput = InitializeUAV(OutResource, _currentOutResource, _uavOutput);

    return resultInput && resultMvs && resultOutput;
}

bool RCAS_Dx11::InitializeViewsDA(ID3D11Texture2D* InResource, ID3D11Texture2D* InMotionVectors,
                                  ID3D11Texture2D* InDepth, ID3D11Texture2D* OutResource)
{
    auto resultBase = InitializeViews(InResource, InMotionVectors, OutResource);
    auto resultDepth = InitializeSRV(InDepth, _currentDepth, _srvDepth);

    return resultBase && resultDepth;
}

bool RCAS_Dx11::DispatchRCAS(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, ID3D11Texture2D* InResource,
                             ID3D11Texture2D* InMotionVectors, RcasConstants InConstants, ID3D11Texture2D* OutResource)
{
    (void) InDevice;

    if (!InitializeViews(InResource, InMotionVectors, OutResource))
        return false;

    InternalConstants constants {};

    D3D11_TEXTURE2D_DESC outDesc {};
    OutResource->GetDesc(&outDesc);
    D3D11_TEXTURE2D_DESC mvsDesc {};
    InMotionVectors->GetDesc(&mvsDesc);

    constants.OutputWidth = (uint32_t) outDesc.Width;
    constants.OutputHeight = outDesc.Height;
    constants.MotionWidth = (uint32_t) mvsDesc.Width;
    constants.MotionHeight = mvsDesc.Height;

    FillMotionConstants(constants, InConstants);

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    auto hr = InContext->Map(_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] Map error {1:x}", _name, hr);

        if (hr == DXGI_ERROR_DEVICE_REMOVED && _device != nullptr)
            Util::GetDeviceRemovedReason(_device);

        return false;
    }

    memcpy(mappedResource.pData, &constants, sizeof(constants));
    InContext->Unmap(_constantBuffer, 0);

    InContext->CSSetShader(_computeShader, nullptr, 0);
    InContext->CSSetConstantBuffers(0, 1, &_constantBuffer);
    InContext->CSSetShaderResources(0, 1, &_srvInput);
    InContext->CSSetShaderResources(1, 1, &_srvMotionVectors);
    InContext->CSSetUnorderedAccessViews(0, 1, &_uavOutput, nullptr);

    UINT dispatchWidth = (constants.OutputWidth + InNumThreadsX - 1) / InNumThreadsX;
    UINT dispatchHeight = (constants.OutputHeight + InNumThreadsY - 1) / InNumThreadsY;

    InContext->Dispatch(dispatchWidth, dispatchHeight, 1);

    ID3D11UnorderedAccessView* nullUAV = nullptr;
    InContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ID3D11ShaderResourceView* nullSRV[2] = { nullptr, nullptr };
    InContext->CSSetShaderResources(0, 2, nullSRV);

    return true;
}

bool RCAS_Dx11::DispatchDepthAdaptive(ID3D11Device* InDevice, ID3D11DeviceContext* InContext,
                                      ID3D11Texture2D* InResource, ID3D11Texture2D* InMotionVectors,
                                      ID3D11Texture2D* InDepth, RcasConstants InConstants, ID3D11Texture2D* OutResource)
{
    (void) InDevice;

    if (InDepth == nullptr || _computeShaderDA == nullptr)
        return false;

    if (!InitializeViewsDA(InResource, InMotionVectors, InDepth, OutResource))
        return false;

    InternalConstantsDA constants {};

    D3D11_TEXTURE2D_DESC outDesc {};
    OutResource->GetDesc(&outDesc);
    D3D11_TEXTURE2D_DESC mvsDesc {};
    InMotionVectors->GetDesc(&mvsDesc);
    D3D11_TEXTURE2D_DESC depthDesc {};
    InDepth->GetDesc(&depthDesc);

    constants.OutputWidth = (uint32_t) outDesc.Width;
    constants.OutputHeight = outDesc.Height;
    constants.MotionWidth = (uint32_t) mvsDesc.Width;
    constants.MotionHeight = mvsDesc.Height;
    constants.DepthWidth = (uint32_t) depthDesc.Width;
    constants.DepthHeight = depthDesc.Height;

    FillMotionConstants(constants, InConstants);

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    auto hr = InContext->Map(_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] Map error {1:x}", _name, hr);

        if (hr == DXGI_ERROR_DEVICE_REMOVED && _device != nullptr)
            Util::GetDeviceRemovedReason(_device);

        return false;
    }

    memcpy(mappedResource.pData, &constants, sizeof(constants));
    InContext->Unmap(_constantBuffer, 0);

    InContext->CSSetShader(_computeShaderDA, nullptr, 0);
    InContext->CSSetConstantBuffers(0, 1, &_constantBuffer);

    ID3D11ShaderResourceView* srvs[3] = { _srvInput, _srvMotionVectors, _srvDepth };
    InContext->CSSetShaderResources(0, 3, srvs);
    InContext->CSSetUnorderedAccessViews(0, 1, &_uavOutput, nullptr);

    UINT dispatchWidth = (constants.OutputWidth + InNumThreadsX - 1) / InNumThreadsX;
    UINT dispatchHeight = (constants.OutputHeight + InNumThreadsY - 1) / InNumThreadsY;

    InContext->Dispatch(dispatchWidth, dispatchHeight, 1);

    ID3D11UnorderedAccessView* nullUAV = nullptr;
    InContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ID3D11ShaderResourceView* nullSRV[3] = { nullptr, nullptr, nullptr };
    InContext->CSSetShaderResources(0, 3, nullSRV);

    return true;
}

bool RCAS_Dx11::DispatchDASDepthAdaptive(ID3D11Device* InDevice, ID3D11DeviceContext* InContext,
                                         ID3D11Texture2D* InResource, ID3D11Texture2D* InMotionVectors,
                                         ID3D11Texture2D* InDepth, RcasConstants InConstants,
                                         ID3D11Texture2D* OutResource)
{
    (void) InDevice;

    if (InDepth == nullptr || _computeShaderDASDA == nullptr)
        return false;

    if (!InitializeViewsDA(InResource, InMotionVectors, InDepth, OutResource))
        return false;

    InternalConstantsDA constants {};

    D3D11_TEXTURE2D_DESC outDesc {};
    OutResource->GetDesc(&outDesc);
    D3D11_TEXTURE2D_DESC mvsDesc {};
    InMotionVectors->GetDesc(&mvsDesc);
    D3D11_TEXTURE2D_DESC depthDesc {};
    InDepth->GetDesc(&depthDesc);

    constants.OutputWidth = (uint32_t) outDesc.Width;
    constants.OutputHeight = outDesc.Height;
    constants.MotionWidth = (uint32_t) mvsDesc.Width;
    constants.MotionHeight = mvsDesc.Height;
    constants.DepthWidth = (uint32_t) depthDesc.Width;
    constants.DepthHeight = depthDesc.Height;

    FillMotionConstants(constants, InConstants);

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    auto hr = InContext->Map(_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] Map error {1:x}", _name, hr);

        if (hr == DXGI_ERROR_DEVICE_REMOVED && _device != nullptr)
            Util::GetDeviceRemovedReason(_device);

        return false;
    }

    memcpy(mappedResource.pData, &constants, sizeof(constants));
    InContext->Unmap(_constantBuffer, 0);

    InContext->CSSetShader(_computeShaderDASDA, nullptr, 0);
    InContext->CSSetConstantBuffers(0, 1, &_constantBuffer);

    ID3D11ShaderResourceView* srvs[3] = { _srvInput, _srvMotionVectors, _srvDepth };
    InContext->CSSetShaderResources(0, 3, srvs);
    InContext->CSSetUnorderedAccessViews(0, 1, &_uavOutput, nullptr);

    UINT dispatchWidth = (constants.OutputWidth + InNumThreadsX - 1) / InNumThreadsX;
    UINT dispatchHeight = (constants.OutputHeight + InNumThreadsY - 1) / InNumThreadsY;

    InContext->Dispatch(dispatchWidth, dispatchHeight, 1);

    ID3D11UnorderedAccessView* nullUAV = nullptr;
    InContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ID3D11ShaderResourceView* nullSRV[3] = { nullptr, nullptr, nullptr };
    InContext->CSSetShaderResources(0, 3, nullSRV);

    return true;
}

bool RCAS_Dx11::EnsureNeuralShader(SharpenShader InShader, OptiVariant InVariant, ID3D11Device* InDevice)
{
    const bool isOpti = InShader == SharpenShader::OptiSharpen;
    auto& computeShader = isOpti ? _computeShaderOpti[(size_t) InVariant] : _computeShaderBaba;
    auto& failed = isOpti ? _optiShaderFailed[(size_t) InVariant] : _babaShaderFailed;

    if (computeShader != nullptr)
        return true;

    // Don't try to compile it again every frame
    if (failed || InDevice == nullptr)
        return false;

    const void* bytecode = baba_neural_sharpen_cso;
    size_t bytecodeSize = sizeof(baba_neural_sharpen_cso);
    std::string source = babaNeuralSharpenCode;
    std::string shaderName = "BaBa Neural";

    if (isOpti)
    {
        shaderName = std::format("OptiSharpen ({})", OptiVariantName(InVariant));

        // GetOptiVariant gives only the fp32 ones on Dx11
        const bool crisp = IsCrisp(InVariant);
        bytecode = crisp ? opti_sharpen_fp32_crisp_cso : opti_sharpen_fp32_natural_cso;
        bytecodeSize = crisp ? sizeof(opti_sharpen_fp32_crisp_cso) : sizeof(opti_sharpen_fp32_natural_cso);

        // The runtime compiled one gets the preset the way the precompiled ones do
        source = std::format("#define OPTI_MODEL {}\n", crisp ? 1 : 0) + optiSharpenFp32Code;
    }

    LOG_INFO("[{0}] Creating {1} sharpening shader", _name, shaderName);

    HRESULT result = CreateComputeShader(InDevice, computeShader, bytecode, bytecodeSize, source.c_str());

    if (FAILED(result))
    {
        LOG_ERROR("[{0}] CreateComputeShader error for {1} shader: {2:X}, RCAS will be used instead", _name, shaderName,
                  result);
        computeShader = nullptr;
        failed = true;
        return false;
    }

    return true;
}

bool RCAS_Dx11::DispatchNeural(SharpenShader InShader, OptiVariant InVariant, ID3D11DeviceContext* InContext,
                               ID3D11Texture2D* InResource, ID3D11Texture2D* InMotionVectors, RcasConstants InConstants,
                               ID3D11Texture2D* OutResource)
{
    auto computeShader =
        InShader == SharpenShader::OptiSharpen ? _computeShaderOpti[(size_t) InVariant] : _computeShaderBaba;

    if (computeShader == nullptr)
        return false;

    if (!InitializeViews(InResource, InMotionVectors, OutResource))
        return false;

    InternalConstantsBaba constants {};

    D3D11_TEXTURE2D_DESC outDesc {};
    OutResource->GetDesc(&outDesc);
    D3D11_TEXTURE2D_DESC mvsDesc {};
    InMotionVectors->GetDesc(&mvsDesc);

    constants.OutputWidth = (uint32_t) outDesc.Width;
    constants.OutputHeight = outDesc.Height;
    constants.MotionWidth = (uint32_t) mvsDesc.Width;
    constants.MotionHeight = mvsDesc.Height;

    FillMotionConstants(constants, InConstants, InShader);

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    auto hr = InContext->Map(_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] Map error {1:x}", _name, hr);

        if (hr == DXGI_ERROR_DEVICE_REMOVED && _device != nullptr)
            Util::GetDeviceRemovedReason(_device);

        return false;
    }

    memcpy(mappedResource.pData, &constants, sizeof(constants));
    InContext->Unmap(_constantBuffer, 0);

    InContext->CSSetShader(computeShader, nullptr, 0);
    InContext->CSSetConstantBuffers(0, 1, &_constantBuffer);
    InContext->CSSetShaderResources(0, 1, &_srvInput);
    InContext->CSSetShaderResources(1, 1, &_srvMotionVectors);
    InContext->CSSetUnorderedAccessViews(0, 1, &_uavOutput, nullptr);

    UINT dispatchWidth = (constants.OutputWidth + InNumThreadsX - 1) / InNumThreadsX;
    UINT dispatchHeight = (constants.OutputHeight + InNumThreadsY - 1) / InNumThreadsY;

    InContext->Dispatch(dispatchWidth, dispatchHeight, 1);

    ID3D11UnorderedAccessView* nullUAV = nullptr;
    InContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ID3D11ShaderResourceView* nullSRV[2] = { nullptr, nullptr };
    InContext->CSSetShaderResources(0, 2, nullSRV);

    return true;
}

bool RCAS_Dx11::Dispatch(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, ID3D11Texture2D* InResource,
                         ID3D11Texture2D* InMotionVectors, RcasConstants InConstants, ID3D11Texture2D* OutResource,
                         ID3D11Texture2D* InDepth)
{
    if (!_init || InDevice == nullptr || InContext == nullptr || InResource == nullptr || OutResource == nullptr ||
        InMotionVectors == nullptr)
    {
        return false;
    }

    LOG_DEBUG("[{0}] Start!", _name);

    ScopedGpuTime_Dx11 scopedGpuTime(GpuTime.get(), InContext);

    _device = InDevice;

    auto sharpnessShader = Config::Instance()->SharpnessShader.value_or_default();

    // Depth aware shaders fall back to plain RCAS without depth
    if (InDepth == nullptr &&
        (sharpnessShader == SharpenShader::DepthAwareDAS || sharpnessShader == SharpenShader::DepthAware))
        sharpnessShader = SharpenShader::RCAS;

    // Read once, the menu can change it meanwhile. FillMotionConstants takes the shader's Model from it.
    _optiModel = OptiModelSetting();
    const auto optiVariant = GetOptiVariant(_optiModel, true);

    // Neural shaders fall back to plain RCAS when their shader can't be created
    const bool isNeural = sharpnessShader == SharpenShader::BabaNeural || sharpnessShader == SharpenShader::OptiSharpen;
    if (isNeural && !EnsureNeuralShader(sharpnessShader, optiVariant, InDevice))
        sharpnessShader = SharpenShader::RCAS;

    SetTimedShader(sharpnessShader, optiVariant);

    if (sharpnessShader == SharpenShader::DepthAwareDAS)
    {
        return DispatchDASDepthAdaptive(InDevice, InContext, InResource, InMotionVectors, InDepth, InConstants,
                                        OutResource);
    }
    else if (sharpnessShader == SharpenShader::DepthAware)
    {
        return DispatchDepthAdaptive(InDevice, InContext, InResource, InMotionVectors, InDepth, InConstants,
                                     OutResource);
    }
    else if (sharpnessShader == SharpenShader::BabaNeural || sharpnessShader == SharpenShader::OptiSharpen)
    {
        return DispatchNeural(sharpnessShader, optiVariant, InContext, InResource, InMotionVectors, InConstants,
                              OutResource);
    }
    else if (sharpnessShader == SharpenShader::RCAS)
    {
        return DispatchRCAS(InDevice, InContext, InResource, InMotionVectors, InConstants, OutResource);
    }
    else
    {
        return false;
    }
}

RCAS_Dx11::RCAS_Dx11(std::string InName, ID3D11Device* InDevice) : Shader_Dx11(InName, InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    HRESULT result = CreateComputeShader(InDevice, _computeShader, reinterpret_cast<const void*>(rcas_cso),
                                         sizeof(rcas_cso), rcasCode.c_str());
    if (FAILED(result))
    {
        LOG_ERROR("[{0}] CreateComputeShader error for rcas shader: {1:X}", _name, result);
        return;
    }

    result = CreateComputeShader(InDevice, _computeShaderDA, reinterpret_cast<const void*>(da_rcas_sharpen_cso),
                                 sizeof(da_rcas_sharpen_cso), daRcasSharpenCode.c_str());
    if (FAILED(result))
    {
        LOG_ERROR("[{0}] CreateComputeShader error for depth aware shader: {1:X}", _name, result);
        return;
    }

    result = CreateComputeShader(InDevice, _computeShaderDASDA, reinterpret_cast<const void*>(da_das_sharpen_cso),
                                 sizeof(da_das_sharpen_cso), dasDASharpenCode.c_str());
    if (FAILED(result))
    {
        LOG_ERROR("[{0}] CreateComputeShader error for DAS depth aware shader: {1:X}", _name, result);
        return;
    }

    // CBV
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth = sizeof(InternalConstantsDA);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    result = InDevice->CreateBuffer(&cbDesc, nullptr, &_constantBuffer);
    if (result != S_OK)
    {
        LOG_ERROR("CreateBuffer error: {0:X}", (UINT) result);
        return;
    }

    _init = true;
}

RCAS_Dx11::~RCAS_Dx11()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    SAFE_RELEASE(_computeShaderDA);
    SAFE_RELEASE(_computeShaderDASDA);
    SAFE_RELEASE(_computeShaderBaba);
    for (auto& computeShader : _computeShaderOpti)
        SAFE_RELEASE(computeShader);
    SAFE_RELEASE(_srvMotionVectors);
    SAFE_RELEASE(_srvDepth);

    // _currentMotionVectors/_currentDepth are only cache keys, InitializeSRV holds no reference on them
}
