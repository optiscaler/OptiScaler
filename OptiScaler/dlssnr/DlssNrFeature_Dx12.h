#pragma once

#include "DlssNr_Status.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <string>
#include <shaders/dlssnr/DlssNr_Common.h>
#include <nvsdk_ngx.h>

namespace DlssNr
{
inline constexpr unsigned int MaxPassCount = 30;
inline constexpr unsigned int DefaultMaxPassCount = 3;

// Public callbacks route through registered upscaler owners. They do not own GPU state.
std::string FinishedPictureStatus();
bool WaitForFinishedPicture();
void FinishedPictureResetCommandList(ID3D12CommandList* cmd);
void FinishedPictureSubmitted(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists);
void ApplyToFinishedPicture(IDXGISwapChain* swapchain, ID3D12CommandQueue* queue);
void ApplyToStreamlinePicture(IDXGISwapChain* swapchain, ID3D12Resource* picture, ID3D12CommandQueue* queue);
void ApplyToFinishedPictureDx11(IDXGISwapChain* swapchain);
void FinishedPictureColorSpace(IDXGISwapChain* swapchain, DXGI_COLOR_SPACE_TYPE colorSpace);

// Suggested exposure calibration and steadiness; the user chooses whether to apply it.
struct CalibrationReading
{
    float suggestion = 0.0f;
    float steadiness = 0.0f;
    unsigned long long samples = 0;
    bool usable = false;
    const char* why = "";
};
CalibrationReading Calibration();
std::string DeferredDlssStatus();
void Shutdown();
} // namespace DlssNr
