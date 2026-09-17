#pragma once

#include <cstdint>
#include <optional>
#include <string>

class Config;

namespace DlssNr
{
enum class Backend
{
    Dx12,
    Vulkan
};

struct ExposureStatus
{
    unsigned long long seenFrames = 0;
    bool offeredNow = false;
    bool everOffered = false;
    float exposure = 0.0f;
    float preExposure = 1.0f;
};

// Menu telemetry contains values only. GPU resources remain owned by the shader instance.
struct StatusSnapshot
{
    bool running = false;
    std::string failureReason;
    std::optional<double> gpuTime;
    unsigned long long frames = 0;
    ExposureStatus exposure;
    bool captureInProgress = false;
};

struct ControlRequests
{
    uint64_t retryGeneration = 0;
    uint64_t captureGeneration = 0;
    unsigned int captureFrames = 0;
};

void PublishStatus(const void* owner, Backend backend, const StatusSnapshot& status);
void ClearStatus(const void* owner);
ControlRequests ReadControlRequests();

void RenderMenu(::Config* config, float menuResScale);
void RetryAfterFailure();
bool IsRunning();
const char* FailureReason();
ExposureStatus GameExposureStatus();
std::optional<double> LastGpuTime();
void RequestCapture(unsigned int frames);
bool CaptureInProgress();

bool IsRunningVk();
const char* FailureReasonVk();
unsigned long long FramesVk();
std::optional<double> LastGpuTimeVk();
bool ExposureOfferedVk();
} // namespace DlssNr
