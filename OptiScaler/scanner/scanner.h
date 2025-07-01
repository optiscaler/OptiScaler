#pragma once

#include <pch.h>

namespace scanner
{
uintptr_t GetAddress(const std::wstring_view moduleName, const std::string_view pattern, ptrdiff_t offset = 0,
                     uintptr_t startAddress = 0);
uintptr_t GetOffsetFromInstruction(const std::wstring_view moduleName, const std::string_view pattern,
                                   ptrdiff_t offset = 0);
} // namespace scanner
