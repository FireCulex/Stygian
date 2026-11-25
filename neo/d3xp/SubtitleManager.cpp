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

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "Game_local.h"
#include "../framework/DeclManager.h"
#include "../sound/sound.h"
#include "SubtitleManager.h"

idCVar ui_subtitles( "ui_subtitles", "1", CVAR_ARCHIVE | CVAR_INTEGER, "subtitle display: 0 = off, 1 = on", 0, 1 );
idCVar ui_subtitleFile( "ui_subtitleFile", "subtitles/subtitles.json", CVAR_ARCHIVE, "subtitle file, relative to the game dir" );
idCVar ui_subtitleExtraTime( "ui_subtitleExtraTime", "0", CVAR_ARCHIVE | CVAR_INTEGER, "ms a subtitle stays visible after its last cue ended" );
idCVar ui_subtitleScale( "ui_subtitleScale", "0.44", CVAR_FLOAT, "subtitle font scale (~20% smaller than the old 0.55: line pitch drops from ~6% to ~5% of screen height)", 0.25f, 0.5f );
idCVar ui_subtitleDebug( "ui_subtitleDebug", "0", CVAR_ARCHIVE | CVAR_INTEGER, "log sound starts: 1 = subtitle entries + radio/raw-vo dialogue + cues on screen; 2 = every speech start (VOICE/VOICE2/RADIO/PDA + sound/vo/ waves on ANY, e.g. cinematic sound_global) with entity name — BODY excluded (prop spam)", 0, 2 );

idSubtitleManager subtitleManager;

// repeat starts of the same shader within this window are ignored
// (map scripts can retry a sound every frame when it reports len 0)
static const int kSubtitleDebounceMs = 500;

/*
  ====================
  SubtitleReload_f
  ====================
  */
static void SubtitleReload_f( const idCmdArgs &args ) {
	subtitleManager.Reload();
}

/*
  ====================
  idSubtitleManager::idSubtitleManager
  ====================
  */
idSubtitleManager::idSubtitleManager() {
	actives.Clear();
	lastTime = -kSubtitleDebounceMs - 1;
	dcReady = false;
	cmdSystem->AddCommand( "subtitle_reload", SubtitleReload_f, CMD_FL_SYSTEM, "reload the subtitle file" );
}

/*
  ====================
  idSubtitleManager::Shutdown

  Drops cached data and the device context so the next map re-inits
  cleanly (materials can be recreated by a vid_restart).
  ====================
  */
void idSubtitleManager::Shutdown() {
	actives.Clear();
	dcReady = false;
	loadedName.Clear();
	file.Clear();
	events.Clear();
	lastShader.Clear();
	lastTime = -kSubtitleDebounceMs - 1;
}

/*
  ====================
  idSubtitleManager::Reload
  ====================
  */
void idSubtitleManager::Reload() {
	actives.Clear();
	loadedName.Clear();
	file.Clear();
	EnsureLoaded();

	if ( ui_subtitleDebug.GetInteger() >= 1 && file.IsLoaded() ) {
		idLib::Printf( "Subtitles: %d sound(s) from '%s'\n", file.NumSounds(), file.GetFileName() );
		for ( int i = 0; i < file.NumSounds(); i++ ) {
			const idSubtitleData *d = file.GetSoundData( i );
			idLib::Printf( "  key: %s (speaker='%s', %d cues)\n",
				file.GetSoundName( i ), d ? d->speaker.c_str() : "", d ? d->cues.Num() : 0 );
		}
	}
}

/*
  ====================
  idSubtitleManager::EnsureLoaded

  Loads the subtitle file on first use, or again if ui_subtitleFile changed.
  ====================
  */
void idSubtitleManager::EnsureLoaded() {
	const char *fileName = ui_subtitleFile.GetString();
	if ( file.IsLoaded() && loadedName == fileName ) {
		return;
	}
	loadedName = fileName;
	file.Load( fileName );
	if ( ui_subtitleDebug.GetInteger() >= 1 && file.IsLoaded() ) {
		idLib::Printf( "Subtitles: %d sound(s) from '%s'\n", file.NumSounds(), file.GetFileName() );
		for ( int i = 0; i < file.NumSounds(); i++ ) {
			const idSubtitleData *d = file.GetSoundData( i );
			idLib::Printf( "  key: %s (speaker='%s', %d cues)\n",
				file.GetSoundName( i ), d ? d->speaker.c_str() : "", d ? d->cues.Num() : 0 );
		}
	}
}

/*
  ====================
  idSubtitleManager::IsSubtitleSound
  ====================
 */
bool idSubtitleManager::IsSubtitleSound( const char *shaderName ) {
	if ( shaderName == NULL || shaderName[0] == '\0' ) {
		return false;
	}
	EnsureLoaded();
	return file.Find( shaderName ) != NULL;
}

/*
  ====================
  idSubtitleManager::OnSoundStart

  Called from idEntity::StartSoundShader for every sound start (even
  len == 0, e.g. s_noSound, so subtitles still work in muted runs).

  - every start is recorded in the event ring (MCP "subtitle_log");
  - repeat starts of the same shader within kSubtitleDebounceMs are
    dropped entirely (script loops re-firing a len==0 sound);
  - console logging under ui_subtitleDebug: subtitle-keyed sounds and
    VOICE/RADIO dialogue (radio channel or raw sound/vo/ wave paths);
    each displayed cue is logged from Draw() as it appears on screen;
  - display is driven purely by the presence of an entry for the
    shader — no channel filtering (covers VOICE, RADIO and speaker
    entities playing raw wave paths);
  - a pure sound-effect caption ("(screaming)" & co, flagged sfxCaption
    at load) may display when nothing else is active, but NEVER preempts
    a live dialogue subtitle — looping monster screams used to override
    radio lines with this.
  ====================
  */
void idSubtitleManager::OnSoundStart( idEntity *ent, const char *shaderName, int lengthMs, int channel ) {
	if ( shaderName == NULL || shaderName[0] == '\0' ) {
		return;
	}

	// blacklist: combat barks ("combat_chatter" -> "(screaming)" spam) never
	// produce subtitles and are not logged. Raw sound/vo/ wave paths are never
	// blacklisted, so real dialogue like science_team_chatter.wav still works.
	const bool isBlacklisted = idStr::Icmpn( shaderName, "sound/vo/", 9 ) != 0
		&& idStr( shaderName ).Find( "combat_chatter", false ) >= 0;
	if ( isBlacklisted ) {
		return;
	}

	// debounce repeat starts of the same shader
	if ( lastShader == shaderName && gameLocal.time - lastTime < kSubtitleDebounceMs ) {
		return;
	}
	lastShader = shaderName;
	lastTime = gameLocal.time;

	EnsureLoaded();

	const idSubtitleData *data = file.Find( shaderName );

	// record in the event ring
	if ( events.Num() >= kMaxEvents ) {
		events.RemoveIndex( 0 );
	}
	idSubtitleEvent &ev = events.Alloc();
	ev.time = gameLocal.time;
	ev.shader = shaderName;
	ev.speaker = data ? data->speaker : "";
	ev.numCues = data ? data->cues.Num() : 0;
	ev.lengthMs = lengthMs;
	ev.displayed = false;

	// tiered console logging
	const int dbg = ui_subtitleDebug.GetInteger();
	if ( dbg >= 2 ) {
		// every speech start, with the speaking entity — this is how you find
		// WHICH wav a guy standing next to you is playing. BODY channels
		// excluded: props/machinery/footsteps spam them constantly.
		bool speech = channel == SND_CHANNEL_VOICE || channel == SND_CHANNEL_VOICE2
			|| channel == SND_CHANNEL_RADIO || channel == SND_CHANNEL_PDA_AUDIO
			|| channel == SND_CHANNEL_PDA_VIDEO;
		if ( !speech ) {
			// anim frame commands ("sound_global" etc.) play voice on
			// SND_CHANNEL_ANY — still log those when the shader's wave
			// actually lives under sound/vo/ (e.g. marscity_rocketintro_vo)
			if ( idStr::Icmpn( shaderName, "sound/vo/", 9 ) == 0 ) {
				speech = true;
			} else {
				const idSoundShader *sh = declManager->FindSound( shaderName, false );
				if ( sh != NULL ) {
					for ( int i = 0; i < sh->GetNumSounds(); i++ ) {
						const char *w = sh->GetSound( i );
						if ( w != NULL && idStr::Icmpn( w, "sound/vo/", 9 ) == 0 ) {
							speech = true;
							break;
						}
					}
				}
			}
		}
		if ( speech ) {
			// skip AI chatter decls (monster_*_chatter etc.) — pure spam; raw
			// sound/vo/ wave paths are still logged
			const bool chatterDecl = idStr::Icmpn( shaderName, "sound/vo/", 9 ) != 0
				&& idStr( shaderName ).Find( "_chatter", false ) >= 0;
			if ( !chatterDecl ) {
				idLib::Printf( "PlayWav: %s ch=%d ent=%s len=%dms%s\n", shaderName, channel,
					ent != NULL ? ent->GetName() : "?", lengthMs, data != NULL ? " [subtitle]" : "" );
			}
		}
	} else if ( dbg >= 1 ) {
		if ( data != NULL ) {
			idLib::Printf( "Subtitle: %s speaker='%s' cues=%d len=%dms\n",
				shaderName, data->speaker.c_str(), data->cues.Num(), lengthMs );
		} else if ( channel == SND_CHANNEL_RADIO
			|| ( ( channel == SND_CHANNEL_VOICE || channel == SND_CHANNEL_VOICE2 )
				&& idStr::Icmpn( shaderName, "sound/vo/", 9 ) == 0 ) ) {
			// dialogue: radio channel or a raw voice-over wave path (skips AI
			// chatter decl names like monster_*_chatter, which are VOICE too)
			idLib::Printf( "PlayWav: %s len=%dms\n", shaderName, lengthMs );
		}
	}

	if ( ui_subtitles.GetBool() == false || data == NULL ) {
		return;
	}

	// sound-effect captions never preempt a live dialogue subtitle
	if ( data->sfxCaption ) {
		for ( int i = 0; i < actives.Num(); i++ ) {
			if ( !actives[i].data->sfxCaption && EntryLive( actives[i] ) ) {
				return;
			}
		}
	}

	// re-fire of an already-displayed shader: restart its clock instead of
	// stacking a duplicate
	for ( int i = 0; i < actives.Num(); i++ ) {
		if ( actives[i].data == data ) {
			actives[i].startTime = gameLocal.time;
			actives[i].soundLength = lengthMs;
			actives[i].lastShownCue = -1;
			ev.displayed = true;
			return;
		}
	}

	// stack overlapping speakers (cinematic VO overlaps a lot); when full,
	// drop the oldest
	if ( actives.Num() >= kMaxActive ) {
		actives.RemoveIndex( 0 );
	}
	ActiveSub &a = actives.Alloc();
	a.data = data;
	a.startTime = gameLocal.time;
	a.soundLength = lengthMs;
	a.lastShownCue = -1;		// Draw() logs each cue as it actually appears
	ev.displayed = true;
}

/*
  ====================
  idSubtitleManager::EntryDuration / EntryLive

  Effective on-screen window of one stacked subtitle (same test Draw()
  uses to hide it).
  ====================
  */
int idSubtitleManager::EntryDuration( const ActiveSub &a ) {
	if ( a.data == NULL || a.data->cues.Num() == 0 ) {
		return a.soundLength;
	}
	const int lastCueEnd = a.data->cues[a.data->cues.Num() - 1].end;
	// when the sound length is known, never outlast the last cue by more
	// than the sound itself (wav padding/music is not dialogue)
	return ( a.soundLength > 0 && a.soundLength < lastCueEnd ) ? a.soundLength : lastCueEnd;
}

bool idSubtitleManager::EntryLive( const ActiveSub &a ) const {
	const int elapsed = gameLocal.time - a.startTime;
	return elapsed <= EntryDuration( a ) + ui_subtitleExtraTime.GetInteger();
}

/*
  ====================
  idSubtitleManager::Draw
  ====================
  */
void idSubtitleManager::Draw() {
	if ( ui_subtitles.GetBool() == false || actives.Num() == 0 ) {
		return;
	}

	if ( !dcReady ) {
		dc.Init();
		dc.SetSize( VIRTUAL_WIDTH, VIRTUAL_HEIGHT );
		dcReady = true;
	}

	// clamp here too: a stale archived value (e.g. 0.55) must not blow up the display
	const float scale = idMath::ClampFloat( 0.25f, 0.5f, ui_subtitleScale.GetFloat() );
	const float lineH = (float)dc.MaxCharHeight( scale );
	// cap the wrap width at ~65% of the screen, so long lines don't run
	// nearly edge to edge
	const float maxTextW = VIRTUAL_WIDTH * 0.65f;

	// collect live entries (oldest first = bottom-most)
	idList< const ActiveSub * > live;
	for ( int i = 0; i < actives.Num(); i++ ) {
		if ( EntryLive( actives[i] ) ) {
			live.Append( &actives[i] );
		}
	}
	// purge expired
	for ( int i = actives.Num() - 1; i >= 0; i-- ) {
		if ( !EntryLive( actives[i] ) ) {
			actives.RemoveIndex( i );
		}
	}
	if ( live.Num() == 0 ) {
		return;
	}

	// build all text rects first (measure without drawing)
	struct Measured {
		ActiveSub *			entry;
		idStr				text;
		int					cueIndex;
		idRectangle			rect;
	};
	idList< Measured, TAG_SUBTITLE > measured;
	for ( int i = 0; i < live.Num(); i++ ) {
		ActiveSub *a = const_cast< ActiveSub * >( live[i] );
		const int elapsed = gameLocal.time - a->startTime;

		// current cue: the last cue whose start time has passed — but only
		// while its own end has not passed either, so a cue never lingers
		// into the pause before the next cue
		int cueIndex = -1;
		for ( int j = 0; j < a->data->cues.Num(); j++ ) {
			if ( a->data->cues[j].start > elapsed ) {
				break;
			}
			cueIndex = j;
		}
		if ( cueIndex < 0 ) {
			continue;
		}
		const idSubtitleCue *cue = &a->data->cues[cueIndex];
		if ( elapsed > cue->end ) {
			continue;
		}

		// on-screen text is the cue only — the speaker name is deliberately not
		// drawn (it stays in the debug log and the subtitle event ring)
		const idStr text = cue->text;

		// measure wrapped lines with the SAME metric DrawText wraps with
		// (TextWidth per segment). Summing per-char CharWidth drifted from
		// the real glyph widths, which made long cues one line short and
		// clipped their wrapped tail.
		int lines = 0;
		float textW = 0.0f;
		idStr seg;
		for ( int k = 0; ; k++ ) {
			const char c = text[k];
			if ( c == '\n' || c == '\0' ) {
				const float w = (float)dc.TextWidth( seg.c_str(), scale, 0 );
				lines += ( w > maxTextW ? (int)( w / maxTextW ) + 1 : 1 );
				if ( w > textW ) {
					textW = w;
				}
				seg.Clear();
				if ( c == '\0' ) {
					break;
				}
			} else {
				seg += c;
			}
		}
		if ( lines < 1 ) {
			lines = 1;
		}

		const float rectW = ( textW < maxTextW ? textW : maxTextW );
		// DrawText advances y by (lineSkip + 5) per wrapped line and clips any
		// line past the rect bottom (DeviceContext.cpp), so the rect must
		// reserve that extra 5 per additional line or the tail gets clipped.
		const float rectH = lines * lineH + ( lines - 1 ) * 5.0f;
		const float rectX = ( VIRTUAL_WIDTH - rectW ) * 0.5f;
		// bottom center, raised ~5% of the screen height so the text clears the
		// HUD ammo counter and the health/armor strip
		const float rectY = VIRTUAL_HEIGHT - rectH - 24.0f - VIRTUAL_HEIGHT * 0.05f;

		Measured m;
		m.entry = a;
		m.text = text;
		m.cueIndex = cueIndex;
		m.rect = idRectangle( rectX, rectY, rectW, rectH );
		measured.Append( m );

		// log each cue as it appears on screen, speaker included for context
		if ( ui_subtitleDebug.GetInteger() >= 1 && cueIndex != a->lastShownCue ) {
			idStr logText;
			if ( !a->data->speaker.IsEmpty() ) {
				logText = a->data->speaker;
				logText += ": ";
			}
			logText += cue->text;
			idLib::Printf( "SUBTITLE: \"%s\"\n", logText.c_str() );
			a->lastShownCue = cueIndex;
		}
	}
	if ( measured.Num() == 0 ) {
		return;
	}

	// draw stacked, oldest at the bottom, newest on top, small gap between
	// blocks. Hard-sub style per line: drop shadow, black outline (8 offsets),
	// white face LAST — in the HUD 2D pass later-inserted surfaces render ON
	// TOP, so the face must be inserted last or the text comes out black.
	// NO background box (user request 2026-11-24). The rect is passed to
	// DrawText for wrap/centering.
	const float kLineGap = 2.0f;
	const float kShadowOff = 2.0f;
	const float kOutline = 1.5f;
	static const float kDirs[8][2] = {
		{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 }
	};
	float cursorY = measured[0].rect.y;		// start from the bottom-most block
	for ( int i = 0; i < measured.Num(); i++ ) {
		const Measured &m = measured[i];
		idRectangle r = m.rect;
		r.y = cursorY;
		idRectangle sr = r;
		sr.Offset( kShadowOff, kShadowOff );
		dc.DrawText( m.text.c_str(), scale, idDeviceContext::ALIGN_CENTER, idVec4( 0.0f, 0.0f, 0.0f, 0.85f ), sr, true );
		for ( int j = 0; j < 8; j++ ) {
			sr = r;
			sr.Offset( kOutline * kDirs[j][0], kOutline * kDirs[j][1] );
			dc.DrawText( m.text.c_str(), scale, idDeviceContext::ALIGN_CENTER, idVec4( 0.0f, 0.0f, 0.0f, 1.0f ), sr, true );
		}
		dc.DrawText( m.text.c_str(), scale, idDeviceContext::ALIGN_CENTER, idVec4( 1.0f, 1.0f, 1.0f, 1.0f ), r, true );
		cursorY -= m.rect.h + kLineGap;
	}
}
