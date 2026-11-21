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

#ifndef __SDL_LOCAL_H__
#define __SDL_LOCAL_H__

#include "sdl_public.h"
#include "../idlib/precompiled.h"

// Forward declaration for vidMode_t from renderer/tr_local.h
struct vidMode_t;

#include <SDL.h>
#include <SDL_thread.h>
#include <SDL_timer.h>
#include <SDL_filesystem.h>
#include <SDL_gamecontroller.h>
#include <SDL_haptic.h>

class idSysLocalSDL : public idSys {
public:
	idSysLocalSDL();
	~idSysLocalSDL();

	// Initialization
	bool Init();
	void Shutdown();

	// Window & OpenGL
	bool CreateWindow(const char* title, int width, int height, bool fullscreen, bool vsync, int posX = -1, int posY = -1);
	void DestroyWindow();
	void SwapBuffers();

	// Input
	void ProcessEvents();
	void GetMouseDelta(int* x, int* y);
	void CenterMouse();
	void SetMouseGrab(bool grab);

	// Gamepad
	void UpdateGamepads();
	bool IsGamepadConnected(int index);

	// Timing
	uint64 GetTimeMicroseconds();
	uint32 GetTicks();

	// Threading
	SDL_Thread* CreateThread(SDL_ThreadFunction fn, void* data, const char* name);
	int WaitThread(SDL_Thread* thread, int* status);
	SDL_mutex* CreateMutex();
	void LockMutex(SDL_mutex* mutex);
	void UnlockMutex(SDL_mutex* mutex);
	void DestroyMutex(SDL_mutex* mutex);
	SDL_sem* CreateSemaphore(Uint32 initialValue);
	void WaitSemaphore(SDL_sem* sem);
	void PostSemaphore(SDL_sem* sem);
	void DestroySemaphore(SDL_sem* sem);
	SDL_cond* CreateCondition();
	void WaitCondition(SDL_cond* cond, SDL_mutex* mutex);
	void SignalCondition(SDL_cond* cond);
	void BroadcastCondition(SDL_cond* cond);
	void DestroyCondition(SDL_cond* cond);

	// File I/O
	SDL_RWops* OpenFile(const char* path, const char* mode);
	void CloseFile(SDL_RWops* rw);
	size_t ReadFile(SDL_RWops* rw, void* ptr, size_t size, size_t maxnum);
	size_t WriteFile(SDL_RWops* rw, const void* ptr, size_t size, size_t num);
	Sint64 SeekFile(SDL_RWops* rw, Sint64 offset, int whence);
	Sint64 TellFile(SDL_RWops* rw);
	Sint64 SizeFile(SDL_RWops* rw);
	bool FileExists(const char* path);

	// Paths
	const char* GetBasePath();
	const char* GetPrefPath(const char* org, const char* app);

	// Clipboard
	void SetClipboardText(const char* text);
	const char* GetClipboardText();
	bool HasClipboardText();

	// Display
	void GetDesktopResolution(int* width, int* height);
	void GetDisplayModes(idList<SDL_DisplayMode>& modes);
	SDL_DisplayMode GetCurrentDisplayMode();
	bool GetModeListForDisplay(const int displayNum, idList<vidMode_t>& modeList);

	// Message box
	void ShowMessageBox(const char* title, const char* message, bool error);

	// idSys pure virtual overrides
	virtual void DebugPrintf(const char* fmt, ...) override;
	virtual void DebugVPrintf(const char* fmt, va_list arg) override;
	virtual double GetClockTicks() override;
	virtual double ClockTicksPerSecond() override;
	virtual cpuid_t GetProcessorId() override;
	virtual const char* GetProcessorString() override;
	virtual const char* FPU_GetState() override;
	virtual bool FPU_StackIsEmpty() override;
	virtual void FPU_SetFTZ(bool enable) override;
	virtual void FPU_SetDAZ(bool enable) override;
	virtual void FPU_EnableExceptions(int exceptions) override;
	virtual bool LockMemory(void* ptr, int size) override;
	virtual bool UnlockMemory(void* ptr, int size) override;
	virtual void GetCallStack(address_t* stack, int maxFrames) override;
	virtual const char* GetCallStackStr(const address_t* stack, int maxFrames) override;
	virtual const char* GetCallStackCurStr(int skipFrames) override;
	virtual void ShutdownSymbols() override;
	virtual int DLL_Load(const char* dllName) override;
	virtual void* DLL_GetProcAddress(int dllHandle, const char* procName) override;
	virtual void DLL_Unload(int dllHandle) override;
	virtual void DLL_GetFileName(const char* baseName, char* fileName, int maxLength) override;
	virtual sysEvent_t GenerateMouseButtonEvent(int button, bool down) override;
	virtual sysEvent_t GenerateMouseMoveEvent(int x, int y) override;
	virtual void OpenURL(const char* url, bool bShouldOpenInSteam) override;
	virtual void StartProcess(const char* commandLine, bool waitForFinish) override;

private:
	SDL_GameController* gamepads[16] = {nullptr};
	int numGamepads = 0;

public:
	SDL_Window* window = nullptr;
	SDL_GLContext glContext = nullptr;
	uint64 startTime = 0;
};

extern idSysLocalSDL sysLocalSDL;

// Defined in sdl_glimp.cpp: forward a window resize to the renderer
// (updates glConfig.nativeScreenWidth/Height + r_windowWidth/Height).
void GLimp_HandleWindowResize( int width, int height );

#endif // __SDL_LOCAL_H__