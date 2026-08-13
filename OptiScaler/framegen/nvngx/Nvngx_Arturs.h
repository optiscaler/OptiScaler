#pragma once
#include "Nvngx_DllProxy.h"

class Nvngx_Arturs : public Nvngx_DllProxy
{
    feature_version ghostbusterVersion {};

    void QueryVersions();

  protected:
    void LoadLibraries() override final;

  public:
    Nvngx_Arturs() { LoadLibraries(); }

    int getMaxFakeFramesCount() override { return 5; }
    FGNvngxReplacement getType() override { return FGNvngxReplacement::Arturs; }
    feature_version extraVersion() override;
};
