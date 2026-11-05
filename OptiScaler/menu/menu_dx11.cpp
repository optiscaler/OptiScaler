#include "pch.h"
#include "menu_dx11.h"

#include "Config.h"
#include "menu_common.h"
#include <imgui/imgui_impl_dx11.h>
#include <imgui/imgui_impl_win32.h>

void Menu_Dx11::CreateRenderTarget(ID3D11Resource* out)
{
    ID3D11Texture2D* outTexture2D = nullptr;

    if (out->QueryInterface(IID_PPV_ARGS(&outTexture2D)) != S_OK)
        return;

    LOG_FUNC();

    D3D11_TEXTURE2D_DESC outDesc {};
    outTexture2D->GetDesc(&outDesc);
    outTexture2D->Release();

    if (_renderTargetTexture != nullptr)
    {
        D3D11_TEXTURE2D_DESC rtDesc;
        _renderTargetTexture->GetDesc(&rtDesc);

        if (outDesc.Width != rtDesc.Width || outDesc.Height != rtDesc.Height || outDesc.Format != rtDesc.Format)
        {
            _renderTargetTexture->Release();
            _renderTargetTexture = nullptr;
        }
        else
            return;
    }
    // RTV holds a reference to the output, so its address can't be reused meanwhile
    else if (_renderTargetView != nullptr && _renderTargetOutput == out)
    {
        return;
    }

    if (_renderTargetView != nullptr)
    {
        _renderTargetView->Release();
        _renderTargetView = nullptr;
    }

    _renderTargetOutput = nullptr;

    if ((outDesc.BindFlags & D3D11_BIND_RENDER_TARGET) > 0)
    {
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
        ZeroMemory(&rtvDesc, sizeof(rtvDesc));
        rtvDesc.Format = TranslateTypelessFormats(outDesc.Format);
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;

        if (_device->CreateRenderTargetView(out, &rtvDesc, &_renderTargetView) == S_OK)
            _renderTargetOutput = out;
    }
    else
    {
        D3D11_TEXTURE2D_DESC textureDesc;
        ZeroMemory(&textureDesc, sizeof(textureDesc));
        textureDesc.Width = outDesc.Width;
        textureDesc.Height = outDesc.Height;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.Format = outDesc.Format;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
        textureDesc.CPUAccessFlags = 0;
        textureDesc.MiscFlags = 0;

        _device->CreateTexture2D(&textureDesc, nullptr, &_renderTargetTexture);

        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
        ZeroMemory(&rtvDesc, sizeof(rtvDesc));
        rtvDesc.Format = textureDesc.Format;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;

        _device->CreateRenderTargetView(_renderTargetTexture, &rtvDesc, &_renderTargetView);
    }
}

bool Menu_Dx11::Render(ID3D11DeviceContext* pCmdList, ID3D11Resource* outTexture)
{
    if (Config::Instance()->OverlayMenu.value_or_default())
        return false;

    if (pCmdList == nullptr || outTexture == nullptr)
        return false;

    frameCounter++;

    // if (!IsVisible())
    //	return true;

    // LOG_FUNC();

    CreateRenderTarget(outTexture);

    ImGuiIO& io = ImGui::GetIO();
    (void) io;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    if (!_dx11Init && io.BackendRendererUserData == nullptr)
        _dx11Init = ImGui_ImplDX11_Init(_device, pCmdList);

    if (!_dx11Init)
        return false;

    ImGui_ImplDX11_NewFrame();
    // ImGui_ImplWin32_NewFrame();

    if (MenuDxBase::RenderMenu())
    {
        // Create RTV for out
        pCmdList->OMSetRenderTargets(1, &_renderTargetView, nullptr);

        if (_renderTargetTexture == nullptr)
        {
            // Render
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            return true;
        }

        // Copy first
        pCmdList->CopyResource(_renderTargetTexture, outTexture);

        // Render
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Copy result
        pCmdList->CopyResource(outTexture, _renderTargetTexture);
    }

    return true;
}

Menu_Dx11::Menu_Dx11(HWND handle, ID3D11Device* pDevice) : MenuDxBase(handle), _device(pDevice) { Dx11Ready(); }

Menu_Dx11::~Menu_Dx11()
{
    if (!_dx11Init)
        return;

    ImGui_ImplDX11_Shutdown(false);
    MenuCommon::Shutdown();

    // hackzor
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    SAFE_RELEASE(_renderTargetTexture);
    SAFE_RELEASE(_renderTargetView);
}
