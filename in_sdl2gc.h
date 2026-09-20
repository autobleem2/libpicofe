/*
 * SDL2 GameController input driver for libpicofe ("sdl2gc:..."): every controller SDL has a mapping for
 * (its built-in database plus the gamecontrollerdb.txt files given to in_sdl2gc_init) is a device with
 * the PlayStation button names below as its keys - the same layout on every pad, so no per-pad binding.
 * Player 1 is the first pad, player 2 the second (their PLAYER12 binds land in the upper 16 bits).
 *
 * (C) Artur "screemer" Jakubowicz, 2020 - SDL AutoBleem GameControllerAPI; AutoBleem team, 2026
 *
 * This work is licensed under the terms of any of these licenses (at your option):
 *  - GNU GPL, version 2 or later.
 *  - GNU LGPL, version 2.1 or later.
 *  - MAME license.
 * See the COPYING file in the top-level directory.
 */
#ifndef LIBPICOFE_IN_SDL2GC_H
#define LIBPICOFE_IN_SDL2GC_H

struct in_pdata;

enum {
	SDL2GC_DPAD_UP = 0,
	SDL2GC_DPAD_LEFT,
	SDL2GC_DPAD_DOWN,
	SDL2GC_DPAD_RIGHT,
	SDL2GC_BTN_START,
	SDL2GC_BTN_SELECT,
	SDL2GC_BTN_TRIANGLE,
	SDL2GC_BTN_CIRCLE,
	SDL2GC_BTN_SQUARE,
	SDL2GC_BTN_CROSS,
	SDL2GC_BTN_L1,
	SDL2GC_BTN_L2,
	SDL2GC_BTN_R1,
	SDL2GC_BTN_R2,
	SDL2GC_BTN_PS,		/* the Guide button, or Select+Start held together on a pad without one */
	SDL2GC_BTN_L3,
	SDL2GC_BTN_R3,
	SDL2GC_NBUTTONS
};

#define SDL2GC_MAX_PADS 2
/* the analog axes as update_analog() numbers them */
enum { SDL2GC_AXIS_LX = 0, SDL2GC_AXIS_LY, SDL2GC_AXIS_RX, SDL2GC_AXIS_RY, SDL2GC_NAXES };

/* mapping_files: gamecontrollerdb.txt paths tried in order, NULL-terminated; may be NULL.
 * pads_changed: called after a (re)probe with the number of pads found, so the frontend can point its
 * analog-stick devices at them (in_sdl2gc_dev_id). */
int in_sdl2gc_init(const struct in_pdata *pdata, const char * const *mapping_files,
	void (*pads_changed)(int pad_count));
/* the libpicofe device id of player <player> (1-based), -1 when there is no such pad */
int in_sdl2gc_dev_id(int player);
int in_sdl2gc_pad_count(void);
/* rumble on player <player>'s pad (1-based), strengths 0..0xffff, for <ms>; 0 when done, -1 without */
int in_sdl2gc_rumble(int player, unsigned short low, unsigned short high, unsigned int ms);

#endif
