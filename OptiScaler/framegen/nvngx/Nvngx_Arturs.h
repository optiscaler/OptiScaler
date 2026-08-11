#pragma once
#include "Nvngx_DllProxy.h"

class Nvngx_Arturs : public Nvngx_DllProxy
{
  protected:
    void LoadLibraries() override final;

  public:
    Nvngx_Arturs() { LoadLibraries(); }

    int getMaxFakeFramesCount(API api) override { return 5; }
    FGNvngxReplacement getType() override { return FGNvngxReplacement::Arturs; }
};
