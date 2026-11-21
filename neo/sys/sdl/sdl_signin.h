/*
===========================================================================

Linux SDL SignIn stubs

===========================================================================
*/
#ifndef __SDL_SIGNIN_H__
#define __SDL_SIGNIN_H__

#include "sys_signin.h"
#include "sys_localuser.h"

/*
================================================
idLocalUserSDL
================================================
*/
class idLocalUserSDL : public idLocalUser {
public:
	idLocalUserSDL() : inputDevice(0) {}
	virtual ~idLocalUserSDL() {}

	virtual void PumpPlatform() {}
	virtual bool IsProfileReady() const { return true; }
	virtual bool IsOnline() const { return false; }
	virtual uint32 GetOnlineCaps() const { return 0; }
	virtual int GetInputDevice() const { return inputDevice; }
	virtual const char* GetGamerTag() const { return "Player"; }
	virtual bool IsInParty() const { return false; }
	virtual int GetPartyCount() const { return 0; }

private:
	int inputDevice;
};

/*
================================================
idSignInManagerSDL
================================================
*/
class idSignInManagerSDL : public idSignInManagerBase {
public:
	idSignInManagerSDL() {
		// Register one local user on input device 0 so GetMasterInputDevice()
		// returns 0 instead of -1; otherwise common_frame.cpp skips
		// usercmdGen->Joystick() entirely and drains gamepad events every frame.
		// (Alloc, not Append: idLocalUser has no usable copy-assignment.)
		localUsers.Alloc();
	}
	virtual ~idSignInManagerSDL() {}

	virtual void Pump() {}
	virtual void Shutdown() {}
	virtual int GetNumLocalUsers() const { return localUsers.Num(); }
	virtual idLocalUser* GetLocalUserByIndex(int index) { return &localUsers[index]; }
	virtual const idLocalUser* GetLocalUserByIndex(int index) const { return &localUsers[index]; }
	virtual void RemoveLocalUserByIndex(int index) {}
	virtual void RegisterLocalUser(int inputDevice) {}

private:
	idStaticList<idLocalUserSDL, 4> localUsers;
};

#endif
