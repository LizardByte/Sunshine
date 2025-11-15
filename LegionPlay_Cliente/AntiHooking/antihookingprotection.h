#pragma once

#ifdef ANTIHOOKING_LIBRARY
#define AH_EXPORT  extern "C" __declspec(dllexport)
#else
#define AH_EXPORT  extern "C" __declspec(dllimport)
#endif

AH_EXPORT void AntiHookingDummyImport();

