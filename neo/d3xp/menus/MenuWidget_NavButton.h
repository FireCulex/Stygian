#ifndef __MENUWIDGET_NAVBUTTON_H__
#define __MENUWIDGET_NAVBUTTON_H__

#include "MenuWidget.h"

class idMenuWidget_NavButton : public idMenuWidget {
public:
    enum navWidget_t {
        NAV_WIDGET_LEFT = 0,
        NAV_WIDGET_RIGHT = 1,
        NAV_WIDGET_SELECTED = 2
    };
    virtual void Update() {}
    virtual bool ExecuteEvent( const idWidgetEvent & event ) { return false; }
    virtual void SetNavIndex( int navIndex, navWidget_t widget ) {}
};

#endif
