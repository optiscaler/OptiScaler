#pragma once
#include <dlssnr/DlssNr_Upscaler.h>
#include <d3d12.h>
#include <memory>
#include <array>
#include <string>

struct NVSDK_NGX_Parameter;

namespace DlssNr
{
struct PrivateUpscalerCreateDx12
{
    unsigned width = 0, height = 0, outputWidth = 0, outputHeight = 0;
    int quality = 0;
    bool depthInverted = false, jitteredMotion = false, lowResolutionMotion = true;
    bool rayReconstruction = false;
    unsigned roughnessMode = 0, hardwareDepth = 1;
};
struct PrivateUpscalerResourceDx12
{
    ID3D12Resource* resource = nullptr;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
};
struct PrivateRrInputsDx12
{
    // Albedo, specular albedo, normals, roughness, reflection motion, specular/diffuse hit distance.
    std::array<PrivateUpscalerResourceDx12, 7> guides {};
    std::array<unsigned, 7> baseX {}, baseY {};
    std::array<float, 16> worldToView {}, viewToClip {};
    unsigned roughnessMode = 0, hardwareDepth = 1;
    bool matrices = false, valid = false;
};
// Borrowed resources, zero colour/depth/motion offsets, explicit active extents.
// RR guides carry their own offsets. States are restored after evaluation.
// Exposure must be a unit-valued texture, with no sharpening or auto exposure.
// RR uses linear HDR mode for its bounded float carrier; SR retains LDR mode.
struct PrivateUpscalerFrameDx12
{
    PrivateUpscalerResourceDx12 color, depth, motion, exposure;
    PrivateUpscalerResourceDx12 output { nullptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
    unsigned width = 0, height = 0, outputWidth = 0, outputHeight = 0;
    float jitterX = 0, jitterY = 0, motionScaleX = 1, motionScaleY = 1, frameTimeMs = 16.67f;
    float cameraNear = 0.1f, cameraFar = 1000.0f, cameraFovVertical = 1.04719755f, viewSpaceToMeters = 1;
    bool reset = false;
    PrivateRrInputsDx12 rr;
};
// One independent history per instance; never falls back to another backend.
// Init is single-use. The owning Generation must prove GPU completion before destruction,
// including after failed creation or evaluation (which can still record commands).
class PrivateUpscalerDx12
{
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    explicit PrivateUpscalerDx12(PrivateUpscaler selected);
    ~PrivateUpscalerDx12();
    PrivateUpscalerDx12(const PrivateUpscalerDx12&) = delete;
    PrivateUpscalerDx12& operator=(const PrivateUpscalerDx12&) = delete;
    bool Init(ID3D12Device* device, ID3D12GraphicsCommandList* cmd, const PrivateUpscalerCreateDx12& info);
    bool Evaluate(ID3D12GraphicsCommandList* cmd, const PrivateUpscalerFrameDx12& frame);
    static PrivateRrInputsDx12 ReadRrInputs(NVSDK_NGX_Parameter* source, unsigned width, unsigned height);
    const char* Name() const;
    const std::string& Error() const;
};
} // namespace DlssNr
