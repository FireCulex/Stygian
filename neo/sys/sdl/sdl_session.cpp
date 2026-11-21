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
void idSessionLocalSDL::OnMasterLocalUserSignin() { }
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
saveGameHandle_t idSessionLocalSDL::SaveGameSync( const char * name, const saveFileEntryList_t & files, const idSaveGameDetails & description ) {
    // Single-player SDL stub: Sys_ExecuteSavegameCommandAsyncImpl is a no-op on
    // this platform, so the async save processor would hang forever inside
    // ExecuteProcessorAndWait (and block SDL_QUIT). Write the in-memory save
    // files straight to disk under fs_savepath instead, then run common's
    // completion path (frees pipelineFile) and clear the "SAVING" indicator —
    // idSessionLocal::OnSaveCompleted would do the latter, but it requires a
    // master local user, which the SDL sign-in stub doesn't have.
    saveFileEntryList_t filesWithDetails( files );
    idFile_SaveGame * gameDetailsFile = new (TAG_SAVEGAMES) idFile_SaveGame( SAVEGAME_DETAILS_FILENAME, SAVEGAMEFILE_TEXT | SAVEGAMEFILE_AUTO_DELETE );
    gameDetailsFile->MakeWritable();
    description.descriptors.WriteToIniFile( gameDetailsFile );
    filesWithDetails.Append( gameDetailsFile );

    idStr saveDir = va( "savegames/%s", name );
    fileSystem->CreateOSPath( fileSystem->RelativePathToOSPath( saveDir, "fs_savepath" ) );
    for ( int i = 0; i < filesWithDetails.Num(); i++ ) {
        idFile_SaveGame * f = filesWithDetails[i];
        if ( f == NULL ) {
            continue;
        }
        idStr path = saveDir;
        path.AppendPath( f->GetName() );
        idFile * out = fileSystem->OpenFileWrite( path, "fs_savepath" );
        if ( out != NULL ) {
            int len = f->Length();
            if ( len > 0 ) {
                idTempArray<byte> buf( len );
                f->Seek( 0, FS_SEEK_SET );
                f->Read( buf.Ptr(), len );
                out->Write( buf.Ptr(), len );
            }
            delete out;
        }
    }

    idSaveLoadParms parms;
    parms.errorCode = SAVEGAME_E_NONE;
    parms.directory = name;
    common->OnSaveCompleted( parms );
    common->Dialog().ShowSaveIndicator( false );
    return 1;
}
saveGameHandle_t idSessionLocalSDL::SaveGameAsync( const char * name, const saveFileEntryList_t & files, const idSaveGameDetails & description ) { return SaveGameSync( name, files, description ); }
saveGameHandle_t idSessionLocalSDL::LoadGameSync( const char * name, saveFileEntryList_t & files ) { return 0; }
saveGameHandle_t idSessionLocalSDL::EnumerateSaveGamesSync() { return 0; }
saveGameHandle_t idSessionLocalSDL::EnumerateSaveGamesAsync() { return 0; }
saveGameHandle_t idSessionLocalSDL::DeleteSaveGameSync( const char * name ) { return 0; }
saveGameHandle_t idSessionLocalSDL::DeleteSaveGameAsync( const char * name ) { return 0; }
void idSessionLocalSDL::CancelSaveGameWithHandle( const saveGameHandle_t & handle ) { }
void idSessionLocalSDL::SetCurrentSaveSlot( const char * slotName ) { }
bool idSessionLocalSDL::IsDLCAvailable( const char * mapName ) { return false; }
bool idSessionLocalSDL::LoadGameCheckDiscNumber( idSaveLoadParms & parms ) { return false; }
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
bool idSessionLocalSDL::IsSaveGameCompletedFromHandle( const saveGameHandle_t & handle ) const { return false; }
bool idSessionLocalSDL::IsEnumerating() const { return false; }
const char * idSessionLocalSDL::GetCurrentSaveSlot() const { return ""; }
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
saveGameHandle_t idSessionLocalSDL::GetEnumerationHandle() const { return 0; }
const saveGameDetailsList_t & idSessionLocalSDL::GetEnumeratedSavegames() const {
    static saveGameDetailsList_t empty;
    return empty;
}

idSessionLocalSDL sessionLocalSDL;
idSession *session = &sessionLocalSDL;