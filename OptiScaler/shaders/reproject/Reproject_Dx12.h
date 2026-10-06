#pragma once

#include "SysUtils.h"

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <dxgi1_6.h>
#include <shaders/Shader_Dx12Utils.h>
#include <shaders/Shader_Dx12.h>
#include <DirectXMath.h>

#include "mouseInputs/InputCollection.h"

#define Reproject_NUM_OF_HEAPS 3

struct alignas(16) ReprojectionParams
{
    uint32_t ScreenWidth;
    uint32_t ScreenHeight;
    float InvScreenWidth;
    float InvScreenHeight;

    uint32_t DepthWidth;
    uint32_t DepthHeight;
    float _Pad0[2];

    float UiDiffThreshold;
    float DepthCutoff;
    float DitherWidthPx;
    uint32_t CutoffExpandPx;

    uint32_t EdgeMode;
    uint32_t ShowStaticElements;
    uint32_t InvertedDepth;
    uint32_t FakeFrame; // need to provide fakePresent

    float TanHalfFovX;
    float TanHalfFovY;
    float InvTanHalfFovX;
    float InvTanHalfFovY;

    // Inputs for the latch pass which builds the reprojection rotation on the GPU
    DirectX::XMFLOAT4 CameraRight;   // xyz, normalized
    DirectX::XMFLOAT4 CameraUp;      // xyz, normalized
    DirectX::XMFLOAT4 CameraForward; // xyz, normalized
    DirectX::XMFLOAT4 Calibration;   // yawFromX, yawFromY, pitchFromX, pitchFromY

    float PixelAngle;
    float YawSign;
    float PitchSign;
    uint32_t LateLatch; // 1 = read mouse from the live buffer when the GPU executes, 0 = use MouseDelta

    DirectX::XMINT2 MouseDelta;    // mouse delta since sim start, captured on the CPU
    DirectX::XMINT2 SimStartMouse; // running mouse total at sim start, for late latching
};

// Written by the CPU on every mouse event, read by the latch pass
struct alignas(16) ReprojectionLiveInput
{
    DirectX::XMINT2 Mouse; // running mouse total, see InputCollection
    uint32_t _Pad0[2];
};

struct FilloutData
{
    float diffThreshold;
    uint32_t screenWidth;
    uint32_t screenHeight;
    uint32_t depthWidth;
    uint32_t depthHeight;
    bool invertedDepth;
    bool fakeFrame;

    float cameraVFov;
    float cameraAspectRatio;
    float cameraUp[3];
    float cameraRight[3];
    float cameraForward[3];
    float cameraPrevForward[3];

    DirectX::XMINT2 mouseDeltaSinceSim; // for reprojection
    DirectX::XMINT2 mouseDeltaSimToSim; // for calibration

    // Late latching, mouseDeltaSinceSim gets replaced by (live mouse - simStartMouse) when the GPU executes
    bool lateLatch = false;
    DirectX::XMINT2 simStartMouse {};
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

    // Late latching
    ID3D12PipelineState* _latchPipeline = nullptr;
    ID3D12Resource* _liveInput = nullptr; // upload heap, persistently mapped, written by the input thread
    void* _liveInputMapped = nullptr;
    ID3D12Resource* _latchedOutput = nullptr; // rotation rows + used mouse delta, written by the latch pass

    ID3D12Resource* _buffer[Reproject_NUM_OF_HEAPS] = {};
    ID3D12Resource* _constantBuffers[Reproject_NUM_OF_HEAPS] = {};

    // Depth cutout mask prepass, separable dilation by CutoffExpandPx
    // Both are kept in D3D12_RESOURCE_STATE_UNORDERED_ACCESS between dispatches
    ID3D12PipelineState* _maskPipelineH = nullptr;
    ID3D12PipelineState* _maskPipelineV = nullptr;
    ID3D12Resource* _depthMaskTemp = nullptr;
    ID3D12Resource* _depthMask = nullptr;

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

    CalibrationState _calibration;
    bool _isFirstFrame = true;

    bool CreateMaskResource(ID3D12Resource** resource, uint32_t width, uint32_t height, const wchar_t* name);

    static void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);

  public:
    void FilloutStruct(const FilloutData& data, ReprojectionParams& params);

    // Leaves present unmodified, new image available via GetCurrentBuffer
    bool Dispatch(ID3D12GraphicsCommandList* cmdList, ReprojectionParams& params, ID3D12Resource* realPresent,
                  D3D12_RESOURCE_STATES realPresentState, ID3D12Resource* fakePresent,
                  D3D12_RESOURCE_STATES fakePresentState, ID3D12Resource* hudless, D3D12_RESOURCE_STATES hudlessState,
                  ID3D12Resource* depth, D3D12_RESOURCE_STATES depthState);

    bool Dispatch(IDXGISwapChain3* sc, ID3D12GraphicsCommandList* cmdList, ReprojectionParams& params,
                  ID3D12Resource* hudless, D3D12_RESOURCE_STATES state, ID3D12Resource* depth,
                  D3D12_RESOURCE_STATES depthState);

    // Can be called only after Dispatch
    // Expected to be in D3D12_RESOURCE_STATE_COPY_SOURCE
    ID3D12Resource* GetCurrentBuffer();

    Reproject_Dx12(std::string InName, ID3D12Device* InDevice);

    ~Reproject_Dx12();
};
