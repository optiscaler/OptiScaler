#pragma once
#include "SysUtils.h"

void HookFSR3ExeInputs();
void FSR3Dx12DeviceDestroyed(IUnknown* device);
void HookFSR3Inputs(HMODULE module);
void HookFSR3Dx12Inputs(HMODULE module);
