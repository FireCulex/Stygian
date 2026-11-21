/*
===========================================================================

Minimal Doom event stubs for Linux build

===========================================================================
*/
#ifndef __D_EVENT_H__
#define __DOOMEVENT_H__

struct event_t {
    int type;
    int data1;
    int data2;
    int data3;
};

#define EV_KEYDOWN        1
#define EV_KEYUP          2
#define EV_MOUSE          3
#define EV_JOYSTICK       4

#endif
