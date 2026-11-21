/*
===========================================================================

MinGW stub for Microsoft X3DAudio.h

This is a minimal stub to allow compilation with MinGW-w64.
The actual Microsoft header provides X3DAudio DSP functionality.

===========================================================================
*/

#ifndef _X3DAUDIO_H_
#define _X3DAUDIO_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// X3DAudio types and constants (minimal stubs)
typedef struct X3DAUDIO_HANDLE { int dummy; } X3DAUDIO_HANDLE;
typedef struct X3DAUDIO_LISTENER { int dummy; } X3DAUDIO_LISTENER;
typedef struct X3DAUDIO_EMITTER { int dummy; } X3DAUDIO_EMITTER;
typedef struct X3DAUDIO_CONE { int dummy; } X3DAUDIO_CONE;
typedef struct X3DAUDIO_DSP_SETTINGS { int dummy; } X3DAUDIO_DSP_SETTINGS;

#define X3DAUDIO_PI 3.141592654f
#define SPEED_OF_SOUND 343.5f

// Minimal function stubs
#define X3DAudioInitialize(a,b,c) 0
#define X3DAudioCalculate(a,b,c,d) 0

#ifdef __cplusplus
}
#endif

#endif // _X3DAUDIO_H_