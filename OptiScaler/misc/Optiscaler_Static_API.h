#pragma once
#include <windows.h>

struct OptiScalerConfig
{

};

bool OptiScaler_Init(HMODULE self, const OptiScalerConfig* cfg);
void OptiScaler_Shutdown();