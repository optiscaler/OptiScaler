#include "pch.h"
#include "DlssNr_Dx12_State.h"

// Optional route definitions are supplied by the following commits.
auto DlssNr_Dx12::State::DeferredSrContext::Say(const std::string& text) -> void { return; }

auto DlssNr_Dx12::State::DeferredSrContext::Cancel() -> void { return; }

auto DlssNr_Dx12::State::DeferredSrContext::Collect() -> void { return; }

auto DlssNr_Dx12::State::DeferredSrContext::UInt(NVSDK_NGX_Parameter* p, const char* key, unsigned fallback) -> unsigned { return {}; }

auto DlssNr_Dx12::State::DeferredSrContext::Float(NVSDK_NGX_Parameter* p, const char* key, float fallback) -> float { return {}; }

auto DlssNr_Dx12::State::DeferredSrContext::Allocate(Generation& g) -> bool { return {}; }

auto DlssNr_Dx12::State::DeferredSrContext::Before(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* source, unsigned long long epoch,
                    unsigned long long submittedEpoch, ID3D12CommandQueue* queue, bool interop, bool rayReconstruction) -> void { return; }

auto DlssNr_Dx12::State::DeferredSrContext::After(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* source, unsigned long long epoch) -> void { return; }

auto DlssNr_Dx12::State::DeferredSrContext::ReleaseResources() -> void { return; }
