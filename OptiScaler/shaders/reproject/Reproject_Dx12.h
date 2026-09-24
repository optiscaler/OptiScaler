#pragma once

#include "SysUtils.h"

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <dxgi1_6.h>
#include <shaders/Shader_Dx12Utils.h>
#include <shaders/Shader_Dx12.h>
#include <DirectXMath.h>

#define Reproject_NUM_OF_HEAPS 3

struct alignas(16) ReprojectionParams
{
    uint32_t ScreenWidth;
    uint32_t ScreenHeight;
    float InvScreenWidth;
    float InvScreenHeight;

    float UiDiffThreshold;
    float DepthCutoff;
    float DitherWidthPx;
    uint32_t CutoffExpandPx;

    uint32_t EdgeMode;
    uint32_t ShowStaticElements;
    uint32_t InvertedDepth;
    float Pad1;

    float TanHalfFovX;
    float TanHalfFovY;
    float InvTanHalfFovX;
    float InvTanHalfFovY;

    // Inverse reprojection rotation matrix
    DirectX::XMFLOAT4 ReprojectionRow0;
    DirectX::XMFLOAT4 ReprojectionRow1;
    DirectX::XMFLOAT4 ReprojectionRow2;
};

struct FilloutData
{
    float diffThreshold;
    uint32_t screenWidth;
    uint32_t screenHeight;
    bool invertedDepth;

    float cameraVFov;
    float cameraAspectRatio;
    float cameraUp[3];
    float cameraRight[3];
    float cameraForward[3];
    float cameraPrevForward[3];

    DirectX::XMINT2 mouseDeltaSinceSim; // for reprojection
    DirectX::XMINT2 mouseDeltaSimToSim; // for calibration
};

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

class Reproject_Dx12 : public Shader_Dx12
{
  private:
    FrameDescriptorHeap _frameHeaps[Reproject_NUM_OF_HEAPS];

    ID3D12Resource* _buffer[Reproject_NUM_OF_HEAPS] = {};

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

    CalibrationState _calibration;
    bool _isFirstFrame = true;

    static void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);

  public:
    void FilloutStruct(const FilloutData& data, ReprojectionParams& params);

    bool Dispatch(IDXGISwapChain3* sc, ID3D12GraphicsCommandList* cmdList, ReprojectionParams& params,
                  ID3D12Resource* hudless, D3D12_RESOURCE_STATES state, ID3D12Resource* depth,
                  D3D12_RESOURCE_STATES depthState);

    Reproject_Dx12(std::string InName, ID3D12Device* InDevice);

    ~Reproject_Dx12();
};
