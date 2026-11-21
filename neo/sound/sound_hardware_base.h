/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.

===========================================================================
*/

#ifndef __SOUND_HARDWARE_BASE_H__
#define __SOUND_HARDWARE_BASE_H__

// Forward declarations
class idSoundVoice;
class idSoundSample;

// Base class for sound hardware abstraction
class idSoundHardware {
public:
	virtual					~idSoundHardware() {}
	virtual void			Init() = 0;
	virtual void			Shutdown() = 0;
	virtual void			Update() = 0;
	virtual void*			GetIXAudio2() const { return nullptr; }
	virtual idSoundVoice *	AllocateVoice( const idSoundSample * leadinSample, const idSoundSample * loopingSample ) = 0;
	virtual void			FreeVoice( idSoundVoice * voice ) = 0;
	virtual void			SetMasterVolume( float volume ) = 0;
	virtual float			GetMasterVolume() const = 0;
	
	// Debug methods
	virtual int				GetNumFreeVoices() const { return 0; }
	virtual int				GetNumZombieVoices() const { return 0; }
};

#endif // __SOUND_HARDWARE_BASE_H__