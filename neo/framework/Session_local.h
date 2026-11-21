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
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#ifndef __SESSION_LOCAL_H__
#define __SESSION_LOCAL_H__

#include "../idlib/precompiled.h"

#include "sys/sys_lobby_backend.h"

class idSignInManager;
class idAchievementSystem;
class idSessionLocalCallbacks;
class idSessionLocal {

public:
	enum state_t {
		STATE_PRESS_START,
		STATE_IDLE,
		STATE_PARTY_LOBBY_HOST,
		STATE_PARTY_LOBBY_PEER,
		STATE_GAME_LOBBY_HOST,
		STATE_GAME_LOBBY_PEER,
		STATE_LOADING,
		STATE_INGAME,
		STATE_CREATE_AND_MOVE_TO_PARTY_LOBBY,
		STATE_CREATE_AND_MOVE_TO_GAME_LOBBY,
		STATE_FIND_OR_CREATE_MATCH,
		STATE_CONNECT_AND_MOVE_TO_PARTY,
		STATE_CONNECT_AND_MOVE_TO_GAME,
		STATE_BUSY,
		NUM_STATES
	};

	enum sessionState_t {
		PRESS_START,
		IDLE,
		PARTY_LOBBY,
		GAME_LOBBY,
		CONNECTING,
		LOADING,
		INGAME,
		BUSY
	};

	enum connectType_t {
		CONNECT_NONE,
		CONNECT_STEAM,
		CONNECT_LAN
	};

	enum pendingInviteMode_t {
		PENDING_INVITE_NONE,
		PENDING_INVITE_ACCEPTED,
		PENDING_INVITE_DECLINED
	};

	void				RecvLeaderboardStatsForPlayer( idBitMsg & msg );

protected:
	idSignInManager *		signInManager;
	idAchievementSystem *	achievementSystem;
	idSessionLocalCallbacks * sessionCallbacks;

	state_t					localState;
	connectType_t			connectType;
	pendingInviteMode_t		pendingInviteMode;
	int						pendingInviteDevice;
	bool					isSysUIShowing;
	bool					flushedStats;
	int						enumerationHandle;

public:
						idSessionLocal();
						~idSessionLocal();

	void				Init();
	void				Shutdown();
	void				InitBaseState();
	void				InitSteam();
	void				ConstructSteamObjects();
	void				DestroySteamObjects();
	void				MoveToPressStart( gameDialogMessages_t msg );
	void				MoveToPressStart();
	void				MoveToMainMenu();
	void				SetState( state_t newState );
	sessionState_t		GetState() const;

	void				Pump();

	void				OnMasterLocalUserSignin();
	void				CancelSaveGameWithHandle( const saveGameHandle_t & handle );

	const leaderboardDefinition_t*	ReadLeaderboardFromMsg( idBitMsg & msg, column_t * stats );

	void				SendLeaderboardStatsToPlayer( int sessionUserIndex, const leaderboardDefinition_t * leaderboard, const column_t * stats );

	bool				ReadRawPacket( lobbyAddress_t & from, void * data, int & size, int maxSize );

	idSignInManager &	GetSignInManager() { return *signInManager; }
	idAchievementSystem & GetAchievementSystem() { return *achievementSystem; }

	// Steam integration
	bool				steamInitialized;
	bool				steamFailed;


	// Session state
	
	// Other
	int					currentID;
};

#endif // __SESSION_LOCAL_H__