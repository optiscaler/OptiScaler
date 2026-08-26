#pragma once

#include "SysUtils.h"

ID3DBlob* CompileShader(const char* shaderCode, const char* entryPoint, const char* target);
