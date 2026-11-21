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
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "Session_local.h"
#include "../sys/sys_achievements.h"
#include "../sys/sys_signin.h"

#if defined(ID_PC_WIN)
#include "Lobby.h"
#include "VoiceChat.h"
#include "Leaderboards.h"
#endif

// No-op achievement system for the Linux single-player build
namespace {
	class idAchievementSystemLinux : public idAchievementSystem {
	public:
		void AchievementUnlock( idLocalUser *, const int ) override {}
		void Pump() override {}
		bool IsInitialized() override { return true; }
	};

	// Concrete sign-in manager: single local user, no platform backend
	class idSignInManagerLinux : public idSignInManagerBase {
	public:
		void Pump() override {}
		int GetNumLocalUsers() const override { return 1; }
		idLocalUser * GetLocalUserByIndex( int ) override { return NULL; }
		const idLocalUser * GetLocalUserByIndex( int ) const override { return NULL; }
		void RemoveLocalUserByIndex( int ) override {}
		void RegisterLocalUser( int ) override {}
	};
}

idSessionLocal::idSessionLocal() {
	achievementSystem = NULL;
	sessionCallbacks = NULL;
	signInManager = NULL;
	InitBaseState();
}

idSessionLocal::~idSessionLocal() {
}

void idSessionLocal::Init() {
	common->Printf( "-------- Initializing Session --------\n" );

	signInManager = (idSignInManager *)(void *)new idSignInManagerLinux();
	achievementSystem = new idAchievementSystemLinux();
	achievementSystem->Init();

	common->Printf( "session initialized\n" );
	common->Printf( "--------------------------------------\n" );
}

void idSessionLocal::Shutdown() {
	if ( signInManager != NULL ) {
		delete (idSignInManagerBase *)(void *)signInManager;
		signInManager = NULL;
	}
	if ( achievementSystem != NULL ) {
		achievementSystem->Shutdown();
		delete achievementSystem;
		achievementSystem = NULL;
	}
}

void idSessionLocal::InitBaseState() {
	localState = STATE_PRESS_START;
	connectType = CONNECT_NONE;
	pendingInviteMode = PENDING_INVITE_NONE;
	pendingInviteDevice = 0;
	isSysUIShowing = false;
	flushedStats = false;
	enumerationHandle = 0;
}

void idSessionLocal::InitSteam() {
}

void idSessionLocal::ConstructSteamObjects() {
}

void idSessionLocal::DestroySteamObjects() {
}

void idSessionLocal::MoveToPressStart( gameDialogMessages_t msg ) {
}

void idSessionLocal::MoveToPressStart() {
}

void idSessionLocal::SetState( idSessionLocal::state_t newState ) {
}

idSessionLocal::sessionState_t idSessionLocal::GetState() const {
	return idSessionLocal::PRESS_START;
}

void idSessionLocal::Pump() {
}

void idSessionLocal::OnMasterLocalUserSignin() {
}

void idSessionLocal::CancelSaveGameWithHandle( const saveGameHandle_t & handle ) {
}

#if defined(ID_PC_WIN)
void idSessionLocal::PrePickNewHost( idLobby & lobby, bool forceMe, bool inviteOldHost ) {
}
#endif

#if defined(ID_PC_WIN)
void idSessionLocal::HandleDedicatedServerQueryRequest( lobbyAddress_t & remoteAddr, idBitMsg & msg, int msgType ) {
}
#endif

#if defined(ID_PC_WIN)
void idSessionLocal::HandleDedicatedServerQueryAck( lobbyAddress_t & remoteAddr, idBitMsg & msg ) {
}
#endif

#if defined(ID_PC_WIN)
void idSessionLocal::StartSessions() {
}
#endif

#if defined(ID_PC_WIN)
void idSessionLocal::EndSessions() {
}
#endif

#if defined(ID_PC_WIN)
void idSessionLocal::SetLobbiesAreJoinable( bool joinable ) {
}
#endif

#if defined(ID_PC_WIN)
void idSessionLocal::EndMatchForMigration() {
}
#endif

#if defined(ID_PC_WIN)
void idSessionLocal::ClearVoiceGroups() {
}
#endif

#if defined(ID_PC_WIN)
void idSessionLocal::GoodbyeFromHost( idLobby & lobby, int peerNum, const lobbyAddress_t & remoteAddress, int msgType ) {
}
#endif

void idSessionLocal::MoveToMainMenu() {
}

#if defined(ID_PC_WIN)
void idSessionLocal::HandleConnectionFailed( idLobby & lobby, bool wasFull ) {
}
#endif

bool idSessionLocal::ReadRawPacket( lobbyAddress_t & from, void * data, int & size, int maxSize ) {
	return false;
}

#if defined(ID_PC_WIN)
void idSessionLocal::SendRawPacket( const lobbyAddress_t & to, const void * data, int size ) {
}
#endif

#if defined(ID_PC_WIN)
void idSessionLocal::ConnectAndMoveToLobby( idLobby & lobby, const lobbyConnectInfo_t & connectInfo, bool fromInvite ) {
}
#endif

#if defined(ID_PC_WIN)
void idSessionLocal::WriteLeaderboardToMsg( idBitMsg & msg, const leaderboardDefinition_t * leaderboard, const column_t * stats ) {
}
#endif

#if defined(ID_PC_WIN)
void idSessionLocal::EndMatchInternal( bool premature ) {
}
#endif

#if defined(ID_PC_WIN)
void idSessionLocal::HandleOobVoiceAudio( const lobbyAddress_t & from, const idBitMsg & msg ) {
}
#endif

#if defined(ID_PC_WIN)
void idSessionLocal::ClearMigrationState() {
}
#endif

#if defined(ID_PC_WIN)
void idSessionLocal::SendLeaderboardStatsToPlayer( int sessionUserIndex, const leaderboardDefinition_t * leaderboard, const column_t * stats ) {
}
#endif

#if defined(ID_PC_WIN)
void idSessionLocal::RecvLeaderboardStatsForPlayer( idBitMsg & msg ) {
}
#endif

#if defined(ID_PC_WIN)
const leaderboardDefinition_t * idSessionLocal::ReadLeaderboardFromMsg( idBitMsg & msg, column_t * stats ) {
	return NULL;
}
#endif
