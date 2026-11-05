#pragma once
#include "SysUtils.h"

void HookFSR2ExeInputs();
void FSR2Dx12DeviceDestroyed(IUnknown* device);
void HookFSR2Inputs(HMODULE module);
void HookFSR2Dx12Inputs(HMODULE module);
