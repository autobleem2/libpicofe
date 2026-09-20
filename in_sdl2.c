/*
 * SDL2 keyboard input driver for libpicofe - see in_sdl2.h. The shape of in_sdl.c (notaz, 2012) on
 * SDL2's API: scancodes instead of SDL 1.2 keysyms, no joystick part (in_sdl2gc.c is the pad driver).
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of any of these licenses (at your option):
 *  - GNU GPL, version 2 or later.
 *  - GNU LGPL, version 2.1 or later.
 *  - MAME license.
 * See the COPYING file in the top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <SDL.h>
#include "input.h"
#include "in_sdl2.h"

#define IN_SDL2_PREFIX "sdl:"
#define IN_SDL2_NKEYS SDL_NUM_SCANCODES
/* should be machine word for best performace */
typedef unsigned long keybits_t;
#define KEYBITS_WORD_BITS (sizeof(keybits_t) * 8)
#define KEYBITS_WORDS (IN_SDL2_NKEYS / KEYBITS_WORD_BITS + 1)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

struct in_sdl2_state {
	const in_drv_t *drv;
	keybits_t keystate[KEYBITS_WORDS];
	// emulator keys should always be processed immediately lest one is lost
	keybits_t emu_keys[KEYBITS_WORDS];
	int mouse_x, mouse_y, mouse_xrel, mouse_yrel, mouse_buttons;
	int win_w, win_h;
	unsigned int redraw:1;
};

static void (*ext_event_handler)(void *event);

/* the scancode names, lower case, made once: "escape", "f1", "left", "c", "[1]" style keypad, and the
 * PlayStation Classic's front buttons as the kernel reports them, "eject" (Open) and "reset" (the
 * AUDIOPLAY scancode) */
static const char *key_names[IN_SDL2_NKEYS];
static char key_name_pool[IN_SDL2_NKEYS * 12];

static void make_key_names(void)
{
	char *p = key_name_pool, *end = key_name_pool + sizeof(key_name_pool);
	int sc, i;

	if (key_names[SDL_SCANCODE_ESCAPE] != NULL)
		return;
	for (sc = 0; sc < IN_SDL2_NKEYS; sc++) {
		const char *name = SDL_GetScancodeName(sc);
		char *start = p;
		if (sc == SDL_SCANCODE_EJECT)
			name = "eject";
		else if (sc == SDL_SCANCODE_AUDIOPLAY)
			name = "reset";
		if (name == NULL || name[0] == 0)
			continue;
		if (p + strlen(name) + 3 >= end)
			break;
		// "Keypad 1" -> "[1]", as the SDL 1.2 names had it
		if (strncmp(name, "Keypad ", 7) == 0 && strlen(name + 7) <= 3) {
			*p++ = '[';
			for (i = 7; name[i]; i++)
				*p++ = tolower((unsigned char)name[i]);
			*p++ = ']';
		}
		else {
			for (i = 0; name[i]; i++)
				*p++ = name[i] == ' ' ? '_' : tolower((unsigned char)name[i]);
		}
		*p++ = 0;
		key_names[sc] = start;
	}
}

static void in_sdl2_probe(const in_drv_t *drv)
{
	const struct in_pdata *pdata = drv->pdata;
	const char * const *names = key_names;
	struct in_sdl2_state *state;

	make_key_names();
	if (pdata->key_names)
		names = pdata->key_names;

	state = calloc(1, sizeof(*state));
	if (state == NULL)
		return;
	state->drv = drv;
	in_register(IN_SDL2_PREFIX "keys", -1, state, IN_SDL2_NKEYS, names, 0);
}

static void in_sdl2_free(void *drv_data)
{
	free(drv_data);
}

static const char * const *
in_sdl2_get_key_names(const in_drv_t *drv, int *count)
{
	const struct in_pdata *pdata = drv->pdata;
	*count = IN_SDL2_NKEYS;

	make_key_names();
	if (pdata->key_names)
		return pdata->key_names;
	return key_names;
}

static void update_keystate(keybits_t *keystate, int sym, int is_down)
{
	keybits_t *ks_word, mask;

	mask = 1;
	mask <<= sym & (KEYBITS_WORD_BITS - 1);
	ks_word = keystate + sym / KEYBITS_WORD_BITS;
	if (is_down)
		*ks_word |= mask;
	else
		*ks_word &= ~mask;
}

static int get_keystate(keybits_t *keystate, int sym)
{
	keybits_t *ks_word, mask;

	mask = 1;
	mask <<= sym & (KEYBITS_WORD_BITS - 1);
	ks_word = keystate + sym / KEYBITS_WORD_BITS;
	return !!(*ks_word & mask);
}

static int handle_event(struct in_sdl2_state *state, SDL_Event *event,
	int *kc_out, int *down_out, int *emu_out)
{
	int sc, emu;

	if (event->type != SDL_KEYDOWN && event->type != SDL_KEYUP)
		return -1;
	if (event->key.repeat)
		return -1;

	sc = event->key.keysym.scancode;
	if ((unsigned)sc >= IN_SDL2_NKEYS)
		return -1;

	emu = get_keystate(state->emu_keys, sc);
	update_keystate(state->keystate, sc, event->type == SDL_KEYDOWN);
	if (kc_out != NULL)
		*kc_out = sc;
	if (down_out != NULL)
		*down_out = event->type == SDL_KEYDOWN;
	if (emu_out != NULL)
		*emu_out = emu;

	return 1;
}

static void handle_other_event(struct in_sdl2_state *state, SDL_Event *event)
{
	switch (event->type) {
	case SDL_MOUSEMOTION:
		state->mouse_x = event->motion.x;
		state->mouse_y = event->motion.y;
		state->mouse_xrel += event->motion.xrel;
		state->mouse_yrel += event->motion.yrel;
		return;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP: {
		int mask = SDL_BUTTON(event->button.button);
		if (event->button.state == SDL_PRESSED)
			state->mouse_buttons |= mask;
		else
			state->mouse_buttons &= ~mask;
		return;
	}
	case SDL_WINDOWEVENT:
		if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED
		    || event->window.event == SDL_WINDOWEVENT_EXPOSED)
			state->redraw = 1;
		break;
	default:
		break;
	}
	if (ext_event_handler != NULL)
		ext_event_handler(event);
}

/* the whole queue: keys are ours, everything else goes to the window's handler; with one_kc the first
 * key event ends the collection (the menu wants one key at a time) */
static int collect_events(struct in_sdl2_state *state, int *one_kc, int *one_down)
{
	SDL_Event event;
	int is_emukey = 0, ret, retval = 0;

	SDL_PumpEvents();
	while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) > 0) {
		ret = handle_event(state, &event, one_kc, one_down, &is_emukey);
		if (ret < 0) {
			handle_other_event(state, &event);
			continue;
		}
		retval |= ret;
		if ((is_emukey || one_kc != NULL) && retval)
			break;
	}

	if (state->redraw && !retval) {
		state->redraw = 0;
		// dummy key event to force returning from the key loop,
		// so the application has a chance to redraw the window
		if (one_kc != NULL) {
			*one_kc = SDL_SCANCODE_UNKNOWN;
			retval |= 1;
		}
		if (one_down != NULL)
			*one_down = 1;
	}
	return retval;
}

static int in_sdl2_update(void *drv_data, const int *binds, int *result)
{
	struct in_sdl2_state *state = drv_data;
	keybits_t mask;
	int i, sym, bit, b;

	collect_events(state, NULL, NULL);

	for (i = 0; i < KEYBITS_WORDS; i++) {
		mask = state->keystate[i];
		if (mask == 0)
			continue;
		for (bit = 0; mask != 0; bit++, mask >>= 1) {
			if ((mask & 1) == 0)
				continue;
			sym = i * KEYBITS_WORD_BITS + bit;

			for (b = 0; b < IN_BINDTYPE_COUNT; b++)
				result[b] |= binds[IN_BIND_OFFS(sym, b)];
		}
	}

	return 0;
}

static int in_sdl2_update_kbd(void *drv_data, const int *binds, int *result)
{
	struct in_sdl2_state *state = drv_data;
	keybits_t mask;
	int i, sym, bit, b = 0;

	collect_events(state, NULL, NULL);

	for (i = 0; i < KEYBITS_WORDS; i++) {
		mask = state->keystate[i];
		if (mask == 0)
			continue;
		for (bit = 0; mask != 0; bit++, mask >>= 1) {
			if ((mask & 1) == 0)
				continue;
			sym = i * KEYBITS_WORD_BITS + bit;
			result[b++] = binds[sym];
		}
	}

	return b;
}

static int in_sdl2_update_pointer(void *drv_data, int id, int *result)
{
	struct in_sdl2_state *state = drv_data;
	SDL_Window *win = SDL_GetMouseFocus();
	int max;

	*result = 0;
	if (win != NULL)
		SDL_GetWindowSize(win, &state->win_w, &state->win_h);

	switch (id) {
	// absolute position, clipped at the window/screen border
	case 0:	if ((max = state->win_w))
			*result = state->mouse_x * 2*1024/max - 1024;
		break;
	case 1:	if ((max = state->win_h))
			*result = state->mouse_y * 2*1024/max - 1024;
		break;
	// relative mouse movements since last query
	case 2:	if ((max = state->win_w))
			*result = state->mouse_xrel * 2*1024/max;
		state->mouse_xrel = 0;
		break;
	case 3:	if ((max = state->win_h))
			*result = state->mouse_yrel * 2*1024/max;
		state->mouse_yrel = 0;
		break;
	// buttons
	case -1: *result = state->mouse_buttons;
		break;
	default: return -1;
	}

	return 0;
}

static int in_sdl2_update_keycode(void *drv_data, int *is_down)
{
	struct in_sdl2_state *state = drv_data;
	int ret_kc = -1, ret_down = 0;

	collect_events(state, &ret_kc, &ret_down);

	if (is_down != NULL)
		*is_down = ret_down;

	return ret_kc;
}

static int in_sdl2_menu_translate(void *drv_data, int keycode, char *charcode)
{
	struct in_sdl2_state *state = drv_data;
	const struct in_pdata *pdata = state->drv->pdata;
	const char * const *names = key_names;
	const struct menu_keymap *map = pdata->key_map;
	int map_len = pdata->kmap_size;
	int ret = 0;
	int i;

	if (pdata->key_names)
		names = pdata->key_names;

	if (keycode < 0)
	{
		/* menu -> kc */
		keycode = -keycode;
		for (i = 0; i < map_len; i++)
			if (map[i].pbtn == keycode)
				return map[i].key;
	}
	else
	{
		if (keycode == SDL_SCANCODE_UNKNOWN)
			ret = PBTN_RDRAW;
		else
		for (i = 0; i < map_len; i++) {
			if (map[i].key == keycode) {
				ret = map[i].pbtn;
				break;
			}
		}

		if (charcode != NULL && (unsigned int)keycode < IN_SDL2_NKEYS &&
		    names[keycode] != NULL && names[keycode][1] == 0)
		{
			ret |= PBTN_CHAR;
			*charcode = names[keycode][0];
		}
	}

	return ret;
}

static int in_sdl2_clean_binds(void *drv_data, int *binds, int *def_binds)
{
	struct in_sdl2_state *state = drv_data;
	int i, t, cnt = 0;

	memset(state->emu_keys, 0, sizeof(state->emu_keys));
	for (t = 0; t < IN_BINDTYPE_COUNT; t++) {
		for (i = 0; i < IN_SDL2_NKEYS; i++) {
			int offs = IN_BIND_OFFS(i, t);
			if (binds[offs]) {
				if (t == IN_BINDTYPE_EMU)
					update_keystate(state->emu_keys, i, 1);
				cnt ++;
			}
		}
	}

	return cnt;
}

static const in_drv_t in_sdl2_drv = {
	.prefix          = IN_SDL2_PREFIX,
	.probe           = in_sdl2_probe,
	.free            = in_sdl2_free,
	.get_key_names   = in_sdl2_get_key_names,
	.update          = in_sdl2_update,
	.update_kbd      = in_sdl2_update_kbd,
	.update_pointer  = in_sdl2_update_pointer,
	.update_keycode  = in_sdl2_update_keycode,
	.menu_translate  = in_sdl2_menu_translate,
	.clean_binds     = in_sdl2_clean_binds,
};

int in_sdl2_init(const struct in_pdata *pdata, void (*handler)(void *event))
{
	if (!pdata) {
		fprintf(stderr, "in_sdl2: Missing input platform data\n");
		return -1;
	}

	in_register_driver(&in_sdl2_drv, pdata->defbinds, pdata->kbd_map, pdata);
	ext_event_handler = handler;
	return 0;
}
