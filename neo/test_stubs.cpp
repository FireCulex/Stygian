/*
===========================================================================

Minimal stubs for test build - only provides missing global symbols

===========================================================================
*/

#include "precompiled.h"
#include "../idlib/containers/List.h"  // For idList template

// uiManager global (declared in UserInterface.h)
class idUserInterfaceManager;
idUserInterfaceManager* uiManager = nullptr;

// game global (declared in Game.h)
class idGame;
idGame* game = nullptr;

// g_demoMode cvar (declared in CVarSystem.h)
extern idCVar g_demoMode;
idCVar g_demoMode("g_demoMode", "0", CVAR_SYSTEM | CVAR_BOOL, "demo mode");

// DoomLib namespace (declared in DoomLib.h)
namespace DoomLib {
    bool expansionDirty = false;
    bool skipToNew = false;
    bool skipToLoad = false;
    int matchParms = 0;
    
    class DoomInterface {
    public:
        static DoomInterface* Interface;
        virtual void Frame(int, void*) {}
        virtual void Shutdown() {}
        virtual void Startup(int, bool) {}
        virtual void SetCurrentExpansion(int) {}
    };
    DoomInterface* DoomInterface::Interface = nullptr;
    int GetGlobalData(int) { return 0; }
    void SetPlayer(int) {}
    int RemapControl(int) { return 0; }
}

// R_GetModeListForDisplay (declared in tr_public.h)
struct vidMode_t {};
void R_GetModeListForDisplay(int, idList<vidMode_t, TAG_IDLIB_LIST>&) {}

// DumpAllDisplayDevices (declared in tr_public.h)
void DumpAllDisplayDevices() {}

// SDLPlatform::ShowMessageBox (declared in SDLPlatform.h)
class SDLPlatform {
public:
    static void ShowMessageBox(const char*, const char*, bool) {}
};

// Sys_GetDriveFreeSpaceInBytes (declared in sys_sys.h)
long long Sys_GetDriveFreeSpaceInBytes(const char*) { return 1000000000LL; }

// Sys_GetCmdLine (declared in sys_sys.h)
const char* Sys_GetCmdLine() { return ""; }

// Sys_ReLaunch (declared in sys_sys.h)
void Sys_ReLaunch(void*, unsigned int) {}

// Sys_TimeStampToStr (declared in sys_sys.h)
const char* Sys_TimeStampToStr(long long) { return ""; }

// Sys_SecToStr (declared in sys_sys.h)
const char* Sys_SecToStr(int) { return ""; }

// D_PostEvent (declared in doom_events.h)
struct event_t;
void D_PostEvent(event_t*) {}

// Sys_Init, Sys_InitNetworking, Sys_AlreadyRunning, Sys_Quit, Sys_Shutdown
void Sys_Init() {}
void Sys_InitNetworking() {}
bool Sys_AlreadyRunning() { return false; }
void Sys_Quit() {}
void Sys_Shutdown() {}

// Sys_Launch, Sys_DefaultBasePath, Sys_ListFiles, Sys_Error
void Sys_Launch(const char*, idCmdArgs&, void*, unsigned int) {}
const char* Sys_DefaultBasePath() { return "."; }
int Sys_ListFiles(const char*, const char*, idStrList&) { return 0; }
void Sys_Error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    idLib::Printf("SYS ERROR: ");
    vprintf(fmt, args);
    idLib::Printf("\n");
    va_end(args);
    exit(1);
}

// Sys_FPU_StackIsEmpty and Sys_FPU_GetState stubs
bool Sys_FPU_StackIsEmpty() { return true; }
const char* Sys_FPU_GetState() { return ""; }

// Sys_SetRumble stub
void Sys_SetRumble(int, int, int) {}

// g global (Doom classic)
int g = 0;

// sys_lang cvar
extern idCVar sys_lang;
idCVar sys_lang("sys_lang", "english", 0, "Language");

// saveGame_enable cvar
extern idCVar saveGame_enable;
idCVar saveGame_enable("saveGame_enable", "1", CVAR_SYSTEM | CVAR_BOOL, "enable save games");
