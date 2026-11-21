#ifndef __MENUSCREEN_SHELL_PARTYLOBBY_H__
#define __MENUSCREEN_SHELL_PARTYLOBBY_H__

#include "MenuScreen.h"

class idMenuScreen_Shell_PartyLobby : public idMenuScreen {
public:
    virtual void Initialize( idMenuHandler * data ) {}
    virtual void Update() {}
    virtual void UpdateOptions() {}
    virtual void ShowScreen( const mainMenuTransition_t transitionType ) {}
    virtual void HideScreen( const mainMenuTransition_t transitionType ) {}
    virtual bool CanKickSelectedPlayer( void * luid ) { return false; }
    virtual void ShowLeaderboards() {}
    virtual bool HandleAction( idWidgetAction & action, const idWidgetEvent & event, idMenuWidget * widget, bool forceHandled ) { return false; }
    virtual void UpdateLobby() {}
};

#endif
