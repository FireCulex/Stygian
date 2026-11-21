#ifndef __MENUWIDGET_NAVBAR_H__
#define __MENUWIDGET_NAVBAR_H__

#include "MenuWidget.h"

class idMenuWidget_NavBar : public idMenuWidget {
public:
    virtual void Initialize( idMenuHandler * data ) {}
    virtual void Update() {}
    virtual bool PrepareListElement( idMenuWidget & widget, const int navIndex ) { return false; }
    virtual void SetListHeadings( idList< idStr > & list ) {}
    virtual int GetTotalNumberOfOptions() const { return 0; }
};

#endif
