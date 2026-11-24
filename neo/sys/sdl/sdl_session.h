/*
===============================================================================

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

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===============================================================================
*/

// SDL2 session interface

#ifndef __SDL_SESSION_H__
#define __SDL_SESSION_H__

#include "../../idlib/precompiled.h"
#include "../sys_session.h"
#include "../sys_lobby_backend.h"
#include "../sys_session_local.h"
#include "../sys_session_savegames.h"
#include "../../sys/sys_savegame.h"

class idSessionLocalSDL : public idSession {
public:
    virtual ~idSessionLocalSDL() {
        // Intentionally leak the savegame processors: this is a global object,
        // so the dtor runs during static destruction, and their idSysSignal
        // members would call SDL_DestroyMutex/SDL_DestroyCond after SDL_Quit
        // (same reason g_stubMatchParms in sdl_session.cpp is never freed).
    }

    // idSession pure virtual functions
    virtual void Initialize() override;
    virtual void Shutdown() override;
    virtual void InitializeSoundRelatedSystems() override;
    virtual void ShutdownSoundRelatedSystems() override;

    // Lobby management (from idSession)
    virtual void CreatePartyLobby( const idMatchParameters & parms_ ) override;
    virtual void FindOrCreateMatch( const idMatchParameters & parms_ ) override;
    virtual void CreateMatch( const idMatchParameters & parms_ ) override;
    virtual void CreateGameStateLobby( const idMatchParameters & parms_ ) override;
    virtual void UpdateMatchParms( const idMatchParameters & parms_ ) override;
    virtual void UpdatePartyParms( const idMatchParameters & parms_ ) override;
    virtual void StartMatch() override;
    virtual void EndMatch( bool premature = false ) override;
    virtual void MatchFinished() override;
    virtual void QuitMatch() override;
    virtual void QuitMatchToTitle() override;
    virtual void SetSessionOption( sessionOption_t option ) override;
    virtual void ClearSessionOption( sessionOption_t option ) override;
    virtual sessionState_t GetBackState() override;
    virtual void Cancel() override;
    virtual void MoveToPressStart() override;
    virtual void FinishDisconnect() override;
    virtual void LoadingFinished() override;
    virtual bool IsCurrentLobbyMigrating() const override;
    virtual bool IsLosingConnectionToHost() const override;
    virtual bool WasMigrationGame() const override;
    virtual bool ShouldRelaunchMigrationGame() const override;
    virtual bool WasGameLobbyCoalesced() const override;
    virtual bool GetMigrationGameData( idBitMsg & msg, bool reading ) override;
    virtual bool GetMigrationGameDataUser( lobbyUserID_t lobbyUserID, idBitMsg & msg, bool reading ) override;
    virtual bool GetMatchParamUpdate( int & peer, int & msg ) override;

    virtual void Pump() override;
    virtual void ProcessSnapAckQueue() override;

    virtual void InviteFriends() override;
    virtual void InviteParty() override;
    virtual void ShowPartySessions() override;
    virtual bool IsPlatformPartyInLobby() override;

    virtual idLobbyBase & GetPartyLobbyBase() override;
    virtual idLobbyBase & GetGameLobbyBase() override;
    virtual idLobbyBase & GetActingGameStateLobbyBase() override;
    virtual idLobbyBase & GetActivePlatformLobbyBase() override;
    virtual idLobbyBase & GetLobbyFromLobbyUserID( lobbyUserID_t lobbyUserID ) override;
    virtual idPlayerProfile * GetProfileFromMasterLocalUser() override;

    virtual bool ProcessInputEvent( const sysEvent_t * ev ) override;
    virtual float GetUpstreamDropRate() override;
    virtual float GetUpstreamQueueRate() override;
    virtual int GetQueuedBytes() override;

    virtual int GetLoadingID() override;
    virtual bool IsAboutToLoad() const override;

    virtual const char * GetLocalUserName( int i ) const override;

    virtual int NumServers() const override;
    virtual void ListServers( const idCallback & callback ) override;
    virtual void CancelListServers() override;
    virtual void ConnectToServer( int i ) override;
    virtual const serverInfo_t * ServerInfo( int i ) const override;
    virtual const idList< idStr > * ServerPlayerList( int i ) override;
    virtual void ShowServerGamerCardUI( int i ) override;
    virtual void ShowOnlineSignin() override;

    virtual void DropClient( int peerNum, int lobbyType ) override;

    virtual void JoinAfterSwap( void * joinID ) override;

    virtual void EnumerateDownloadableContent() override;
    virtual int GetNumContentPackages() const override;
    virtual int GetContentPackageID( int contentIndex ) const override;
    virtual const char * GetContentPackagePath( int contentIndex ) const override;
    virtual int GetContentPackageIndexForID( int contentID ) const override;

    virtual void ShowSystemMarketplaceUI() const override;
    virtual bool GetSystemMarketplaceHasNewContent() const override;
    virtual void SetSystemMarketplaceHasNewContent( bool hasNewContent ) override;

    virtual float GetTitleStorageFloat( const char * name, float defaultFloat ) const override;
    virtual int GetTitleStorageInt( const char * name, int defaultInt ) const override;
    virtual bool GetTitleStorageBool( const char * name, bool defaultBool ) const override;
    virtual const char * GetTitleStorageString( const char * name, const char * defaultString ) const override;

    virtual bool GetTitleStorageFloat( const char * name, float defaultFloat, float & out ) const override;
    virtual bool GetTitleStorageInt( const char * name, int defaultInt, int & out ) const override;
    virtual bool GetTitleStorageBool( const char * name, bool defaultBool, bool & out ) const override;
    virtual bool GetTitleStorageString( const char * name, const char * defaultString, const char ** out ) const override;

    // SaveGameManager() not needed - set saveGameManager directly

    // idLobbyBase interface methods (not in idSession, so no 'override')
    virtual bool IsHost() const;
    virtual bool IsPeer() const;
    virtual bool HasActivePeers() const;
    virtual int GetNumLobbyUsers() const;
    virtual int GetNumActiveLobbyUsers() const;
    virtual bool IsLobbyUserConnected( int index ) const;
    virtual lobbyUserID_t GetLobbyUserIdByOrdinal( int userIndex ) const;
    virtual int GetLobbyUserIndexFromLobbyUserID( lobbyUserID_t lobbyUserID ) const;
    virtual void SendReliable( int type, idBitMsg & msg, bool callReceiveReliable = true, peerMask_t sessionUserMask = MAX_UNSIGNED_TYPE( peerMask_t ) );
    virtual void SendReliableToLobbyUser( lobbyUserID_t lobbyUserID, int type, idBitMsg & msg );
    virtual void SendReliableToHost( int type, idBitMsg & msg );
    virtual const char * GetLobbyUserName( lobbyUserID_t lobbyUserID ) const;
    virtual void KickLobbyUser( lobbyUserID_t lobbyUserID );
    virtual bool IsLobbyUserValid( lobbyUserID_t lobbyUserID ) const;
    virtual bool IsLobbyUserLoaded( lobbyUserID_t lobbyUserID ) const;
    virtual bool LobbyUserHasFirstFullSnap( lobbyUserID_t lobbyUserID ) const;
    virtual void EnableSnapshotsForLobbyUser( lobbyUserID_t lobbyUserID );
    virtual int GetLobbyUserSkinIndex( lobbyUserID_t lobbyUserID ) const;
    virtual bool GetLobbyUserWeaponAutoReload( lobbyUserID_t lobbyUserID ) const;
    virtual bool GetLobbyUserWeaponAutoSwitch( lobbyUserID_t lobbyUserID ) const;
    virtual int GetLobbyUserLevel( lobbyUserID_t lobbyUserID ) const;
    virtual int GetLobbyUserQoS( lobbyUserID_t lobbyUserID ) const;
    virtual int GetLobbyUserTeam( lobbyUserID_t lobbyUserID ) const;
    virtual bool SetLobbyUserTeam( lobbyUserID_t lobbyUserID, int teamNumber );
    virtual int GetLobbyUserPartyToken( lobbyUserID_t lobbyUserID ) const;
    virtual idPlayerProfile * GetProfileFromLobbyUser( lobbyUserID_t lobbyUserID );
    virtual idLocalUser * GetLocalUserFromLobbyUser( lobbyUserID_t lobbyUserID );
    virtual int GetNumLobbyUsersOnTeam( int teamNumber ) const;
    virtual int PeerIndexFromLobbyUser( lobbyUserID_t lobbyUserID ) const;
    virtual int GetPeerTimeSinceLastPacket( int peerIndex ) const;
    virtual int PeerIndexForHost() const;
    virtual lobbyUserID_t AllocLobbyUserSlotForBot( const char * botName );
    virtual void RemoveBotFromLobbyUserList( lobbyUserID_t lobbyUserID );
    virtual bool GetLobbyUserIsBot( lobbyUserID_t lobbyUserID ) const;
    virtual const char * GetHostUserName() const;
    virtual const idMatchParameters & GetMatchParms() const;
    virtual bool IsLobbyFull() const;
    virtual bool EnsureAllPeersHaveBaseState();
    virtual bool AllPeersInGame() const;
    virtual int GetNumConnectedPeers() const;
    virtual int GetNumConnectedPeersInGame() const;
    virtual int PeerIndexOnHost() const;
    virtual bool IsPeerDisconnected( int peerIndex ) const;
    virtual bool AllPeersHaveStaleSnapObj( int objId );
    virtual bool AllPeersHaveExpectedSnapObj( int objId );
    virtual void RefreshSnapObj( int objId );
    virtual void MarkSnapObjDeleted( int objId );
    virtual void AddSnapObjTemplate( int objID, idBitMsg & msg );
    virtual void DrawDebugNetworkHUD() const;
    virtual void DrawDebugNetworkHUD2() const;
    virtual void DrawDebugNetworkHUD_ServerSnapshotMetrics( bool draw );

    // Additional methods not in idSession base class
    virtual void OnLocalUserSignin( idLocalUser * user );
    virtual void OnLocalUserSignout( idLocalUser * user );
    virtual void OnMasterLocalUserSignout();
    virtual void OnMasterLocalUserSignin();
    virtual void OnLocalUserProfileLoaded( idLocalUser * user );
    virtual void SetDiscSwapMPInvite( void * parm );
    virtual void * GetDiscSwapMPInviteParms();
    virtual bool IsDiscSwapMPInviteRequested() const;
    virtual bool HasPendingBootableInvite();
    virtual void CheckVoicePrivileges();
    virtual void SetVoiceGroupsToTeams();
    virtual void ClearVoiceGroups();
    virtual bool StartOrContinueBandwidthChallenge( bool forceStart );
    virtual void DebugSetPeerSnaprate( int peerIndex, int snapRateMS );
    virtual float GetIncomingByteRate();
    virtual void HandleBootableInvite( int64 lobbyId = 0 );
    virtual void ClearBootableInvite();
    virtual void ClearPendingInvite();
    virtual bool IsTitleStorageLoaded();
    virtual void LeaderboardUpload( lobbyUserID_t lobbyUserID, const leaderboardDefinition_t * leaderboard, const column_t * stats, const idFile_Memory * attachment = NULL );
    virtual void LeaderboardDownload( int sessionUserIndex, const leaderboardDefinition_t * leaderboard, int startingRank, int numRows, const idLeaderboardCallback & callback );
    virtual void LeaderboardDownloadAttachment( int sessionUserIndex, const leaderboardDefinition_t * leaderboard, int64 attachmentID );
    virtual void LeaderboardFlush();
    virtual void SetLobbyUserRelativeScore( lobbyUserID_t lobbyUserID, int relativeScore, int team );
    virtual saveGameHandle_t SaveGameSync( const char * name, const saveFileEntryList_t & files, const idSaveGameDetails & description );
    virtual saveGameHandle_t SaveGameAsync( const char * name, const saveFileEntryList_t & files, const idSaveGameDetails & description );
    virtual saveGameHandle_t LoadGameSync( const char * name, saveFileEntryList_t & files );
    virtual saveGameHandle_t EnumerateSaveGamesSync();
    virtual saveGameHandle_t EnumerateSaveGamesAsync();
    virtual saveGameHandle_t DeleteSaveGameSync( const char * name );
    virtual saveGameHandle_t DeleteSaveGameAsync( const char * name );
    virtual void CancelSaveGameWithHandle( const saveGameHandle_t & handle );
    virtual void SetCurrentSaveSlot( const char * slotName );
    virtual bool IsDLCAvailable( const char * mapName );
    virtual bool LoadGameCheckDiscNumber( idSaveLoadParms & parms );
    virtual void ShowLobbyUserGamerCardUI( lobbyUserID_t lobbyUserID );
    virtual void UpdateRichPresence();
    virtual void SendUsercmds( idBitMsg & msg );
    virtual void SendSnapshot( class idSnapShot & ss );
    virtual void UpdateSignInManager();
    virtual void SetSystemUIShowing( bool show );
    virtual voiceState_t GetLobbyUserVoiceState( lobbyUserID_t lobbyUserID );
    virtual voiceStateDisplay_t GetDisplayStateFromVoiceState( voiceState_t voiceState ) const;
    virtual void ToggleLobbyUserVoiceMute( lobbyUserID_t lobbyUserID );
    virtual void SetActiveChatGroup( int groupIndex );
    virtual bool IsSaveGameCompletedFromHandle( const saveGameHandle_t & handle ) const;
    virtual bool IsEnumerating() const;
    virtual const char * GetCurrentSaveSlot() const;
    virtual bool IsSystemUIShowing() const;

    // Methods from idSessionLocal that we need to implement
    virtual void PlatformPump();
    virtual void HandleServerQueryRequest( lobbyAddress_t & remoteAddr, idBitMsg & msg, int msgType );
    virtual void HandleServerQueryAck( lobbyAddress_t & remoteAddr, idBitMsg & msg );
    virtual idNetSessionPort & GetPort( bool dedicated = false );
    virtual idLobbyBackend * CreateLobbyBackend( const idMatchParameters & p, float skillLevel, idLobbyBackend::lobbyBackendType_t lobbyType );
    virtual idLobbyBackend * FindLobbyBackend( const idMatchParameters & p, int numPartyUsers, float skillLevel, idLobbyBackend::lobbyBackendType_t lobbyType );
    virtual idLobbyBackend * JoinFromConnectInfo( const lobbyConnectInfo_t & connectInfo, idLobbyBackend::lobbyBackendType_t lobbyType );
    virtual void DestroyLobbyBackend( idLobbyBackend * lobby );

    virtual void PumpLobbies();
    virtual bool GetLobbyAddressFromNetAddress( const netadr_t & netAddr, lobbyAddress_t & outAddr ) const;
    virtual bool GetNetAddressFromLobbyAddress( const lobbyAddress_t & lobbyAddress, netadr_t & outNetAddr ) const;

    // Functions from idSessionLocal that we need
    virtual const char * GetStateString() const;
    virtual int GetInputRouting( int inputRouting[ MAX_INPUT_DEVICES ] );
    virtual bool ShouldShowMigratingDialog() const;
    virtual const char * GetPeerName( int peerNum );
    virtual void PrePickNewHost( class idLobby & lobby, bool forceMe, bool inviteOldHost );
    virtual bool PreMigrateInvite( class idLobby & lobby );
    virtual void UpdateMasterUserHeadsetState();
    virtual void ListServersCommon();
    virtual void UpdatePendingInvite();

    // State management from idSessionLocal
    virtual void SetState( idSession::sessionState_t newState );
    virtual idSession::sessionState_t GetState() const;
    virtual void MoveToPressStart( int msg );
    virtual void MoveToMainMenu();

    // Missing pure virtual from idSession
    virtual saveGameHandle_t GetEnumerationHandle() const;
    virtual const saveGameDetailsList_t & GetEnumeratedSavegames() const;

    // Savegame completion callbacks.  This class inherits idSession (not
    // idSessionLocal), so it needs its own processors and slot state.
    void OnSaveCompleted( idSaveLoadParms * parms );
    void OnEnumerationCompleted( idSaveLoadParms * parms );
    void OnDeleteCompleted( idSaveLoadParms * parms );
    bool LoadGameCheckDescriptionFile( idSaveLoadParms & parms );

private:
    idSaveGameProcessorSaveFiles *      processorSaveFiles      = new (TAG_SAVEGAMES) idSaveGameProcessorSaveFiles();
    idSaveGameProcessorLoadFiles *      processorLoadFiles      = new (TAG_SAVEGAMES) idSaveGameProcessorLoadFiles();
    idSaveGameProcessorDelete *         processorDelete         = new (TAG_SAVEGAMES) idSaveGameProcessorDelete();
    idSaveGameProcessorEnumerateGames * processorEnumerate      = new (TAG_SAVEGAMES) idSaveGameProcessorEnumerateGames();
    idStr                               currentSaveSlot;
    saveGameHandle_t                    enumerationHandle = 0;
    bool                                savesEnumerated = false;
};

#endif // __SDL_SESSION_H__
