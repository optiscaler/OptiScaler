#pragma once
#include "SysUtils.h"
#include <sl.h>
#include <sl_dlss_g.h>

class IFGFeature;

// DLSSG inputs for both the DX12 and Vulkan FG outputs
class Sl_Inputs
{
  private:
    bool infiniteDepth = false;
    sl::EngineType engineType = sl::EngineType::eCount;
    std::atomic<bool> _gameFGOff = false;
    std::atomic<bool> _inputsUntagged = false;

    std::mutex _frameBoundaryMutex;
    bool _isFrameFinished = true;

    uint32_t _currentFrameId = 0;
    uint32_t _currentIndex = -1;
    uint32_t _lastPresentFrameId = UINT32_MAX;
    uint32_t _frameIdIndex[BUFFER_COUNT] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };

    uint64_t mvsWidth = 0;
    uint32_t mvsHeight = 0;

    void CheckForFrame(IFGFeature* fg, uint32_t frameId);
    int IndexForFrameId(uint32_t frameId) const;

  public:
    bool setConstants(const sl::Constants& constants, uint32_t frameId);
    bool evaluateState();
    // cmdBuffer is ID3D12GraphicsCommandList* or VkCommandBuffer
    bool reportResource(const sl::ResourceTag& tag, void* cmdBuffer, uint32_t frameId);
    void reportTagState(const sl::ResourceTag& tag);
    void reportEngineType(sl::EngineType type) { engineType = type; };
    void reportGameMode(sl::DLSSGMode mode) { _gameFGOff = mode == sl::DLSSGMode::eOff; };
    bool dispatchFG();
    void markPresent(uint64_t frameId);

    // TODO: some shutdown and cleanup methods
};
