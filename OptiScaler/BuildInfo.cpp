#include "pch.h"

#define OPTISCALER_BUILD_METADATA
#include "resource.h"

#include "BuildInfo.h"

namespace BuildInfo
{
const char* ProductName() { return VER_PRODUCT_NAME; }

const char* ProductVersion() { return VER_PRODUCT_VERSION_STR; }
} // namespace BuildInfo
