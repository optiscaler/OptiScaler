#include <pch.h>
#include "DlssNr_Status.h"

#include <array>
#include <mutex>

namespace DlssNr
{
namespace
{
struct PublishedStatus
{
    const void* owner = nullptr; // Identity only; never dereferenced.
    StatusSnapshot value;
};

std::mutex statusMutex;
std::array<PublishedStatus, 2> published;
ControlRequests requests;

StatusSnapshot ReadStatus(Backend backend)
{
    std::lock_guard lock(statusMutex);
    return published[static_cast<size_t>(backend)].value;
}
} // namespace

void PublishStatus(const void* owner, Backend backend, const StatusSnapshot& status)
{
    std::lock_guard lock(statusMutex);
    published[static_cast<size_t>(backend)] = { owner, status };
}

void ClearStatus(const void* owner)
{
    std::lock_guard lock(statusMutex);
    for (auto& status : published)
        if (status.owner == owner)
            status = {};
}

ControlRequests ReadControlRequests()
{
    std::lock_guard lock(statusMutex);
    return requests;
}

void RetryAfterFailure()
{
    std::lock_guard lock(statusMutex);
    ++requests.retryGeneration;
}

void RequestCapture(unsigned int frames)
{
    std::lock_guard lock(statusMutex);
    requests.captureFrames = frames;
    ++requests.captureGeneration;
}

bool IsRunning() { return ReadStatus(Backend::Dx12).running; }
bool IsRunningVk() { return ReadStatus(Backend::Vulkan).running; }

const char* FailureReason()
{
    thread_local std::string reason;
    reason = ReadStatus(Backend::Dx12).failureReason;
    return reason.c_str();
}

const char* FailureReasonVk()
{
    thread_local std::string reason;
    reason = ReadStatus(Backend::Vulkan).failureReason;
    return reason.c_str();
}

ExposureStatus GameExposureStatus() { return ReadStatus(Backend::Dx12).exposure; }
std::optional<double> LastGpuTime() { return ReadStatus(Backend::Dx12).gpuTime; }
std::optional<double> LastGpuTimeVk() { return ReadStatus(Backend::Vulkan).gpuTime; }
unsigned long long FramesVk() { return ReadStatus(Backend::Vulkan).frames; }
bool ExposureOfferedVk() { return ReadStatus(Backend::Vulkan).exposure.offeredNow; }
bool CaptureInProgress() { return ReadStatus(Backend::Dx12).captureInProgress; }
} // namespace DlssNr
