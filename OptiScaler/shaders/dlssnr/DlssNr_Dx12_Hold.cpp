#include "pch.h"
#include "DlssNr_Dx12_State.h"

// Optional route definitions are supplied by the following commits.
auto DlssNr_Dx12::State::SameHoldShape(const D3D12_RESOURCE_DESC& a, const D3D12_RESOURCE_DESC& b) -> bool { return {}; }

auto DlssNr_Dx12::State::ReleaseInputHold() -> void { return; }

auto DlssNr_Dx12::State::BeginInputHold(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* params,
                        const D3D12_RESOURCE_STATES* states) -> void { return; }

auto DlssNr_Dx12::State::CheckCaptureTrigger() -> void { return; }
