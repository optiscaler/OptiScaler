#include "pch.h"
#include "DlssNr_Dx12_State.h"

// Optional route definitions are supplied by the following commits.
auto DlssNr_Dx12::State::LateContext::Say(const char* message) -> void { return; }

auto DlssNr_Dx12::State::LateContext::Finished(const Slot& slot) -> bool { return false; }

auto DlssNr_Dx12::State::LateContext::Cancel() -> void { return; }

auto DlssNr_Dx12::State::LateContext::Clone(ComPtr<ID3D12Resource>& copy, ID3D12Resource* source) -> bool { return false; }

auto DlssNr_Dx12::State::LateContext::Acquire(ID3D12GraphicsCommandList* cmd) -> Slot* { return {}; }

auto DlssNr_Dx12::State::LateContext::Arm(Slot& slot, ID3D12GraphicsCommandList* cmd) -> void { return; }

auto DlssNr_Dx12::State::LateContext::Capture(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* params, bool rr) -> void { return; }

auto DlssNr_Dx12::State::LateContext::CaptureResidual(ID3D12GraphicsCommandList* cmd, ID3D12Resource* clean, ID3D12Resource* residual,
                             float scale, bool sceneLinear, bool reset) -> bool { return false; }
