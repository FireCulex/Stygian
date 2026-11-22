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
#include "SubtitleManager.h"

idCVar ui_subtitles( "ui_subtitles", "1", CVAR_ARCHIVE | CVAR_INTEGER, "subtitle display: 0 = off, 1 = on", 0, 1 );
idCVar ui_subtitleFile( "ui_subtitleFile", "subtitles/subtitles.json", CVAR_ARCHIVE, "subtitle file, relative to the game dir" );
idCVar ui_subtitleExtraTime( "ui_subtitleExtraTime", "2500", CVAR_ARCHIVE | CVAR_INTEGER, "ms a subtitle stays visible after its sound ended" );
idCVar ui_subtitleScale( "ui_subtitleScale", "0.44", CVAR_FLOAT, "subtitle font scale (~20% smaller than the old 0.55: line pitch drops from ~6% to ~5% of screen height)", 0.25f, 0.5f );
idCVar ui_subtitleBgAlpha( "ui_subtitleBgAlpha", "0.25", CVAR_ARCHIVE | CVAR_FLOAT, "subtitle background alpha (faint box)", 0.0f, 0.3f );
idCVar ui_subtitleDebug( "ui_subtitleDebug", "0", CVAR_ARCHIVE | CVAR_BOOL, "log subtitle events: entry sounds, voice/radio plays, and each cue as it appears on screen" );

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
	active = false;
	activeData = NULL;
	startTime = 0;
	soundLength = 0;
	lastShownCue = -1;
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
	active = false;
	activeData = NULL;
	lastShownCue = -1;
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
	active = false;
	activeData = NULL;
	loadedName.Clear();
	file.Clear();
	EnsureLoaded();

	if ( ui_subtitleDebug.GetBool() && file.IsLoaded() ) {
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
	if ( ui_subtitleDebug.GetBool() && file.IsLoaded() ) {
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
    entities playing raw wave paths).
  ====================
  */
void idSubtitleManager::OnSoundStart( idEntity *ent, const char *shaderName, int lengthMs, int channel ) {
	if ( shaderName == NULL || shaderName[0] == '\0' ) {
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
	if ( ui_subtitleDebug.GetBool() ) {
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

	active = true;
	activeData = data;
	startTime = gameLocal.time;
	soundLength = lengthMs;
	ev.displayed = true;
	lastShownCue = -1;		// Draw() logs each cue as it actually appears
}

/*
  ====================
  idSubtitleManager::Draw
  ====================
  */
void idSubtitleManager::Draw() {
	if ( ui_subtitles.GetBool() == false || !active || activeData == NULL ) {
		return;
	}

	const int elapsed = gameLocal.time - startTime;

	// hide when the sound is over (plus linger); fall back to the last cue's end
	int duration = soundLength;
	if ( duration <= 0 && activeData->cues.Num() > 0 ) {
		duration = activeData->cues[activeData->cues.Num() - 1].end;
	}
	if ( elapsed > duration + ui_subtitleExtraTime.GetInteger() ) {
		active = false;
		activeData = NULL;
		return;
	}

	// show the current cue only: the last cue whose start time has passed
	int cueIndex = -1;
	for ( int i = 0; i < activeData->cues.Num(); i++ ) {
		if ( activeData->cues[i].start > elapsed ) {
			break;
		}
		cueIndex = i;
	}
	if ( cueIndex < 0 ) {
		return;
	}
	const idSubtitleCue *cue = &activeData->cues[cueIndex];

	// on-screen text is the cue only — the speaker name is deliberately not
	// drawn (it stays in the debug log and the subtitle event ring)
	const idStr text = cue->text;

	// log each cue as it appears on screen, speaker included for context
	if ( ui_subtitleDebug.GetBool() && cueIndex != lastShownCue ) {
		idStr logText;
		if ( !activeData->speaker.IsEmpty() ) {
			logText = activeData->speaker;
			logText += ": ";
		}
		logText += cue->text;
		idLib::Printf( "SUBTITLE: \"%s\"\n", logText.c_str() );
		lastShownCue = cueIndex;
	}

	if ( !dcReady ) {
		dc.Init();
		dc.SetSize( VIRTUAL_WIDTH, VIRTUAL_HEIGHT );
		dcReady = true;
	}

	// clamp here too: a stale archived value (e.g. 0.55) must not blow up the display
	const float scale = idMath::ClampFloat( 0.25f, 0.5f, ui_subtitleScale.GetFloat() );
	const float lineH = (float)dc.MaxCharHeight( scale );
	const float padX = 12.0f;
	const float padY = 6.0f;
	// cap the wrap width at ~65% of the screen (box incl. padding <= ~69%),
	// so the box no longer runs nearly edge to edge
	const float maxTextW = VIRTUAL_WIDTH * 0.65f;

	// Size the box with the SAME metric DrawText wraps with (TextWidth per
	// segment). Summing per-char CharWidth drifted from the real glyph widths,
	// which made long cues one line short and clipped their wrapped tail.
	int lines = 0;
	float textW = 0.0f;
	idStr seg;
	for ( int i = 0; ; i++ ) {
		const char c = text[i];
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

	const float boxW = ( textW < maxTextW ? textW : maxTextW ) + 2.0f * padX;
	// DrawText advances y by (lineSkip + 5) per wrapped line and clips any
	// line past the rect bottom (DeviceContext.cpp), so the box must reserve
	// that extra 5 per additional line or the wrapped tail gets clipped.
	const float boxH = lines * lineH + ( lines - 1 ) * 5.0f + 2.0f * padY;
	const float boxX = ( VIRTUAL_WIDTH - boxW ) * 0.5f;
	// bottom center, raised ~5% of the screen height so the box clears the
	// HUD ammo counter and the health/armor strip
	const float boxY = VIRTUAL_HEIGHT - boxH - 24.0f - VIRTUAL_HEIGHT * 0.05f;

	// soft drop shadow: stacked expanding rects, darkest at the box edge and
	// fading out over ~3px (blur). A hard offset shadow alone gets lost on
	// bright surfaces; this holds up everywhere.
	const int kShadowLayers = 3;
	const float kShadowAlpha = 0.9f;
	for ( int i = kShadowLayers; i >= 1; i-- ) {
		const float expand = (float)i;
		const float a = kShadowAlpha * ( 1.0f - (float)i / (float)( kShadowLayers + 1 ) );
		dc.DrawFilledRect( boxX - expand, boxY - expand, boxW + 2.0f * expand, boxH + 2.0f * expand,
			idVec4( 0.0f, 0.0f, 0.0f, a ) );
	}

	// faint background box (~20-30% opacity)
	const float bgAlpha = idMath::ClampFloat( 0.0f, 0.3f, ui_subtitleBgAlpha.GetFloat() );
	dc.DrawFilledRect( boxX, boxY, boxW, boxH, idVec4( 0.0f, 0.0f, 0.0f, bgAlpha ) );

	// thin black outline on the box edge, so it also reads on dark scenes
	dc.DrawRect( boxX, boxY, boxW, boxH, 1.0f, idVec4( 0.0f, 0.0f, 0.0f, 0.9f ) );

	dc.DrawText( text.c_str(), scale, idDeviceContext::ALIGN_CENTER, idVec4( 1.0f, 1.0f, 1.0f, 1.0f ),
		idRectangle( boxX + padX, boxY + padY, boxW - 2.0f * padX, boxH - 2.0f * padY ), true );
}
