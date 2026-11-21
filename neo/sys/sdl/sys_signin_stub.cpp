/*
===============================================================================

Linux stub for sign-in manager (no-op for single-player)
Only implements pure virtual functions from idSignInManagerBase

===============================================================================
*/

#include "../../idlib/precompiled.h"
#include "../../sys/sys_session_local.h"

void idSignInManagerBase::Pump() {
}

int idSignInManagerBase::GetNumLocalUsers() const {
	return 1;
}

idLocalUser * idSignInManagerBase::GetLocalUserByIndex( int index ) {
	return NULL;
}

const idLocalUser * idSignInManagerBase::GetLocalUserByIndex( int index ) const {
	return NULL;
}

void idSignInManagerBase::RemoveLocalUserByIndex( int index ) {
}

void idSignInManagerBase::RegisterLocalUser( int inputDevice ) {
}

idLocalUser * idSignInManagerBase::GetLocalUserByInputDevice( int index ) {
	return NULL;
}

idLocalUser * idSignInManagerBase::GetLocalUserByHandle( localUserHandle_t handle ) {
	return NULL;
}

idPlayerProfile * idSignInManagerBase::GetPlayerProfileByInputDevice( int index ) {
	return NULL;
}

bool idSignInManagerBase::RemoveLocalUserByInputDevice( int index ) {
	return false;
}

bool idSignInManagerBase::ProcessInputEvent( const sysEvent_t * ev ) {
	return false;
}