/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.

===========================================================================
*/

#ifndef __SDL_AUDIO_H__
#define __SDL_AUDIO_H__

#include <SDL.h>
#include <SDL_audio.h>
#include <vector>

#include "../WaveFile.h"
#include "SoundVoice.h"
#include "sound_hardware_base.h"

// Forward declaration for SamplesToMsec (defined in snd_local.h)
ID_INLINE_EXTERN uint32 SamplesToMsec( uint32 samples, uint32 sampleRate );

/*
================================================
idSoundSample_SDL
================================================
*/
class idSoundSample_SDL {
public:
					idSoundSample_SDL();
					idSoundSample_SDL( const char* name );
					~idSoundSample_SDL();

	// Loads and initializes the resource based on the name.
	virtual void	LoadResource();

	void			SetName( const char * n ) { name = n; }
	const char *	GetName() const { return name; }
	ID_TIME_T		GetTimestamp() const { return timestamp; }

	// turns it into a beep
	void			MakeDefault();

	// frees all data
	void			FreeData();

	int				LengthInMsec() const { return SamplesToMsec( NumSamples(), SampleRate() ); }
	int				SampleRate() const { return sampleRate; }
	int				NumSamples() const { return playLength; }
	int				NumChannels() const { return channels; }
	int				BufferSize() const { return totalBufferSize; }

	bool			IsCompressed() const { return false; } // SDL uses uncompressed PCM

	bool			IsDefault() const { return timestamp == FILE_NOT_FOUND_TIMESTAMP; }
	bool			IsLoaded() const { return loaded; }

	void			SetNeverPurge() { neverPurge = true; }
	bool			GetNeverPurge() const { return neverPurge; }

	void			SetLevelLoadReferenced() { levelLoadReferenced = true; }
	void			ResetLevelLoadReferenced() { levelLoadReferenced = false; }
	bool			GetLevelLoadReferenced() const { return levelLoadReferenced; }

	int				GetLastPlayedTime() const { return lastPlayedTime; }
	void			SetLastPlayedTime( int t ) { lastPlayedTime = t; }

	float			GetAmplitude( int timeMS ) const;

	const void*		GetData() const { return data; }
	size_t			GetSize() const { return totalBufferSize; }
	bool			IsDefaultSample() const { return isDefault; }

protected:
	friend class idSoundHardwareSDL;
	friend class idSoundVoice_SDL;

	bool			LoadWav( const idStr & name );
	bool			LoadGenerated( const idStr & name );
	bool			LoadDecodedWav( SDL_RWops * rw, const idStr & debugName );
	void			CreateDefaultBeep();

	struct sampleBuffer_t {
		void * buffer;
		int bufferSize;
		int numSamples;
	};

	idStr			name;

	ID_TIME_T		timestamp;
	bool			loaded;

	bool			neverPurge;
	bool			levelLoadReferenced;

	uint32			lastPlayedTime;

	int				totalBufferSize;	// total size of all the buffers
	idList<sampleBuffer_t, TAG_AUDIO> buffers;

	int				playBegin;
	int				playLength;

	int				sampleRate;
	int				channels;

	const void*		data;

	bool			isDefault;
};

/*
================================================
idSoundSample

This reverse-inheritance purportedly makes working on
multiple platforms easier.
================================================
*/
class idSoundSample : public idSoundSample_SDL {
public:
					idSoundSample() : idSoundSample_SDL() {}
					idSoundSample( const char* name ) : idSoundSample_SDL( name ) {}
};

/*
================================================
idSoundVoice_SDL
================================================
*/
class idSoundVoice_SDL : public idSoundVoice_Base {
public:
					idSoundVoice_SDL();
					~idSoundVoice_SDL();

	void			Create( const idSoundSample_SDL * leadinSample, const idSoundSample_SDL * loopingSample );

	// Start playing at a particular point in the buffer.  Does an Update() too
	void			Start( int offsetMS, int ssFlags );

	// Stop playing.
	void			Stop();

	// Stop consuming buffers
	void			Pause();
	// Start consuming buffers again
	void			UnPause();

	// Sends new position/volume/pitch information to the hardware
	bool			Update();

	// returns the RMS levels of the most recently processed block of audio, SSF_FLICKER must have been passed to Start
	float			GetAmplitude();

	// returns true if we can re-use this voice
	bool			CompatibleFormat( idSoundSample_SDL * s );

	uint32			GetSampleRate() const { return sampleRate; }

	// callback function
	void			OnBufferStart( idSoundSample_SDL * sample, int bufferNumber );

private:
	friend class idSoundHardwareSDL;

	// Returns true when all the buffers are finished processing
	bool			IsPlaying();

	// Called after the voice has been stopped
	void			FlushSourceBuffers();

	// Destroy the internal hardware resource
	void			DestroyInternal();

	// Helper function used by the initial start as well as for looping a streamed buffer
	int				RestartAt( int offsetSamples );

	// Helper function to submit a buffer
	int				SubmitBuffer( idSoundSample_SDL * sample, int bufferNumber, int offset );

	// Adjust the voice frequency based on the new sample rate for the buffer
	void			SetSampleRate( uint32 newSampleRate, uint32 operationSet );

	SDL_AudioStream*	stream;
	idSoundSample_SDL * leadinSample;
	idSoundSample_SDL * loopingSample;

	// These are the fields from the sample format that matter to us for voice reuse
	uint16			formatTag;
	uint16			numChannels;

	uint32			sourceVoiceRate;
	uint32			sampleRate;

	bool			hasVUMeter;
	bool			paused;
};

/*
================================================
idSoundVoice
================================================
*/
class idSoundVoice : public idSoundVoice_SDL {
public:
					idSoundVoice() : idSoundVoice_SDL() {}
};

/*
================================================
idSoundHardwareSDL
================================================
*/
class idSoundHardwareSDL : public idSoundHardware {
public:
					idSoundHardwareSDL();
					~idSoundHardwareSDL();

	void			Init() override;
	void			Shutdown() override;
	void			Update() override;
	void*			GetIXAudio2() const override { return nullptr; }
	idSoundVoice *	AllocateVoice( const idSoundSample * leadinSample, const idSoundSample * loopingSample ) override;
	void			FreeVoice( idSoundVoice * voice ) override;
	void			SetMasterVolume( float volume ) override;
	float			GetMasterVolume() const override;

	// Debug methods
	int				GetNumFreeVoices() const override;
	int				GetNumZombieVoices() const override;

	void			MixAudio( float* buffer, int numSamples );

private:
	SDL_AudioDeviceID audioDevice;
	SDL_AudioSpec deviceSpec;

	std::vector<idSoundVoice_SDL*> voices;
	float masterVolume;
};

#endif // __SDL_AUDIO_H__