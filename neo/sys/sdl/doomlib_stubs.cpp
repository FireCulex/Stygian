/*
===========================================================================

DoomLib stubs for Linux build (Doom 3 BFG only - no classic Doom)

===========================================================================
*/

#include "sys/sdl/sdl_local.h"
#include "../../doomclassic/doom/doomlib.h"
#include "sys/sys_savegame.h"
#include "../sys_public.h"

struct event_t;

// DoomLib namespace implementation
namespace DoomLib {
	bool expansionDirty = false;
	bool skipToNew = false;
	bool skipToLoad = false;
	idMatchParameters matchParms;
	
	// Concrete implementation
	struct DoomInterfaceImpl : public DoomInterface {
		void Frame(int, idUserCmdMgr*) override {}
		void Shutdown() override {}
		void Startup(int, bool) override {}
		void SetCurrentExpansion(int) override {}
	};
	
	// Must define the pure virtual destructor
	DoomInterface::~DoomInterface() {}
	
	// Static interface instance
	static DoomInterfaceImpl interfaceInstance;
	
	// Interface pointer
	DoomInterface* g_DoomInterface = &interfaceInstance;
	
	int GetGlobalData(int) { return 0; }
	void SetPlayer(int) {}
	int RemapControl(keyNum_t key) { return static_cast<int>(key); }
	void SetCurrentExpansion(int) {}
} // namespace DoomLib

/*
================================================
D_PostEvent
================================================
*/
void D_PostEvent(event_t*) {}

/*
================================================
Sys_FPU functions
================================================
*/
bool Sys_FPU_StackIsEmpty() { return true; }
const char* Sys_FPU_GetState() { return ""; }

/*
================================================
Sys_GetCmdLine / Sys_ReLaunch
================================================
*/
const char* Sys_GetCmdLine() { return ""; }
void Sys_ReLaunch(void*, unsigned int) {}

/*
================================================
g global (Doom classic)
================================================
*/
int g = 0;

/*
================================================
Sys_Error
================================================
*/
void Sys_Error(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	exit(1);
}

/*
================================================
Sys_ExecuteSavegameCommandAsyncImpl
================================================
*/
void Sys_ExecuteSavegameCommandAsyncImpl( idSaveLoadParms* ) {}