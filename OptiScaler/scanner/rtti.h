#pragma once

#include "SysUtils.h"

// Finds classes through the RTTI that MSVC embeds in binaries, by name instead of by byte patterns,
// which stay valid across builds as long as the class keeps its name
namespace rtti
{
// Vtable of the most derived object of a class, nullptr when the class isn't in the module
// `className` is the name used in the type descriptor, e.g. "ffxProvider_FSR4" or "UpscalerModel@fsr4_common"
void** FindVtable(HMODULE module, std::string_view className);

// Number of leading vtable entries that point into the module's code
size_t CountVirtualFunctions(HMODULE module, void** vtable);

} // namespace rtti
