#include "pch.h"
#include "DlssNr_Dx12_State.h"

// Optional route definitions are supplied by the following commits.
auto DlssNr_Dx12::State::Run(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* colour, ID3D12Resource* depth, ID3D12Resource* motion,
             ID3D12Resource* output, const DlssNrFrameInfo& frame, ID3D12CommandQueue* timingQueue) -> void { return; }
