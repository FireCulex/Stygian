#ifndef __MENUSCREEN_SHELL_LEADERBOARDS_H__
#define __MENUSCREEN_SHELL_LEADERBOARDS_H__

#include "MenuScreen.h"

class idMenuScreen_Shell_Leaderboards : public idMenuScreen {
public:
    virtual ~idMenuScreen_Shell_Leaderboards() {}
    virtual void Initialize( idMenuHandler * data ) {}
    virtual void PumpLBCache() {}
    virtual void ClearLeaderboard() {}
    virtual void Update() {}
    virtual void ShowScreen( const mainMenuTransition_t transitionType ) {}
    virtual void HideScreen( const mainMenuTransition_t transitionType ) {}
    virtual bool HandleAction( idWidgetAction & action, const idWidgetEvent & event, idMenuWidget * widget, bool forceHandled ) { return false; }
    virtual void UpdateLeaderboard( const void * callback ) {}
    virtual void SetLeaderboardIndex() {}
    virtual void RefreshLeaderboard() {}
    virtual void ShowMessage( bool show, idStr message, bool spinner ) {}
};

#endif
