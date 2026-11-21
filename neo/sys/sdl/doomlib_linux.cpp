/*
===========================================================================

DoomLib namespace stubs for the native Linux build.

The classic Doom 1/2 code is excluded from this build (single-player
Doom 3 BFG only), so only the symbols referenced by the engine
(framework/Common.cpp) are provided here. These are declared by the
original doomclassic headers (top-level doomclassic/doom/doomlib.h,
which framework/Common.cpp includes), NOT by the duplicate stub
header under neo/doomclassic.

Platform functions that were also defined in the old
doomlib_stubs.cpp (Sys_Error, Sys_GetCmdLine, Sys_FPU_*, ...) live in
sys_linux_stubs.cpp / sdl_local.cpp instead, so they are intentionally
NOT defined here to avoid duplicate symbols.

===========================================================================
*/

#include "../../idlib/precompiled.h"
#include "../../sys/sys_session.h"		// idMatchParameters
#include "../../../doomclassic/doom/doominterface.h"
#include "../../../doomclassic/doom/doomlib.h"

// ---------------------------------------------------------------------------
// Global DoomInterface stubs (doominterface.cpp is not part of the build).
// Only the members ODR-used by the engine are defined; Shutdown() is called
// non-virtually on DoomLib::Interface from idCommonLocal::PerformGameSwitch.
// ---------------------------------------------------------------------------
DoomInterface::DoomInterface() {}
DoomInterface::~DoomInterface() {}
void DoomInterface::Startup( int, bool ) {}
bool DoomInterface::Frame( int, idUserCmdMgr * ) { return false; }
void DoomInterface::Shutdown() {}

// ---------------------------------------------------------------------------
// DoomLib namespace globals referenced by framework/Common.cpp
// ---------------------------------------------------------------------------
namespace DoomLib {

	DoomInterface	Interface;

	bool			expansionDirty = false;
	bool			skipToNew = false;
	bool			skipToLoad = false;
	char			loadGamePath[ MAX_PATH ] = { 0 };
	int				idealExpansion = 0;
	int				expansionSelected = 0;
	int				chosenSkill = 0;
	int				chosenEpisode = 0;
	idMatchParameters matchParms;

	void SetPlayer( int ) {}
	int GetPlayer() { return 0; }
	void * GetGlobalData( int ) { return NULL; }
	keyNum_t RemapControl( keyNum_t key ) { return key; }
	void SetCurrentExpansion( int ) {}
	const ExpansionData * GetCurrentExpansion() { return NULL; }

} // namespace DoomLib
