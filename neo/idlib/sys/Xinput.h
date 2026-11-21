/*
===========================================================================

MinGW stub for Microsoft Xinput.h

This is a minimal stub to allow compilation with MinGW-w64.
The actual Microsoft header provides XInput gamepad API.

===========================================================================
*/

#ifndef _XINPUT_H_
#define _XINPUT_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// XInput version
#define XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE  7849
#define XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE 8689
#define XINPUT_GAMEPAD_TRIGGER_THRESHOLD    30

// XInput structures (minimal stubs)
typedef struct XINPUT_GAMEPAD {
    unsigned short wButtons;
    unsigned char bLeftTrigger;
    unsigned char bRightTrigger;
    short sThumbLX;
    short sThumbLY;
    short sThumbRX;
    short sThumbRY;
} XINPUT_GAMEPAD;

typedef struct XINPUT_STATE {
    unsigned int dwPacketNumber;
    XINPUT_GAMEPAD Gamepad;
} XINPUT_STATE;

typedef struct XINPUT_VIBRATION {
    unsigned short wLeftMotorSpeed;
    unsigned short wRightMotorSpeed;
} XINPUT_VIBRATION;

typedef struct XINPUT_CAPABILITIES {
    unsigned char Type;
    unsigned char SubType;
    unsigned short Flags;
    XINPUT_GAMEPAD Gamepad;
    XINPUT_VIBRATION Vibration;
} XINPUT_CAPABILITIES;

typedef struct XINPUT_BATTERY_INFORMATION {
    unsigned char BatteryType;
    unsigned char BatteryLevel;
} XINPUT_BATTERY_INFORMATION;

typedef struct XINPUT_KEYSTROKE {
    unsigned short VirtualKey;
    unsigned short Unicode;
    unsigned short Flags;
    unsigned char UserIndex;
    unsigned char HidCode;
} XINPUT_KEYSTROKE;

typedef XINPUT_KEYSTROKE *PXINPUT_KEYSTROKE;

// XInput function stubs
#define XINPUT_DLL_A "xinput1_4.dll"

DWORD XInputGetState(DWORD dwUserIndex, XINPUT_STATE *pState) { return ERROR_DEVICE_NOT_CONNECTED; }
DWORD XInputSetState(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration) { return ERROR_DEVICE_NOT_CONNECTED; }
DWORD XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES *pCapabilities) { return ERROR_DEVICE_NOT_CONNECTED; }
DWORD XInputGetBatteryInformation(DWORD dwUserIndex, BYTE devType, XINPUT_BATTERY_INFORMATION *pBatteryInformation) { return ERROR_DEVICE_NOT_CONNECTED; }
DWORD XInputGetKeystroke(DWORD dwUserIndex, DWORD dwReserved, PXINPUT_KEYSTROKE pKeystroke) { return ERROR_DEVICE_NOT_CONNECTED; }

#ifdef __cplusplus
}
#endif

#endif // _XINPUT_H_