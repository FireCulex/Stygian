#ifndef __MENUWIDGET_LOBBYLIST_H__
#define __MENUWIDGET_LOBBYLIST_H__

#include "MenuWidget.h"

class idMenuWidget_LobbyList : public idMenuWidget {
public:
    virtual void Update() {}
    virtual bool PrepareListElement( idMenuWidget & widget, const int childIndex ) { return false; }
    virtual void SetHeadingInfo( idList< idStr > & list ) {}
    virtual void SetEntryData( int index, idStr name, int voiceState ) {}
};

#endif
