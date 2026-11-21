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

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

===========================================================================
*/

#ifndef __SDL_PUBLIC_H__
#define __SDL_PUBLIC_H__

#include <SDL.h>
#include <SDL_thread.h>
#include <SDL_timer.h>
#include <SDL_filesystem.h>
#include <SDL_gamecontroller.h>
#include <SDL_haptic.h>

#include "../idlib/precompiled.h"

// SDL2 platform types and constants
typedef SDL_Window*			win32Window_t;
typedef SDL_GLContext		win32GLContext_t;
typedef SDL_Thread*			win32Thread_t;
typedef SDL_mutex*			win32CriticalSection_t;
typedef SDL_sem*			win32Semaphore_t;
typedef SDL_cond*			win32ConditionVariable_t;

// Platform-independent types
typedef int64				ID_TIME_T;
typedef Uint32				ID_TICK_T;

// Key mapping: SDL scancodes -> idKey codes
#define SDL_KEY_TO_IDKEY(key)		( (key) + K_FIRST_SDL_KEY )

// SDL key offset to avoid conflicts with existing idKey enums
#define K_FIRST_SDL_KEY			256

// Mouse buttons
#define SDL_MOUSE_BUTTON_LEFT		0
#define SDL_MOUSE_BUTTON_MIDDLE		1
#define SDL_MOUSE_BUTTON_RIGHT		2
#define SDL_MOUSE_BUTTON_X1		3
#define SDL_MOUSE_BUTTON_X2		4

// Gamepad buttons (SDL_GameControllerButton)
#define SDL_GAMEPAD_BUTTON_A		SDL_CONTROLLER_BUTTON_A
#define SDL_GAMEPAD_BUTTON_B		SDL_CONTROLLER_BUTTON_B
#define SDL_GAMEPAD_BUTTON_X		SDL_CONTROLLER_BUTTON_X
#define SDL_GAMEPAD_BUTTON_Y		SDL_CONTROLLER_BUTTON_Y
#define SDL_GAMEPAD_BUTTON_BACK		SDL_CONTROLLER_BUTTON_BACK
#define SDL_GAMEPAD_BUTTON_GUIDE	SDL_CONTROLLER_BUTTON_GUIDE
#define SDL_GAMEPAD_BUTTON_START	SDL_CONTROLLER_BUTTON_START
#define SDL_GAMEPAD_BUTTON_LSTICK	SDL_CONTROLLER_BUTTON_LEFTSTICK
#define SDL_GAMEPAD_BUTTON_RSTICK	SDL_CONTROLLER_BUTTON_RIGHTSTICK
#define SDL_GAMEPAD_BUTTON_LSHOULDER	SDL_CONTROLLER_BUTTON_LEFTSHOULDER
#define SDL_GAMEPAD_BUTTON_RSHOULDER	SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
#define SDL_GAMEPAD_BUTTON_DPAD_UP	SDL_CONTROLLER_BUTTON_DPAD_UP
#define SDL_GAMEPAD_BUTTON_DPAD_DOWN	SDL_CONTROLLER_BUTTON_DPAD_DOWN
#define SDL_GAMEPAD_BUTTON_DPAD_LEFT	SDL_CONTROLLER_BUTTON_DPAD_LEFT
#define SDL_GAMEPAD_BUTTON_DPAD_RIGHT	SDL_CONTROLLER_BUTTON_DPAD_RIGHT

// Gamepad axes
#define SDL_GAMEPAD_AXIS_LEFTX		SDL_CONTROLLER_AXIS_LEFTX
#define SDL_GAMEPAD_AXIS_LEFTY		SDL_CONTROLLER_AXIS_LEFTY
#define SDL_GAMEPAD_AXIS_RIGHTX		SDL_CONTROLLER_AXIS_RIGHTX
#define SDL_GAMEPAD_AXIS_RIGHTY		SDL_CONTROLLER_AXIS_RIGHTY
#define SDL_GAMEPAD_AXIS_TRIGGERLEFT	SDL_CONTROLLER_AXIS_TRIGGERLEFT
#define SDL_GAMEPAD_AXIS_TRIGGERRIGHT	SDL_CONTROLLER_AXIS_TRIGGERRIGHT

// Platform function declarations
namespace SDLPlatform {
	// Initialization
	bool Init(const char* windowTitle, int width, int height, bool fullscreen, bool vsync);
	void Shutdown();

	// Window management
	void SetWindowTitle(const char* title);
	void GetWindowSize(int* width, int* height);
	void SetFullscreen(bool fullscreen);
	bool IsFullscreen();
	void SwapBuffers();

	// Input
	void ProcessEvents();
	bool IsKeyDown(int key);
	bool IsMouseButtonDown(int button);
	void GetMousePos(int* x, int* y);
	void SetMousePos(int x, int y);
	void SetMouseGrab(bool grab);
	void SetRelativeMouseMode(bool enable);
	bool IsRelativeMouseMode();

	// Gamepad
	int GetNumGamepads();
	bool IsGamepadConnected(int index);
	const char* GetGamepadName(int index);
	bool GetGamepadButton(int index, int button);
	float GetGamepadAxis(int index, int axis);

	// Timing
	ID_TICK_T GetTicks();
	ID_TIME_T GetTimeMicroseconds();
	void Sleep(int ms);

	// Threading
	win32Thread_t CreateThread(int (*fn)(void*), void* data, const char* name);
	void JoinThread(win32Thread_t thread);
	win32CriticalSection_t CreateCriticalSection();
	void EnterCriticalSection(win32CriticalSection_t cs);
	void LeaveCriticalSection(win32CriticalSection_t cs);
	void DestroyCriticalSection(win32CriticalSection_t cs);
	win32Semaphore_t CreateSemaphore(int initialValue);
	void WaitSemaphore(win32Semaphore_t sem);
	void PostSemaphore(win32Semaphore_t sem);
	void DestroySemaphore(win32Semaphore_t sem);

	// File I/O
	const char* GetBasePath();
	const char* GetPrefPath(const char* org, const char* app);
	SDL_RWops* OpenFile(const char* path, const char* mode);
	void CloseFile(SDL_RWops* rw);
	size_t ReadFile(SDL_RWops* rw, void* ptr, size_t size, size_t maxnum);
	size_t WriteFile(SDL_RWops* rw, const void* ptr, size_t size, size_t num);
	Sint64 SeekFile(SDL_RWops* rw, Sint64 offset, int whence);
	Sint64 TellFile(SDL_RWops* rw);
	Sint64 SizeFile(SDL_RWops* rw);
	bool FileExists(const char* path);

	// Clipboard
	void SetClipboardText(const char* text);
	const char* GetClipboardText();
	bool HasClipboardText();

	// Display modes
	int GetNumDisplayModes();
	bool GetDisplayMode(int index, int* width, int* height, int* refreshRate);
	int GetCurrentDisplayMode(int* width, int* height, int* refreshRate);

	// Message boxes
	void ShowMessageBox(const char* title, const char* message, bool error);
}

#endif // __SDL_PUBLIC_H__