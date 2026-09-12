#pragma once

// Neural Rendering uses the existing NVIDIA NGX driver dispatcher.

#include <d3d12.h>
#include <memory>
#include <cstdint>

namespace DlssNr
{
namespace Proxy
{
struct Settings
{
    unsigned int preset = 0, style = 0;
    float intensity = 1.0f, localStructure = 1.0f, localTone = 0.0f, skinStructure = -1.0f;
    bool autoMask = true;
    bool operator==(const Settings&) const = default;
};

class Context
{
    struct Impl;
    std::unique_ptr<Impl> _impl;

  public:
    Context();
    ~Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    // True when the driver's nvngx is initialised and exports what this path needs.
    static bool Available();

    // Creation records GPU work. A feature becomes ready only in a later submission epoch.
    unsigned int Prepare(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device, unsigned int width,
                         unsigned int height, const Settings& settings, uint64_t submissionEpoch, bool* ready);
    bool HasFeature() const;
    bool Ready(uint64_t submissionEpoch) const;
    void AdvanceEpoch(uint64_t submissionEpoch);

    // Each pass owns its feature, parameter map and temporal history. False evaluated means no output.
    unsigned int Run(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device, ID3D12Resource* color,
                     ID3D12Resource* depth, ID3D12Resource* motion, ID3D12Resource* output, unsigned int width,
                     unsigned int height, unsigned int guideWidth, unsigned int guideHeight, unsigned int motionWidth,
                     unsigned int motionHeight, unsigned int depthBaseX, unsigned int depthBaseY,
                     unsigned int motionBaseX, unsigned int motionBaseY, bool depthInverted, bool reset, float mvScaleX,
                     float mvScaleY, const Settings& settings, uint64_t submissionEpoch, bool* evaluated = nullptr);

    // Retires the current feature and clears the failure latch without immediately freeing GPU work.
    void RetryAfterFailure();

    void Submitted(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists);
    void ResetRecording(ID3D12CommandList* commands);
    bool Idle();

    // Retires ownership; destruction occurs only after recordings are discarded and GPU work completes.
    // Unresolved ownership is abandoned if this context is destroyed.
    void Release();
};
} // namespace Proxy
} // namespace DlssNr
