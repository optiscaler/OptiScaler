#pragma once
#include "MenuBlur_Common.h"

#include <d3d11.h>
#include <shaders/Shader_Dx11.h>
#include <imgui/imgui.h>

// Blurs the swapchain image for the background of the overlay menu
class MenuBlur_Dx11 : public Shader_Dx11, public MenuBlur_Common
{
  private:
    // Back buffers usually can't be bound as SRV, blur reads from a copy
    ID3D11Texture2D* _copy = nullptr;
    ID3D11ShaderResourceView* _copySrv = nullptr;

    // Final result is always in A
    ID3D11Texture2D* _blurA = nullptr;
    ID3D11Texture2D* _blurB = nullptr;
    ID3D11ShaderResourceView* _blurASrv = nullptr;
    ID3D11ShaderResourceView* _blurBSrv = nullptr;
    ID3D11UnorderedAccessView* _blurAUav = nullptr;
    ID3D11UnorderedAccessView* _blurBUav = nullptr;

    ID3D11SamplerState* _sampler = nullptr;

    // Part of the blurred texture covered by the back buffer
    ImVec2 _uvScale { 1.0f, 1.0f };

    void DispatchPass(ID3D11DeviceContext* InContext, ID3D11ShaderResourceView* InSrv,
                      ID3D11UnorderedAccessView* OutUav, BlurMode InMode, float InSpacing, UINT InWidth, UINT InHeight);
    void ReleaseTextures();

    static DXGI_FORMAT CopyFormat(DXGI_FORMAT InFormat);
    static DXGI_FORMAT CopySrvFormat(DXGI_FORMAT InFormat);

  public:
    // Makes sure blur textures match the back buffer, false if the back buffer can't be blurred
    bool Prepare(ID3D11Texture2D* InBackBuffer);

    // Restores the compute shader bindings it changes
    bool Dispatch(ID3D11DeviceContext* InContext, ID3D11Texture2D* InBackBuffer);

    ImTextureID TextureId() const { return (ImTextureID) _blurASrv; }
    ImVec2 UVScale() const { return _uvScale; }

    MenuBlur_Dx11(std::string InName, ID3D11Device* InDevice);

    ~MenuBlur_Dx11();
};
