/*
===========================================================================

MinGW stub for Microsoft xma2defs.h

This is a minimal stub to allow compilation with MinGW-w64.
The actual Microsoft header provides XMA2 audio codec definitions.

===========================================================================
*/

#ifndef _XMA2DEFS_H_
#define _XMA2DEFS_H_

#ifdef __cplusplus
extern "C" {
#endif

// XMA2 format definitions (minimal stubs)
#define XMA2_MAX_CHANNELS 8
#define XMA2_MAX_LOOPS 16

typedef struct XMA2WAVEFORMAT {
    unsigned short wFormatTag;
    unsigned short nChannels;
    unsigned int nSamplesPerSec;
    unsigned int nAvgBytesPerSec;
    unsigned short nBlockAlign;
    unsigned short wBitsPerSample;
    unsigned short NumStreams;
    unsigned short LoopCount;
    unsigned short Version;
    unsigned int LoopRegion[2];
} XMA2WAVEFORMAT;

#ifdef __cplusplus
}
#endif

#endif // _XMA2DEFS_H_