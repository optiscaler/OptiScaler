#include "pch.h"
#include "DlssNr_Dx12_State.h"

// Optional route definitions are supplied by the following commits.
void DlssNr_Dx12::State::EncodeInput(EncodeContext& context) { return; }

DlssNrConstants DlssNr_Dx12::State::MakeResolveConstants(const EncodeContext& context, unsigned int effectivePasses) { return {}; }
