// WARP executes the shipped residual shader. No game or NVIDIA model is loaded.
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <cstddef>
#include "../OptiScaler/shaders/dlssnr/DlssNr_Common.h"
#include "../OptiScaler/shaders/dlssnr/DlssNr_ResidualPair.h"
#include "../OptiScaler/shaders/dlssnr/precompile/dlssnr_residual_Shader.h"
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
    DlssNrResidualPair pair;
    int cmd, params, output, other;
    expect(!pair.Take(&cmd, &params, &output), "Unarmed residual consumed");
    pair.Arm(&cmd, &params, &output);
    expect(pair.Take(&cmd, &params, &output), "Matching residual dropped");
    expect(!pair.Take(&cmd, &params, &output), "Residual consumed twice");
    pair.Arm(&cmd, &params, &output);
    pair.Cancel(); // next pre-seam, even if that frame skips NR
    expect(!pair.Take(&cmd, &params, &output), "Skipped frame reused old residual");
    pair.Arm(&cmd, &params, &output);
    expect(!pair.Take(&cmd, &params, &other), "Different output accepted");
    expect(!pair.Take(&cmd, &params, &output), "Mismatch was not consumed");

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> ctx;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr,
                            &ctx));
    ComPtr<ID3D11ComputeShader> shader;
    check(device->CreateComputeShader(dlssnr_residual_cso, sizeof(dlssnr_residual_cso), nullptr, &shader));
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
        d.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
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
        std::vector<Pixel> result(first, first + c.Width);
        ctx->Unmap(readback.Get(), 0);
        return result;
    };
    std::vector<Pixel> base(2, { 10, 10, 10, 0.4f }), model(2, { 12, 8, 11, 1 }), history(2, { 1, 2, 3, 1 }),
        motion(2, { 0, 0, 0, 0 });
    DlssNrConstants c {};
    c.Width = c.GuideWidth = 2;
    c.Height = c.GuideHeight = 1;
    c.MvScaleX = 0.5f;
    c.MvScaleY = 1;
    c.ResidualBlend = 0.25f;
    auto result = run(c, base, model, history, motion);
    expect(closeFloat(result[0].r, 0.5f) && closeFloat(result[0].g, -0.5f), "Cold history or signed residual wrong");
    c.ResidualHistoryValid = 1;
    result = run(c, base, model, history, motion);
    expect(closeFloat(result[0].r, 1.25f) && closeFloat(result[0].g, 1) && closeFloat(result[0].b, 2.5f),
           "Accumulation wrong");
    // The restored host path composes at INPUT resolution, then lets the existing private
    // upscaler enlarge its encoded difference. No extra strength or spatial enlargement here.
    const auto accumulated = result;
    DlssNrConstants compose {};
    compose.Mode = DlssNrResidualMode_Apply;
    compose.Width = 2; compose.Height = 1; compose.TransferStrength = 1;
    result = run(compose, base, accumulated, history, motion);
    expect(closeFloat(result[0].r, 11.25f) && closeFloat(result[0].g, 11) &&
           closeFloat(result[0].b, 12.5f) && result[0].a == base[0].a,
           "Accumulated edit was not preserved for private DLSS encoding");
    c.ResidualHistoryValid = 0;
    c.ResidualBlend = .08f;
    result = run(c, base, model, accumulated, motion);
    expect(closeFloat(result[0].r, .16f) && closeFloat(result[0].g, -.16f),
           "Reset did not discard previous residual history at the release default blend");
    c.ResidualHistoryValid = 1;
    c.ResidualBlend = 0;
    c.ResidualMotionBaseX = 1;
    history = { { 1, 1, 1, 1 }, { 3, 3, 3, 1 } };
    motion = { { 99, 0, 0, 0 }, { 1, 0, 0, 0 }, { 99, 0, 0, 0 }, { 99, 0, 0, 0 } };
    result = run(c, base, model, history, motion);
    expect(closeFloat(result[0].r, 3) && closeFloat(result[1].r, 0), "MV subrect / invalid motion handling wrong");
    c.Mode = DlssNrResidualMode_Apply;
    c.Width = 4;
    c.TransferStrength = 0.5f;
    base = { { 10, 10, 10, 0.1f }, { 10, 10, 10, 0.2f }, { 10, 10, 10, 0.3f }, { 10, 10, 10, 0.4f } };
    model = { { -2, -2, -2, 1 }, { 2, 2, 2, 1 } };
    result = run(c, base, model, history, motion);
    const float expected[] = { 9, 9.5f, 10.5f, 11 };
    for (unsigned i = 0; i < 4; ++i)
        expect(closeFloat(result[i].r, expected[i]) && result[i].a == base[i].a, "Signed upscale or alpha wrong");
    c.TransferStrength = 0;
    result = run(c, base, model, history, motion);
    for (unsigned i = 0; i < 4; ++i)
        expect(result[i].r == base[i].r && result[i].a == base[i].a, "Zero strength changed output");
    std::puts("PASS: residual seams, cold/warm/reset history, motion reprojection, input-resolution composition, alpha");
    return 0;
}
catch (const std::exception& e)
{
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
}
