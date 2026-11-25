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

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "../idlib/Token.h"
#include "../idlib/Lexer.h"
#include "SubtitleLoader.h"

/*
 ==============================================================

	JSON subset reader for user-generated subtitle files.

	Everything here is non-fatal on purpose: the file is user
	supplied, so a syntax error must never bring the game down.
	We deliberately avoid idParser/idLexer Expect*() helpers,
	which call Error() (fatal) on a mismatch.

 ==============================================================
 */

static const int SUBTITLE_LEXFLAGS = LEXFL_NOERRORS | LEXFL_NOWARNINGS | LEXFL_NOFATALERRORS |
									 LEXFL_NOSTRINGCONCAT | LEXFL_ALLOWPATHNAMES;

/*
 ====================
 SubSkipValue

 Skips one JSON value (string, number, literal, array or object).
 ====================
 */
static bool SubSkipValue( idLexer &lexer ) {
	idToken token;
	if ( !lexer.ReadToken( &token ) ) {
		return false;
	}
	if ( token != "{" && token != "[" ) {
		return true;
	}
	int depth = 1;
	while ( depth > 0 ) {
		if ( !lexer.ReadToken( &token ) ) {
			return false;
		}
		if ( token == "{" || token == "[" ) {
			depth++;
		} else if ( token == "}" || token == "]" ) {
			depth--;
		}
	}
	return true;
}

/*
  ====================
  SubIsSfxCaption

  True when the cue text is purely a parenthetical sound effect, e.g.
  "(screaming)" or "(distant explosion)". Such captions describe noise,
  not speech — they may show on their own but must not override a live
  dialogue subtitle.
  ====================
  */
static bool SubIsSfxCaption( const idStr &text ) {
	const int len = text.Length();
	if ( len < 2 || text[0] != '(' || text[len - 1] != ')' ) {
		return false;
	}
	// reject "(hi) there" style mixed text: nothing but the outer parens
	for ( int i = 1; i < len - 1; i++ ) {
		if ( text[i] == '(' || text[i] == ')' ) {
			return false;
		}
	}
	return true;
}

/*
  ====================
  SubFinishEntry

  Derives the per-entry flags after the cues are parsed.
  ====================
  */
static void SubFinishEntry( idSubtitleData &data ) {
	data.sfxCaption = data.cues.Num() > 0;
	for ( int i = 0; i < data.cues.Num(); i++ ) {
		if ( !SubIsSfxCaption( data.cues[i].text ) ) {
			data.sfxCaption = false;
			break;
		}
	}
}

/*
  ====================
  SubParseCues

  Parses the "cues" array into out: [{ "start": ms, "end": ms, "text": "..." }].
  Cues with empty text or end < start are skipped.
  ====================
  */
static bool SubParseCues( idLexer &lexer, idSubtitleData &out ) {
	idToken token;
	if ( !lexer.ReadToken( &token ) || token != "[" ) {
		return false;
	}
	while ( 1 ) {
		if ( !lexer.ReadToken( &token ) ) {
			return false;
		}
		if ( token == "]" ) {
			break;
		}
		if ( token == "," ) {
			continue;
		}
		if ( token != "{" ) {
			return false;
		}

		idSubtitleCue cue;
		cue.start = 0;
		cue.end = 0;
		bool gotText = false;

		while ( 1 ) {
			if ( !lexer.ReadToken( &token ) ) {
				return false;
			}
			if ( token == "}" ) {
				break;
			}
			if ( token == "," ) {
				continue;
			}

			const idStr key = token;
			if ( !lexer.ReadToken( &token ) || token != ":" ) {
				return false;
			}

			if ( key == "text" || key == "phrase" ) {
				if ( !lexer.ReadToken( &token ) ) {
					return false;
				}
				cue.text = token;
				gotText = true;
			} else if ( key == "start" || key == "startTime" || key == "time" ) {
				if ( !lexer.ReadToken( &token ) ) {
					return false;
				}
				cue.start = ( token.type == TT_NUMBER ) ? token.GetIntValue() : 0;
			} else if ( key == "end" || key == "endTime" ) {
				if ( !lexer.ReadToken( &token ) ) {
					return false;
				}
				cue.end = ( token.type == TT_NUMBER ) ? token.GetIntValue() : 0;
			} else if ( !SubSkipValue( lexer ) ) {
				return false;
			}
		}

		if ( gotText && !cue.text.IsEmpty() && cue.end >= cue.start ) {
			out.cues.Append( cue );
		}
	}
	return true;
}

/*
 ====================
 SubParseEntry

 Parses one "speaker"/"cues" object. Expects the opening brace.
 ====================
 */
static bool SubParseEntry( idLexer &lexer, idSubtitleData &out ) {
	idToken token;
	if ( !lexer.ReadToken( &token ) || token != "{" ) {
		return false;
	}
	while ( 1 ) {
		if ( !lexer.ReadToken( &token ) ) {
			return false;
		}
		if ( token == "}" ) {
			break;
		}
		if ( token == "," ) {
			continue;
		}

		const idStr key = token;
		if ( !lexer.ReadToken( &token ) || token != ":" ) {
			return false;
		}

		if ( key == "speaker" ) {
			if ( !lexer.ReadToken( &token ) ) {
				return false;
			}
			out.speaker = token;
		} else if ( key == "cues" ) {
			if ( !SubParseCues( lexer, out ) ) {
				return false;
			}
		} else if ( !SubSkipValue( lexer ) ) {
			return false;
		}
	}
	return true;
}

/*
 ====================
 idSubtitleFile::idSubtitleFile
 ====================
 */
idSubtitleFile::idSubtitleFile() {
	loaded = false;
}

/*
 ====================
 idSubtitleFile::Clear
 ====================
 */
void idSubtitleFile::Clear() {
	sounds.Clear();
	fileName.Clear();
	loaded = false;
}

/*
 ====================
 idSubtitleFile::Parse
 ====================
 */
bool idSubtitleFile::Parse( const char *buffer, int length, const char *name ) {
	idLexer lexer( SUBTITLE_LEXFLAGS );
	if ( !lexer.LoadMemory( buffer, length, name ) ) {
		return false;
	}

	idToken token;
	if ( !lexer.ReadToken( &token ) || token != "{" ) {
		return false;
	}

	while ( 1 ) {
		if ( !lexer.ReadToken( &token ) ) {
			return false;			// ran off the end before the closing brace
		}
		if ( token == "}" ) {
			break;
		}
		if ( token == "," ) {
			continue;
		}

		const idStr key = token;
		if ( !lexer.ReadToken( &token ) || token != ":" ) {
			return false;
		}

		if ( key == "sounds" ) {
			// canonical layout: { "sounds": { "<shader>": { ... } } }
			if ( !lexer.ReadToken( &token ) || token != "{" ) {
				return false;
			}
			while ( 1 ) {
				if ( !lexer.ReadToken( &token ) ) {
					return false;
				}
				if ( token == "}" ) {
					break;
				}
				if ( token == "," ) {
					continue;
				}

				const idStr soundName = token;
				if ( !lexer.ReadToken( &token ) || token != ":" ) {
					return false;
				}

				entry_t entry;
				entry.name = soundName;
				if ( !SubParseEntry( lexer, entry.data ) ) {
					return false;
				}
				SubFinishEntry( entry.data );
				if ( entry.data.cues.Num() > 0 ) {
					sounds.Append( entry );
				}
			}
		} else if ( key == "version" || key == "language" || key == "generator" ) {
			if ( !SubSkipValue( lexer ) ) {
				return false;
			}
		} else {
			// tolerate a flat map: { "<shader>": { ... } }
			entry_t entry;
			entry.name = key;
			if ( !SubParseEntry( lexer, entry.data ) ) {
				return false;
			}
			SubFinishEntry( entry.data );
			if ( entry.data.cues.Num() > 0 ) {
				sounds.Append( entry );
			}
		}
	}

	return sounds.Num() > 0;
}

/*
 ====================
 idSubtitleFile::Load
 ====================
 */
bool idSubtitleFile::Load( const char *fileName ) {
	Clear();
	this->fileName = fileName;
	loaded = true;			// remember the attempt so a missing file is not retried per sound

	if ( fileName == NULL || fileName[0] == '\0' ) {
		return false;
	}

	void *buffer = NULL;
	const int length = fileSystem->ReadFile( fileName, &buffer );
	if ( length <= 0 || buffer == NULL ) {
		return false;
	}

	const bool ok = Parse( (const char *)buffer, length, fileName );
	Mem_Free( buffer );

	if ( !ok ) {
		sounds.Clear();
		idLib::Warning( "Subtitle file '%s' is malformed - subtitles disabled", fileName );
		return false;
	}

	idLib::Printf( "Subtitles: %d sound(s) from '%s'\n", sounds.Num(), fileName );
	return true;
}

/*
 ====================
 idSubtitleFile::Find
 ====================
 */
const idSubtitleData *idSubtitleFile::Find( const char *soundShaderName ) const {
	if ( soundShaderName == NULL || soundShaderName[0] == '\0' ) {
		return NULL;
	}
	for ( int i = 0; i < sounds.Num(); i++ ) {
		if ( sounds[i].name.Icmp( soundShaderName ) == 0 ) {
			return &sounds[i].data;
		}
	}
	// Raw wave paths are keyed in the file with their ".wav" extension, but
	// idSoundShader strips it from the decl name — so also compare with a
	// trailing ".wav" ignored on either side.
	idStr want = soundShaderName;
	if ( want.Length() > 4 && want.Right( 4 ).Icmp( ".wav" ) == 0 ) {
		want = want.Left( want.Length() - 4 );
	}
	for ( int i = 0; i < sounds.Num(); i++ ) {
		idStr key = sounds[i].name;
		if ( key.Length() > 4 && key.Right( 4 ).Icmp( ".wav" ) == 0 ) {
			key = key.Left( key.Length() - 4 );
		}
		if ( key.Icmp( want ) == 0 ) {
			return &sounds[i].data;
		}
	}
	return NULL;
}
