#pragma once

#include <framegen/IFGFeature.h>

#include <proxies/Streamline_Proxy.h>

#include <framegen/xefg/XeFG_DynamicMFG.h>

// The Streamline side of the DLSSG output, shared by the D3D12 and Vulkan ones
class DLSSG_Common : public virtual IFGFeature
{
  protected:
    sl::ViewportHandle _viewport { 0 };
    sl::FrameToken* _frameToken = nullptr;

    // Streamline's own max, over _maxInterpolationCount when an nvngx provider generates fewer frames
    int _streamlineMaxInterpolationCount = 1;

    // Dynamic MFG in software, for a provider without Streamline's own Dynamic mode: the count asked of Streamline
    // follows the target FPS, between 2X and the Frame Gen setting. No 1X, DLSSG's off is its mode and that rebuilds
    // its swapchain.
    XeFG_DynamicMFG _dynamic;

    // Max interpolation count and DMFG support, once the swapchain exists
    void QueryState();

    // DLSSG and Reflex options for the next present, Reflex optimizes with the game's markers when it has them
    // window: the game's window, Dynamic MFG takes the refresh rate and the focus from it
    void SetOptions(bool gameMarkersReachStreamline, HWND window = nullptr);

    // Fills _frameToken and sends the frame's constants, pauses FG when Streamline refuses them
    bool SetConstants(int fIndex, uint32_t frameId, uint32_t mvWidth, uint32_t mvHeight);

    void PauseAfterError();

  public:
    // IFGFeature
    const char* Name() override final { return "DLSSG"; }
    feature_version Version() override final;
    void Activate() override final;
    void Deactivate() override final;
    bool SetInterpolatedFrameCount(UINT interpolatedFrameCount) override final { return true; }
};
