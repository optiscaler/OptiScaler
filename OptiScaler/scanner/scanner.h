#pragma once

#include "SysUtils.h"

namespace scanner
{
uintptr_t GetAddress(const std::wstring_view moduleName, const std::string_view pattern, ptrdiff_t offset = 0,
                     uintptr_t startAddress = 0);
uintptr_t GetAddress(HMODULE module, const std::string_view pattern, ptrdiff_t offset = 0, uintptr_t startAddress = 0);
uintptr_t GetOffsetFromInstruction(const std::wstring_view moduleName, const std::string_view pattern,
                                   ptrdiff_t offset = 0);

// Code of a function, the compiler can split it into several chunks that aren't next to each other
std::vector<std::span<const uint8_t>> GetFunctionChunks(HMODULE module, const void* function);

// Null terminated wide string in the module's initialized data
uintptr_t FindWideString(HMODULE module, std::wstring_view text);

// First lea r64, [rip + disp32] in the module's code that loads target
uintptr_t FindRipReference(HMODULE module, uintptr_t target);

// Start of the function containing address, the first chunk of a split function
uintptr_t GetFunctionStart(uintptr_t address);

} // namespace scanner
