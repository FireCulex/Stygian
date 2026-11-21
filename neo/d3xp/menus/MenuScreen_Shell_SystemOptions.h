#ifndef __MENUSCREEN_SHELL_SYSTEMOPTIONS_H__
#define __MENUSCREEN_SHELL_SYSTEMOPTIONS_H__

#include "MenuScreen.h"

class idMenuScreen_Shell_SystemOptions : public idMenuScreen {
public:
    class idMenuDataSource_SystemSettings {
    public:
        idMenuDataSource_SystemSettings() {}
    };
    virtual ~idMenuScreen_Shell_SystemOptions() {}
    virtual void Initialize( idMenuHandler * data ) {}
};

#endif
