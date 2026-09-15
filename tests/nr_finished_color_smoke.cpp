// WARP executes the HDR conversion HLSL. No game or NVIDIA model is loaded.
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <DirectXPackedVector.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <cstddef>
#include <limits>
#include "../OptiScaler/shaders/dlssnr/DlssNr_Common.h"


using Microsoft::WRL::ComPtr;
struct Pixel
{
    float r, g, b, a;
};
void check(HRESULT hr)
{
    if (FAILED(hr))
        throw std::runtime_error("D3D call failed");
}
void expect(bool ok, const char* why)
{
    if (!ok)
        throw std::runtime_error(why);
}
bool closeFloat(float a, float b) { return std::abs(a - b) < 0.0001f; }
int main()
try
{
    static_assert(sizeof(DlssNrConstants) == 256);
    static_assert(offsetof(DlssNrConstants, ResidualBlend) == 116);
    static_assert(offsetof(DlssNrConstants, ResidualMotionBaseY) == 128);
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> ctx;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr,
                            &ctx));
    ComPtr<ID3D11ComputeShader> shader;
    ComPtr<ID3DBlob> code, errors;
    HRESULT compiled = D3DCompileFromFile(L"OptiScaler/shaders/dlssnr/precompile/dlssnr_finished_color.hlsl", nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE, "CSMain", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (errors) std::fputs((const char*)errors->GetBufferPointer(), stderr);
    check(compiled);
    check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
    auto run = [&](const DlssNrConstants& c, const std::vector<Pixel>& base, const std::vector<Pixel>& model,
                   const std::vector<Pixel>& history, const std::vector<Pixel>& motion)
    {
        std::vector<ComPtr<ID3D11Texture2D>> textures;
        std::vector<ComPtr<ID3D11ShaderResourceView>> views;
        for (const auto* pixels : { &base, &model, &history, &motion })
        {
            D3D11_TEXTURE2D_DESC d {};
            d.Width = (UINT) pixels->size();
            d.Height = d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
            d.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data { pixels->data(), (UINT) (pixels->size() * sizeof(Pixel)), 0 };
            ComPtr<ID3D11Texture2D> texture;
            check(device->CreateTexture2D(&d, &data, &texture));
            ComPtr<ID3D11ShaderResourceView> view;
            check(device->CreateShaderResourceView(texture.Get(), nullptr, &view));
            textures.push_back(texture);
            views.push_back(view);
        }
        D3D11_TEXTURE2D_DESC d {};
        d.Width = c.Width;
        d.Height = d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
        d.Format = c.Mode == 5 ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT;
        d.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> target, readback;
        check(device->CreateTexture2D(&d, nullptr, &target));
        ComPtr<ID3D11UnorderedAccessView> uav;
        check(device->CreateUnorderedAccessView(target.Get(), nullptr, &uav));
        d.BindFlags = 0;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        check(device->CreateTexture2D(&d, nullptr, &readback));
        D3D11_BUFFER_DESC bd {};
        bd.ByteWidth = sizeof(c);
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial { &c, 0, 0 };
        ComPtr<ID3D11Buffer> cb;
        check(device->CreateBuffer(&bd, &initial, &cb));
        D3D11_SAMPLER_DESC sd {};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;
        check(device->CreateSamplerState(&sd, &sampler));
        ID3D11ShaderResourceView* srvs[] = { views[0].Get(), views[1].Get(), views[2].Get(), views[3].Get() };
        ctx->CSSetShader(shader.Get(), nullptr, 0);
        ctx->CSSetShaderResources(0, 4, srvs);
        ctx->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);
        ctx->CSSetConstantBuffers(0, 1, cb.GetAddressOf());
        ctx->CSSetSamplers(0, 1, sampler.GetAddressOf());
        ctx->Dispatch((c.Width + 7) / 8, 1, 1);
        ctx->ClearState();
        ctx->CopyResource(readback.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped {};
        check(ctx->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        auto* first = (Pixel*) mapped.pData;
        std::vector<Pixel> result(c.Width);
        if (c.Mode == 5)
        {
            auto* half = (const uint16_t*) mapped.pData;
            for (unsigned i = 0; i < c.Width; ++i)
                result[i] = {DirectX::PackedVector::XMConvertHalfToFloat(half[i*4]),
                    DirectX::PackedVector::XMConvertHalfToFloat(half[i*4+1]),
                    DirectX::PackedVector::XMConvertHalfToFloat(half[i*4+2]),
                    DirectX::PackedVector::XMConvertHalfToFloat(half[i*4+3])};
        }
        else result.assign(first, first + c.Width);
        ctx->Unmap(readback.Get(), 0);
        return result;
    };
    // Independent reference points: ST.2084 code values for 0, 80, 1000 and 10000 nits.
    std::vector<Pixel> pq = { {0,0,0,0.1f}, {0.4858567654f,0.4858567654f,0.4858567654f,0.2f},
        {0.7518270962f,0.7518270962f,0.7518270962f,0.3f}, {1,1,1,0.4f},
        {0.7f,0.1f,0.2f,0.5f}, {0.2f,0.8f,0.1f,0.6f}, {0.1f,0.2f,0.9f,0.7f} };
    DlssNrConstants c {}; c.Width = (unsigned)pq.size(); c.Height = 1;
    auto decoded = run(c, pq, pq, pq, pq);
    const float reference[] = {0,1,12.5f,125};
    for(unsigned i=0;i<4;++i)
        expect(std::abs(decoded[i].r-reference[i]) < 0.005f, "PQ reference luminance wrong");
    expect(decoded[5].r < 0, "Wide-gamut negative component was clipped");
    c.Mode=1;
    auto encoded = run(c, decoded, pq, pq, pq);
    for(unsigned i=0;i<pq.size();++i)
    {
        expect(std::abs(encoded[i].r-pq[i].r) < 0.0002f && std::abs(encoded[i].g-pq[i].g) < 0.0002f &&
               std::abs(encoded[i].b-pq[i].b) < 0.0002f, "HDR10 round trip changed colour");
        expect(encoded[i].a == pq[i].a, "Alpha changed");
    }
    // Model brightening stays HDR: a 1000-nit neutral becomes 2000 nits, not SDR white.
    decoded[2] = {25,25,25,1};
    encoded = run(c, decoded, pq, pq, pq);
    expect(std::abs(encoded[2].r-0.8274246449f) < 0.0002f, "HDR highlight headroom lost");
    // Pre-SR residual transfer: same relative light change in SDR, scRGB and HDR10.
    c.Width = 3; c.WhitePoint = 1; c.TransferStrength = 1; c.MaxRatio = 2;
    const std::vector<Pixel> clean(3, {1,1,1,1});
    const std::vector<Pixel> carrier = {{0.625f,0.625f,0.625f,1}, {0.375f,0.375f,0.375f,1}, {0.5f,0.5f,0.5f,1}};
    std::vector<Pixel> final(3, {0.5f,0.5f,0.5f,0.375f});
    c.Mode = 2;
    auto applied = run(c, final, clean, carrier, clean);
    expect(std::abs(applied[0].r - 0.5f*std::pow(2.0f,1.0f/2.2f)) < 0.0001f, "SDR gain used encoded light");
    expect(std::abs(applied[1].r - 0.5f*std::pow(0.5f,1.0f/2.2f)) < 0.0001f, "Negative residual lost");
    expect(applied[2].r == final[2].r && applied[0].a == final[0].a, "Neutral residual or alpha changed");
    c.Mode = 3; final.assign(3, {12.5f,12.5f,12.5f,0.375f});
    applied = run(c, final, clean, carrier, clean);
    expect(closeFloat(applied[0].r,25) && closeFloat(applied[1].r,6.25f), "scRGB residual lost HDR range");
    c.Mode = 4; final.assign(3, pq[2]);
    applied = run(c, final, clean, carrier, clean);
    expect(std::abs(applied[0].r-0.8274246449f) < 0.0002f, "HDR10 residual applied to PQ code values");
    expect(applied[2].r == final[2].r && applied[2].a == final[2].a, "Neutral HDR10 residual was not exact");
    // Exposure-normalized carrier must reconstruct the same gain at another scene exposure.
    c.WhitePoint = 4;
    auto exposed = run(c, final, std::vector<Pixel>(3,{4,4,4,1}), carrier, clean);
    expect(closeFloat(exposed[0].r,applied[0].r), "Residual transfer changed with scene exposure");
    auto invalid = carrier; invalid[0].r = std::numeric_limits<float>::quiet_NaN();
    auto guarded = run(c, final, clean, invalid, clean);
    expect(guarded[0].r == final[0].r, "Invalid residual changed the picture");
    c.Mode = 5; c.WhitePoint = 1; c.TransferStrength = 0; c.MaxRatio = 4;
    auto gainCarrier = run(c, std::vector<Pixel>(3,{0.5f,0.5f,0.5f,1}),
        std::vector<Pixel>(3,{0.75f,0.75f,0.75f,1}), clean, clean);
    c.Mode = 2;
    applied = run(c, std::vector<Pixel>(3,{0.4f,0.4f,0.4f,1}), clean, gainCarrier, clean);
    expect(std::abs(applied[0].r-0.6f) < 0.001f, "LDR relative change was treated as linear");
    // Actual FP16 storage must preserve a 10% edit to a very dark scene value.
    c.Mode = 5; c.TransferStrength = 1; c.MaxRatio = 2;
    gainCarrier = run(c, std::vector<Pixel>(3,{0.0002f,0.0002f,0.0002f,1}),
        std::vector<Pixel>(3,{0.00022f,0.00022f,0.00022f,1}), clean, clean);
    c.Mode = 3;
    applied = run(c, std::vector<Pixel>(3,{1,1,1,0.3f}), clean, gainCarrier, clean);
    expect(std::abs(applied[0].r-1.1f) < 0.003f, "FP16 carrier destroyed a dark-scene edit");
    auto ringing = std::vector<Pixel>{{-1,-1,-1,1},{2,2,2,1},{0.5f,0.5f,0.5f,1}};
    applied = run(c, std::vector<Pixel>(3,{1,1,1,1}), clean, ringing, clean);
    expect(closeFloat(applied[0].r,0.5f) && closeFloat(applied[1].r,2) && closeFloat(applied[2].r,1),
           "Carrier ringing escaped the symmetric gain bounds");
    // Known, independent scene -> display transform. Compare the transferred edit with
    // actually processing the edited scene through the transform (not with the shader's own formula).
    constexpr unsigned samples = 1024;
    std::vector<Pixel> scene(samples), display(samples), editedDisplay(samples), edits(samples);
    const auto tone = [](float x) { return 12.5f * x / (1.0f + x); };
    for (unsigned i = 0; i < samples; ++i)
    {
        float s = std::exp2(-10.0f + 20.0f * i / (samples - 1));
        scene[i] = {s,s,s,0.4f};
        display[i] = {tone(s),tone(s),tone(s),0.4f};
        editedDisplay[i] = {tone(s*1.2f),tone(s*1.2f),tone(s*1.2f),0.4f};
        float gain = 0.5f + std::log2(1.2f) / 8.0f;
        edits[i] = {gain,gain,gain,1};
    }
    c = {}; c.Mode=6; c.Width=kDlssNrHdrCurveBins; c.Height=1; c.WhitePoint=1;
    auto curve = run(c, display, scene, clean, clean);
    expect(curve[28].b > 0.5f, "Known tone curve did not obtain a confident fit");
    c.Mode=3; c.Width=samples; c.MaxRatio=2;
    const auto oldTransfer = run(c, display, scene, edits, curve);
    c.Mode=8;
    const auto matched = run(c, display, scene, edits, curve);
    double oldError=0, newError=0;
    for (unsigned i=0; i<samples; ++i)
    {
        if (scene[i].r >= 0.1f && scene[i].r <= 64.0f)
        {
            oldError += std::abs(oldTransfer[i].r-editedDisplay[i].r);
            newError += std::abs(matched[i].r-editedDisplay[i].r);
        }
        expect(std::isfinite(matched[i].r) && matched[i].a == display[i].a,
               "Matched transfer changed alpha or produced invalid light");
    }
    printf("Known HDR shoulder: total absolute error %.5f -> %.5f\n",oldError,newError);
    expect(newError < oldError * 0.2, "Response fitting did not substantially reduce highlight transfer error");
    auto darkEdits=edits;
    for(auto& p:darkEdits) p.r=p.g=p.b=0.5f+std::log2(0.8f)/8.0f;
    auto darkMatch=run(c,display,scene,darkEdits,curve);
    double darkOldError=0, darkNewError=0;
    for(unsigned i=0;i<samples;++i)
        if(scene[i].r>=0.1f && scene[i].r<=64.0f)
        {
            darkOldError+=std::abs(display[i].r*0.8f-tone(scene[i].r*0.8f));
            darkNewError+=std::abs(darkMatch[i].r-tone(scene[i].r*0.8f));
        }
    expect(darkNewError<darkOldError*0.2,"Fitted transfer failed for darkening edits");
    auto colourEdits=std::vector<Pixel>(samples,{0.625f,0.375f,0.5f,1});
    auto colourMatch=run(c,display,scene,colourEdits,curve);
    for(unsigned i=0;i<samples;++i)
        expect(colourMatch[i].r<=display[i].r*2.0001f && colourMatch[i].g>=display[i].g*0.4999f,
               "Luminance matching escaped the RGB gain bounds");
    // Flat scenes cannot identify a curve: preserve the previous transfer exactly.
    c.Mode=6; c.Width=kDlssNrHdrCurveBins;
    auto flatCurve = run(c, clean, clean, clean, clean);
    for (const auto& bin : flatCurve) expect(bin.b == 0, "Flat scene invented a response");
    c.Mode=8; c.Width=samples;
    auto fallback = run(c, display, scene, edits, flatCurve);
    for (unsigned i=0; i<samples; ++i)
        expect(closeFloat(fallback[i].r,oldTransfer[i].r), "Unreliable curve failed to use existing transfer");
    // An overlay that does not match the scene-to-display fit must use local fallback.
    auto overlay = display; overlay[samples/2] = {100,100,100,0.6f};
    c.Mode=3; auto overlayOld=run(c,overlay,scene,edits,curve);
    c.Mode=8; auto overlayNew=run(c,overlay,scene,edits,curve);
    expect(closeFloat(overlayNew[samples/2].r,overlayOld[samples/2].r), "Mismatched overlay trusted the curve");
    auto neutral = std::vector<Pixel>(samples,{0.5f,0.5f,0.5f,1});
    auto unchanged=run(c,display,scene,neutral,curve);
    for (unsigned i=0; i<samples; ++i)
        expect(unchanged[i].r == display[i].r, "Zero edit was not an exact bypass");
    // Pre-exposure changes the buffer's numbers, not the fitted physical response.
    auto exposedScene=scene;
    for (auto& p: exposedScene) {p.r*=4; p.g*=4; p.b*=4;}
    c.Mode=6; c.Width=kDlssNrHdrCurveBins; c.WhitePoint=4;
    auto exposedCurve=run(c,display,exposedScene,clean,clean);
    c.Mode=8; c.Width=samples;
    auto exposedMatch=run(c,display,exposedScene,edits,exposedCurve);
    for (unsigned i=0; i<samples; ++i)
        expect(std::abs(exposedMatch[i].r-matched[i].r)<0.002f, "Fitted transfer depended on pre-exposure");
    // Consistent curve histories smooth; a large response change resets immediately.
    auto historyCurve=curve;
    for(auto& p:historyCurve) p.r+=0.1f;
    c.Mode=6; c.Width=kDlssNrHdrCurveBins; c.WhitePoint=1; c.DebugView=1; c.ColourStrength=0.35f;
    auto smoothed=run(c,display,scene,historyCurve,clean);
    expect(std::abs(smoothed[28].r-(curve[28].r+0.065f))<0.002f,"Consistent fit history did not smooth");
    for(auto& p:historyCurve) p.r+=3.0f;
    auto resetCurve=run(c,display,scene,historyCurve,clean);
    expect(closeFloat(resetCurve[28].r,curve[28].r),"Changed response retained stale history");
    // Fit and apply to PQ samples too, using the same linear-light reference.
    c={}; c.Mode=1; c.Width=samples; c.Height=1;
    auto displayPq=run(c,display,scene,display,clean);
    auto expectedPq=run(c,editedDisplay,scene,display,clean);
    c.Mode=7; c.Width=kDlssNrHdrCurveBins; c.WhitePoint=1;
    auto pqCurve=run(c,displayPq,scene,clean,clean);
    c.Mode=9; c.Width=samples; c.MaxRatio=2;
    auto matchedPq=run(c,displayPq,scene,edits,pqCurve);
    double pqError=0;
    for(unsigned i=0;i<samples;++i)
        if(scene[i].r>=0.1f && scene[i].r<=64.0f) pqError+=std::abs(matchedPq[i].r-expectedPq[i].r);
    expect(pqError < 1.0,"HDR10 fitted transfer did not follow the reference tone curve");
    puts("PASS: fitted HDR shoulder, local/flat fallback, neutral bypass, exposure, history and PQ transfer");
    std::puts("PASS: PQ reference luminance, HDR10 round trip, wide gamut, alpha, 2000-nit highlight (WARP)");
    std::puts("PASS: pre-SR residual transfer in SDR/scRGB/HDR10, signed edits, neutral bypass, exposure, invalid input");
    return 0;
}
catch (const std::exception& e)
{
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
}
