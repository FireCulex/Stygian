/*
 ===============================================================================

 Linux stub for idLocalUser's non-inline methods (sys/sys_localuser.cpp is
 excluded from the SDL build together with sys_local.cpp). Provides just
 enough for idLocalUserSDL (sdl_signin.h) to instantiate and link: the vtable
 entries plus a no-op Pump.

 ===============================================================================
 */

#include "../../idlib/precompiled.h"
#include "../sys_localuser.h"

extern idCVar saveGame_enable;

idLocalUser::idLocalUser() {
	memset( joiningLobby, 0, sizeof( joiningLobby ) );
	localUserHandle = localUserHandle_t( 1 );
	profileMgr.Init( this );
	syncAchievementsRequested = 0;
}

void idLocalUser::Pump() {
	PumpPlatform();
}

bool idLocalUser::IsStorageDeviceAvailable() const {
	return saveGame_enable.GetBool();
}

void idLocalUser::ResetStorageDevice() {
}

bool idLocalUser::StorageSizeAvailable( uint64 minSizeInBytes, int64 & neededBytes ) {
	neededBytes = 0;
	return true;
}

void idLocalUser::SetStatInt( int stat, int value ) {
}

void idLocalUser::SetStatFloat( int stat, float value ) {
}

int idLocalUser::GetStatInt( int stat ) {
	return 0;
}

float idLocalUser::GetStatFloat( int stat ) {
	return 0.0f;
}
