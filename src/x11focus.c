/*  x11focus.c
 *
 *
 *  Copyright (C) 2020 Toxic All Rights Reserved.
 *
 *  This file is part of Toxic.
 *
 *  Toxic is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Toxic is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Toxic.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "x11focus.h"

#ifndef __APPLE__

#include <X11/Xlib.h>

static struct Focus {
    Display *display;
    Window terminal_window;
} Focus;

static long unsigned int focused_window_id(void)
{
    if (!Focus.display) {
        return 0;
    }

    Window focus;
    int revert;

    XGetInputFocus(Focus.display, &focus, &revert);

    return focus;
}

bool is_focused(void)
{
    if (!Focus.display) {
        return false;
    }

    XLockDisplay(Focus.display);
    bool ret = Focus.terminal_window == focused_window_id();
    XUnlockDisplay(Focus.display);

    return ret;
}

int init_x11focus(void)
{
    if (XInitThreads() == 0) {
        return -1;
    }

    Focus.display = XOpenDisplay(NULL);

    if (!Focus.display) {
        return -1;
    }

    Focus.terminal_window = focused_window_id();

    return 0;
}

void terminate_x11focus(void)
{
    if (!Focus.display) {
        return;
    }

    XLockDisplay(Focus.display);

    if (!Focus.terminal_window) {
        XUnlockDisplay(Focus.display);
        return;
    }

    XCloseDisplay(Focus.display);
    XUnlockDisplay(Focus.display);
}

#endif /* !__APPLE__ */
