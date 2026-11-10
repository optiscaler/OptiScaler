#pragma once
#include <proxies/Nvngx_DllProxy.h>

class Nvngx_Arturs : public Nvngx_DllProxy
{
    void QueryVersions();

  protected:
    void LoadLibraries() override final;

  public:
    Nvngx_Arturs() { LoadLibraries(); }

    int getMaxFakeFramesCount() override { return 5; }
    FGNvngxReplacement getType() override { return FGNvngxReplacement::Arturs; }

    // DLSS Enabler's own, even when it doesn't report its antighosting version
    bool supportsDispatchFlags() override { return true; }
};
