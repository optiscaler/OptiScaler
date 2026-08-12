#pragma once
#include "Nvngx_DllProxy.h"

typedef void (*PFN_RefreshGlobalConfiguration)();

class Nvngx_Nukems : public Nvngx_DllProxy
{
    PFN_RefreshGlobalConfiguration _refreshGlobalConfiguration = nullptr;

    void setSetting(const wchar_t* setting, const wchar_t* value);
    bool is120orNewer() const { return _refreshGlobalConfiguration != nullptr; }

  protected:
    void LoadLibraries() override final;

  public:
    Nvngx_Nukems() { LoadLibraries(); }

    void setDebugView(bool enabled);
    void setInterpolatedOnly(bool enabled);
    int getMaxFakeFramesCount() override { return 1; }
    FGNvngxReplacement getType() override { return FGNvngxReplacement::Nukems; }
};
