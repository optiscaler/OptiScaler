#pragma once

#include <framegen/IFGFeature_Dx12.h>
#include <shaders/reproject/Reproject_Dx12.h>

struct CalibrationState
{
    float Sxx = 0.0f, Sxy = 0.0f, Syy = 0.0f;
    float BxYaw = 0.0f, ByYaw = 0.0f;
    float BxPitch = 0.0f, ByPitch = 0.0f;

    float yawFromX = 1.0f, yawFromY = 0.0f;
    float pitchFromX = 0.0f, pitchFromY = 1.0f;
    int sampleCount = 0;

    void Reset() { *this = CalibrationState {}; }

    void Update(float mx, float my, float camYawDelta, float camPitchDelta)
    {
        if (mx * mx + my * my < 1e-12f)
            return;

        constexpr float DECAY = 0.99f;

        Sxx = Sxx * DECAY + mx * mx;
        Sxy = Sxy * DECAY + mx * my;
        Syy = Syy * DECAY + my * my;

        BxYaw = BxYaw * DECAY + mx * camYawDelta;
        ByYaw = ByYaw * DECAY + my * camYawDelta;
        BxPitch = BxPitch * DECAY + mx * camPitchDelta;
        ByPitch = ByPitch * DECAY + my * camPitchDelta;

        sampleCount++;

        const float det = Sxx * Syy - Sxy * Sxy;
        if (std::abs(det) >= 1e-10f && sampleCount >= 20)
        {
            const float invDet = 1.0f / det;
            yawFromX = std::clamp((BxYaw * Syy - ByYaw * Sxy) * invDet, -1.5f, 1.5f);
            yawFromY = std::clamp((ByYaw * Sxx - BxYaw * Sxy) * invDet, -1.5f, 1.5f);
            pitchFromX = std::clamp((BxPitch * Syy - ByPitch * Sxy) * invDet, -1.5f, 1.5f);
            pitchFromY = std::clamp((ByPitch * Sxx - BxPitch * Sxy) * invDet, -1.5f, 1.5f);
        }
    }
};

class Reprojection_Dx12 : public virtual IFGFeature_Dx12
{
    std::unique_ptr<Reproject_Dx12> _reproject;

    CalibrationState _calibration;
    bool _isFirstFrame = true;

    void FilloutStruct(ReprojectionParams& params, float diffThreshold, uint32_t resX, uint32_t resY, int currIndex,
                       DirectX::XMINT2 direction, DirectX::XMINT2 fullFrameMouseDelta);

  protected:
    void ReleaseObjects() override final;
    void CreateObjects(ID3D12Device* InDevice) override final;

  public:
    // IFGFeature
    const char* Name() override final { return "Reprojection"; };
    feature_version Version() override final { return { 0, 0, 1 }; };
    HWND Hwnd() override final { return _hwnd; };

    // IFGFeature_Dx12
    bool CreateSwapchain(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, DXGI_SWAP_CHAIN_DESC* desc,
                         IDXGISwapChain** swapChain, bool readyToRelease) override final;
    bool CreateSwapchain1(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, HWND hwnd, DXGI_SWAP_CHAIN_DESC1* desc,
                          DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGISwapChain1** swapChain,
                          bool readyToRelease) override final;

    bool ReleaseSwapchain(HWND hwnd) override final;

    void CreateContext(ID3D12Device* device, FG_Constants& fgConstants) override final;
    void Activate() override final;
    void Deactivate() override final;
    void DestroyFGContext() override final;
    bool Shutdown() override final;

    void EvaluateState(ID3D12Device* device, FG_Constants& fgConstants) override final;

    bool Present() override final;

    bool SetResource(Dx12Resource* inputResource) override final;
    void SetCommandQueue(FG_ResourceType type, ID3D12CommandQueue* queue) override final;

    void* FrameGenerationContext() override final;
    void* SwapchainContext() override final;

    Reprojection_Dx12() : IFGFeature_Dx12(), IFGFeature() { _framesToInterpolate = 0; }

    ~Reprojection_Dx12() {};

    // Inherited via IFGFeature_Dx12
    bool SetInterpolatedFrameCount(UINT interpolatedFrameCount) override { return true; };
};
