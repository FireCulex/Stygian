/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.

===========================================================================
*/

#include "../../idlib/precompiled.h"
#include "sdl_audio.h"
#include "../sound.h"
#include <SDL.h>
#include <SDL_audio.h>
#include <vector>
#include <cmath>

static void SDLCALL AudioCallback(void* userdata, Uint8* stream, int len) {
	idSoundHardwareSDL* hw = static_cast<idSoundHardwareSDL*>(userdata);
	// SDL gives us bytes, we need to convert to float samples
	hw->MixAudio(reinterpret_cast<float*>(stream), len / sizeof(float));
}

idSoundHardwareSDL::idSoundHardwareSDL()
	: audioDevice(0), masterVolume(1.0f) {
	memset(&deviceSpec, 0, sizeof(deviceSpec));
}

idSoundHardwareSDL::~idSoundHardwareSDL() {
	Shutdown();
}

void idSoundHardwareSDL::Init() {
	SDL_AudioSpec want, have;
	SDL_zero(want);
	want.freq = 48000;
	want.format = AUDIO_F32SYS;
	want.channels = 2;
	want.samples = 4096;
	want.callback = AudioCallback;
	want.userdata = this;

	audioDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (audioDevice == 0) {
		idLib::Warning("SDL_OpenAudioDevice failed: %s", SDL_GetError());
		return;
	}

	deviceSpec = have;
	SDL_PauseAudioDevice(audioDevice, 0);
}

void idSoundHardwareSDL::Shutdown() {
	for (auto voice : voices) {
		if (voice) {
			voice->DestroyInternal();
			delete voice;
		}
	}
	voices.clear();

	if (audioDevice) {
		SDL_CloseAudioDevice(audioDevice);
		audioDevice = 0;
	}
}

void idSoundHardwareSDL::Update() {
	// Process any pending voice updates
	for (auto voice : voices) {
		if (voice) {
			voice->Update();
		}
	}
}

idSoundVoice* idSoundHardwareSDL::AllocateVoice(const idSoundSample* leadinSample, const idSoundSample* loopingSample) {
	idSoundVoice_SDL* voice = new idSoundVoice_SDL();
	voice->Create(static_cast<const idSoundSample_SDL*>(leadinSample), static_cast<const idSoundSample_SDL*>(loopingSample));
	voices.push_back(voice);
	return (idSoundVoice*)voice;
}

void idSoundHardwareSDL::FreeVoice(idSoundVoice* voice) {
	idSoundVoice_SDL* sdlVoice = (idSoundVoice_SDL*)voice;
	auto it = std::find(voices.begin(), voices.end(), sdlVoice);
	if (it != voices.end()) {
		(*it)->DestroyInternal();
		delete *it;
		voices.erase(it);
	}
}

void idSoundHardwareSDL::SetMasterVolume(float volume) {
	masterVolume = idMath::ClampFloat(0.0f, 1.0f, volume);
}

float idSoundHardwareSDL::GetMasterVolume() const {
	return masterVolume;
}

// Debug methods
int idSoundHardwareSDL::GetNumFreeVoices() const {
	return 0;
}

int idSoundHardwareSDL::GetNumZombieVoices() const {
	return 0;
}

void idSoundHardwareSDL::MixAudio(float* buffer, int numSamples) {
	memset(buffer, 0, numSamples * sizeof(float));

	for (auto voice : voices) {
		if (!voice || !voice->IsPlaying()) {
			continue;
		}

		// Get audio from the voice's stream
		int available = SDL_AudioStreamAvailable(voice->stream);
		int toMix = idMath::ClampInt(0, numSamples * sizeof(float), available);
		
		if (toMix > 0) {
			// Use a temporary buffer for converted audio
			std::vector<float> tempBuffer(toMix / sizeof(float));
			SDL_AudioStreamGet(voice->stream, tempBuffer.data(), toMix);
			
			// Mix into the output buffer
			for (int i = 0; i < toMix / sizeof(float); i++) {
				buffer[i] += tempBuffer[i] * voice->GetGain() * masterVolume;
			}
		}
	}
}

// ============================================================
// idSoundSample_SDL implementation
// ============================================================

// The shipped `.idwav` resources store their integers big-endian (they were
// written with idFile::WriteBig) while the embedded waveFmt_t is little-endian.
static Uint16 AudioLE16( const Uint8 * p ) {
	return (Uint16)( p[ 0 ] | ( p[ 1 ] << 8 ) );
}
static Uint32 AudioLE32( const Uint8 * p ) {
	return (Uint32)( p[ 0 ] | ( p[ 1 ] << 8 ) | ( p[ 2 ] << 16 ) | ( (Uint32)p[ 3 ] << 24 ) );
}
static Uint32 AudioBE32( const Uint8 * p ) {
	return ( (Uint32)p[ 0 ] << 24 ) | ( (Uint32)p[ 1 ] << 16 ) | ( (Uint32)p[ 2 ] << 8 ) | (Uint32)p[ 3 ];
}
static void AppendLE16( std::vector<Uint8> & out, Uint16 v ) {
	out.push_back( (Uint8)( v & 0xff ) );
	out.push_back( (Uint8)( ( v >> 8 ) & 0xff ) );
}
static void AppendLE32( std::vector<Uint8> & out, Uint32 v ) {
	out.push_back( (Uint8)( v & 0xff ) );
	out.push_back( (Uint8)( ( v >> 8 ) & 0xff ) );
	out.push_back( (Uint8)( ( v >> 16 ) & 0xff ) );
	out.push_back( (Uint8)( ( v >> 24 ) & 0xff ) );
}
static void AppendTag( std::vector<Uint8> & out, const char * tag ) {
	out.insert( out.end(), tag, tag + 4 );
}

idSoundSample_SDL::idSoundSample_SDL()
	: name(""), timestamp(0), loaded(false), neverPurge(false), levelLoadReferenced(false),
	  lastPlayedTime(0), totalBufferSize(0), playBegin(0), playLength(0),
	  sampleRate(48000), channels(2), data(nullptr), isDefault(false) {
}

idSoundSample_SDL::idSoundSample_SDL(const char* name)
	: name(name), timestamp(0), loaded(false), neverPurge(false), levelLoadReferenced(false),
	  lastPlayedTime(0), totalBufferSize(0), playBegin(0), playLength(0),
	  sampleRate(48000), channels(2), data(nullptr), isDefault(false) {
}

idSoundSample_SDL::~idSoundSample_SDL() {
	FreeData();
}

void idSoundSample_SDL::LoadResource() {
	if (loaded) {
		return;
	}

	if (idStr::Icmpn(name, "_default", 8) == 0) {
		MakeDefault();
		loaded = true;
		return;
	}

	// Original load order: generated/<name>.idwav first, then <name>.wav.
	if (LoadGenerated(name) || LoadWav(name)) {
		loaded = true;
	} else {
		// Create a default beep sound
		idLib::Warning("LoadResource: '%s' not loadable — using default beep", name.c_str());
		CreateDefaultBeep();
		loaded = true;
	}
}

bool idSoundSample_SDL::LoadWav(const idStr& name) {
	// idSoundSystemLocal::LoadSample() strips the extension from sample names
	// (canonical.StripFileExtension()), so `name` arrives as e.g. "sound/guis/ping".
	// The loose entries under base/sound/ carry the ".wav" extension, so re-add
	// it before asking the fileSystem — otherwise ReadFile returns -1.
	idStr wavname = name;
	wavname.DefaultFileExtension( ".wav" );

	// Sound files live under fs_basepath/fs_game (e.g. base/sound/...) or inside
	// .resources containers — a raw SDL_RWFromFile on the game-relative name fails.
	// Read through the engine fileSystem instead, then parse from memory.
	void* fileBuf = nullptr;
	int fileLen = fileSystem->ReadFile(wavname, &fileBuf);
	SDL_RWops* rw = nullptr;
	if (fileLen > 0 && fileBuf != nullptr) {
		rw = SDL_RWFromMem(fileBuf, fileLen);
	}
	if (!rw) {
		// last resort: loose file relative to the process CWD
		rw = SDL_RWFromFile(wavname.c_str(), "rb");
	}
	if (!rw) {
		idLib::Warning("LoadWav: cannot open '%s' (ReadFile len=%d) — falling back to beep", wavname.c_str(), fileLen);
		return false;
	}

	bool ok = LoadDecodedWav(rw, wavname);
	if (fileBuf != nullptr) {
		fileSystem->FreeFile(fileBuf);
	}
	return ok;
}

bool idSoundSample_SDL::LoadDecodedWav(SDL_RWops* rw, const idStr& debugName) {
	SDL_AudioSpec spec;
	Uint8* audioData = nullptr;
	Uint32 audioLen = 0;

	if (SDL_LoadWAV_RW(rw, 1, &spec, &audioData, &audioLen) == nullptr) {
		idLib::Warning("Failed to load WAV %s: %s", debugName.c_str(), SDL_GetError());
		return false;
	}

	// Convert to float32 if needed
	if (spec.format != AUDIO_F32SYS) {
		SDL_AudioCVT cvt;
		if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq,
							 AUDIO_F32SYS, spec.channels, spec.freq) < 0) {
			SDL_FreeWAV(audioData);
			return false;
		}

		cvt.len = audioLen;
		cvt.buf = (Uint8*)SDL_malloc(cvt.len * cvt.len_mult);
		if (cvt.buf == nullptr) {
			SDL_FreeWAV(audioData);
			return false;
		}
		memcpy(cvt.buf, audioData, audioLen);
		SDL_FreeWAV(audioData);

		if (SDL_ConvertAudio(&cvt) < 0) {
			SDL_free(cvt.buf);
			return false;
		}

		audioData = cvt.buf;
		audioLen = cvt.len_cvt;
	}

	// Store the data
	this->sampleRate = spec.freq;
	this->channels = spec.channels;
	this->data = audioData;
	this->totalBufferSize = audioLen;
	this->playLength = audioLen / (channels * sizeof(float));
	this->timestamp = Sys_Milliseconds();

	// Create a single buffer
	sampleBuffer_t buf;
	buf.buffer = const_cast<void*>(data);
	buf.bufferSize = totalBufferSize;
	buf.numSamples = playLength;
	buffers.Append(buf);

	return true;
}

// Loads a `generated/<name>.idwav` entry from a .resources pack. Layout (writer:
// idSoundSample_XAudio2::WriteGeneratedSample): BE magic 'mzrt', BE timestamp,
// BE loaded, BE playBegin, BE playLength, one pad byte, LE waveFmt basic, for
// ADPCM LE extraSize + verbatim extra bytes, BE ampnum + amplitude bytes, BE
// totalBufferSize, BE numBuffers, then per buffer BE numSamples, BE bufferSize
// and the raw compressed/PCM payload. We rewrap that payload as a normal
// RIFF/WAVE so SDL can decode MS ADPCM for us.
bool idSoundSample_SDL::LoadGenerated(const idStr& name) {
	// Keep any interior extension (e.g. "foo.st" → "generated/foo.st.idwav"),
	// exactly like idSoundSample_XAudio2::LoadResource does.
	idStr gen = "generated/";
	gen.Append(name);
	gen.Append(".idwav");

	void* fileBuf = nullptr;
	int fileLen = fileSystem->ReadFile(gen, &fileBuf);
	if (fileLen <= 0 || fileBuf == nullptr) {
		return false;
	}

	const Uint8* b = static_cast<const Uint8*>(fileBuf);
	const int len = fileLen;
	bool ok = false;

	if (len >= 64 && AudioBE32(b) == 0x6d7a7274u) {
		// The shipped packs have one pad byte after the five BE header fields;
		// scan a small window instead of hard-coding the exact offset.
		int fmtOff = -1;
		Uint16 tag = 0, channels = 0, block = 0, bits = 0;
		Uint32 rate = 0;
		for (int o = 16; o <= 32 && o + 16 <= len; o++) {
			Uint16 t = AudioLE16(b + o);
			Uint16 ch = AudioLE16(b + o + 2);
			Uint32 r = AudioLE32(b + o + 4);
			Uint16 blk = AudioLE16(b + o + 12);
			Uint16 bt = AudioLE16(b + o + 14);
			if ((t == 1 || t == 2) && (ch == 1 || ch == 2) && r >= 4000 && r <= 96000 &&
				blk > 0 && blk < 8192 && ((t == 1 && (bt == 8 || bt == 16)) || (t == 2 && bt == 4))) {
				fmtOff = o;
				tag = t;
				channels = ch;
				rate = r;
				block = blk;
				bits = bt;
				break;
			}
		}

		if (fmtOff >= 0) {
			int p = fmtOff + 16;
			Uint16 extraSize = 0;
			const Uint8* extra = nullptr;
			if (tag == 2) {
				if (p + 2 > len) {
					goto done;
				}
				extraSize = AudioLE16(b + p);
				p += 2;
				if (extraSize == 0 || p + extraSize > len) {
					goto done;
				}
				extra = b + p;
				p += extraSize;
			}

			if (p + 4 > len) {
				goto done;
			}
			Uint32 ampnum = AudioBE32(b + p);
			p += 4;
			if (p + ampnum > len || p + 8 > len) {
				goto done;
			}
			p += ampnum;
			/*Uint32 totalBufferSize =*/ AudioBE32(b + p);
			Uint32 numBuffers = AudioBE32(b + p + 4);
			p += 8;
			if (numBuffers == 0 || numBuffers > 64) {
				goto done;
			}

			std::vector<Uint8> payload;
			payload.reserve( len - p );
			for (Uint32 i = 0; i < numBuffers; i++) {
				if (p + 8 > len) {
					goto done;
				}
				/*Uint32 numSamples =*/ AudioBE32(b + p);
				Uint32 bufferSize = AudioBE32(b + p + 4);
				p += 8;
				if (bufferSize == 0 || p + bufferSize > len) {
					goto done;
				}
				payload.insert(payload.end(), b + p, b + p + bufferSize);
				p += bufferSize;
			}
			if (payload.empty()) {
				goto done;
			}

			// Build a synthetic RIFF/WAVE around the raw payload.
			const Uint32 fmtSize = 16 + (tag == 2 ? (Uint32)(2 + extraSize) : 0);
			const Uint32 dataSize = (Uint32)payload.size();
			const Uint32 riffSize = 4 + 8 + fmtSize + 8 + dataSize;
			std::vector<Uint8> wave;
			wave.reserve(12 + 8 + fmtSize + 8 + dataSize);
			AppendTag(wave, "RIFF");
			AppendLE32(wave, riffSize);
			AppendTag(wave, "WAVE");
			AppendTag(wave, "fmt ");
			AppendLE32(wave, fmtSize);
			wave.insert(wave.end(), b + fmtOff, b + fmtOff + 16); // basic waveFmt is already LE
			if (tag == 2) {
				AppendLE16(wave, extraSize);
				wave.insert(wave.end(), extra, extra + extraSize);
			}
			AppendTag(wave, "data");
			AppendLE32(wave, dataSize);
			wave.insert(wave.end(), payload.begin(), payload.end());

			SDL_RWops* rw = SDL_RWFromMem(wave.data(), (int)wave.size());
			if (rw != nullptr) {
				ok = LoadDecodedWav(rw, gen);
			}
		}
	}

done:
	if (fileBuf != nullptr) {
		fileSystem->FreeFile(fileBuf);
	}
	return ok;
}

void idSoundSample_SDL::CreateDefaultBeep() {
	// Generate a simple 440Hz sine wave beep
	const int duration = 100; // ms
	const int freq = 440;
	const int sampleRate = 48000;
	const int numSamples = (duration * sampleRate) / 1000;

	this->sampleRate = sampleRate;
	this->channels = 1;
	this->totalBufferSize = numSamples * sizeof(float);
	this->playLength = numSamples;

	float* samples = (float*)SDL_malloc(numSamples * sizeof(float));
	if (samples != nullptr) {
		for (int i = 0; i < numSamples; i++) {
			float t = (float)i / sampleRate;
			samples[i] = sinf(2.0f * idMath::PI * freq * t) * 0.5f;
		}
	}

	this->data = samples;

	sampleBuffer_t buf;
	buf.buffer = samples;
	buf.bufferSize = totalBufferSize;
	buf.numSamples = playLength;
	buffers.Append(buf);

	// Match the XAudio2 backend: FILE_NOT_FOUND_TIMESTAMP is what IsDefault()
	// checks, so a fallback sample is not mistaken for a real loaded sound.
	this->timestamp = FILE_NOT_FOUND_TIMESTAMP;
	this->isDefault = true;
	this->loaded = true;
}

void idSoundSample_SDL::MakeDefault() {
	if (!isDefault) {
		FreeData();
		CreateDefaultBeep();
	}
}

void idSoundSample_SDL::FreeData() {
	for (int i = 0; i < buffers.Num(); i++) {
		if (buffers[i].buffer) {
			SDL_free(const_cast<void*>(buffers[i].buffer));
		}
	}
	buffers.Clear();
	data = nullptr;
	totalBufferSize = 0;
	playLength = 0;
	loaded = false;
}

float idSoundSample_SDL::GetAmplitude(int timeMS) const {
	if (!loaded || buffers.Num() == 0) {
		return 0.0f;
	}

	// Simple amplitude calculation - return a constant for now
	return 0.5f;
}

// ============================================================
// idSoundVoice_SDL implementation
// ============================================================

idSoundVoice_SDL::idSoundVoice_SDL()
	: stream(nullptr), leadinSample(nullptr), loopingSample(nullptr),
	  formatTag(0), numChannels(0), sourceVoiceRate(0), sampleRate(0),
	  hasVUMeter(false), paused(false) {
}

idSoundVoice_SDL::~idSoundVoice_SDL() {
	DestroyInternal();
}

void idSoundVoice_SDL::Create(const idSoundSample_SDL* leadinSample_, const idSoundSample_SDL* loopingSample_) {
	leadinSample = const_cast<idSoundSample_SDL*>(leadinSample_);
	loopingSample = const_cast<idSoundSample_SDL*>(loopingSample_);

	// Create audio stream for conversion
	if (leadinSample && leadinSample->NumSamples() > 0) {
		stream = SDL_NewAudioStream(AUDIO_F32SYS, leadinSample->NumChannels(), leadinSample->SampleRate(),
									AUDIO_F32SYS, 2, 48000);
	} else if (loopingSample && loopingSample->NumSamples() > 0) {
		stream = SDL_NewAudioStream(AUDIO_F32SYS, loopingSample->NumChannels(), loopingSample->SampleRate(),
									AUDIO_F32SYS, 2, 48000);
	} else {
		// Default format
		stream = SDL_NewAudioStream(AUDIO_F32SYS, 1, 48000, AUDIO_F32SYS, 2, 48000);
	}

	if (leadinSample) {
		this->sampleRate = leadinSample->SampleRate();
		this->numChannels = leadinSample->NumChannels();
	} else if (loopingSample) {
		this->sampleRate = loopingSample->SampleRate();
		this->numChannels = loopingSample->NumChannels();
	}
}

void idSoundVoice_SDL::Start(int offsetMS, int ssFlags) {
	if (!stream) return;

	// Put initial data into the stream
	if (leadinSample && leadinSample->NumSamples() > 0) {
		const float* data = static_cast<const float*>(leadinSample->GetData());
		int offsetSamples = (offsetMS * sampleRate) / 1000;
		int numSamples = leadinSample->NumSamples() - offsetSamples;
		if (numSamples > 0) {
			SDL_AudioStreamPut(stream, data + offsetSamples, numSamples * sizeof(float));
		}
	}

	// Mark as playing
	paused = false;
}

void idSoundVoice_SDL::Stop() {
	paused = true;
	if (stream) {
		SDL_AudioStreamClear(stream);
	}
}

void idSoundVoice_SDL::Pause() {
	paused = true;
}

void idSoundVoice_SDL::UnPause() {
	paused = false;
}

bool idSoundVoice_SDL::Update() {
	if (!stream || paused) return false;

	// If we have a looping sample and the stream is getting low, add more data
	if (loopingSample && loopingSample->NumSamples() > 0) {
		int available = SDL_AudioStreamAvailable(stream);
		int maxSize = loopingSample->NumSamples() * sizeof(float) * 2; // Keep 2 buffers worth
		
		if (available < maxSize / 2) {
			const float* data = static_cast<const float*>(loopingSample->GetData());
			SDL_AudioStreamPut(stream, data, loopingSample->NumSamples() * sizeof(float));
		}
	}

	return true;
}

float idSoundVoice_SDL::GetAmplitude() {
	// Return a simple amplitude based on gain
	return GetGain();
}

bool idSoundVoice_SDL::CompatibleFormat(idSoundSample_SDL* s) {
	if (!s) return false;
	return (s->SampleRate() == sampleRate && s->NumChannels() == numChannels);
}

void idSoundVoice_SDL::OnBufferStart(idSoundSample_SDL* sample, int bufferNumber) {
	// Not used for SDL
}

bool idSoundVoice_SDL::IsPlaying() {
	if (!stream) return false;
	return !paused && SDL_AudioStreamAvailable(stream) > 0;
}

void idSoundVoice_SDL::FlushSourceBuffers() {
	if (stream) {
		SDL_AudioStreamClear(stream);
	}
}

void idSoundVoice_SDL::DestroyInternal() {
	if (stream) {
		SDL_FreeAudioStream(stream);
		stream = nullptr;
	}
	leadinSample = nullptr;
	loopingSample = nullptr;
}

int idSoundVoice_SDL::RestartAt(int offsetSamples) {
	if (!loopingSample) return -1;

	if (stream) {
		SDL_AudioStreamClear(stream);
	}

	const float* data = static_cast<const float*>(loopingSample->GetData());
	int numSamples = loopingSample->NumSamples() - offsetSamples;
	if (numSamples > 0) {
		SDL_AudioStreamPut(stream, data + offsetSamples, numSamples * sizeof(float));
	}

	return 0;
}

int idSoundVoice_SDL::SubmitBuffer(idSoundSample_SDL* sample, int bufferNumber, int offset) {
	if (!sample || !stream) return -1;

	const float* data = static_cast<const float*>(sample->GetData());
	int numSamples = sample->NumSamples() - offset;
	if (numSamples > 0) {
		SDL_AudioStreamPut(stream, data + offset, numSamples * sizeof(float));
	}

	return 0;
}

void idSoundVoice_SDL::SetSampleRate(uint32 newSampleRate, uint32 operationSet) {
	sampleRate = newSampleRate;
	// Recreate stream with new sample rate
	if (stream) {
		SDL_FreeAudioStream(stream);
		stream = SDL_NewAudioStream(AUDIO_F32SYS, numChannels, sampleRate,
									AUDIO_F32SYS, 2, 48000);
	}
}