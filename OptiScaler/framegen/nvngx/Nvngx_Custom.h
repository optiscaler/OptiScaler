#pragma once
#include "Nvngx_DllProxy.h"

// nvngx_dlssg replacement from custom/<name>/dlssg, it's loaded by Opti so it can't check for nvngx.dll as the caller
class Nvngx_Custom : public Nvngx_DllProxy
{
    std::string _name;
    int _maxFakeFrames = 1;

  protected:
    void LoadLibraries() override final;

  public:
    explicit Nvngx_Custom(std::string name) : _name(std::move(name)) { LoadLibraries(); }

    int getMaxFakeFramesCount() override { return _maxFakeFrames; }
    FGNvngxReplacement getType() override { return FGNvngxReplacement::Custom; }
};
