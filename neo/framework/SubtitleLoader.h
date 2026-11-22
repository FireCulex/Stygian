/*
 ===========================================================================

 Doom 3 BFG Edition GPL Source Code
 Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.

 This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").

 Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

 In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms.
 You should have received a copy of these additional terms immediately following the terms
 and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.
 If not, please request a copy in writing from id Software at the address below.

 If you have questions concerning this license or the applicable additional terms, you may contact
 in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

 ===========================================================================
 */

#ifndef __SUBTITLELOADER_H__
#define __SUBTITLELOADER_H__

/*
 ==============================================================

 Subtitle file — a single user-generated JSON file (default
 "subtitles/subtitles.json" under the game install) holding
 subtitles for any number of sound shaders, produced by the
 external generator tool (tools/subtitle_gen). Nothing
 transcribed ships in the repo; a missing file simply means
 "no subtitles".

 Schema (times are integer ms relative to sound start, one entry
 per subtitle line):

   { "version": 2,
     "sounds": {
       "mc2_sarge_radio1": {
         "speaker": "Sgt. Kelly",
         "cues": [ { "start": 120, "end": 2540, "text": "All remaining marine units, this is Sergeant Kelly." }, ... ]
       },
       "sound/vo/mars_city2/01_ceilingguy_trigger.wav": { ... }
     } }

 Keys are sound shader names exactly as idSoundShader::GetName()
 returns them (radio shaders use their decl name, raw wave paths
 keep their path). A flat map without the "sounds" wrapper is
 also accepted.

 ==============================================================
 */

struct idSubtitleCue {
	idStr	text;
	int		start;				// ms relative to sound start
	int		end;				// ms relative to sound start
};

struct idSubtitleData {
	idStr									speaker;
	idList< idSubtitleCue, TAG_SUBTITLE >	cues;
};

class idSubtitleFile {
public:
						idSubtitleFile();

	// Loads (or reloads) the whole subtitle file. Missing file is not an
	// error: it just means no subtitles. Malformed file: warning, empty.
	bool				Load( const char *fileName );

	void				Clear();

	bool				IsLoaded() const { return loaded; }
	int					NumSounds() const { return sounds.Num(); }
	const char *		GetFileName() const { return fileName.c_str(); }
	const char *		GetSoundName( int index ) const { return sounds[index].name.c_str(); }
	const idSubtitleData *	GetSoundData( int index ) const { return &sounds[index].data; }

	// Returns the subtitle for a sound shader, or NULL when there is none.
	// The pointer stays valid until the next Load()/Clear().
	const idSubtitleData *	Find( const char *soundShaderName ) const;

private:
	struct entry_t {
		idStr			name;
		idSubtitleData	data;
	};

	bool				Parse( const char *buffer, int length, const char *name );

	idList< entry_t, TAG_SUBTITLE >	sounds;
	idStr							fileName;
	bool							loaded;
};

#endif /* !__SUBTITLELOADER_H__ */
