#ifndef __MENUSCREEN_SHELL_STEREOSCOPICS_H__
#define __MENUSCREEN_SHELL_STEREOSCOPICS_H__

#include "MenuScreen.h"

class idMenuScreen_Shell_Stereoscopics : public idMenuScreen {
public:
    class idMenuDataSource_StereoSettings {
    public:
        idMenuDataSource_StereoSettings() {}
    };
    virtual ~idMenuScreen_Shell_Stereoscopics() {}
    virtual void Initialize( idMenuHandler * data ) {}
};

#endif
