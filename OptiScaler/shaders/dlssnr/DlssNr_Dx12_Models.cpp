#include "pch.h"
#include "DlssNr_Dx12_State.h"

// Optional route definitions are supplied by the following commits.
DlssNr::Proxy::Settings DlssNr_Dx12::State::ModelSettings(const Config& cfg, unsigned int pass) { return {}; }

bool DlssNr_Dx12::State::PrepareRunModels(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device,
                                        const DlssNrFrameInfo& frame, const D3D12_RESOURCE_DESC& desc,
                                        DlssNr::ColorExtent native, DlssNr::ColorExtent work,
                                        float workScale, unsigned int requestedPasses) { return {}; }

auto DlssNr_Dx12::State::NgxResultName(unsigned int r) -> const char* { return {}; }

auto DlssNr_Dx12::State::TuningMatchesFeature(const Config& cfg, unsigned int requestedPasses) -> bool { return {}; }
