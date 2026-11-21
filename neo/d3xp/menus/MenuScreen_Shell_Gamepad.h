#ifndef __MENUSCREEN_SHELL_GAMEPAD_H__
#define __MENUSCREEN_SHELL_GAMEPAD_H__

#include "MenuScreen.h"

class idMenuScreen_Shell_Gamepad : public idMenuScreen {
public:
    class idMenuDataSource_GamepadSettings {
    public:
        idMenuDataSource_GamepadSettings() {}
    };
    virtual ~idMenuScreen_Shell_Gamepad() {}
    virtual void Initialize( idMenuHandler * data ) {}
};

#endif
