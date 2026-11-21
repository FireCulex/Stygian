/*
===========================================================================

Minimal DoomLib stubs for Linux build (Doom 3 BFG only)

===========================================================================
*/
#ifndef __DOOMLIB_H__
#define __DOOMLIB_H__

// Forward declarations
class idMatchParameters;
class idUserCmdMgr;

namespace DoomLib {
	extern bool expansionDirty;
	extern bool skipToNew;
	extern bool skipToLoad;
	extern idMatchParameters matchParms;
	
	// Original interface with virtual functions (Doom 3 BFG expects this)
	class DoomInterface {
	public:
		virtual ~DoomInterface() = 0;  // Pure virtual destructor
		virtual void Frame(int, idUserCmdMgr*) = 0;
		virtual void Shutdown() = 0;
		virtual void Startup(int, bool) = 0;
		virtual void SetCurrentExpansion(int) = 0;
	};
	
	extern DoomInterface* g_DoomInterface;
	extern DoomInterface& Interface;
	
	extern int GetGlobalData(int);
	extern void SetPlayer(int);
	extern int RemapControl(int key);
	extern void SetCurrentExpansion(int);
}

#endif
