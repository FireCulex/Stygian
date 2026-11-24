/*
===============================================================================

Linux stub implementations for missing platform-specific functions
(single-player only build - provides stubs for removed multiplayer code)

===============================================================================
*/

#include "../../idlib/precompiled.h"
#include "../../sys/sys_session_local.h"
#include "../../sys/sys_session.h"
#include "../../sys/sys_lobby_backend.h"
#include "../../sys/sys_lobby.h"
#include "../../sys/sys_voicechat.h"
#include "../../sys/sys_dedicated_server_search.h"
#include "../../sys/sys_localuser.h"
#include "../../framework/CVarSystem.h"

// FPU functions (no-op on Linux)
bool Sys_FPU_StackIsEmpty() {
	return true;
}

const char * Sys_FPU_GetState() {
	return "fpu_ok";
}

// Network cvars - net_verboseReliable was in sys_session_local.cpp (now removed)
idCVar net_verboseReliable( "net_verboseReliable", "0", CVAR_BOOL, "verbose reliable network messages" );
idCVar net_bw_challenge_enable( "net_bw_challenge_enable", "0", CVAR_BOOL, "bandwidth challenge enable" );

// Leaderboard functions
int LeaderboardLocal_GetID( int a, int b ) {
	return 0;
}

// Command line / relaunch stubs
const char * Sys_GetCmdLine() {
	return "";
}

void Sys_ReLaunch( void * data, unsigned int flags ) {
	common->Quit();
}

// Sys_ExecuteSavegameCommandAsyncImpl and Sys_SaveGameCheck are implemented in
// sdl_savegame.cpp (synchronous port of the win32 savegame thread).

// Minimal idNetSessionPort implementation
idNetSessionPort::idNetSessionPort() {}
bool idNetSessionPort::InitPort( int portNumber, bool useBackend ) { return true; }
bool idNetSessionPort::ReadRawPacket( lobbyAddress_t & from, void * data, int & size, int maxSize ) { return false; }
void idNetSessionPort::SendRawPacket( const lobbyAddress_t & to, const void * data, int size ) {}
bool idNetSessionPort::IsOpen() { return true; }
void idNetSessionPort::Close() {}

// sys_lang cvar
idCVar sys_lang( "sys_lang", "english", CVAR_SYSTEM | CVAR_ARCHIVE, "system language" );

// Language/locale stubs
const char * Sys_DefaultLanguage() {
	return "english";
}

int Sys_NumLangs() {
	return 1;
}

const char * Sys_Lang( int index ) {
	return "english";
}

// Time/date stubs
const char * Sys_TimeStampToStr( long long timestamp ) { return ""; }
const char * Sys_SecToStr( int seconds ) { return ""; }

// leaderboardStats_t struct for LeaderboardLocal_Upload
struct leaderboardStats_t {};

// lobbyAddress_t default constructor
lobbyAddress_t::lobbyAddress_t() {}

// ============================================================================
// idLobbyBase pure virtual functions NOT implemented in idLobby header
// ============================================================================

int idLobby::GetNumActiveLobbyUsers() const { return 1; }
bool idLobby::AllPeersInGame() const { return true; }
lobbyUserID_t idLobby::AllocLobbyUserSlotForBot( const char * botName ) { return lobbyUserID_t(); }
void idLobby::RemoveBotFromLobbyUserList( lobbyUserID_t lobbyUserID ) {}
bool idLobby::GetLobbyUserIsBot( lobbyUserID_t lobbyUserID ) const { return false; }
lobbyUserID_t idLobby::GetLobbyUserIdByOrdinal( int userIndex ) const { return lobbyUserID_t(); }
int idLobby::GetLobbyUserIndexFromLobbyUserID( lobbyUserID_t lobbyUserID ) const { return 0; }
bool idLobby::IsLobbyUserValid( lobbyUserID_t lobbyUserID ) const { return true; }
bool idLobby::IsLobbyUserLoaded( lobbyUserID_t lobbyUserID ) const { return true; }
bool idLobby::LobbyUserHasFirstFullSnap( lobbyUserID_t lobbyUserID ) const { return true; }
void idLobby::EnableSnapshotsForLobbyUser( lobbyUserID_t lobbyUserID ) {}
int idLobby::GetLobbyUserSkinIndex( lobbyUserID_t lobbyUserID ) const { return 0; }
// Stock lobbyUser_t ctor defaults: weaponAutoReload = true, weaponAutoSwitch = true
// (neo/sys/sys_lobby_backend.h:103-104), and idLobby::GetLobbyUserWeaponAutoReload
// falls back to true when there is no user. Returning false here disabled
// auto-reload on empty clip in SP — restore the stock defaults.
bool idLobby::GetLobbyUserWeaponAutoReload( lobbyUserID_t lobbyUserID ) const { return true; }
bool idLobby::GetLobbyUserWeaponAutoSwitch( lobbyUserID_t lobbyUserID ) const { return true; }
int idLobby::GetLobbyUserLevel( lobbyUserID_t lobbyUserID ) const { return 0; }
int idLobby::GetLobbyUserQoS( lobbyUserID_t lobbyUserID ) const { return 0; }
int idLobby::GetLobbyUserTeam( lobbyUserID_t lobbyUserID ) const { return 0; }
bool idLobby::SetLobbyUserTeam( lobbyUserID_t lobbyUserID, int teamNumber ) { return false; }
int idLobby::GetLobbyUserPartyToken( lobbyUserID_t lobbyUserID ) const { return 0; }
idPlayerProfile * idLobby::GetProfileFromLobbyUser( lobbyUserID_t lobbyUserID ) { return NULL; }
idLocalUser * idLobby::GetLocalUserFromLobbyUser( lobbyUserID_t lobbyUserID ) { return NULL; }
int idLobby::GetNumLobbyUsersOnTeam( int teamNumber ) const { return 0; }
int idLobby::PeerIndexFromLobbyUser( lobbyUserID_t lobbyUserID ) const { return 0; }
int idLobby::GetPeerTimeSinceLastPacket( int peerIndex ) const { return 0; }
void idLobby::SendReliable( int type, idBitMsg & msg, bool callReceiveReliable, peerMask_t sessionUserMask ) {}
void idLobby::SendReliableToLobbyUser( lobbyUserID_t lobbyUserID, int type, idBitMsg & msg ) {}
void idLobby::SendReliableToHost( int type, idBitMsg & msg ) {}
const char * idLobby::GetLobbyUserName( lobbyUserID_t lobbyUserID ) const { return "Player"; }
void idLobby::KickLobbyUser( lobbyUserID_t lobbyUserID ) {}
const char * idLobby::GetHostUserName() const { return "Player"; }
bool idLobby::EnsureAllPeersHaveBaseState() { return true; }
int idLobby::GetNumConnectedPeers() const { return 1; }
int idLobby::GetNumConnectedPeersInGame() const { return 1; }
bool idLobby::AllPeersHaveStaleSnapObj( int objId ) { return false; }
bool idLobby::AllPeersHaveExpectedSnapObj( int objId ) { return true; }
void idLobby::RefreshSnapObj( int objId ) {}
void idLobby::MarkSnapObjDeleted( int objId ) {}
void idLobby::AddSnapObjTemplate( int objID, idBitMsg & msg ) {}
void idLobby::DrawDebugNetworkHUD() const {}
void idLobby::DrawDebugNetworkHUD2() const {}
void idLobby::DrawDebugNetworkHUD_ServerSnapshotMetrics( bool draw ) {}

// Missing pure virtual from linker error
bool idLobby::IsLobbyUserDisconnected( int index ) const { return false; }

// ============================================================================
// idLobby virtual functions (not inline in header, not in other stub files)
// ============================================================================

idLobby::idLobby() {}

bool idLobby::BandwidthTestStarted() { return false; }
idPacketProcessor::sessionId_t idLobby::IncrementSessionID( idPacketProcessor::sessionId_t sessionID ) const { return sessionID; }
void idLobby::CompactDisconnectedUsers() {}
void idLobby::ConnectTo(const lobbyConnectInfo_t & connectInfo, bool force) {}
void idLobby::NotifyPartyOfLeavingGameLobby() {}
bool idLobby::ConnectToNextSearchResult() { return false; }
bool idLobby::SendResources(int clientNum) { return false; }
void idLobby::ResetBandwidthStats() {}
bool idLobby::IsSessionUserLocal(const lobbyUser_t * user) const { return true; }
void idLobby::UpdateSnaps() {}
void idLobby::PumpPings() {}
void idLobby::PumpPackets() {}
void idLobby::StartHosting( const idMatchParameters & ) {}
bool idLobby::IsSessionUserIndexLocal( int ) const { return true; }
idLocalUser * idLobby::GetLocalUserFromLobbyUserIndex( int ) { return NULL; }
void idLobby::ProcessOutgoingMsg( int, const void *, int, bool, int ) {}
void idLobby::SendSnapshotToPeer( idSnapShot &, int ) {}
int idLobby::GetLobbyUserIndexByID( lobbyUserID_t, bool ) const { return -1; }
void idLobby::QueueReliableMessage( int peerNum, byte type, const byte * data, int dataLen ) {}

// Additional idLobby functions needed (from linker errors)
int idLobby::GetLobbyUserIndexByLocalUserHandle( const localUserHandle_t localUserHandle ) const { return -1; }
lobbyUser_t * idLobby::GetSessionUserFromLocalUser( const idLocalUser * controller ) { return NULL; }
bool idLobby::ShouldRelaunchMigrationGame() const { return false; }
bool idLobby::IsMigratedStatsGame() const { return false; }
bool idLobby::GetMigrationGameData( idBitMsg &msg, bool reading ) { return false; }
bool idLobby::GetMigrationGameDataUser( lobbyUserID_t lobbyUserID, idBitMsg &msg, bool reading ) { return false; }
void idLobby::DisconnectPeerFromSession( int p ) {}
const char * idLobby::GetPeerName( int peerNum ) const { return ""; }
void idLobby::BeginBandwidthTest() {}
void idLobby::SyncLobbyUsersWithLocalUsers( bool allowJoin, bool onlineMatch ) {}
void idLobby::PickNewHost( bool forceMe, bool inviteOldHost ) {}
void idLobby::PickNewHostInternal( bool forceMe, bool inviteOldHost ) {}
void idLobby::ResetAllMigrationState() {}
void idLobby::Shutdown( bool retainMigrationInfo, bool skipGoodbye ) {}
bool idLobby::ShouldShowMigratingDialog() const { return false; }
bool idLobby::IsMigrating() const { return false; }
void idLobby::UpdateMatchParms( const idMatchParameters & p ) {}
void idLobby::ProcessSnapAckQueue() {}

// Missing functions from latest linker errors
void idLobby::SendConnectionLess(const lobbyAddress_t & to, unsigned char type, const unsigned char * data, int size) {}
lobbyUser_t * idLobby::GetLobbyUserByID(lobbyUserID_t id, bool create) { return NULL; }
void idLobby::StartFinding( const idMatchParameters & ) {}
bool idLobby::HasActivePeers() const { return false; }
void idLobby::DisconnectAllPeers() {}
void idLobby::SendMembersToLobby(idLobby & lobby, bool force) {}
void idLobby::SendMembersToLobby( lobbyType_t destLobbyType, const lobbyConnectInfo_t & connectInfo, bool waitForOtherMembers ) {}
void idSignInManagerBase::ValidateLocalUsers(bool requireOnline) {}
void idLobby::HandlePacket(lobbyAddress_t & from, idBitMsg msg, unsigned short seq) {}
void idLobby::Pump() {}
int idLobby::GetTotalOutgoingRate() { return 0; }
int idLobby::NumFreeSlots() const { return 0; }
bool idLobby::IsLosingConnectionToHost() const { return false; }

// peer_t::GetConnectionState
idLobby::connectionState_t idLobby::peer_t::GetConnectionState() const { return idLobby::CONNECTION_FREE; }

// ============================================================================
// idVoiceChatMgr virtual functions (not inline in header)
// ============================================================================

void idVoiceChatMgr::Init( void * pXAudio2 ) {}
void idVoiceChatMgr::Shutdown() {}
void idVoiceChatMgr::SetActiveChatGroup( int ) {}
voiceState_t idVoiceChatMgr::GetVoiceState( lobbyUser_t const* ) { return VOICECHAT_STATE_NO_MIC; }
void idVoiceChatMgr::SetActiveLobby(int lobby) {}
void idVoiceChatMgr::SubmitIncomingChatData(const unsigned char * data, int size) {}
void idVoiceChatMgr::GetActiveLocalTalkers(idStaticList<int, 8> & talkers) {}
bool idVoiceChatMgr::GetLocalChatData(int index, byte * data, int & size) { return false; }
void idVoiceChatMgr::GetRecipientsForTalker(int talker, idStaticList<lobbyAddress_t const*, 8> & recipients) {}
void idVoiceChatMgr::SetTalkerGroup( const lobbyUser_t * user, int lobbyType, int groupIndex ) {}
int idVoiceChatMgr::FindTalkerByUserId( lobbyUserID_t lobbyUserID, int lobbyType ) { return -1; }
bool idVoiceChatMgr::CanSendVoiceTo( int talkerFromIndex, int talkerToIndex ) { return false; }
bool idVoiceChatMgr::IsRestrictedByPrivleges() { return false; }
void idVoiceChatMgr::SetHeadsetState( int talkerIndex, bool state ) {}
bool idVoiceChatMgr::HasHeadsetStateChanged( int talkerIndex ) { return false; }
void idVoiceChatMgr::SetDisableVoiceReason( disableVoiceReason_t reason ) {}
void idVoiceChatMgr::ClearDisableVoiceReason( disableVoiceReason_t reason ) {}
bool idVoiceChatMgr::GetLocalChatDataInternal( int talkerIndex, byte * data, int & dataSize ) { return false; }
void idVoiceChatMgr::SubmitIncomingChatDataInternal( int talkerIndex, const byte * data, int dataSize ) {}
bool idVoiceChatMgr::TalkerHasData( int talkerIndex ) { return false; }
void idVoiceChatMgr::ToggleMuteLocal( const lobbyUser_t * src, const lobbyUser_t * target ) {}

// ============================================================================
// idDedicatedServerSearch (constructor/destructor not inline)
// ============================================================================

idDedicatedServerSearch::idDedicatedServerSearch() {}
idDedicatedServerSearch::~idDedicatedServerSearch() {}
void idDedicatedServerSearch::HandleQueryAck( lobbyAddress_t &, idBitMsg & ) {}

// ============================================================================
// idLocalUser (implementation not in build)
// ============================================================================

void idLocalUser::LoadProfileSettings() {}

// ============================================================================
// Leaderboard stubs (different signature from sys_leaderboard_stub.cpp)
// ============================================================================

bool LeaderboardLocal_Upload(lobbyUserID_t userID, int leaderboardIndex, int & stats) { return true; }
bool LeaderboardLocal_Upload(lobbyUserID_t userID, int leaderboardIndex, leaderboardStats_t & stats) { return true; }

// localUserHandle_t stubs
void localUserHandle_t::Serialize(idSerializer & serializer) {}

// ============================================================================
// idSignInManagerBase pure virtual functions NOT in sys_signin_stub.cpp
// ============================================================================

idPlayerProfile * idSignInManagerBase::GetDefaultProfile() { return NULL; }
bool idSignInManagerBase::RequirePersistentMaster() { return false; }
void idSignInManagerBase::RemoveAllLocalUsers() {}

// ============================================================================
// idSession - virtual destructor to emit vtable for derived classes
// ============================================================================

idSession::~idSession() {}
