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

// Minimal Linux session implementation
// This provides basic single-player session functionality

#include "sys/sdl/sdl_session.h"
#include "../../idlib/precompiled.h"
#include "sys/sdl/sdl_session_local.h"
#include "../../sys/sys_savegame.h"
#include "../../sys/sys_session_savegames.h"
#include "../../sys/sys_achievements.h"
#include "../../sys/sys_signin.h"
#include "../../d3xp/Game.h"
#include <SDL.h>

// No-op platform backends for the single-player Linux build
namespace {
	class idAchievementSystemLinux : public idAchievementSystem {
	public:
		void AchievementUnlock( idLocalUser *, const int ) override {}
		void Pump() override {}
		bool IsInitialized() override { return true; }
	};

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

// Match parameters stored by CreatePartyLobby/CreateMatch so that
// ExecuteMapChange (which reads them back via GetActingGameStateLobbyBase().GetMatchParms())
// knows which map to load.
// Heap-allocated and intentionally never freed: as a file-static object its
// idDict destructor runs during exit() and double-frees idStrPool strings
// (shutdown SIGSEGV in idStrPool::FreeString). Leaking avoids the static
// destruction-order crash.
static idMatchParameters * g_stubMatchParms = new idMatchParameters();

// Concrete dummy implementation of idLobbyBase for single-player mode
class idDummyLobby : public idLobbyBase {
public:
    // Helper to create a valid lobbyUserID_t for the local player
    static lobbyUserID_t GetLocalUserID() { return lobbyUserID_t( localUserHandle_t(1), 0 ); }
    static lobbyUserID_t GetInvalidID() { return lobbyUserID_t(); }

    virtual bool IsHost() const { return true; }
    virtual bool IsPeer() const { return false; }
    virtual bool HasActivePeers() const { return true; }
    virtual int GetNumLobbyUsers() const { return 1; }
    virtual int GetNumActiveLobbyUsers() const { return 1; }
    virtual bool IsLobbyUserConnected( int index ) const { return index == 0; }
    virtual lobbyUserID_t GetLobbyUserIdByOrdinal( int userIndex ) const { return userIndex == 0 ? GetLocalUserID() : GetInvalidID(); }
    virtual int GetLobbyUserIndexFromLobbyUserID( lobbyUserID_t lobbyUserID ) const { return lobbyUserID == GetLocalUserID() ? 0 : -1; }
    virtual void SendReliable( int type, idBitMsg & msg, bool callReceiveReliable = true, peerMask_t sessionUserMask = MAX_UNSIGNED_TYPE( peerMask_t ) ) { }
    virtual void SendReliableToLobbyUser( lobbyUserID_t lobbyUserID, int type, idBitMsg & msg ) { }
    virtual void SendReliableToHost( int type, idBitMsg & msg ) { }
    virtual const char * GetLobbyUserName( lobbyUserID_t lobbyUserID ) const { return "Player"; }
    virtual void KickLobbyUser( lobbyUserID_t lobbyUserID ) { }
    virtual bool IsLobbyUserValid( lobbyUserID_t lobbyUserID ) const { return lobbyUserID == GetLocalUserID(); }
    virtual bool IsLobbyUserLoaded( lobbyUserID_t lobbyUserID ) const { return lobbyUserID == GetLocalUserID(); }
    virtual bool LobbyUserHasFirstFullSnap( lobbyUserID_t lobbyUserID ) const { return lobbyUserID == GetLocalUserID(); }
    virtual void EnableSnapshotsForLobbyUser( lobbyUserID_t lobbyUserID ) { }
    virtual int GetLobbyUserSkinIndex( lobbyUserID_t lobbyUserID ) const { return 0; }
    // stock lobbyUser_t defaults are true (sys_lobby_backend.h:103-104)
    virtual bool GetLobbyUserWeaponAutoReload( lobbyUserID_t lobbyUserID ) const { return true; }
    virtual bool GetLobbyUserWeaponAutoSwitch( lobbyUserID_t lobbyUserID ) const { return true; }
    virtual int GetLobbyUserLevel( lobbyUserID_t lobbyUserID ) const { return 0; }
    virtual int GetLobbyUserQoS( lobbyUserID_t lobbyUserID ) const { return 0; }
    virtual int GetLobbyUserTeam( lobbyUserID_t lobbyUserID ) const { return 0; }
    virtual bool SetLobbyUserTeam( lobbyUserID_t lobbyUserID, int teamNumber ) { return false; }
    virtual int GetLobbyUserPartyToken( lobbyUserID_t lobbyUserID ) const { return 0; }
    virtual idPlayerProfile * GetProfileFromLobbyUser( lobbyUserID_t lobbyUserID ) { return nullptr; }
    virtual idLocalUser * GetLocalUserFromLobbyUser( lobbyUserID_t lobbyUserID ) { return nullptr; }
    virtual int GetNumLobbyUsersOnTeam( int teamNumber ) const { return 0; }
    virtual int PeerIndexFromLobbyUser( lobbyUserID_t lobbyUserID ) const { return lobbyUserID == GetLocalUserID() ? 0 : -1; }
    virtual int GetPeerTimeSinceLastPacket( int peerIndex ) const { return 0; }
    virtual int PeerIndexForHost() const { return 0; }
    virtual lobbyUserID_t AllocLobbyUserSlotForBot( const char * botName ) { return GetInvalidID(); }
    virtual void RemoveBotFromLobbyUserList( lobbyUserID_t lobbyUserID ) { }
    virtual bool GetLobbyUserIsBot( lobbyUserID_t lobbyUserID ) const { return false; }
    virtual const char * GetHostUserName() const { return "Player"; }
    virtual const idMatchParameters & GetMatchParms() const { return *g_stubMatchParms; }
    virtual bool IsLobbyFull() const { return false; }
    virtual bool EnsureAllPeersHaveBaseState() { return true; }
    virtual bool AllPeersInGame() const { return true; }
    virtual int GetNumConnectedPeers() const { return 1; }
    virtual int GetNumConnectedPeersInGame() const { return 1; }
    virtual int PeerIndexOnHost() const { return 0; }
    virtual bool IsPeerDisconnected( int peerIndex ) const { return false; }
    virtual bool AllPeersHaveStaleSnapObj( int objId ) { return false; }
    virtual bool AllPeersHaveExpectedSnapObj( int objId ) { return true; }
    virtual void RefreshSnapObj( int objId ) { }
    virtual void MarkSnapObjDeleted( int objId ) { }
    virtual void AddSnapObjTemplate( int objID, idBitMsg & msg ) { }
    virtual void DrawDebugNetworkHUD() const { }
    virtual void DrawDebugNetworkHUD2() const { }
    virtual void DrawDebugNetworkHUD_ServerSnapshotMetrics( bool draw ) { }
};

// State management
static idSession::sessionState_t localState = idSession::PRESS_START;

void idSessionLocalSDL::SetState( idSession::sessionState_t newState ) {
    localState = newState;
}

idSession::sessionState_t idSessionLocalSDL::GetState() const {
    return localState;
}

void idSessionLocalSDL::MoveToPressStart( int msg ) {
    idLib::Printf("Session: MoveToPressStart\n");
    SetState( idSession::PRESS_START );
}

void idSessionLocalSDL::MoveToPressStart() {
    idLib::Printf("Session: MoveToPressStart\n");
    SetState( idSession::PRESS_START );
}

void idSessionLocalSDL::MoveToMainMenu() {
    idLib::Printf("Session: MoveToMainMenu\n");
    // "Title" in single-player is the main menu, which is the IDLE session state
    // (StartNewGame waits for IDLE right after QuitMatchToTitle).
    SetState( idSession::IDLE );
    // Make sure the save/load menus have a fresh enumerated list.
    if ( !savesEnumerated ) {
        savesEnumerated = true;
        EnumerateSaveGamesAsync();
    }
}

void idSessionLocalSDL::Initialize() {
    idLib::Printf("Session: Initialized (single-player mode)\n");
    SetState( idSession::PRESS_START );

	// The idSession base leaves these NULL; the engine dereferences them
	// unconditionally (e.g. GetAchievementSystem().Start()), so provide
	// no-op implementations.
	// Use idSignInManagerSDL (sdl_signin.h), which registers one local user on
	// input device 0 — the anonymous idSignInManagerLinux above returns NULL
	// from GetLocalUserByIndex, so GetMasterInputDevice() was -1 and
	// usercmdGen->Joystick() never ran (controller dead).
	signInManager = new idSignInManagerSDL();
	achievementSystem = new idAchievementSystemLinux();
	achievementSystem->Init();
	if ( saveGameManager == NULL ) {
		saveGameManager = new idSaveGameManager();
	}
}

void idSessionLocalSDL::Shutdown() {
    idLib::Printf("Session: Shutdown\n");
	if ( achievementSystem != NULL ) {
		achievementSystem->Shutdown();
		delete achievementSystem;
		achievementSystem = NULL;
	}
	if ( signInManager != NULL ) {
		delete signInManager;
		signInManager = NULL;
	}
	if ( saveGameManager != NULL ) {
		delete saveGameManager;
		saveGameManager = NULL;
	}
}

void idSessionLocalSDL::InitializeSoundRelatedSystems() {
}

void idSessionLocalSDL::ShutdownSoundRelatedSystems() {
}

void idSessionLocalSDL::CreatePartyLobby( const idMatchParameters & parms_ ) {
    idLib::Printf("Session: CreatePartyLobby\n");
    *g_stubMatchParms = parms_;
    // Local hosting always succeeds immediately in single-player.
    SetState( idSession::PARTY_LOBBY );
}

void idSessionLocalSDL::FindOrCreateMatch( const idMatchParameters & parms_ ) {
    idLib::Printf("Session: FindOrCreateMatch (stubbed)\n");
}

void idSessionLocalSDL::CreateMatch( const idMatchParameters & parms_ ) {
    idLib::Printf("Session: CreateMatch\n");
    *g_stubMatchParms = parms_;
    SetState( idSession::GAME_LOBBY );
}

void idSessionLocalSDL::CreateGameStateLobby( const idMatchParameters & parms_ ) {
    idLib::Printf("Session: CreateGameStateLobby (stubbed)\n");
}

void idSessionLocalSDL::UpdateMatchParms( const idMatchParameters & parms_ ) {
}

void idSessionLocalSDL::UpdatePartyParms( const idMatchParameters & parms_ ) {
}

void idSessionLocalSDL::StartMatch() {
    idLib::Printf("Session: StartMatch\n");
    // LOADING (not INGAME): the main frame loop watches for LOADING and calls
    // ExecuteMapChange(), which loads matchParms.mapName; LoadingFinished() then
    // flips us to INGAME.
    SetState( idSession::LOADING );
}

void idSessionLocalSDL::EndMatch( bool premature ) {
    idLib::Printf("Session: EndMatch\n");
    SetState( idSession::IDLE );
}

void idSessionLocalSDL::MatchFinished() {
    idLib::Printf("Session: MatchFinished\n");
}

void idSessionLocalSDL::QuitMatch() {
    idLib::Printf("Session: QuitMatch\n");
    // win32 QuitMatch() -> EndMatch(true) -> SetState(STATE_IDLE). Without this
    // transition the frame loop never sees "not INGAME while mapSpawned", so
    // LeaveGame()/CreateMainMenu() never run and ESC->Quit->Accept stays stuck
    // instead of returning to the main menu.
    SetState( idSession::IDLE );
}

void idSessionLocalSDL::QuitMatchToTitle() {
    idLib::Printf("Session: QuitMatchToTitle\n");
    SetState( idSession::IDLE );
}

void idSessionLocalSDL::SetSessionOption( sessionOption_t option ) {
}

void idSessionLocalSDL::ClearSessionOption( sessionOption_t option ) {
}

idSession::sessionState_t idSessionLocalSDL::GetBackState() {
    // Use base class GetState() which converts localState to sessionState_t
    return GetState();
}

void idSessionLocalSDL::Cancel() {
    idLib::Printf("Session: Cancel\n");
}

void idSessionLocalSDL::FinishDisconnect() {
    idLib::Printf("Session: FinishDisconnect\n");
}

void idSessionLocalSDL::LoadingFinished() {
    idLib::Printf("Session: LoadingFinished\n");
    SetState( idSession::INGAME );
    // A +map load leaves the shell menu active (it was shown at startup), which
    // pauses the game and eats all input (pauseGame checks Shell_IsActive()).
    // The menu's "start game" button normally hides it; do the same here.
    if ( game != NULL ) {
        game->Shell_Show( false );
    }
}

bool idSessionLocalSDL::IsCurrentLobbyMigrating() const { return false; }
bool idSessionLocalSDL::IsLosingConnectionToHost() const { return false; }
bool idSessionLocalSDL::WasMigrationGame() const { return false; }
bool idSessionLocalSDL::ShouldRelaunchMigrationGame() const { return false; }
bool idSessionLocalSDL::WasGameLobbyCoalesced() const { return false; }

bool idSessionLocalSDL::GetMigrationGameData( idBitMsg & msg, bool reading ) { return false; }
bool idSessionLocalSDL::GetMigrationGameDataUser( lobbyUserID_t lobbyUserID, idBitMsg & msg, bool reading ) { return false; }

bool idSessionLocalSDL::GetMatchParamUpdate( int & peer, int & msg ) { return false; }

void idSessionLocalSDL::Pump() {
    // The SDL sign-in stub has no real idLocalUser (GetMasterLocalUser() is NULL),
    // so the win32 "wait for master user before leaving PRESS_START" logic never
    // fires. In single-player there is always an implicit local player, so leave
    // PRESS_START immediately and land in the menu (IDLE).
    if ( localState == idSession::PRESS_START ) {
        SetState( idSession::IDLE );
        // Kick off savegame enumeration once we reach the menu, so the
        // save/load screens have a populated list (they read
        // GetSaveGameManager().GetEnumeratedSavegames() every frame).
        if ( !savesEnumerated ) {
            savesEnumerated = true;
            EnumerateSaveGamesAsync();
        }
    }
}

void idSessionLocalSDL::ProcessSnapAckQueue() {
}

void idSessionLocalSDL::InviteFriends() {
}

void idSessionLocalSDL::InviteParty() {
}

void idSessionLocalSDL::ShowPartySessions() {
}

bool idSessionLocalSDL::IsPlatformPartyInLobby() { return false; }

idLobbyBase & idSessionLocalSDL::GetPartyLobbyBase() {
    static idDummyLobby dummy;
    return dummy;
}

idLobbyBase & idSessionLocalSDL::GetGameLobbyBase() {
    static idDummyLobby dummy;
    return dummy;
}

idLobbyBase & idSessionLocalSDL::GetActingGameStateLobbyBase() {
    static idDummyLobby dummy;
    return dummy;
}

idLobbyBase & idSessionLocalSDL::GetActivePlatformLobbyBase() {
    static idDummyLobby dummy;
    return dummy;
}

idLobbyBase & idSessionLocalSDL::GetLobbyFromLobbyUserID( lobbyUserID_t lobbyUserID ) {
    static idDummyLobby dummy;
    return dummy;
}

idPlayerProfile * idSessionLocalSDL::GetProfileFromMasterLocalUser() { return nullptr; }

bool idSessionLocalSDL::ProcessInputEvent( const sysEvent_t * ev ) { return false; }
float idSessionLocalSDL::GetUpstreamDropRate() { return 0.0f; }
float idSessionLocalSDL::GetUpstreamQueueRate() { return 0.0f; }
int idSessionLocalSDL::GetQueuedBytes() { return 0; }

int idSessionLocalSDL::GetLoadingID() { return 0; }
bool idSessionLocalSDL::IsAboutToLoad() const { return false; }

const char * idSessionLocalSDL::GetLocalUserName( int i ) const {
    static const char* name = "Player";
    return name;
}

// Don't override GetState() and GetStateString() - use base class implementation

int idSessionLocalSDL::NumServers() const { return 0; }
void idSessionLocalSDL::ListServers( const idCallback & callback ) { }
void idSessionLocalSDL::CancelListServers() { }
void idSessionLocalSDL::ConnectToServer( int i ) { }
const serverInfo_t * idSessionLocalSDL::ServerInfo( int i ) const { return nullptr; }
const idList< idStr > * idSessionLocalSDL::ServerPlayerList( int i ) { return nullptr; }
void idSessionLocalSDL::ShowServerGamerCardUI( int i ) { }

void idSessionLocalSDL::ShowOnlineSignin() { }

void idSessionLocalSDL::DropClient( int peerNum, int lobbyType ) { }

void idSessionLocalSDL::JoinAfterSwap( void * joinID ) { }

void idSessionLocalSDL::EnumerateDownloadableContent() { }
int idSessionLocalSDL::GetNumContentPackages() const { return 0; }
int idSessionLocalSDL::GetContentPackageID( int contentIndex ) const { return 0; }
const char * idSessionLocalSDL::GetContentPackagePath( int contentIndex ) const { return ""; }
int idSessionLocalSDL::GetContentPackageIndexForID( int contentID ) const { return 0; }

void idSessionLocalSDL::ShowSystemMarketplaceUI() const { }
bool idSessionLocalSDL::GetSystemMarketplaceHasNewContent() const { return false; }
void idSessionLocalSDL::SetSystemMarketplaceHasNewContent( bool hasNewContent ) { }

float idSessionLocalSDL::GetTitleStorageFloat( const char * name, float defaultFloat ) const { return defaultFloat; }
int idSessionLocalSDL::GetTitleStorageInt( const char * name, int defaultInt ) const { return defaultInt; }
bool idSessionLocalSDL::GetTitleStorageBool( const char * name, bool defaultBool ) const { return defaultBool; }
const char * idSessionLocalSDL::GetTitleStorageString( const char * name, const char * defaultString ) const { return defaultString; }

bool idSessionLocalSDL::GetTitleStorageFloat( const char * name, float defaultFloat, float & out ) const { out = defaultFloat; return false; }
bool idSessionLocalSDL::GetTitleStorageInt( const char * name, int defaultInt, int & out ) const { out = defaultInt; return false; }
bool idSessionLocalSDL::GetTitleStorageBool( const char * name, bool defaultBool, bool & out ) const { out = defaultBool; return false; }
bool idSessionLocalSDL::GetTitleStorageString( const char * name, const char * defaultString, const char ** out ) const { if ( out != NULL ) { *out = defaultString; } return false; }

bool idSessionLocalSDL::IsLobbyUserValid( lobbyUserID_t lobbyUserID ) const { return false; }
bool idSessionLocalSDL::IsLobbyUserLoaded( lobbyUserID_t lobbyUserID ) const { return false; }
bool idSessionLocalSDL::LobbyUserHasFirstFullSnap( lobbyUserID_t lobbyUserID ) const { return false; }
void idSessionLocalSDL::EnableSnapshotsForLobbyUser( lobbyUserID_t lobbyUserID ) { }

int idSessionLocalSDL::GetLobbyUserSkinIndex( lobbyUserID_t lobbyUserID ) const { return 0; }
// stock lobbyUser_t defaults are true (sys_lobby_backend.h:103-104); returning
// false here disabled auto-reload-on-empty-clip in SP
bool idSessionLocalSDL::GetLobbyUserWeaponAutoReload( lobbyUserID_t lobbyUserID ) const { return true; }
bool idSessionLocalSDL::GetLobbyUserWeaponAutoSwitch( lobbyUserID_t lobbyUserID ) const { return true; }
int idSessionLocalSDL::GetLobbyUserLevel( lobbyUserID_t lobbyUserID ) const { return 0; }
int idSessionLocalSDL::GetLobbyUserQoS( lobbyUserID_t lobbyUserID ) const { return 0; }
int idSessionLocalSDL::GetLobbyUserTeam( lobbyUserID_t lobbyUserID ) const { return 0; }
bool idSessionLocalSDL::SetLobbyUserTeam( lobbyUserID_t lobbyUserID, int teamNumber ) { return false; }
int idSessionLocalSDL::GetLobbyUserPartyToken( lobbyUserID_t lobbyUserID ) const { return 0; }
idPlayerProfile * idSessionLocalSDL::GetProfileFromLobbyUser( lobbyUserID_t lobbyUserID ) { return nullptr; }
idLocalUser * idSessionLocalSDL::GetLocalUserFromLobbyUser( lobbyUserID_t lobbyUserID ) { return nullptr; }
int idSessionLocalSDL::GetNumLobbyUsersOnTeam( int teamNumber ) const { return 0; }

int idSessionLocalSDL::PeerIndexFromLobbyUser( lobbyUserID_t lobbyUserID ) const { return -1; }
int idSessionLocalSDL::GetPeerTimeSinceLastPacket( int peerIndex ) const { return 0; }
int idSessionLocalSDL::PeerIndexForHost() const { return 0; }

lobbyUserID_t idSessionLocalSDL::AllocLobbyUserSlotForBot( const char * botName ) { return idDummyLobby::GetInvalidID(); }
void idSessionLocalSDL::RemoveBotFromLobbyUserList( lobbyUserID_t lobbyUserID ) { }
bool idSessionLocalSDL::GetLobbyUserIsBot( lobbyUserID_t lobbyUserID ) const { return false; }

const char * idSessionLocalSDL::GetHostUserName() const { return "Player"; }
const idMatchParameters & idSessionLocalSDL::GetMatchParms() const {
    static idMatchParameters params;
    return params;
}
bool idSessionLocalSDL::IsLobbyFull() const { return false; }

bool idSessionLocalSDL::EnsureAllPeersHaveBaseState() { return true; }
bool idSessionLocalSDL::AllPeersInGame() const { return true; }
int idSessionLocalSDL::GetNumConnectedPeers() const { return 1; }
int idSessionLocalSDL::GetNumConnectedPeersInGame() const { return 1; }
int idSessionLocalSDL::PeerIndexOnHost() const { return 0; }
bool idSessionLocalSDL::IsPeerDisconnected( int peerIndex ) const { return false; }

bool idSessionLocalSDL::AllPeersHaveStaleSnapObj( int objId ) { return false; }
bool idSessionLocalSDL::AllPeersHaveExpectedSnapObj( int objId ) { return true; }
void idSessionLocalSDL::RefreshSnapObj( int objId ) { }
void idSessionLocalSDL::MarkSnapObjDeleted( int objId ) { }
void idSessionLocalSDL::AddSnapObjTemplate( int objID, idBitMsg & msg ) { }

void idSessionLocalSDL::DrawDebugNetworkHUD() const { }
void idSessionLocalSDL::DrawDebugNetworkHUD2() const { }
void idSessionLocalSDL::DrawDebugNetworkHUD_ServerSnapshotMetrics( bool draw ) { }

// GetSaveGameManager() uses base class implementation

// Additional methods from idLobbyBase interface (not in idSession base class)
bool idSessionLocalSDL::IsHost() const { return true; }
bool idSessionLocalSDL::IsPeer() const { return false; }
bool idSessionLocalSDL::HasActivePeers() const { return true; }
int idSessionLocalSDL::GetNumLobbyUsers() const { return 1; }
int idSessionLocalSDL::GetNumActiveLobbyUsers() const { return 1; }
bool idSessionLocalSDL::IsLobbyUserConnected( int index ) const { return index == 0; }
lobbyUserID_t idSessionLocalSDL::GetLobbyUserIdByOrdinal( int userIndex ) const { return userIndex == 0 ? idDummyLobby::GetLocalUserID() : idDummyLobby::GetInvalidID(); }
int idSessionLocalSDL::GetLobbyUserIndexFromLobbyUserID( lobbyUserID_t lobbyUserID ) const { return lobbyUserID == idDummyLobby::GetLocalUserID() ? 0 : -1; }
void idSessionLocalSDL::SendReliable( int type, idBitMsg & msg, bool callReceiveReliable, peerMask_t sessionUserMask ) { }
void idSessionLocalSDL::SendReliableToLobbyUser( lobbyUserID_t lobbyUserID, int type, idBitMsg & msg ) { }
void idSessionLocalSDL::SendReliableToHost( int type, idBitMsg & msg ) { }
const char * idSessionLocalSDL::GetLobbyUserName( lobbyUserID_t lobbyUserID ) const { return "Player"; }
void idSessionLocalSDL::KickLobbyUser( lobbyUserID_t lobbyUserID ) { }

// Additional missing from idSession
void idSessionLocalSDL::OnLocalUserSignin( idLocalUser * user ) { }
void idSessionLocalSDL::OnLocalUserSignout( idLocalUser * user ) { }
void idSessionLocalSDL::OnMasterLocalUserSignout() { }
void idSessionLocalSDL::OnMasterLocalUserSignin() {
    // win32 enumerates savegames when the master user signs in (sys_session_local.cpp);
    // do the same here so the menus always have a list.
    if ( !savesEnumerated ) {
        savesEnumerated = true;
        EnumerateSaveGamesAsync();
    }
}
void idSessionLocalSDL::OnLocalUserProfileLoaded( idLocalUser * user ) { }
void idSessionLocalSDL::SetDiscSwapMPInvite( void * parm ) { }
void * idSessionLocalSDL::GetDiscSwapMPInviteParms() { return nullptr; }
bool idSessionLocalSDL::IsDiscSwapMPInviteRequested() const { return false; }
bool idSessionLocalSDL::HasPendingBootableInvite() { return false; }
void idSessionLocalSDL::CheckVoicePrivileges() { }
void idSessionLocalSDL::SetVoiceGroupsToTeams() { }
void idSessionLocalSDL::ClearVoiceGroups() { }
bool idSessionLocalSDL::StartOrContinueBandwidthChallenge( bool forceStart ) { return false; }
void idSessionLocalSDL::DebugSetPeerSnaprate( int peerIndex, int snapRateMS ) { }
float idSessionLocalSDL::GetIncomingByteRate() { return 0.0f; }
void idSessionLocalSDL::HandleBootableInvite( int64 lobbyId ) { }
void idSessionLocalSDL::ClearBootableInvite() { }
void idSessionLocalSDL::ClearPendingInvite() { }
bool idSessionLocalSDL::IsTitleStorageLoaded() { return false; }
void idSessionLocalSDL::LeaderboardUpload( lobbyUserID_t lobbyUserID, const leaderboardDefinition_t * leaderboard, const column_t * stats, const idFile_Memory * attachment ) { }
void idSessionLocalSDL::LeaderboardDownload( int sessionUserIndex, const leaderboardDefinition_t * leaderboard, int startingRank, int numRows, const idLeaderboardCallback & callback ) { }
void idSessionLocalSDL::LeaderboardDownloadAttachment( int sessionUserIndex, const leaderboardDefinition_t * leaderboard, int64 attachmentID ) { }
void idSessionLocalSDL::LeaderboardFlush() { }
void idSessionLocalSDL::SetLobbyUserRelativeScore( lobbyUserID_t lobbyUserID, int relativeScore, int team ) { }
/*
========================
idSessionLocalSDL::SaveGameSync

Mirrors idSessionLocal::SaveGameSync: queues the save-files processor and
waits for it.  Sys_ExecuteSavegameCommandAsyncImpl (sdl_savegame.cpp) runs
synchronously on this platform, so ExecuteProcessorAndWait completes instead
of hanging.
========================
*/
saveGameHandle_t idSessionLocalSDL::SaveGameSync( const char * name, const saveFileEntryList_t & files, const idSaveGameDetails & description ) {
    saveGameHandle_t handle = 0;

    // serialize the description file behind their back...
    saveFileEntryList_t filesWithDetails( files );
    idFile_SaveGame * gameDetailsFile = new (TAG_SAVEGAMES) idFile_SaveGame( SAVEGAME_DETAILS_FILENAME, SAVEGAMEFILE_TEXT | SAVEGAMEFILE_AUTO_DELETE );
    gameDetailsFile->MakeWritable();
    description.descriptors.WriteToIniFile( gameDetailsFile );
    filesWithDetails.Append( gameDetailsFile );

    if ( processorSaveFiles->InitSave( name, filesWithDetails, description ) ) {
        processorSaveFiles->AddCompletedCallback( MakeCallback( this, &idSessionLocalSDL::OnSaveCompleted, &processorSaveFiles->GetParmsNonConst() ) );
        handle = GetSaveGameManager().ExecuteProcessorAndWait( processorSaveFiles );
    }

    // Errors within the process of saving are handled in OnSaveCompleted()
    // so that asynchronous save errors are handled the same was as synchronous.
    if ( handle == 0 ) {
        idSaveLoadParms & parms = processorSaveFiles->GetParmsNonConst();
        parms.errorCode = SAVEGAME_E_UNKNOWN;

        // Uniform error handling
        OnSaveCompleted( &parms );
    }

    return handle;
}

saveGameHandle_t idSessionLocalSDL::SaveGameAsync( const char * name, const saveFileEntryList_t & files, const idSaveGameDetails & description ) {
    saveGameHandle_t handle = 0;

    // serialize the description file behind their back...
    saveFileEntryList_t filesWithDetails( files );
    idFile_SaveGame * gameDetailsFile = new (TAG_SAVEGAMES) idFile_SaveGame( SAVEGAME_DETAILS_FILENAME, SAVEGAMEFILE_TEXT | SAVEGAMEFILE_AUTO_DELETE );
    gameDetailsFile->MakeWritable();
    description.descriptors.WriteToIniFile( gameDetailsFile );
    filesWithDetails.Append( gameDetailsFile );

    if ( processorSaveFiles->InitSave( name, filesWithDetails, description ) ) {
        processorSaveFiles->AddCompletedCallback( MakeCallback( this, &idSessionLocalSDL::OnSaveCompleted, &processorSaveFiles->GetParmsNonConst() ) );
        handle = GetSaveGameManager().ExecuteProcessor( processorSaveFiles );
    }

    // Errors within the process of saving are handled in OnSaveCompleted()
    // so that asynchronous save errors are handled the same was as synchronous.
    if ( handle == 0 ) {
        idSaveLoadParms & parms = processorSaveFiles->GetParmsNonConst();
        parms.errorCode = SAVEGAME_E_UNKNOWN;

        common->Dialog().ShowSaveIndicator( false );
        // Uniform error handling
        OnSaveCompleted( &parms );
    }

    return handle;
}

/*
========================
idSessionLocalSDL::OnSaveCompleted
========================
*/
void idSessionLocalSDL::OnSaveCompleted( idSaveLoadParms * parms ) {
    if ( parms->GetError() != SAVEGAME_E_INSUFFICIENT_ROOM ) {
        // if savegame completed we can clear retry info
        GetSaveGameManager().ClearRetryInfo();
    }

    // The SDL sign-in manager's local user has no profile that would be saved
    // alongside the game, so the save indicator can always be turned off here.
    common->Dialog().ShowSaveIndicator( false );

    if ( parms->GetError() == SAVEGAME_E_NONE ) {
        // Update the enumerated savegames
        saveGameDetailsList_t & detailList = GetSaveGameManager().GetEnumeratedSavegamesNonConst();
        idSaveGameDetails * details = detailList.Find( parms->description );
        if ( details == NULL ) {
            // add it
            detailList.Append( parms->description );
        } else {
            // replace it
            *details = parms->description;
        }
    }

    // Error handling and additional processing
    common->OnSaveCompleted( *parms );
}

/*
========================
idSessionLocalSDL::LoadGameSync
========================
*/
saveGameHandle_t idSessionLocalSDL::LoadGameSync( const char * name, saveFileEntryList_t & files ) {
    idSaveLoadParms & parms = processorLoadFiles->GetParmsNonConst();
    saveGameHandle_t handle = 0;

    {
        // Done this way so we know it will be shutdown properly on early exit or exception
        struct local_t {
            local_t( idSaveLoadParms * parms_ ) : parms( parms_ ) {
            }
            ~local_t() {
                common->OnLoadCompleted( *parms );
            }
            idSaveLoadParms * parms;
        } local( &parms );

        // Read the details file when loading games
        saveFileEntryList_t filesWithDetails( files );
        std::auto_ptr< idFile_SaveGame > gameDetailsFile( new (TAG_SAVEGAMES) idFile_SaveGame( SAVEGAME_DETAILS_FILENAME, SAVEGAMEFILE_TEXT ) );
        filesWithDetails.Append( gameDetailsFile.get() );

        // Check the cached save details from the enumeration and make sure we don't load a save from a newer version of the game!
        const saveGameDetailsList_t details = GetSaveGameManager().GetEnumeratedSavegames();
        for ( int i = 0; i < details.Num(); ++i ) {
            if ( idStr::Cmp( name, details[i].slotName ) == 0 ) {
                if ( details[i].GetSaveVersion() > BUILD_NUMBER ) {
                    parms.errorCode = SAVEGAME_E_INCOMPATIBLE_NEWER_VERSION;
                    return 0;
                }
            }
        }

        // Synchronous load
        if ( processorLoadFiles->InitLoadFiles( name, filesWithDetails ) ) {
            handle = GetSaveGameManager().ExecuteProcessorAndWait( processorLoadFiles );
        }

        if ( handle == 0 ) {
            parms.errorCode = SAVEGAME_E_UNKNOWN;
        }

        if ( parms.GetError() != SAVEGAME_E_NONE ) {
            return 0;
        }

        // Checks the description file to see if corrupted or if it's from a newer savegame
        if ( !LoadGameCheckDescriptionFile( parms ) ) {
            return 0;
        }

        // Checks to see if loaded map is from a DLC map and if that DLC is active
        if ( !IsDLCAvailable( parms.description.GetMapName() ) ) {
            parms.errorCode = SAVEGAME_E_DLC_NOT_FOUND;
            return 0;
        }
    }

    common->OnLoadFilesCompleted( parms );

    return handle;
}

/*
========================
idSessionLocalSDL::LoadGameCheckDescriptionFile
========================
*/
bool idSessionLocalSDL::LoadGameCheckDescriptionFile( idSaveLoadParms & parms ) {
    idFile_SaveGame ** detailsFile = FindFromGenericPtr( parms.files, SAVEGAME_DETAILS_FILENAME );
    if ( detailsFile == NULL ) {
        parms.errorCode = SAVEGAME_E_FILE_NOT_FOUND;
        return false;
    }

    assert( *detailsFile != NULL );
    (*detailsFile)->MakeReadOnly();

    if ( !SavegameReadDetailsFromFile( *detailsFile, parms.description ) ) {
        parms.errorCode = SAVEGAME_E_CORRUPTED;
    } else {
        if ( parms.description.GetSaveVersion() > BUILD_NUMBER ) {
            parms.errorCode = SAVEGAME_E_INCOMPATIBLE_NEWER_VERSION;
        }
    }

    return ( parms.GetError() == SAVEGAME_E_NONE );
}

/*
========================
idSessionLocalSDL::EnumerateSaveGamesSync
========================
*/
saveGameHandle_t idSessionLocalSDL::EnumerateSaveGamesSync() {
    saveGameHandle_t handle = 0;

    // flush the old enumerated list
    GetSaveGameManager().GetEnumeratedSavegamesNonConst().Clear();

    if ( processorEnumerate->Init() ) {
        processorEnumerate->AddCompletedCallback( MakeCallback( this, &idSessionLocalSDL::OnEnumerationCompleted, &processorEnumerate->GetParmsNonConst() ) );
        handle = GetSaveGameManager().ExecuteProcessorAndWait( processorEnumerate );
    }

    // Errors within the process of enumerating are handled in OnEnumerationCompleted()
    if ( handle == 0 ) {
        idSaveLoadParms & parms = processorEnumerate->GetParmsNonConst();
        parms.errorCode = SAVEGAME_E_UNKNOWN;

        // Uniform error handling
        OnEnumerationCompleted( &parms );
    }

    enumerationHandle = handle;
    return handle;
}

/*
========================
idSessionLocalSDL::EnumerateSaveGamesAsync
========================
*/
saveGameHandle_t idSessionLocalSDL::EnumerateSaveGamesAsync() {
    saveGameHandle_t handle = 0;

    // flush the old enumerated list
    GetSaveGameManager().GetEnumeratedSavegamesNonConst().Clear();

    if ( processorEnumerate->Init() ) {
        processorEnumerate->AddCompletedCallback( MakeCallback( this, &idSessionLocalSDL::OnEnumerationCompleted, &processorEnumerate->GetParmsNonConst() ) );
        handle = GetSaveGameManager().ExecuteProcessor( processorEnumerate );
    }

    // Errors within the process of enumerating are handled in OnEnumerationCompleted()
    if ( handle == 0 ) {
        idSaveLoadParms & parms = processorEnumerate->GetParmsNonConst();
        parms.errorCode = SAVEGAME_E_UNKNOWN;

        // Uniform error handling
        OnEnumerationCompleted( &parms );
    }

    enumerationHandle = handle;
    return handle;
}

/*
========================
idSessionLocalSDL::OnEnumerationCompleted
========================
*/
void idSessionLocalSDL::OnEnumerationCompleted( idSaveLoadParms * parms ) {
    // idTech4 idList::sort is just a qsort wrapper, which doesn't deal with
    // idStrStatic properly!
    std::sort( parms->detailList.Ptr(), parms->detailList.Ptr() + parms->detailList.Num() );

    if ( parms->GetError() == SAVEGAME_E_NONE ) {
        // Copy into the maintained list
        saveGameDetailsList_t & detailsList = GetSaveGameManager().GetEnumeratedSavegamesNonConst();
        detailsList = parms->detailList;

        // The platform-specific implementations don't know about the prefixes
        // If we don't do this here, we will end up with slots like: GAME-GAME-GAME-GAME-AUTOSAVE...
        for ( int i = 0; i < detailsList.Num(); i++ ) {
            idSaveGameDetails & details = detailsList[i];

            const idStr original = details.slotName;
            const idStr stripped = RemoveSaveFolderPrefix( original, idSaveGameManager::PACKAGE_GAME );
            details.slotName = stripped;
        }
    }

    common->OnEnumerationCompleted( *parms );
}

/*
========================
idSessionLocalSDL::DeleteSaveGameSync
========================
*/
saveGameHandle_t idSessionLocalSDL::DeleteSaveGameSync( const char * name ) {
    saveGameHandle_t handle = 0;

    if ( processorDelete->InitDelete( name ) ) {
        processorDelete->AddCompletedCallback( MakeCallback( this, &idSessionLocalSDL::OnDeleteCompleted, &processorDelete->GetParmsNonConst() ) );
        handle = GetSaveGameManager().ExecuteProcessorAndWait( processorDelete );
    }

    // Errors within the process of deleting are handled in OnDeleteCompleted()
    if ( handle == 0 ) {
        idSaveLoadParms & parms = processorDelete->GetParmsNonConst();
        parms.errorCode = SAVEGAME_E_UNKNOWN;

        // Uniform error handling
        OnDeleteCompleted( &parms );
    }

    return handle;
}

/*
========================
idSessionLocalSDL::DeleteSaveGameAsync
========================
*/
saveGameHandle_t idSessionLocalSDL::DeleteSaveGameAsync( const char * name ) {
    saveGameHandle_t handle = 0;
    if ( processorDelete->InitDelete( name ) ) {
        processorDelete->AddCompletedCallback( MakeCallback( this, &idSessionLocalSDL::OnDeleteCompleted, &processorDelete->GetParmsNonConst() ) );
        common->Dialog().ShowSaveIndicator( true );
        handle = GetSaveGameManager().ExecuteProcessor( processorDelete );
    }

    // Errors within the process of deleting are handled in OnDeleteCompleted()
    if ( handle == 0 ) {
        idSaveLoadParms & parms = processorDelete->GetParmsNonConst();
        parms.errorCode = SAVEGAME_E_UNKNOWN;

        // Uniform error handling
        OnDeleteCompleted( &parms );
    }

    return handle;
}

/*
========================
idSessionLocalSDL::OnDeleteCompleted
========================
*/
void idSessionLocalSDL::OnDeleteCompleted( idSaveLoadParms * parms ) {
    common->Dialog().ShowSaveIndicator( false );

    if ( parms->GetError() == SAVEGAME_E_NONE ) {
        // Update the enumerated list
        saveGameDetailsList_t & details = GetSaveGameManager().GetEnumeratedSavegamesNonConst();
        details.Remove( parms->description );
    }

    common->OnDeleteCompleted( *parms );
}

void idSessionLocalSDL::CancelSaveGameWithHandle( const saveGameHandle_t & handle ) { GetSaveGameManager().CancelWithHandle( handle ); }
void idSessionLocalSDL::SetCurrentSaveSlot( const char * slotName ) { currentSaveSlot = slotName; }
bool idSessionLocalSDL::IsDLCAvailable( const char * mapName ) { return true; }
bool idSessionLocalSDL::LoadGameCheckDiscNumber( idSaveLoadParms & parms ) { return ( parms.GetError() == SAVEGAME_E_NONE ); }
void idSessionLocalSDL::ShowLobbyUserGamerCardUI( lobbyUserID_t lobbyUserID ) { }
void idSessionLocalSDL::UpdateRichPresence() { }
void idSessionLocalSDL::SendUsercmds( idBitMsg & msg ) { }
void idSessionLocalSDL::SendSnapshot( class idSnapShot & ss ) { }
void idSessionLocalSDL::UpdateSignInManager() { }
void idSessionLocalSDL::SetSystemUIShowing( bool show ) { }
voiceState_t idSessionLocalSDL::GetLobbyUserVoiceState( lobbyUserID_t lobbyUserID ) { return voiceState_t(0); }
voiceStateDisplay_t idSessionLocalSDL::GetDisplayStateFromVoiceState( voiceState_t voiceState ) const { return voiceStateDisplay_t(0); }
void idSessionLocalSDL::ToggleLobbyUserVoiceMute( lobbyUserID_t lobbyUserID ) { }
void idSessionLocalSDL::SetActiveChatGroup( int groupIndex ) { }
bool idSessionLocalSDL::IsSaveGameCompletedFromHandle( const saveGameHandle_t & handle ) const { return saveGameManager->IsSaveGameCompletedFromHandle( handle ); }
bool idSessionLocalSDL::IsEnumerating() const { return !IsSaveGameCompletedFromHandle( processorEnumerate->GetHandle() ); }
const char * idSessionLocalSDL::GetCurrentSaveSlot() const { return currentSaveSlot.c_str(); }
bool idSessionLocalSDL::IsSystemUIShowing() const { return false; }

// Missing from idSessionLocal
void idSessionLocalSDL::PlatformPump() { }
void idSessionLocalSDL::HandleServerQueryRequest( lobbyAddress_t & remoteAddr, idBitMsg & msg, int msgType ) { }
void idSessionLocalSDL::HandleServerQueryAck( lobbyAddress_t & remoteAddr, idBitMsg & msg ) { }
idNetSessionPort & idSessionLocalSDL::GetPort( bool dedicated ) { 
    static idNetSessionPort dummyPort; 
    return dummyPort; 
}
idLobbyBackend * idSessionLocalSDL::CreateLobbyBackend( const idMatchParameters & p, float skillLevel, idLobbyBackend::lobbyBackendType_t lobbyType ) { return nullptr; }
idLobbyBackend * idSessionLocalSDL::FindLobbyBackend( const idMatchParameters & p, int numPartyUsers, float skillLevel, idLobbyBackend::lobbyBackendType_t lobbyType ) { return nullptr; }
idLobbyBackend * idSessionLocalSDL::JoinFromConnectInfo( const lobbyConnectInfo_t & connectInfo, idLobbyBackend::lobbyBackendType_t lobbyType ) { return nullptr; }
void idSessionLocalSDL::DestroyLobbyBackend( idLobbyBackend * lobby ) { }

// Missing from idSessionLocal (continued)
void idSessionLocalSDL::PumpLobbies() { }
bool idSessionLocalSDL::GetLobbyAddressFromNetAddress( const netadr_t & netAddr, lobbyAddress_t & outAddr ) const { return false; }
bool idSessionLocalSDL::GetNetAddressFromLobbyAddress( const lobbyAddress_t & lobbyAddress, netadr_t & outNetAddr ) const { return false; }

// Additional missing virtual functions
const char * idSessionLocalSDL::GetStateString() const { return "singleplayer"; }
int idSessionLocalSDL::GetInputRouting( int inputRouting[ MAX_INPUT_DEVICES ] ) { return 0; }
bool idSessionLocalSDL::ShouldShowMigratingDialog() const { return false; }
const char * idSessionLocalSDL::GetPeerName( int peerNum ) { return ""; }
void idSessionLocalSDL::PrePickNewHost( class idLobby & lobby, bool forceMe, bool inviteOldHost ) { }
bool idSessionLocalSDL::PreMigrateInvite( class idLobby & lobby ) { return false; }
void idSessionLocalSDL::UpdateMasterUserHeadsetState() { }
void idSessionLocalSDL::ListServersCommon() { }
void idSessionLocalSDL::UpdatePendingInvite() { }

// Missing pure virtual from idSession
saveGameHandle_t idSessionLocalSDL::GetEnumerationHandle() const { return enumerationHandle; }
const saveGameDetailsList_t & idSessionLocalSDL::GetEnumeratedSavegames() const {
    return saveGameManager->GetEnumeratedSavegames();
}

idSessionLocalSDL sessionLocalSDL;
idSession *session = &sessionLocalSDL;