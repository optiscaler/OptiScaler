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

} // namespace scanner
