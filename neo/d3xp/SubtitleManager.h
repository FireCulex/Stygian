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
  but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

  In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy
  of these additional terms immediate following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.

  If you have questions regarding this license or the applicable additional terms, you may contact
  in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

  ===========================================================================
  */

#ifndef __SUBTITLEMANAGER_H__
#define __SUBTITLEMANAGER_H__

#include "../framework/SubtitleLoader.h"
#include "../ui/DeviceContext.h"

class idEntity;

/*
  ==============================================================

  Subtitle manager — displays user-generated subtitles as an
  engine-drawn 2D overlay (no SWF changes) while a sound with an
  entry in the subtitle file is playing.

  Hooked from idEntity::StartSoundShader; drawn each frame from
  idPlayer::UpdateHud right after the HUD SWF pass.

  Also keeps a small ring of recent sound-start events (the
  "subtitle log") so dialogue activity can be queried via MCP
  without wading through the general console log.

  ==============================================================
 */

struct idSubtitleEvent {
	int			time;			// gameLocal.time when the sound started
	idStr		shader;			// sound shader name
	idStr		speaker;			// empty when the shader has no subtitle entry
	int			numCues;		// 0 when no entry
	int			lengthMs;
	bool		displayed;		// a subtitle was shown for it
};

class idSubtitleManager {
public:
							idSubtitleManager();

	// Called from idEntity::StartSoundShader after a sound started.
	// lengthMs is the sound length in milliseconds (0 if unknown),
	// channel is the SND_CHANNEL_* the sound was started on.
	void					OnSoundStart( idEntity *ent, const char *shaderName, int lengthMs, int channel );

	// Called every frame from idPlayer::UpdateHud (after the HUD render).
	void					Draw();

	// True when the shader has a subtitle entry (dialogue whitelist used by
	// the s_showStartSound debug logging). Extension-tolerant re: ".wav".
	bool					IsSubtitleSound( const char *shaderName );

	void					Reload();			// "subtitle_reload" command
	void					Shutdown();

	// Ring of recent sound-start events (newest last), for the MCP
	// "subtitle_log" tool. Capped at kMaxEvents.
	const idList< idSubtitleEvent, TAG_SUBTITLE > &
							GetEvents() const { return events; }
	static const int		kMaxEvents = 512;

private:
	void					EnsureLoaded();

	// one live on-screen subtitle (a sound with an entry is playing)
	struct ActiveSub {
		const idSubtitleData *	data;
		int						startTime;		// gameLocal.time when the sound started
		int						soundLength;	// ms, 0 if unknown
		int						lastShownCue;	// cue index currently on screen (-1 = none), for debug logging
	};

	// effective on-screen duration of an entry: the last cue's end (clamped
	// to the sound length when known) — wavs often carry music/padding past
	// the last spoken cue, and showing until sound end made subs linger
	static int				EntryDuration( const ActiveSub &a );

	// true while the entry is still within its duration + linger window
	bool					EntryLive( const ActiveSub &a ) const;

	idSubtitleFile			file;
	idStr					loadedName;			// file name the current data came from

	idList< ActiveSub, TAG_SUBTITLE >
							actives;			// live subtitles, oldest first; drawn stacked (newest on top)
	static const int		kMaxActive = 3;		// max simultaneous on-screen subtitles

	idList< idSubtitleEvent, TAG_SUBTITLE >
							events;				// ring of recent sound-start events
	idStr					lastShader;			// debounce: repeat starts of the same shader
	int						lastTime;			// ...within this window are ignored

	idDeviceContext			dc;
	bool					dcReady;
};

extern idSubtitleManager	subtitleManager;

#endif /* !__SUBTITLEMANAGER_H__ */
