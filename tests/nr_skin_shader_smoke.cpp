// Headless shader test: Windows D3D11 WARP executes the shared HLSL, no game or NR DLL.
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <cstddef>
#include <DirectXPackedVector.h>
#include "../OptiScaler/shaders/dlssnr/DlssNr_Common.h"
#include "../OptiScaler/shaders/dlssnr/precompile/DlssNr_Shader.h"
using Microsoft::WRL::ComPtr;
struct Pixel { float r, g, b, a; };
static void check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("D3D call failed"); }
static void expect(bool ok, const char* label) { if (!ok) throw std::runtime_error(label); }
static bool same(Pixel a, Pixel b) {
    return std::abs(a.r-b.r)<0.0001f && std::abs(a.g-b.g)<0.0001f && std::abs(a.b-b.b)<0.0001f && a.a==b.a;
}
int wmain(int argc, wchar_t** argv) try {
    if (argc != 2) throw std::runtime_error("Pass the dlssnr.hlsl path");
    static_assert(offsetof(DlssNrConstants, SkinProtection) == 92);
    static_assert(offsetof(DlssNrConstants, EnvironmentColour) == 112);
    ComPtr<ID3DBlob> code, errors;
    HRESULT compiled = D3DCompileFromFile(argv[1], nullptr, nullptr, "CSMain", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (errors) std::fprintf(stderr, "%s", (char*)errors->GetBufferPointer());
    check(compiled);
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> ctx;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &ctx));
    ComPtr<ID3D11ComputeShader> shader;
    check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
    const std::array<Pixel, 2> base {{{0.75f,0.50f,0.40f,1}, {0.20f,0.40f,0.80f,1}}};
    const std::array<Pixel, 2> edited {{{0.50f,0.75f,0.40f,1}, {0.65f,0.25f,0.40f,1}}};
    D3D11_TEXTURE2D_DESC desc {};
    desc.Width=2; desc.Height=1; desc.MipLevels=1; desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> original, model, output, keep, readback;
    D3D11_SUBRESOURCE_DATA data {base.data(), sizeof(base), 0};
    check(device->CreateTexture2D(&desc,&data,&original));
    data.pSysMem=edited.data(); check(device->CreateTexture2D(&desc,&data,&model));
    ComPtr<ID3D11ShaderResourceView> originalSrv, modelSrv;
    check(device->CreateShaderResourceView(original.Get(),nullptr,&originalSrv));
    check(device->CreateShaderResourceView(model.Get(),nullptr,&modelSrv));
    desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    check(device->CreateTexture2D(&desc,nullptr,&output)); check(device->CreateTexture2D(&desc,nullptr,&keep));
    ComPtr<ID3D11UnorderedAccessView> outputUav, keepUav;
    check(device->CreateUnorderedAccessView(output.Get(),nullptr,&outputUav));
    check(device->CreateUnorderedAccessView(keep.Get(),nullptr,&keepUav));
    desc.BindFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    check(device->CreateTexture2D(&desc,nullptr,&readback));
    D3D11_BUFFER_DESC buffer {}; buffer.ByteWidth=sizeof(DlssNrConstants); buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> constants; check(device->CreateBuffer(&buffer,nullptr,&constants));
    D3D11_SAMPLER_DESC sampling {}; sampling.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampling.AddressU=sampling.AddressV=sampling.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sampling.MaxLOD=D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler; check(device->CreateSamplerState(&sampling,&sampler));
    ID3D11ShaderResourceView* srvs[]={originalSrv.Get(),modelSrv.Get(),originalSrv.Get(),originalSrv.Get(),originalSrv.Get()};
    ID3D11UnorderedAccessView* uavs[]={outputUav.Get(),keepUav.Get()};
    ctx->CSSetShader(shader.Get(),nullptr,0); ctx->CSSetShaderResources(0,5,srvs);
    ctx->CSSetUnorderedAccessViews(0,2,uavs,nullptr); ctx->CSSetConstantBuffers(0,1,constants.GetAddressOf());
    ctx->CSSetSamplers(0,1,sampler.GetAddressOf());
    DlssNrConstants settings {}; settings.Mode=DlssNrMode_Resolve; settings.Width=2; settings.Height=1;
    settings.WhitePoint=1; settings.Passthrough=1; settings.ApplyModel=1; settings.ReversibleMode=2;
    settings.TransferStrength=settings.ColourStrength=1; settings.MaxRatio=2;
    settings.SkinDetail=settings.SkinColour=settings.EnvironmentDetail=settings.EnvironmentColour=1;
    auto run=[&]() {
        ctx->UpdateSubresource(constants.Get(),0,nullptr,&settings,0,0); ctx->Dispatch(1,1,1);
        ctx->CopyResource(readback.Get(),output.Get()); D3D11_MAPPED_SUBRESOURCE mapped {};
        check(ctx->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped));
        std::array<Pixel,2> result; memcpy(result.data(),mapped.pData,sizeof(result)); ctx->Unmap(readback.Get(),0); return result;
    };
    auto result=run(); expect(same(result[0],edited[0]) && same(result[1],edited[1]), "Disabled filter changed output");
    settings.SkinProtection=1; result=run(); expect(same(result[0],edited[0]) && same(result[1],edited[1]), "Unity settings changed output");
    settings.SkinDetail=settings.SkinColour=settings.EnvironmentDetail=settings.EnvironmentColour=0;
    result=run(); expect(same(result[0],base[0]) && same(result[1],base[1]), "Zero strengths did not restore input");
    settings.EnvironmentDetail=settings.EnvironmentColour=1;
    result=run(); expect(same(result[0],base[0]) && same(result[1],edited[1]), "Skin/scene separation failed");
    settings.SkinDetail=1; result=run();
    expect(std::abs(result[0].r/result[0].g - base[0].r/base[0].g)<0.0001f &&
           std::abs(result[0].b/result[0].g - base[0].b/base[0].g)<0.0001f && same(result[1],edited[1]),
           "Skin colour suppression did not preserve chroma / affected environment");
    settings.ShowSkinMask=1; result=run(); expect(result[0].r>0.99f && result[1].r<0.01f, "Preview mismatch");
    settings.ShowSkinMask=0; settings.ApplyModel=0; result=run();
    expect(same(result[0],base[0]) && same(result[1],base[1]), "Model bypass did not preserve input");
    std::puts("PASS: disabled/unity identity, zero restore, skin/scene separation, preview, model bypass (WARP HLSL)");

    // Exercise the exact new carrier shader independently of proprietary DLSS/NR. Both positive
    // and negative RGB changes must survive encoding; neutral must leave the clean raster intact.
    const std::array<Pixel,2> carrierBase {{{2.0f,0.25f,0.75f,0.25f}, {0.10f,1.50f,0.30f,0.75f}}};
    const std::array<Pixel,2> carrierEdit {{{1.0f,0.50f,0.70f,0.25f}, {0.20f,0.25f,0.40f,0.75f}}};
    ctx->UpdateSubresource(original.Get(),0,nullptr,carrierBase.data(),sizeof(carrierBase),0);
    ctx->UpdateSubresource(model.Get(),0,nullptr,carrierEdit.data(),sizeof(carrierEdit),0);
    settings.Mode=DlssNrMode_EncodeResidual; settings.ExposurePreMul=2.0f;
    auto carrier=run();
    expect(carrier[0].r<0.5f && carrier[0].g>0.5f && carrier[1].g<0.5f,
           "Signed residual lost shadow/brightening information");
    ctx->UpdateSubresource(model.Get(),0,nullptr,carrier.data(),sizeof(carrier),0);
    settings.Mode=DlssNrMode_ApplyResidual; result=run();
    expect(same(result[0],carrierEdit[0]) && same(result[1],carrierEdit[1]),
           "Residual roundtrip or original alpha preservation failed");
    const std::array<Pixel,2> neutral {{{0.5f,0.5f,0.5f,1}, {0.5f,0.5f,0.5f,1}}};
    ctx->UpdateSubresource(model.Get(),0,nullptr,neutral.data(),sizeof(neutral),0);
    result=run(); expect(same(result[0],carrierBase[0]) && same(result[1],carrierBase[1]),
                         "Neutral carrier altered the clean raster");
    const std::array<Pixel,2> overshoot {{{-1,2,0.5f,1}, {INFINITY,NAN,0.5f,1}}};
    ctx->UpdateSubresource(model.Get(),0,nullptr,overshoot.data(),sizeof(overshoot),0);
    result=run();
    for (const auto pixel : result)
        expect(std::isfinite(pixel.r) && std::isfinite(pixel.g) && std::isfinite(pixel.b) &&
               pixel.r>=0 && pixel.g>=0 && pixel.b>=0, "Carrier overshoot produced invalid output");
    settings.Mode=DlssNrMode_UnitExposure; result=run();
    expect(result[0].r==1 && result[1].r==1, "Private DLSS exposure is not fixed at one");
    std::puts("PASS: signed residual, shadow/brightening roundtrip, neutral identity, alpha, overshoot, unit exposure");
    // Intermediate NR answers must remain encoded, finite and bounded over a long chain.
    settings.Mode=DlssNrMode_ClampProxy;
    const std::array<Pixel,2> raw {{{-0.2f,1.2f,0.3f,0.25f}, {INFINITY,-INFINITY,NAN,0.75f}}};
    const std::array<Pixel,2> bounded {{{0,1,0.3f,0.25f}, {0.5f,0.5f,0.5f,0.75f}}};
    ComPtr<ID3D11ComputeShader> productionShader;
    check(device->CreateComputeShader(DlssNr_cso,sizeof(DlssNr_cso),nullptr,&productionShader));
    ctx->CSSetShader(productionShader.Get(),nullptr,0);
    ctx->UpdateSubresource(original.Get(),0,nullptr,raw.data(),sizeof(raw),0);
    for (int pass=0; pass<30; ++pass)
    {
        result=run();
        expect(same(result[0],bounded[0]) && same(result[1],bounded[1]),
               "Interpass clamp lost finite RGB, input range, identity or alpha");
        ctx->UpdateSubresource(original.Get(),0,nullptr,result.data(),sizeof(result),0);
    }
    std::puts("PASS: 30 interpass clamps, finite RGB, bounded range, encoded identity and alpha");
    settings = {};
    settings.Width=2; settings.Height=1; settings.Passthrough=1;
    settings.Mode=DlssNrMode_EncodeProxyResidual;
    ctx->UpdateSubresource(original.Get(),0,nullptr,base.data(),sizeof(base),0);
    ctx->UpdateSubresource(model.Get(),0,nullptr,edited.data(),sizeof(edited),0);
    result=run();
    for (int i=0;i<2;++i)
    {
        const float difference=edited[i].r-base[i].r;
        expect(std::abs(result[i].r-(.5f+.5f*difference/(1.0f/64+std::abs(difference))))<.0001f,
               "Private enlargement proxy carrier encoding");
    }
    ctx->UpdateSubresource(model.Get(),0,nullptr,result.data(),sizeof(result),0);
    settings.Mode=DlssNrMode_Resolve; settings.Transfer=2; settings.WhitePoint=1;
    settings.ReversibleMode=2; settings.ApplyModel=1; settings.TransferStrength=settings.ColourStrength=1;
    settings.MaxRatio=2;
    result=run();
    expect(same(result[0],edited[0]) && same(result[1],edited[1]), "DLSS carrier matched reconstruction");
    const std::array<Pixel,2> neutralEnlargement {{{.5f,.5f,.5f,1},{.5f,.5f,.5f,1}}};
    ctx->UpdateSubresource(model.Get(),0,nullptr,neutralEnlargement.data(),sizeof(neutralEnlargement),0);
    result=run();
    expect(same(result[0],base[0]) && same(result[1],base[1]), "Neutral DLSS carrier altered base detail");
    const std::array<Pixel,2> hdrBase {{{2.0f,.3f,4.0f,1},{.02f,1.5f,.4f,1}}};
    ctx->UpdateSubresource(original.Get(),0,nullptr,hdrBase.data(),sizeof(hdrBase),0);
    settings.Passthrough=0;
    result=run();
    expect(same(result[0],hdrBase[0]) && same(result[1],hdrBase[1]), "Neutral DLSS carrier altered HDR base");
    // Model edits in KCD2's pre-tonemap shadows were smaller than one carrier FP16 step.
    // Exercise actual storage rounding between production encode and resolve, in both directions.
    const std::array<Pixel,2> darkBase {{{.0002f,.0002f,.0002f,1},{.0002f,.0002f,.0002f,1}}};
    const std::array<Pixel,2> darkEdit {{{.00035f,.00035f,.00035f,1},{.00008f,.00008f,.00008f,1}}};
    ctx->UpdateSubresource(original.Get(),0,nullptr,darkBase.data(),sizeof(darkBase),0);
    ctx->UpdateSubresource(model.Get(),0,nullptr,darkEdit.data(),sizeof(darkEdit),0);
    settings.Passthrough=1; settings.Mode=DlssNrMode_EncodeProxyResidual;
    result=run();
    for (auto& pixel : result)
        for (float* channel : {&pixel.r,&pixel.g,&pixel.b})
            *channel=DirectX::PackedVector::XMConvertHalfToFloat(
                DirectX::PackedVector::XMConvertFloatToHalf(*channel));
    ctx->UpdateSubresource(model.Get(),0,nullptr,result.data(),sizeof(result),0);
    settings.Mode=DlssNrMode_Resolve;
    result=run();
    for (int i=0;i<2;++i)
        expect(std::abs(result[i].r-darkEdit[i].r)<.00001f &&
               std::abs(result[i].g-darkEdit[i].g)<.00001f &&
               std::abs(result[i].b-darkEdit[i].b)<.00001f,
               "FP16 private enlargement erased shadow brightening or darkening");
    std::puts("PASS: FP16 matched residual preserves shadow brightening and darkening");
    ctx->UpdateSubresource(original.Get(),0,nullptr,base.data(),sizeof(base),0);
    settings = {}; settings.Mode=DlssNrMode_ResizePrivateGuides;
    settings.Width=settings.Height=1; settings.GuideWidth=1; settings.GuideHeight=1;
    settings.DebugView=1; // Select the second depth sample through an active-region offset.
    settings.TransferStrength=settings.ColourStrength=1; settings.CompareSwap=1;
    settings.MvScaleX=2; settings.MvScaleY=3;
    ctx->UpdateSubresource(model.Get(),0,nullptr,edited.data(),sizeof(edited),0);
    result=run();
    expect(std::abs(result[0].r-base[1].r)<.0001f, "Private depth active region resize");
    ctx->CopyResource(readback.Get(),keep.Get());
    D3D11_MAPPED_SUBRESOURCE mapped {};
    check(ctx->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped));
    const auto velocity=*static_cast<const Pixel*>(mapped.pData);
    ctx->Unmap(readback.Get(),0);
    expect(std::abs(velocity.r-edited[1].r*2)<.0001f && std::abs(velocity.g-edited[1].g*3)<.0001f,
           "Private motion active region or pixel scale");
    std::puts("PASS: DLSS proxy carrier, matched reconstruction, neutral identity, depth/motion regions and scale");
    return 0;
} catch (const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
