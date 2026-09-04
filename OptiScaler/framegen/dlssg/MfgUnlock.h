#pragma once

// Multi Frame Generation on Ada.
//
// nvngx_dlssg.dll gates MFG on the architecture id reported by the driver: 0x1b0 is Blackwell, Ada
// is below it. Two sites decide what a card is allowed to do, and both compare against that constant.
//
//   Advertise, the function that publishes DLSSG.MultiFrameCountMax:
//       mov   ebx, 0x1
//       mov   r8d, 0x3            the Blackwell count
//       cmp   edi, 0x1b0
//       cmovl r8d, ebx            below Blackwell the count becomes 1
//
//   Validate, the function that accepts or rejects a requested count:
//       cmp   eax, 0x1b0
//       jl    ada                 Ada takes this branch and accepts only 1
//       cmp   ebx, 0x3
//       jbe   accept
//
// Patched: the count immediates become 5, the cmovl becomes a nop, and the jl becomes two nops. The
// result is a maximum of five generated frames -- 6X -- on any architecture.
//
// Memory only. The file on disk carries an Authenticode signature and is left alone.
//
// Not covered: nvngx_dlssg.dll's frame pacing assumes Blackwell's flip metering hardware, which Ada
// does not have, so spacing above 2X is uneven. The published mods correct it by rewriting a blend
// weight inside the model's PTX; that is not done here.
namespace MfgUnlock
{
// Applies both patches once per process. Silent and harmless when the config option is off, when
// nvngx_dlssg.dll is not loaded, or when either signature does not match exactly once.
void TryApply();
} // namespace MfgUnlock
