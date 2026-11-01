#pragma once

#include "menu_dx_base.h"
#include <d3d11.h>

class Menu_Dx11 : public MenuDxBase
{
  private:
    bool _dx11Init = false;
    ID3D11Device* _device = nullptr;

    ID3D11Texture2D* _renderTargetTexture = nullptr;
    ID3D11RenderTargetView* _renderTargetView = nullptr;
    ID3D11Resource* _renderTargetOutput = nullptr;

    void CreateRenderTarget(ID3D11Resource* out);

  public:
    bool Render(ID3D11DeviceContext* pCmdList, ID3D11Resource* outTexture);

    Menu_Dx11(HWND handle, ID3D11Device* pDevice);

    ~Menu_Dx11();
};
