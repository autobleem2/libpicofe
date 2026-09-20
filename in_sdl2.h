/*
 * SDL2 keyboard input driver for libpicofe ("sdl:keys"): key codes are SDL scancodes, key names the
 * scancode names in lower case (so a config written by the SDL 1.2 driver keeps working for the common
 * keys: "escape", "f1", "left", "c"...). Also the window's event pump: every event that is not a key
 * goes to the handler given to in_sdl2_init().
 *
 * This work is licensed under the terms of any of these licenses (at your option):
 *  - GNU GPL, version 2 or later.
 *  - GNU LGPL, version 2.1 or later.
 *  - MAME license.
 * See the COPYING file in the top-level directory.
 */
#ifndef LIBPICOFE_IN_SDL2_H
#define LIBPICOFE_IN_SDL2_H

struct in_pdata;

int in_sdl2_init(const struct in_pdata *pdata, void (*handler)(void *event));

#endif
