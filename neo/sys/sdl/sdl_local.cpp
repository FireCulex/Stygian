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

===========================================================================
*/

#include "sdl_local.h"
#include "sys_includes.h"
#include "../../renderer/tr_local.h"
#include "../../framework/KeyInput.h"
#include <SDL.h>
#include <SDL_thread.h>
#include <SDL_timer.h>
#include <SDL_filesystem.h>
#include <SDL_gamecontroller.h>
#include <SDL_haptic.h>
#include <SDL_clipboard.h>
#include <SDL_messagebox.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <fnmatch.h>
#include <limits.h>
#include <cstdio>
#include <cstdarg>

// Forward declarations
class idLobbyToSessionCB;

idSysLocalSDL sysLocalSDL;

idSysLocalSDL::idSysLocalSDL() {
	startTime = 0;
}

idSysLocalSDL::~idSysLocalSDL() {
	Shutdown();
}

bool idSysLocalSDL::Init() {
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) < 0) {
		common->Error("SDL_Init failed: %s", SDL_GetError());
		return false;
	}

	startTime = SDL_GetPerformanceCounter();
	return true;
}

void idSysLocalSDL::Shutdown() {
	for (int i = 0; i < 16; i++) {
		if (gamepads[i]) {
			SDL_GameControllerClose(gamepads[i]);
			gamepads[i] = nullptr;
		}
	}

	if (glContext) {
		SDL_GL_DeleteContext(glContext);
		glContext = nullptr;
	}

	if (window) {
		SDL_DestroyWindow(window);
		window = nullptr;
	}

	SDL_Quit();
}

bool idSysLocalSDL::CreateWindow(const char* title, int width, int height, bool fullscreen, bool vsync, int posX, int posY) {
	// Request a compatibility profile: the engine's GLSL render programs use
	// legacy (pre-core) syntax that core-profile drivers reject.
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	// B8b fix: the framebuffer NEEDS an alpha channel. Materials like gui/static
	// use "blend GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA" (a 50/50 noise overlay on
	// monitor screens); without alpha bits in the back buffer GL_DST_ALPHA reads
	// back as 1.0 and the overlay replaces the content at 100% -> pure static.
	// SDL defaults alpha size to 0, so request it explicitly (WGL always had it).
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

	Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
	if (fullscreen) {
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}

	// Honor an explicit window position (r_windowX/r_windowY). SDL_WINDOWPOS_CENTERED
	// centers on the WHOLE desktop, which on a multi-monitor setup can land the window
	// on a different monitor than the one the user is looking at — breaking physical
	// mouse/keyboard input. Fall back to centered only when no position was requested.
	int wx = (posX >= 0) ? posX : SDL_WINDOWPOS_CENTERED;
	int wy = (posY >= 0) ? posY : SDL_WINDOWPOS_CENTERED;
	window = SDL_CreateWindow(title, wx, wy, width, height, flags);
	if (!window) {
		common->Error("SDL_CreateWindow failed: %s", SDL_GetError());
		return false;
	}

	glContext = SDL_GL_CreateContext(window);
	if (!glContext) {
		common->Error("SDL_GL_CreateContext failed: %s", SDL_GetError());
		return false;
	}

	SDL_StartTextInput();
	// Make sure controller button/axis events are pushed into the SDL event
	// queue (they are not, by default, on all SDL2 builds/platforms).
	SDL_GameControllerEventState( SDL_ENABLE );

	if (vsync) {
		SDL_GL_SetSwapInterval(1);
	} else {
		SDL_GL_SetSwapInterval(0);
	}

	// Initialize gamepads
	for (int i = 0; i < SDL_NumJoysticks(); i++) {
		if (SDL_IsGameController(i)) {
			SDL_GameController* ctrl = SDL_GameControllerOpen(i);
			if (ctrl) {
				for (int j = 0; j < 16; j++) {
					if (!gamepads[j]) {
						gamepads[j] = ctrl;
						numGamepads++;
						break;
					}
				}
			}
		}
	}

	// Relative mode is deliberately off (see Sys_GrabMouseCursor); the warp
	// feedback it causes under XWayland makes the view twirl.
	SDL_SetRelativeMouseMode(SDL_FALSE);
	SDL_ShowCursor(SDL_FALSE);

	return true;
}

void idSysLocalSDL::DestroyWindow() {
	for (int i = 0; i < 16; i++) {
		if (gamepads[i]) {
			SDL_GameControllerClose(gamepads[i]);
			gamepads[i] = nullptr;
		}
	}
	numGamepads = 0;

	if (glContext) {
		SDL_GL_DeleteContext(glContext);
		glContext = nullptr;
	}

	if (window) {
		SDL_DestroyWindow(window);
		window = nullptr;
	}
}

void idSysLocalSDL::SwapBuffers() {
	SDL_GL_SwapWindow(window);
}

void idSysLocalSDL::ProcessEvents() {
	// All SDL event translation happens in Sys_GenerateEvents(), which the
	// engine calls once per frame before running its event loop. Polling
	// here as well would steal events from that queue.
}

void idSysLocalSDL::GetMouseDelta(int* x, int* y) {
	int dx, dy;
	SDL_GetRelativeMouseState(&dx, &dy);
	*x = dx;
	*y = dy;
}

void idSysLocalSDL::CenterMouse() {
	if (window) {
		int w, h;
		SDL_GetWindowSize(window, &w, &h);
		SDL_WarpMouseInWindow(window, w / 2, h / 2);
	}
}

void idSysLocalSDL::SetMouseGrab(bool grab) {
	SDL_SetRelativeMouseMode(grab ? SDL_TRUE : SDL_FALSE);
	SDL_ShowCursor(grab ? SDL_FALSE : SDL_TRUE);
}

void idSysLocalSDL::UpdateGamepads() {
	SDL_GameControllerUpdate();
}

bool idSysLocalSDL::IsGamepadConnected(int index) {
	if (index >= 0 && index < 16) {
		return gamepads[index] != nullptr;
	}
	return false;
}

uint64 idSysLocalSDL::GetTimeMicroseconds() {
	return (SDL_GetPerformanceCounter() - startTime) * 1000000 / SDL_GetPerformanceFrequency();
}

uint32 idSysLocalSDL::GetTicks() {
	return SDL_GetTicks();
}

// Threading
SDL_Thread* idSysLocalSDL::CreateThread(SDL_ThreadFunction fn, void* data, const char* name) {
	return SDL_CreateThread(fn, name, data);
}

int idSysLocalSDL::WaitThread(SDL_Thread* thread, int* status) {
	SDL_WaitThread(thread, status);
	return 0;
}

SDL_mutex* idSysLocalSDL::CreateMutex() {
	return SDL_CreateMutex();
}

void idSysLocalSDL::LockMutex(SDL_mutex* mutex) {
	SDL_LockMutex(mutex);
}

void idSysLocalSDL::UnlockMutex(SDL_mutex* mutex) {
	SDL_UnlockMutex(mutex);
}

void idSysLocalSDL::DestroyMutex(SDL_mutex* mutex) {
	SDL_DestroyMutex(mutex);
}

SDL_sem* idSysLocalSDL::CreateSemaphore(Uint32 initialValue) {
	return SDL_CreateSemaphore(initialValue);
}

void idSysLocalSDL::WaitSemaphore(SDL_sem* sem) {
	SDL_SemWait(sem);
}

void idSysLocalSDL::PostSemaphore(SDL_sem* sem) {
	SDL_SemPost(sem);
}

void idSysLocalSDL::DestroySemaphore(SDL_sem* sem) {
	SDL_DestroySemaphore(sem);
}

SDL_cond* idSysLocalSDL::CreateCondition() {
	return SDL_CreateCond();
}

void idSysLocalSDL::WaitCondition(SDL_cond* cond, SDL_mutex* mutex) {
	SDL_CondWait(cond, mutex);
}

void idSysLocalSDL::SignalCondition(SDL_cond* cond) {
	SDL_CondSignal(cond);
}

void idSysLocalSDL::BroadcastCondition(SDL_cond* cond) {
	SDL_CondBroadcast(cond);
}

void idSysLocalSDL::DestroyCondition(SDL_cond* cond) {
	SDL_DestroyCond(cond);
}

// File I/O
SDL_RWops* idSysLocalSDL::OpenFile(const char* path, const char* mode) {
	return SDL_RWFromFile(path, mode);
}

void idSysLocalSDL::CloseFile(SDL_RWops* rw) {
	SDL_RWclose(rw);
}

size_t idSysLocalSDL::ReadFile(SDL_RWops* rw, void* ptr, size_t size, size_t maxnum) {
	return SDL_RWread(rw, ptr, size, maxnum);
}

size_t idSysLocalSDL::WriteFile(SDL_RWops* rw, const void* ptr, size_t size, size_t num) {
	return SDL_RWwrite(rw, ptr, size, num);
}

Sint64 idSysLocalSDL::SeekFile(SDL_RWops* rw, Sint64 offset, int whence) {
	return SDL_RWseek(rw, offset, whence);
}

Sint64 idSysLocalSDL::TellFile(SDL_RWops* rw) {
	return SDL_RWtell(rw);
}

Sint64 idSysLocalSDL::SizeFile(SDL_RWops* rw) {
	return SDL_RWsize(rw);
}

bool idSysLocalSDL::FileExists(const char* path) {
	return SDL_RWFromFile(path, "r") != nullptr;
}

// Paths
const char* idSysLocalSDL::GetBasePath() {
	return SDL_GetBasePath();
}

const char* idSysLocalSDL::GetPrefPath(const char* org, const char* app) {
	return SDL_GetPrefPath(org, app);
}

// Clipboard
void idSysLocalSDL::SetClipboardText(const char* text) {
	SDL_SetClipboardText(text);
}

const char* idSysLocalSDL::GetClipboardText() {
	return SDL_GetClipboardText();
}

bool idSysLocalSDL::HasClipboardText() {
	return SDL_HasClipboardText() == SDL_TRUE;
}

// Display
void idSysLocalSDL::GetDesktopResolution(int* width, int* height) {
	SDL_DisplayMode mode;
	SDL_GetDesktopDisplayMode(0, &mode);
	*width = mode.w;
	*height = mode.h;
}

void idSysLocalSDL::GetDisplayModes(idList<SDL_DisplayMode>& modes) {
	int num = SDL_GetNumDisplayModes(0);
	for (int i = 0; i < num; i++) {
		SDL_DisplayMode mode;
		if (SDL_GetDisplayMode(0, i, &mode) == 0) {
			modes.Append(mode);
		}
	}
}

SDL_DisplayMode idSysLocalSDL::GetCurrentDisplayMode() {
	SDL_DisplayMode mode;
	SDL_GetCurrentDisplayMode(0, &mode);
	return mode;
}

bool idSysLocalSDL::GetModeListForDisplay(const int displayNum, idList<vidMode_t>& modeList) {
	modeList.Clear();

	// Use the display index from the parameter (0-based for SDL)
	int sdlDisplayIndex = displayNum;
	
	// Get number of display modes for this display
	int numModes = SDL_GetNumDisplayModes(sdlDisplayIndex);
	idLib::Printf("R_GetModeListForDisplay: displayNum=%d, sdlDisplayIndex=%d, numModes=%d\n", displayNum, sdlDisplayIndex, numModes);
	if (numModes < 1) {
		return false;
	}

	for (int i = 0; i < numModes; i++) {
		SDL_DisplayMode sdlMode;
		if (SDL_GetDisplayMode(sdlDisplayIndex, i, &sdlMode) != 0) {
			continue;
		}

		// Filter for 32-bit color depth (similar to Windows implementation)
		// SDL_PIXELFORMAT_RGBA8888 = 32-bit, SDL_PIXELFORMAT_BGRA8888 = 32-bit
		Uint32 format = sdlMode.format;
		int bitsPerPixel = 0;
		if (format == SDL_PIXELFORMAT_RGBA8888 || format == SDL_PIXELFORMAT_BGRA8888 ||
			format == SDL_PIXELFORMAT_ARGB8888 || format == SDL_PIXELFORMAT_ABGR8888) {
			bitsPerPixel = 32;
		} else if (format == SDL_PIXELFORMAT_RGB888 || format == SDL_PIXELFORMAT_BGR888) {
			bitsPerPixel = 24;
		} else if (format == SDL_PIXELFORMAT_RGB565) {
			bitsPerPixel = 16;
		}
		
		idLib::Printf("  Mode %d: %dx%d@%dHz format=0x%x bpp=%d\n", i, sdlMode.w, sdlMode.h, sdlMode.refresh_rate, format, bitsPerPixel);

		// Accept both 24-bit and 32-bit modes (Linux typically reports 24-bit)
		if (bitsPerPixel != 32 && bitsPerPixel != 24) {
			continue;
		}

		// Filter for common refresh rates (60Hz and 120Hz, like Windows)
		int refreshRate = sdlMode.refresh_rate;
		if (refreshRate != 60 && refreshRate != 120) {
			continue;
		}

		// Filter minimum resolution (like Windows: 720p minimum)
		if (sdlMode.h < 720) {
			continue;
		}

		vidMode_t mode;
		mode.width = sdlMode.w;
		mode.height = sdlMode.h;
		mode.displayHz = refreshRate;
		modeList.AddUnique(mode);
	}

	// Sort modes by width, then height, then refresh rate (like Windows)
	if (modeList.Num() > 0) {
		class idSort_VidMode : public idSort_Quick< vidMode_t, idSort_VidMode > {
		public:
			int Compare( const vidMode_t & a, const vidMode_t & b ) const {
				int wd = a.width - b.width;
				int hd = a.height - b.height;
				if ( wd != 0 ) {
					return wd;
				}
				if ( hd != 0 ) {
					return hd;
				}
				return a.displayHz - b.displayHz;
			}
		};
		idSort_VidMode sort;
		modeList.SortWithTemplate( sort );
	}

	idLib::Printf("R_GetModeListForDisplay: returning %d modes\n", modeList.Num());
	return modeList.Num() > 0;
}

void idSysLocalSDL::ShowMessageBox(const char* title, const char* message, bool error) {
	SDL_ShowSimpleMessageBox(error ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION, title, message, window);
}

// Threading implementations
uintptr_t Sys_GetCurrentThreadID() {
	return (uintptr_t)SDL_ThreadID();
}

uintptr_t Sys_CreateThread(xthread_t function, void *parms, xthreadPriority priority, 
						  const char *name, core_t core, int stackSize, bool suspended) {
	SDL_Thread *thread = SDL_CreateThread((SDL_ThreadFunction)function, name, parms);
	if (!thread) {
		return 0;
	}
	if (suspended) {
		// SDL doesn't support suspended threads directly
	}
	return (uintptr_t)thread;
}

void Sys_WaitForThread(uintptr_t threadHandle) {
	SDL_WaitThread((SDL_Thread*)threadHandle, NULL);
}

void Sys_DestroyThread(uintptr_t threadHandle) {
	SDL_DetachThread((SDL_Thread*)threadHandle);
}

void Sys_SetCurrentThreadName(const char *name) {
	SDL_SetThreadPriority(SDL_THREAD_PRIORITY_NORMAL);
	// SDL doesn't have a direct thread name API on all platforms
}

// Proper event-style signal: condition variable guarded by a shared mutex
// plus a signaled flag, so raises that happen before a wait are not lost.
struct sdlSignal_t {
	SDL_mutex *	mutex;
	SDL_cond *	cond;
	bool		manualReset;
	bool		signaled;
};

void Sys_SignalCreate(signalHandle_t & handle, bool manualReset) {
	sdlSignal_t *s = new sdlSignal_t;
	s->mutex = SDL_CreateMutex();
	s->cond = SDL_CreateCond();
	s->manualReset = manualReset;
	s->signaled = false;
	handle = s;
}

void Sys_SignalDestroy(signalHandle_t & handle) {
	if (handle) {
		SDL_DestroyCond(handle->cond);
		SDL_DestroyMutex(handle->mutex);
		delete handle;
		handle = nullptr;
	}
}

void Sys_SignalRaise(signalHandle_t & handle) {
	if (!handle) return;
	SDL_LockMutex(handle->mutex);
	handle->signaled = true;
	if (handle->manualReset) {
		SDL_CondBroadcast(handle->cond);
	} else {
		SDL_CondSignal(handle->cond);
	}
	SDL_UnlockMutex(handle->mutex);
}

void Sys_SignalClear(signalHandle_t & handle) {
	if (!handle) return;
	SDL_LockMutex(handle->mutex);
	handle->signaled = false;
	SDL_UnlockMutex(handle->mutex);
}

bool Sys_SignalWait(signalHandle_t & handle, int timeout) {
	if (!handle) return false;
	SDL_LockMutex(handle->mutex);
	bool result = true;
	if (timeout < 0) {
		while (!handle->signaled) {
			SDL_CondWait(handle->cond, handle->mutex);
		}
	} else {
		Uint32 end = SDL_GetTicks() + (Uint32)timeout;
		while (!handle->signaled) {
			Uint32 remaining = (end > SDL_GetTicks()) ? (end - SDL_GetTicks()) : 0;
			if (remaining == 0 && !handle->signaled) {
				result = false;
				break;
			}
			SDL_CondWaitTimeout(handle->cond, handle->mutex, remaining);
		}
	}
	if (!handle->manualReset) {
		handle->signaled = false;	// auto-reset consumes the signal
	}
	SDL_UnlockMutex(handle->mutex);
	return result;
}

void Sys_MutexCreate(mutexHandle_t & handle) {
	handle = SDL_CreateMutex();
}

void Sys_MutexDestroy(mutexHandle_t & handle) {
	if (handle) {
		SDL_DestroyMutex(handle);
		handle = nullptr;
	}
}

bool Sys_MutexLock(mutexHandle_t & handle, bool blocking) {
	if (!handle) return false;
	if (blocking) {
		return SDL_LockMutex(handle) == 0;
	} else {
		return SDL_TryLockMutex(handle) == 0;
	}
}

void Sys_MutexUnlock(mutexHandle_t & handle) {
	if (handle) {
		SDL_UnlockMutex(handle);
	}
}

interlockedInt_t Sys_InterlockedIncrement(interlockedInt_t & value) {
	// SDL2's SDL_AtomicIncRef(a) == SDL_AtomicAdd(a, 1) returns the OLD value,
	// but the contract (idSysInterlockedInteger::Increment) is the NEW value.
	// Use GCC atomic builtins instead (interlockedInt_t is int32_t).
	return __atomic_add_fetch(&value, 1, __ATOMIC_SEQ_CST);
}

interlockedInt_t Sys_InterlockedDecrement(interlockedInt_t & value) {
	// SDL2's SDL_AtomicDecRef(a) == (SDL_AtomicAdd(a, -1) == 1) returns a bool,
	// not the new value — use the GCC builtin.
	return __atomic_sub_fetch(&value, 1, __ATOMIC_SEQ_CST);
}

interlockedInt_t Sys_InterlockedAdd(interlockedInt_t & value, interlockedInt_t i) {
	interlockedInt_t oldValue, newValue;
	do {
		oldValue = value;
		newValue = oldValue + i;
	} while (SDL_AtomicCAS((SDL_atomic_t*)&value, oldValue, newValue) == SDL_FALSE);
	return newValue;
}

interlockedInt_t Sys_InterlockedSub(interlockedInt_t & value, interlockedInt_t i) {
	return Sys_InterlockedAdd(value, -i);
}

interlockedInt_t Sys_InterlockedExchange(interlockedInt_t & value, interlockedInt_t exchange) {
	interlockedInt_t oldValue;
	do {
		oldValue = value;
	} while (SDL_AtomicCAS((SDL_atomic_t*)&value, oldValue, exchange) == SDL_FALSE);
	return oldValue;
}

interlockedInt_t Sys_InterlockedCompareExchange(interlockedInt_t & value, interlockedInt_t comparand, interlockedInt_t exchange) {
	interlockedInt_t oldValue = value;
	if (SDL_AtomicCAS((SDL_atomic_t*)&value, comparand, exchange) == SDL_TRUE) {
		return comparand;
	}
	return oldValue;
}

void * Sys_InterlockedExchangePointer(void * & ptr, void * exchange) {
	void * oldPtr = ptr;
	// Simplified - not atomic on all platforms
	ptr = exchange;
	return oldPtr;
}

void * Sys_InterlockedCompareExchangePointer(void * & ptr, void * comparand, void * exchange) {
	void * oldPtr = ptr;
	if (ptr == comparand) {
		ptr = exchange;
		return comparand;
	}
	return oldPtr;
}

void Sys_Yield() {
	SDL_Delay(0);
}

// SDLPlatform namespace implementations
namespace SDLPlatform {
	void SetMouseGrab(bool grab) {
		SDL_SetRelativeMouseMode(grab ? SDL_TRUE : SDL_FALSE);
		SDL_ShowCursor(grab ? SDL_FALSE : SDL_TRUE);
	}
}

int Sys_Milliseconds() {
	return SDL_GetTicks();
}

char *Sys_GetClipboardData() {
	const char *text = SDL_GetClipboardText();
	if (text) {
		char *result = (char *)Mem_Alloc(strlen(text) + 1, TAG_SYSTEM);
		strcpy(result, text);
		return result;
	}
	return NULL;
}

uint64 Sys_Microseconds() {
	return (SDL_GetPerformanceCounter() - sysLocalSDL.startTime) * 1000000 / SDL_GetPerformanceFrequency();
}

void Sys_DebugPrintf( const char *fmt, ... ) {
	va_list argptr;
	va_start( argptr, fmt );
	char buffer[4096];
	vsnprintf( buffer, sizeof( buffer ), fmt, argptr );
	va_end( argptr );
	SDL_Log( "%s", buffer );
}

void Sys_DebugVPrintf( const char *fmt, va_list arg ) {
	char buffer[4096];
	vsnprintf( buffer, sizeof( buffer ), fmt, arg );
	SDL_Log( "%s", buffer );
}

void Sys_Sleep( int msec ) {
	SDL_Delay( msec );
}

// ---------------------------------------------------------------------------
// Input event translation: SDL events -> engine sysEvent_t queue.
// Sys_GenerateEvents() (called once per frame) drains the SDL queue into
// g_eventQueue; Sys_GetEvent() pops one engine event at a time.
// ---------------------------------------------------------------------------

#include <deque>

static std::deque<sysEvent_t> g_eventQueue;
static SDL_GameController * g_gamepads[16] = { nullptr };

// Separate input buffers for the usercmd poll API (Sys_Poll*InputEvents).
// In-game movement / mouselook / weapon keys are consumed ONLY through
// idUsercmdGenLocal::Mouse()/Keyboard()/Joystick(), which poll via
// Sys_PollMouseInputEvents()/Sys_PollKeyboardInputEvents()/Sys_PollJoystickInputEvents().
// Those are drained once per frame by BuildCurrentUsercmd, while g_eventQueue
// feeds ProcessEvent (ESC / menus / console / GUI). Sys_QueEvent therefore
// mirrors key/mouse events into both paths.
static std::deque<std::pair<int,int>> g_keyEvents;    // (keyNum, down ? 1 : 0)
static std::deque<std::pair<int,int>> g_mouseEvents;  // (M_DELTAX/M_DELTAY, delta)
static std::deque<std::pair<int,int>> g_joyEvents;    // (J_ACTION*/J_AXIS*/J_DPAD*, value)

// Mouse grab state. We deliberately do NOT use SDL_SetRelativeMouseMode: under
// XWayland it re-warps the pointer every frame and each warp emits a motion
// event, so the engine sees a constant delta stream and the view "twirls".
// Instead we confine the pointer to the window (SDL_SetWindowGrab) and compute
// deltas ourselves from absolute motion positions, warping back to center when
// the pointer nears an edge (resetting our last-position so the warp-generated
// motion event yields a zero delta).
static bool g_mouseGrabbed = false;
static bool g_lastMouseValid = false;
static int g_lastMouseX = 0, g_lastMouseY = 0;
static int g_skipMotion = 0;	// pending warp-generated motion events to ignore

static void Sys_QueEvent( sysEventType_t type, int value, int value2 ) {
	sysEvent_t ev;
	memset( &ev, 0, sizeof( ev ) );
	ev.evType = type;
	ev.evValue = value;
	ev.evValue2 = value2;
	g_eventQueue.push_back( ev );

	// mirror into the usercmd poll buffers
	switch ( type ) {
		case SE_KEY:
			g_keyEvents.push_back( std::make_pair( value, value2 ) );
			break;
		case SE_MOUSE:
			// evValue = xrel, evValue2 = yrel
			if ( value != 0 ) {
				g_mouseEvents.push_back( std::make_pair( M_DELTAX, value ) );
			}
			if ( value2 != 0 ) {
				g_mouseEvents.push_back( std::make_pair( M_DELTAY, value2 ) );
			}
			break;
		default:
			break;
	}
}

// Map an SDL scancode to the engine's keyNum_t (which follows the Win32
// scancode layout, so this is a straight remap of the important keys).
static keyNum_t SDL_ScancodeToKeyNum( SDL_Scancode sc ) {
	switch ( sc ) {
		case SDL_SCANCODE_ESCAPE: return K_ESCAPE;
		case SDL_SCANCODE_1: return K_1;
		case SDL_SCANCODE_2: return K_2;
		case SDL_SCANCODE_3: return K_3;
		case SDL_SCANCODE_4: return K_4;
		case SDL_SCANCODE_5: return K_5;
		case SDL_SCANCODE_6: return K_6;
		case SDL_SCANCODE_7: return K_7;
		case SDL_SCANCODE_8: return K_8;
		case SDL_SCANCODE_9: return K_9;
		case SDL_SCANCODE_0: return K_0;
		case SDL_SCANCODE_MINUS: return K_MINUS;
		case SDL_SCANCODE_EQUALS: return K_EQUALS;
		case SDL_SCANCODE_BACKSPACE: return K_BACKSPACE;
		case SDL_SCANCODE_TAB: return K_TAB;
		case SDL_SCANCODE_Q: return K_Q;
		case SDL_SCANCODE_W: return K_W;
		case SDL_SCANCODE_E: return K_E;
		case SDL_SCANCODE_R: return K_R;
		case SDL_SCANCODE_T: return K_T;
		case SDL_SCANCODE_Y: return K_Y;
		case SDL_SCANCODE_U: return K_U;
		case SDL_SCANCODE_I: return K_I;
		case SDL_SCANCODE_O: return K_O;
		case SDL_SCANCODE_P: return K_P;
		case SDL_SCANCODE_LEFTBRACKET: return K_LBRACKET;
		case SDL_SCANCODE_RIGHTBRACKET: return K_RBRACKET;
		case SDL_SCANCODE_RETURN: return K_ENTER;
		case SDL_SCANCODE_LCTRL: return K_LCTRL;
		case SDL_SCANCODE_A: return K_A;
		case SDL_SCANCODE_S: return K_S;
		case SDL_SCANCODE_D: return K_D;
		case SDL_SCANCODE_F: return K_F;
		case SDL_SCANCODE_G: return K_G;
		case SDL_SCANCODE_H: return K_H;
		case SDL_SCANCODE_J: return K_J;
		case SDL_SCANCODE_K: return K_K;
		case SDL_SCANCODE_L: return K_L;
		case SDL_SCANCODE_SEMICOLON: return K_SEMICOLON;
		case SDL_SCANCODE_APOSTROPHE: return K_APOSTROPHE;
		case SDL_SCANCODE_GRAVE: return K_GRAVE;
		case SDL_SCANCODE_LSHIFT: return K_LSHIFT;
		case SDL_SCANCODE_BACKSLASH: return K_BACKSLASH;
		case SDL_SCANCODE_Z: return K_Z;
		case SDL_SCANCODE_X: return K_X;
		case SDL_SCANCODE_C: return K_C;
		case SDL_SCANCODE_V: return K_V;
		case SDL_SCANCODE_B: return K_B;
		case SDL_SCANCODE_N: return K_N;
		case SDL_SCANCODE_M: return K_M;
		case SDL_SCANCODE_COMMA: return K_COMMA;
		case SDL_SCANCODE_PERIOD: return K_PERIOD;
		case SDL_SCANCODE_SLASH: return K_SLASH;
		case SDL_SCANCODE_RSHIFT: return K_RSHIFT;
		case SDL_SCANCODE_KP_MULTIPLY: return K_KP_STAR;
		case SDL_SCANCODE_LALT: return K_LALT;
		case SDL_SCANCODE_SPACE: return K_SPACE;
		case SDL_SCANCODE_CAPSLOCK: return K_CAPSLOCK;
		case SDL_SCANCODE_F1: return K_F1;
		case SDL_SCANCODE_F2: return K_F2;
		case SDL_SCANCODE_F3: return K_F3;
		case SDL_SCANCODE_F4: return K_F4;
		case SDL_SCANCODE_F5: return K_F5;
		case SDL_SCANCODE_F6: return K_F6;
		case SDL_SCANCODE_F7: return K_F7;
		case SDL_SCANCODE_F8: return K_F8;
		case SDL_SCANCODE_F9: return K_F9;
		case SDL_SCANCODE_F10: return K_F10;
		case SDL_SCANCODE_NUMLOCKCLEAR: return K_NUMLOCK;
		case SDL_SCANCODE_SCROLLLOCK: return K_SCROLL;
		case SDL_SCANCODE_KP_7: return K_KP_7;
		case SDL_SCANCODE_KP_8: return K_KP_8;
		case SDL_SCANCODE_KP_9: return K_KP_9;
		case SDL_SCANCODE_KP_MINUS: return K_KP_MINUS;
		case SDL_SCANCODE_KP_4: return K_KP_4;
		case SDL_SCANCODE_KP_5: return K_KP_5;
		case SDL_SCANCODE_KP_6: return K_KP_6;
		case SDL_SCANCODE_KP_PLUS: return K_KP_PLUS;
		case SDL_SCANCODE_KP_1: return K_KP_1;
		case SDL_SCANCODE_KP_2: return K_KP_2;
		case SDL_SCANCODE_KP_3: return K_KP_3;
		case SDL_SCANCODE_KP_0: return K_KP_0;
		case SDL_SCANCODE_KP_PERIOD: return K_KP_DOT;
		case SDL_SCANCODE_F11: return K_F11;
		case SDL_SCANCODE_F12: return K_F12;
		case SDL_SCANCODE_KP_ENTER: return K_KP_ENTER;
		case SDL_SCANCODE_RCTRL: return K_RCTRL;
		case SDL_SCANCODE_KP_COMMA: return K_KP_COMMA;
		case SDL_SCANCODE_KP_DIVIDE: return K_KP_SLASH;
		case SDL_SCANCODE_PRINTSCREEN: return K_PRINTSCREEN;
		case SDL_SCANCODE_RALT: return K_RALT;
		case SDL_SCANCODE_PAUSE: return K_PAUSE;
		case SDL_SCANCODE_HOME: return K_HOME;
		case SDL_SCANCODE_UP: return K_UPARROW;
		case SDL_SCANCODE_PAGEUP: return K_PGUP;
		case SDL_SCANCODE_LEFT: return K_LEFTARROW;
		case SDL_SCANCODE_RIGHT: return K_RIGHTARROW;
		case SDL_SCANCODE_END: return K_END;
		case SDL_SCANCODE_DOWN: return K_DOWNARROW;
		case SDL_SCANCODE_PAGEDOWN: return K_PGDN;
		case SDL_SCANCODE_INSERT: return K_INS;
		case SDL_SCANCODE_DELETE: return K_DEL;
		case SDL_SCANCODE_LGUI: return K_LWIN;
		case SDL_SCANCODE_RGUI: return K_RWIN;
		case SDL_SCANCODE_APPLICATION: return K_APPS;
		default: return K_NONE;
	}
}

void Sys_GenerateEvents() {
	SDL_GameControllerUpdate();
	SDL_Event event;
	while ( SDL_PollEvent( &event ) ) {
		switch ( event.type ) {
			case SDL_QUIT:
				common->Quit();
				break;
			case SDL_KEYDOWN: {
				keyNum_t key = SDL_ScancodeToKeyNum( event.key.keysym.scancode );
				if ( key != K_NONE && !event.key.repeat ) {
					Sys_QueEvent( SE_KEY, key, 1 );
					// win32 delivers these as WM_CHAR -> SE_CHAR; SDL only sends them
					// as keydowns, and the console edit field handles deletion/paste
					// in CharEvent(), not KeyDownEvent(). Mirror WM_CHAR:
					if ( event.key.keysym.scancode == SDL_SCANCODE_BACKSPACE ) {
						Sys_QueEvent( SE_CHAR, 8, 0 );          // BS
					} else if ( event.key.keysym.scancode == SDL_SCANCODE_V &&
								( event.key.keysym.mod & KMOD_CTRL ) ) {
						Sys_QueEvent( SE_CHAR, 22, 0 );         // Ctrl+V
					}
				}
				break;
			}
			case SDL_KEYUP: {
				keyNum_t key = SDL_ScancodeToKeyNum( event.key.keysym.scancode );
				if ( key != K_NONE ) {
					Sys_QueEvent( SE_KEY, key, 0 );
				}
				break;
			}
			case SDL_TEXTINPUT: {
				// engine consumes ASCII chars one SE_CHAR at a time
				const char * text = event.text.text;
				for ( int i = 0; text[i] != 0 && i < 32; i++ ) {
					unsigned char c = (unsigned char)text[i];
					if ( c >= 32 && c < 127 ) {
						Sys_QueEvent( SE_CHAR, c, 0 );
					}
				}
				break;
			}
			case SDL_MOUSEMOTION: {
				int dx = 0, dy = 0;
				if ( g_mouseGrabbed && SDL_GetRelativeMouseMode() == SDL_TRUE ) {
					// Relative mode is active: SDL confines+warps the pointer itself
					// and reports clean deltas. Use xrel/yrel directly — computing
					// deltas from absolute positions here double-counts the warp and
					// makes the view twirl.
					dx = event.motion.xrel;
					dy = event.motion.yrel;
				} else if ( g_skipMotion > 0 ) {
					// motion event generated by our own edge warp — drop its delta
					g_skipMotion--;
					g_lastMouseX = event.motion.x;
					g_lastMouseY = event.motion.y;
					g_lastMouseValid = true;
				} else if ( g_mouseGrabbed ) {
					// Fallback when relative mode could not be engaged: absolute
					// deltas + edge warp-to-center (keeps the pointer in the window)
					if ( g_lastMouseValid ) {
						dx = event.motion.x - g_lastMouseX;
						dy = event.motion.y - g_lastMouseY;
					}
					g_lastMouseX = event.motion.x;
					g_lastMouseY = event.motion.y;
					g_lastMouseValid = true;
					int w = 0, h = 0;
					if ( sysLocalSDL.window != nullptr ) {
						SDL_GetWindowSize( sysLocalSDL.window, &w, &h );
					}
					if ( w > 16 && h > 16 &&
						 ( event.motion.x < 4 || event.motion.x >= w - 4 ||
						   event.motion.y < 4 || event.motion.y >= h - 4 ) ) {
						SDL_WarpMouseInWindow( sysLocalSDL.window, w / 2, h / 2 );
						g_lastMouseX = w / 2;
						g_lastMouseY = h / 2;
						g_skipMotion = 1;
					}
				} else {
					dx = event.motion.xrel;
					dy = event.motion.yrel;
					g_lastMouseX = event.motion.x;
					g_lastMouseY = event.motion.y;
					g_lastMouseValid = true;
				}
				if ( dx != 0 || dy != 0 ) {
					Sys_QueEvent( SE_MOUSE, dx, dy );
				}
				break;
			}
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP: {
				int btn = 0;
				switch ( event.button.button ) {
					case SDL_BUTTON_LEFT:   btn = K_MOUSE1; break;
					case SDL_BUTTON_MIDDLE: btn = K_MOUSE3; break;
					case SDL_BUTTON_RIGHT:  btn = K_MOUSE2; break;
					case SDL_BUTTON_X1:     btn = K_MOUSE4; break;
					case SDL_BUTTON_X2:     btn = K_MOUSE5; break;
					default: break;
				}
				if ( btn != 0 ) {
					Sys_QueEvent( SE_KEY, btn, ( event.type == SDL_MOUSEBUTTONDOWN ) ? 1 : 0 );
				}
				break;
			}
			case SDL_MOUSEWHEEL: {
				int wheel = event.wheel.y;
				if ( wheel > 0 ) {
					for ( int i = 0; i < wheel; i++ ) {
						Sys_QueEvent( SE_KEY, K_MWHEELUP, 1 );
						Sys_QueEvent( SE_KEY, K_MWHEELUP, 0 );
					}
				} else if ( wheel < 0 ) {
					for ( int i = 0; i < -wheel; i++ ) {
						Sys_QueEvent( SE_KEY, K_MWHEELDOWN, 1 );
						Sys_QueEvent( SE_KEY, K_MWHEELDOWN, 0 );
					}
				}
				break;
			}
			case SDL_CONTROLLERDEVICEADDED:
				if ( event.cdevice.which < 16 && !g_gamepads[event.cdevice.which] ) {
					g_gamepads[event.cdevice.which] = SDL_GameControllerOpen( event.cdevice.which );
				}
				break;
			case SDL_CONTROLLERDEVICEREMOVED:
				for ( int i = 0; i < 16; i++ ) {
					if ( g_gamepads[i] ) {
						SDL_Joystick * joy = SDL_GameControllerGetJoystick( g_gamepads[i] );
						if ( joy && SDL_JoystickInstanceID( joy ) == event.cdevice.which ) {
							SDL_GameControllerClose( g_gamepads[i] );
							g_gamepads[i] = nullptr;
							break;
						}
					}
				}
				break;
			case SDL_CONTROLLERBUTTONDOWN:
			case SDL_CONTROLLERBUTTONUP: {
				bool down = ( event.type == SDL_CONTROLLERBUTTONDOWN );
				int btn = event.cbutton.button;
				int action;
				if ( btn >= SDL_CONTROLLER_BUTTON_DPAD_UP && btn <= SDL_CONTROLLER_BUTTON_DPAD_RIGHT ) {
					action = J_DPAD_UP + ( btn - SDL_CONTROLLER_BUTTON_DPAD_UP );
				} else if ( btn >= 0 && btn < ( J_ACTION_MAX - J_ACTION1 + 1 ) ) {
					action = J_ACTION1 + btn;
				} else {
					break;
				}
				if ( g_joyEvents.size() > 2048 ) {
					g_joyEvents.clear();	// never let this grow unbounded if never polled
				}
				g_joyEvents.push_back( std::make_pair( action, down ? 1 : 0 ) );
				// also mirror into the main event queue so menus/SWF can bind
				// K_JOY* keys (win32 delivers controller buttons as key events)
				if ( action >= J_ACTION1 && action <= J_ACTION_MAX ) {
					Sys_QueEvent( SE_KEY, K_JOY1 + ( action - J_ACTION1 ), down ? 1 : 0 );
				} else {
					Sys_QueEvent( SE_KEY, K_JOY_DPAD_UP + ( action - J_DPAD_UP ), down ? 1 : 0 );
				}
				break;
			}
			case SDL_CONTROLLERAXISMOTION: {
				// SDL_GameControllerAxis order (LEFTX,LEFTY,RIGHTX,RIGHTY,LEFTTRIG,RIGHTTRIG)
				// matches joystickAxis_t (AXIS_LEFT_X..AXIS_RIGHT_TRIG)
				int axis = event.caxis.axis;
				if ( axis >= 0 && axis < MAX_JOYSTICK_AXIS ) {
					if ( g_joyEvents.size() > 2048 ) {
						g_joyEvents.clear();
					}
					g_joyEvents.push_back( std::make_pair( J_AXIS_MIN + axis, (int)event.caxis.value ) );
				}
				break;
			}
			case SDL_WINDOWEVENT:
				if ( event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ) {
					idKeyInput::ClearStates();
					g_lastMouseValid = false;
				}
				if ( event.window.event == SDL_WINDOWEVENT_RESIZED ||
					 event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ) {
					// mirror win32 WM_WINDOWPOSCHANGED: the renderer sizes its
					// viewport from glConfig.nativeScreenWidth/Height, so a WM
					// fullscreen/resize must update them or the game only draws
					// into the old area (black bars / half-window picture).
					GLimp_HandleWindowResize( event.window.data1, event.window.data2 );
				}
				break;
			default:
				break;
		}
	}
}

sysEvent_t Sys_GetEvent() {
	if ( g_eventQueue.empty() ) {
		sysEvent_t ev;
		memset( &ev, 0, sizeof( ev ) );
		ev.evType = SE_NONE;
		return ev;
	}
	sysEvent_t ev = g_eventQueue.front();
	g_eventQueue.pop_front();
	return ev;
}

void Sys_ClearEvents() {
	g_eventQueue.clear();
	g_keyEvents.clear();
	g_mouseEvents.clear();
	g_joyEvents.clear();
}

void Sys_InitInput() {
	// Input is initialized in CreateWindow
}

void Sys_ShutdownInput() {
	// Input is shutdown in DestroyWindow
}

int Sys_PollKeyboardInputEvents() {
	// Return number of keyboard events available
	return (int)g_keyEvents.size();
}

int Sys_ReturnKeyboardInputEvent( const int n, int &ch, bool &state ) {
	if ( n < 0 || n >= (int)g_keyEvents.size() ) {
		return 0;
	}
	ch = g_keyEvents[n].first;
	state = ( g_keyEvents[n].second != 0 );
	return 1;
}

void Sys_EndKeyboardInputEvents() {
	g_keyEvents.clear();
}

int Sys_PollMouseInputEvents( int mouseEvents[MAX_MOUSE_EVENTS][2] ) {
	int num = (int)g_mouseEvents.size();
	if ( num > MAX_MOUSE_EVENTS ) {
		num = MAX_MOUSE_EVENTS;
	}
	for ( int i = 0; i < num; i++ ) {
		mouseEvents[i][0] = g_mouseEvents[i].first;
		mouseEvents[i][1] = g_mouseEvents[i].second;
	}
	// Consume the events here: win32 drains the DirectInput buffer inside the
	// poll call, and idUsercmdGenLocal::Mouse() never calls Sys_EndMouseInputEvents.
	g_mouseEvents.clear();
	return num;
}

void Sys_EndMouseInputEvents() {
	g_mouseEvents.clear();
}

int Sys_PollJoystickInputEvents( int deviceNum ) {
	return (int)g_joyEvents.size();
}

int Sys_ReturnJoystickInputEvent( const int n, int &action, int &value ) {
	if ( n < 0 || n >= (int)g_joyEvents.size() ) {
		return 0;
	}
	action = g_joyEvents[n].first;
	value = g_joyEvents[n].second;
	return 1;
}

void Sys_EndJoystickInputEvents() {
	g_joyEvents.clear();
}

void Sys_GrabMouseCursor( bool grabIt ) {
	// When grabbed, use SDL relative mouse mode: under X11/XWayland SDL
	// confines the pointer to the window and warps it to center itself, so
	// the pointer can never leave the window (niri/XWayland safe). The
	// SDL_MOUSEMOTION handler uses xrel/yrel in this mode, which avoids the
	// old warp-feedback twirl (that came from computing deltas from absolute
	// positions while SDL was warping). If relative mode cannot be engaged,
	// the motion handler falls back to absolute deltas + edge warp.
	//
	// common_frame calls this every frame with the same value; only reset the
	// tracking state when the grab actually changes, otherwise the first
	// motion event of every frame is treated as a zero-delta "first" event
	// and single synthetic mouse moves never accumulate.
	if ( g_mouseGrabbed == grabIt ) {
		if ( sysLocalSDL.window != nullptr ) {
			SDL_SetRelativeMouseMode( grabIt ? SDL_TRUE : SDL_FALSE );
		}
		SDL_ShowCursor( grabIt ? SDL_FALSE : SDL_TRUE );
		return;
	}
	if ( sysLocalSDL.window != nullptr ) {
		SDL_SetRelativeMouseMode( grabIt ? SDL_TRUE : SDL_FALSE );
	}
	SDL_ShowCursor( grabIt ? SDL_FALSE : SDL_TRUE );
	g_mouseGrabbed = grabIt;
	g_lastMouseValid = false;
	g_skipMotion = 0;
}

void Sys_ShowWindow( bool show ) {
	if (sysLocalSDL.window) {
		if (show) {
			SDL_ShowWindow(sysLocalSDL.window);
		} else {
			SDL_HideWindow(sysLocalSDL.window);
		}
	}
}

bool Sys_IsWindowVisible() {
	if (sysLocalSDL.window) {
		Uint32 flags = SDL_GetWindowFlags(sysLocalSDL.window);
		return (flags & SDL_WINDOW_SHOWN) != 0;
	}
	return false;
}

double Sys_GetClockTicks() {
	return (double)SDL_GetPerformanceCounter();
}

double Sys_ClockTicksPerSecond() {
	return (double)SDL_GetPerformanceFrequency();
}

cpuid_t Sys_GetProcessorId() {
	return CPUID_NONE; // TODO: Implement proper CPU detection
}

const char *Sys_GetProcessorString() {
	return "Unknown";
}

int Sys_GetSystemRam() {
	return 0; // TODO: Implement
}

int Sys_GetVideoRam() {
	return 0; // TODO: Implement
}

void Sys_SetPhysicalWorkMemory( int minBytes, int maxBytes ) {
	// Not implemented on SDL
}

void Sys_CPUCount( int & numLogicalCPUCores, int & numPhysicalCPUCores, int & numCPUPackages ) {
	numLogicalCPUCores = SDL_GetCPUCount();
	numPhysicalCPUCores = SDL_GetCPUCount(); // SDL doesn't distinguish
	numCPUPackages = 1;
}

// idSys pure virtual implementations
void idSysLocalSDL::DebugPrintf(const char* fmt, ...) {
	va_list argptr;
	va_start(argptr, fmt);
	char buffer[4096];
	vsnprintf(buffer, sizeof(buffer), fmt, argptr);
	va_end(argptr);
	SDL_Log("%s", buffer);
}

void idSysLocalSDL::DebugVPrintf(const char* fmt, va_list arg) {
	char buffer[4096];
	vsnprintf(buffer, sizeof(buffer), fmt, arg);
	SDL_Log("%s", buffer);
}

double idSysLocalSDL::GetClockTicks() {
	return (double)SDL_GetPerformanceCounter();
}

double idSysLocalSDL::ClockTicksPerSecond() {
	return (double)SDL_GetPerformanceFrequency();
}

cpuid_t idSysLocalSDL::GetProcessorId() {
	return CPUID_NONE; // TODO: Implement proper CPU detection
}

const char* idSysLocalSDL::GetProcessorString() {
	return "Unknown";
}

const char* idSysLocalSDL::FPU_GetState() {
	return "";
}

bool idSysLocalSDL::FPU_StackIsEmpty() {
	return true;
}

void idSysLocalSDL::FPU_SetFTZ(bool enable) {
	// Not implemented on SDL
}

void idSysLocalSDL::FPU_SetDAZ(bool enable) {
	// Not implemented on SDL
}

void idSysLocalSDL::FPU_EnableExceptions(int exceptions) {
	// Not implemented on SDL
}

bool idSysLocalSDL::LockMemory(void* ptr, int size) {
	// Not implemented on SDL
	return false;
}

bool idSysLocalSDL::UnlockMemory(void* ptr, int size) {
	// Not implemented on SDL
	return false;
}

void idSysLocalSDL::GetCallStack(address_t* stack, int maxFrames) {
	// Not implemented on SDL
	if (stack && maxFrames > 0) {
		stack[0] = 0;
	}
}

const char* idSysLocalSDL::GetCallStackStr(const address_t* stack, int maxFrames) {
	return "Call stack not available on SDL";
}

const char* idSysLocalSDL::GetCallStackCurStr(int skipFrames) {
	return "Call stack not available on SDL";
}

void idSysLocalSDL::ShutdownSymbols() {
	// Not implemented on SDL
}

int idSysLocalSDL::DLL_Load(const char* dllName) {
	// Not implemented on SDL - no dynamic library loading
	return 0;
}

void* idSysLocalSDL::DLL_GetProcAddress(int dllHandle, const char* procName) {
	// Not implemented on SDL
	return nullptr;
}

void idSysLocalSDL::DLL_Unload(int dllHandle) {
	// Not implemented on SDL
}

void idSysLocalSDL::DLL_GetFileName(const char* baseName, char* fileName, int maxLength) {
	// Not implemented on SDL
	if (fileName && maxLength > 0) {
		fileName[0] = '\0';
	}
}

sysEvent_t idSysLocalSDL::GenerateMouseButtonEvent(int button, bool down) {
	// Must match the base convention (neo/sys/sys_local.cpp): evValue = key code,
	// evValue2 = down/up. The previous version had them swapped, which broke all
	// world-GUI clicks (airlock panels etc.): Window.cpp checks evValue == K_MOUSE1.
	sysEvent_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.evType = SE_KEY;
	ev.evValue = K_MOUSE1 + button - 1;
	ev.evValue2 = down ? 1 : 0;
	return ev;
}

sysEvent_t idSysLocalSDL::GenerateMouseMoveEvent(int x, int y) {
	sysEvent_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.evType = SE_MOUSE;
	ev.evValue = x;
	ev.evValue2 = y;
	return ev;
}

void idSysLocalSDL::OpenURL(const char* url, bool bShouldOpenInSteam) {
	// Not implemented on SDL - would need platform-specific URL opening
	(void)bShouldOpenInSteam;
	(void)url;
}

void idSysLocalSDL::StartProcess(const char* commandLine, bool waitForFinish) {
	// Not implemented on SDL - would need platform-specific process creation
	(void)commandLine;
	(void)waitForFinish;
}

// Global sys pointer
idSys *sys = &sysLocalSDL;
/*
================================================
SDLPlatform::ShowMessageBox
================================================
*/
void SDLPlatform::ShowMessageBox( const char *title, const char *message, bool isError ) {
	Uint32 flags = isError ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION;
	SDL_ShowSimpleMessageBox( flags, title, message, sysLocalSDL.window );
}

/*
================================================
Sys_Printf
================================================
*/
void Sys_Printf( const char *fmt, ... ) {
	va_list argptr;
	va_start( argptr, fmt );
	char buffer[ 32768 ];
	vsnprintf( buffer, sizeof( buffer ), fmt, argptr );
	va_end( argptr );
	SDL_Log( "%s", buffer );
}

/*
================================================
Sys_SetFatalError
================================================
*/
void Sys_SetFatalError( const char *error ) {
	SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, "Fatal Error", error, sysLocalSDL.window );
}

/*
================================================
Sys_SetClipboardData
================================================
*/
void Sys_SetClipboardData( const char *string ) {
	SDL_SetClipboardText( string );
}

/*
================================================
Sys_FileTimeStamp
================================================
*/
ID_TIME_T Sys_FileTimeStamp( idFileHandle fp ) {
	// Not implemented for Linux/SDL
	return 0;
}

/*
================================================
Sys_Rmdir
================================================
*/
bool Sys_Rmdir( const char *path ) {
	return rmdir( path ) == 0;
}

/*
================================================
Sys_Mkdir
================================================
*/
void Sys_Mkdir( const char *path ) {
	mkdir( path, 0755 );
}

/*
================================================
Sys_EXEPath
================================================
*/
const char *Sys_EXEPath() {
	static char exePath[ MAX_OSPATH ];
	if ( exePath[ 0 ] == '\0' ) {
		char buf[ 1024 ];
		ssize_t len = readlink( "/proc/self/exe", buf, sizeof( buf ) - 1 );
		if ( len > 0 ) {
			buf[ len ] = '\0';
			strcpy( exePath, buf );
		} else {
			strcpy( exePath, "." );
		}
	}
	return exePath;
}

/*
================================================
Sys_DefaultSavePath
================================================
*/
const char *Sys_DefaultSavePath() {
	static char savePath[ MAX_OSPATH ];
	if ( savePath[ 0 ] == '\0' ) {
		const char *home = getenv( "HOME" );
		if ( home ) {
			snprintf( savePath, sizeof( savePath ), "%s/.doom3bfg/savegames", home );
		} else {
			strcpy( savePath, "./savegames" );
		}
		// mkdir(2) is not recursive: create each missing parent (e.g. ~/.doom3bfg)
		// the same way idFileSystemLocal::CreateOSPath does.
		for ( char *p = savePath + 1; *p; p++ ) {
			if ( *p == '/' ) {
				*p = '\0';
				mkdir( savePath, 0755 );
				*p = '/';
			}
		}
		mkdir( savePath, 0755 );
	}
	return savePath;
}

/*
================================================
Sys_IsFolder
================================================
*/
sysFolder_t Sys_IsFolder( const char *path ) {
	struct stat st;
	if ( stat( path, &st ) == 0 ) {
		if ( S_ISDIR( st.st_mode ) ) {
			return FOLDER_YES;
		}
		return FOLDER_NO;
	}
	return FOLDER_NO;
}

/*
================================================
Sys_IsFileWritable
================================================
*/
bool Sys_IsFileWritable( const char *path ) {
	return access( path, W_OK ) == 0;
}

/*
================================================
Sys_SetLanguageFromSystem
================================================
*/
void Sys_SetLanguageFromSystem() {
	// Default to English for now
	extern idCVar sys_lang;
	sys_lang.SetString( "english" );
}

/*
================================================
================================================
*/
void idLocalUser::SaveProfileSettings() {
	// Stub implementation
}

/*
================================================
idSaveGameDetails
================================================
*/
// idSaveGameDetails already implemented in sys/Snapshot.cpp

/*
================================================
idSaveGameManager
================================================
*/
// idSaveGameManager methods already implemented in sys/sys_savegame.cpp

/*
================================================
idSignInManagerBase
================================================
*/
// idSignInManagerBase::ProcessInputEvent implemented in sys/sys_signin.cpp

/*
================================================
idSnapShot - already implemented in sys/Snapshot.cpp
================================================
*/

/*
================================================
lobbyToSessionCB
================================================
*/
idLobbyToSessionCB* lobbyToSessionCB = nullptr;

/*
================================================
idUDP destructor
================================================
*/
// idUDP destructor implemented in sys/Snapshot.cpp

/*
================================================
Sys_DefaultLanguage
================================================
*/
// Sys_DefaultLanguage already defined at line 1064
/*
================================================
Network functions (Linux/SDL implementation)
================================================
*/

#include <sys/socket.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <cstring>
#include <dlfcn.h>

static int udpSocket = -1;

bool Sys_StringToNetAdr( const char *s, netadr_t *a, bool doDNSResolve ) {
	memset( a, 0, sizeof( netadr_t ) );
	
	if ( doDNSResolve ) {
		struct hostent *h = gethostbyname( s );
		if ( h ) {
			a->type = NA_IP;
			memcpy( a->ip, h->h_addr_list[0], 4 );
			return true;
		}
		return false;
	}
	
	// Parse IP:port
	char buf[256];
	strncpy( buf, s, sizeof(buf)-1 );
	buf[sizeof(buf)-1] = '\0';
	
	char *colon = strrchr( buf, ':' );
	if ( colon ) {
		*colon = '\0';
		a->port = htons( atoi( colon + 1 ) );
	} else {
		a->port = 0;
	}
	
	if ( inet_pton( AF_INET, buf, a->ip ) == 1 ) {
		a->type = NA_IP;
		return true;
	}
	
	if ( doDNSResolve ) {
		struct hostent *h = gethostbyname( buf );
		if ( h ) {
			a->type = NA_IP;
			memcpy( a->ip, h->h_addr_list[0], 4 );
			return true;
		}
	}
	
	return false;
}

const char *Sys_NetAdrToString( const netadr_t a ) {
	static char buf[64];
	if ( a.type == NA_IP ) {
		snprintf( buf, sizeof(buf), "%d.%d.%d.%d:%d", 
			a.ip[0], a.ip[1], a.ip[2], a.ip[3], ntohs(a.port) );
	} else if ( a.type == NA_IP ) {
		snprintf( buf, sizeof(buf), "[%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]:%d",
			a.ip[0], a.ip[1], a.ip[2], a.ip[3], a.ip[4], a.ip[5], a.ip[6], a.ip[7],
			a.ip[8], a.ip[9], a.ip[10], a.ip[11], a.ip[12], a.ip[13], a.ip[14], a.ip[15],
			ntohs(a.port) );
	} else {
		strcpy( buf, "unknown" );
	}
	return buf;
}

bool Sys_CompareNetAdrBase( const netadr_t a, const netadr_t b ) {
	if ( a.type != b.type ) {
		return false;
	}
	if ( a.type == NA_IP ) {
		return memcmp( a.ip, b.ip, 4 ) == 0;
	} else if ( a.type == NA_IP ) {
		return memcmp( a.ip, b.ip, 4 ) == 0;
	}
	return false;
}

int Sys_GetLocalIPCount() {
	return 1;
}

const char *Sys_GetLocalIP( int i ) {
	return "127.0.0.1";
}

bool Sys_IsLANAddress( const netadr_t a ) {
	return true;
}

/*
================================================
idUDP implementation
================================================
*/
idUDP::idUDP() {
	bound_to.type = NA_IP;
	memset( bound_to.ip, 0, 4 );
	bound_to.port = 0;
	netSocket = -1;
	silent = false;
	packetsRead = 0;
	bytesRead = 0;
	packetsWritten = 0;
	bytesWritten = 0;
}

idUDP::~idUDP() {
	Close();
}

bool idUDP::InitForPort( int portNumber ) {
	if ( netSocket != -1 ) {
		close( netSocket );
	}
	
	netSocket = socket( AF_INET, SOCK_DGRAM, 0 );
	if ( netSocket == -1 ) {
		return false;
	}
	
	struct sockaddr_in addr;
	memset( &addr, 0, sizeof(addr) );
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons( portNumber );
	
	if ( bind( netSocket, (struct sockaddr*)&addr, sizeof(addr) ) == -1 ) {
		close( netSocket );
		netSocket = -1;
		return false;
	}
	
	// Set non-blocking
	int flags = fcntl( netSocket, F_GETFL, 0 );
	fcntl( netSocket, F_SETFL, flags | O_NONBLOCK );
	
	bound_to.type = NA_IP;
	memset( bound_to.ip, 0, 4 );
	bound_to.port = htons( portNumber );
	
	return true;
}

void idUDP::Close() {
	if ( netSocket != -1 ) {
		close( netSocket );
		netSocket = -1;
	}
	bound_to.port = 0;
}

bool idUDP::GetPacket( netadr_t &from, void *data, int &size, int maxSize ) {
	if ( netSocket == -1 ) {
		return false;
	}
	
	struct sockaddr_in fromAddr;
	socklen_t fromLen = sizeof(fromAddr);
	
	ssize_t len = recvfrom( netSocket, data, maxSize, 0, (struct sockaddr*)&fromAddr, &fromLen );
	if ( len <= 0 ) {
		return false;
	}
	
	from.type = NA_IP;
	memcpy( from.ip, &fromAddr.sin_addr.s_addr, 4 );
	from.port = fromAddr.sin_port;
	
	size = len;
	packetsRead++;
	bytesRead += len;
	
	return true;
}

bool idUDP::GetPacketBlocking( netadr_t &from, void *data, int &size, int maxSize, int timeout ) {
	// For simplicity, just call GetPacket
	return GetPacket( from, data, size, maxSize );
}

void idUDP::SendPacket( const netadr_t to, const void *data, int size ) {
	if ( netSocket == -1 ) {
		return;
	}
	
	struct sockaddr_in toAddr;
	memset( &toAddr, 0, sizeof(toAddr) );
	toAddr.sin_family = AF_INET;
	toAddr.sin_port = to.port;
	memcpy( &toAddr.sin_addr.s_addr, to.ip, 4 );
	
	ssize_t len = sendto( netSocket, data, size, 0, (struct sockaddr*)&toAddr, sizeof(toAddr) );
	if ( len > 0 ) {
		packetsWritten++;
		bytesWritten += len;
	}
}

/*
================================================
FPU and memory functions
================================================
*/
void Sys_FPU_SetFTZ( bool enable ) {
	// Flush to zero - not implemented for Linux
}

void Sys_FPU_SetDAZ( bool enable ) {
	// Denormals are zero - not implemented for Linux
}

void Sys_GetCallStack( unsigned long *stack, int maxFrames ) {
	// Not implemented
}



void Sys_ShutdownSymbols() {
	// Not implemented
}

int Sys_DLL_Load( const char *name ) {
	void *handle = dlopen( name, 1 );
	return (int)(intptr_t)handle;
}

void *Sys_DLL_GetProcAddress( int handle, const char *name ) {
	return dlsym( (void*)(intptr_t)handle, name );
}

void Sys_DLL_Unload( int handle ) {
	dlclose( (void*)(intptr_t)handle );
}

void Sys_FPU_EnableExceptions( int exceptions ) {
	// Not implemented for Linux
}







/*
================================================
Missing Sys functions (correct signatures)
================================================
*/
bool Sys_LockMemory( void *ptr, int bytes ) {
	return mlock( ptr, bytes ) == 0;
}

bool Sys_UnlockMemory( void *ptr, int bytes ) {
	return munlock( ptr, bytes ) == 0;
}

const char *Sys_GetCallStackStr( const address_t *callStack, const int callStackSize ) {
	return "";
}

const char *Sys_GetCallStackCurStr( int depth ) {
	return "";
}

// Sys_SaveGameCheck and Sys_ExecuteSavegameCommandAsyncImpl(idSaveLoadParms *)
// are implemented in sdl_savegame.cpp.

// idSaveGameThread defined in doomlib_stubs.cpp

/*
================================================
Missing Sys functions
================================================
*/
void DumpAllDisplayDevices() {
	// Not implemented for Linux
}

bool R_GetModeListForDisplay(const int displayNum, idList<vidMode_t>& modeList) {
	return sysLocalSDL.GetModeListForDisplay(displayNum, modeList);
}

long long Sys_GetDriveFreeSpaceInBytes(const char* path) {
	return 1000000000LL; // 1GB
}

void Sys_Shutdown() {
	// Not implemented
}

void Sys_Quit() {
	exit(0);
}

void Sys_Init() {
	// Not implemented
}

void Sys_InitNetworking() {
	// Not implemented
}

bool Sys_AlreadyRunning() {
	return false;
}

void Sys_SetRumble(int controller, int lowFreq, int highFreq) {
	// Not implemented for Linux
}

void Sys_Error(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	exit(1);
}

void Sys_Launch(const char* path, idCmdArgs& args, void* data, unsigned int dataSize) {
	// Not implemented
}

const char* Sys_DefaultBasePath() {
	return ".";
}

int Sys_ListFiles(const char* directory, const char* extension, idStrList& list) {
	if (!extension) {
		extension = "";
	}

	// passing a slash as extension will find directories (matches Win32 _findfirst semantics)
	bool wantDirs = (extension[0] == '/' && extension[1] == 0);
	if (wantDirs) {
		extension = "";
	}

	// build an fnmatch pattern: ".resources" -> "*.resources", "*.cgb" stays as-is
	idStr pattern;
	if (extension[0] == '*') {
		pattern = extension;
	} else if (extension[0] == 0) {
		pattern = "*";
	} else {
		pattern = va_format("*%s", extension);
	}

	list.Clear();

	DIR* dir = opendir(directory);
	if (!dir) {
		return -1;
	}

	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		idStr full = va_format("%s/%s", directory, entry->d_name);
		struct stat st;
		bool isDir = (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
		if (wantDirs != isDir) {
			continue;
		}
		if (!wantDirs && extension[0] != 0 && fnmatch(pattern.c_str(), entry->d_name, 0) != 0) {
			continue;
		}
		list.Append(entry->d_name);
	}
	closedir(dir);

	return list.Num() > 0 ? list.Num() : -1;
}

// g global for Doom classic
int g = 0;

// Sys_FPU functions
