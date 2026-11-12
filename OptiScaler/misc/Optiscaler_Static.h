#pragma once
#ifdef OPTISCALER_STATIC

#define DllMain org_dllMain
#define GetFileVersionInfoSizeW SafeGetFileVersionInfoSizeW
#define GetFileVersionInfoW SafeGetFileVersionInfoW
#define VerQueryValueW SafeVerQueryValueW
#endif