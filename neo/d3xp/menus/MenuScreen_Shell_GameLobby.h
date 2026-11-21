#ifndef __MENUSCREEN_SHELL_GAMELOBBY_H__
#define __MENUSCREEN_SHELL_GAMELOBBY_H__

#include "MenuScreen.h"

class idMenuScreen_Shell_GameLobby : public idMenuScreen {
public:
    virtual void Initialize( idMenuHandler * data ) {}
    virtual void Update() {}
    virtual void UpdateLobby() {}
    virtual void ShowScreen( const mainMenuTransition_t transitionType ) {}
    virtual void HideScreen( const mainMenuTransition_t transitionType ) {}
    virtual bool CanKickSelectedPlayer( void * luid ) { return false; }
    virtual bool HandleAction( idWidgetAction & action, const idWidgetEvent & event, idMenuWidget * widget, bool forceHandled ) { return false; }
};

#endif
