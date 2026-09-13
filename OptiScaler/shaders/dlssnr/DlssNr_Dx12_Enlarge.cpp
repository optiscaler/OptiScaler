#include "pch.h"
#include "DlssNr_Dx12_State.h"

// Optional route definitions are supplied by the following commits.
void DlssNr_Dx12::State::ReleaseEnlarger() { return; }

void DlssNr_Dx12::State::CollectEnlargers() { return; }

ID3D12Resource* DlssNr_Dx12::State::EnlargeMatchedResidual(ID3D12GraphicsCommandList* cmd,
    ID3D12Device* device, ID3D12Resource* proxy, ID3D12Resource* answer, ID3D12Resource* depth,
    ID3D12Resource* motion, const DlssNrFrameInfo& frame, const DlssNrConstants& resolve,
    bool reset, ID3D12CommandQueue* timingQueue) { return {}; }
