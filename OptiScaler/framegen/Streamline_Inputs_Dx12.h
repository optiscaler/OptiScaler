#pragma once

#include <pch.h>
#include <sl.h>

// camera data
// hudless, depth, color, ui(?)
//
// init values
// fg feature creation
// fg dispatch
//

class Sl_Inputs_Dx12
{
  private:
    std::optional<sl::Constants> slConstants;
    bool infiniteDepth = false;
    sl::EngineType engineType = sl::EngineType::eCount;

    bool depthSent = false;
    bool hudlessSent = false;
    bool mvsSent = false;
    bool uiSent = false;
    bool uiRequired = false;

    bool allRequiredSent = false;

  public:
    bool setConstants(const sl::Constants& constants);
    bool evaluateState(ID3D12Device* device);
    bool reportResource(const sl::ResourceTag& tag, ID3D12GraphicsCommandList* cmdBuffer);
    bool readyToDispatch();
    bool dispatchFG(ID3D12GraphicsCommandList* cmdBuffer);
    void reportEngineType(sl::EngineType type) { engineType = type; };

    // TODO: some shutdown and cleanup methods
};
