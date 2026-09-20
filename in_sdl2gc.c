/*
 * SDL2 GameController input driver for libpicofe - see in_sdl2gc.h.
 *
 * (C) Artur "screemer" Jakubowicz, 2020 - SDL AutoBleem GameControllerAPI; AutoBleem team, 2026
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
#include <SDL.h>

#include "input.h"
#include "in_sdl2gc.h"

#define IN_SDL2GC_PREFIX "sdl2gc:"
#define TRIGGER_DEADZONE 10000	/* of 32767: an analog trigger past this is the L2/R2 button */
#define STICK_DEADZONE 10	/* of IN_ABS_RANGE */
#define REPROBE_INTERVAL_MS 1000	/* how often a pad coming or going is looked for */

typedef unsigned long keybits_t;	/* SDL2GC_NBUTTONS bits */

#define CHECK_BIT(var, pos) (((var) >> (pos)) & 1UL)
#define SET_BIT(var, pos) ((var) |= (1UL << (pos)))
#define CLEAR_BIT(var, pos) ((var) &= ~(1UL << (pos)))

struct in_sdl2gc_state {
	const in_drv_t *drv;
	SDL_GameController *pad;
	SDL_JoystickID instance;
	keybits_t keystate;
	keybits_t menu_old;	/* update_keycode()'s last seen state */
	int player;	/* 1-based */
	int dev_id;
	int has_ps;
	int has_analog;
};

static const struct in_default_bind in_sdl2gc_defbinds[] = {
	/* the frontend supplies its own through in_pdata (its DKEY_* numbering is not ours to know) */
	{ 0, 0, 0 }
};

static const char * const in_sdl2gc_keys[SDL2GC_NBUTTONS] = {
	[SDL2GC_DPAD_UP]      = "Up",
	[SDL2GC_DPAD_LEFT]    = "Left",
	[SDL2GC_DPAD_DOWN]    = "Down",
	[SDL2GC_DPAD_RIGHT]   = "Right",
	[SDL2GC_BTN_START]    = "Start",
	[SDL2GC_BTN_SELECT]   = "Select",
	[SDL2GC_BTN_TRIANGLE] = "Triangle",
	[SDL2GC_BTN_CIRCLE]   = "Circle",
	[SDL2GC_BTN_SQUARE]   = "Square",
	[SDL2GC_BTN_CROSS]    = "Cross",
	[SDL2GC_BTN_L1]       = "L1",
	[SDL2GC_BTN_L2]       = "L2",
	[SDL2GC_BTN_R1]       = "R1",
	[SDL2GC_BTN_R2]       = "R2",
	[SDL2GC_BTN_PS]       = "Home",
	[SDL2GC_BTN_L3]       = "L3",
	[SDL2GC_BTN_R3]       = "R3",
};

/* SDL controller button -> our button. Anything not listed must be -1 (get_keybits skips -1): SDL 2.0.x had
 * 15 buttons and all were listed, but 2.0.14+ added MISC1/PADDLE1-4/TOUCHPAD, and with the old default of
 * 0 (= SDL2GC_DPAD_UP) every one of those, being unpressed, cleared d-pad UP. */
static const int in_sdl2gc_key_map[SDL_CONTROLLER_BUTTON_MAX] = {
	[0 ... SDL_CONTROLLER_BUTTON_MAX - 1] = -1,
	[SDL_CONTROLLER_BUTTON_A]             = SDL2GC_BTN_CROSS,
	[SDL_CONTROLLER_BUTTON_B]             = SDL2GC_BTN_CIRCLE,
	[SDL_CONTROLLER_BUTTON_X]             = SDL2GC_BTN_SQUARE,
	[SDL_CONTROLLER_BUTTON_Y]             = SDL2GC_BTN_TRIANGLE,
	[SDL_CONTROLLER_BUTTON_BACK]          = SDL2GC_BTN_SELECT,
	[SDL_CONTROLLER_BUTTON_GUIDE]         = SDL2GC_BTN_PS,
	[SDL_CONTROLLER_BUTTON_START]         = SDL2GC_BTN_START,
	[SDL_CONTROLLER_BUTTON_LEFTSTICK]     = SDL2GC_BTN_L3,
	[SDL_CONTROLLER_BUTTON_RIGHTSTICK]    = SDL2GC_BTN_R3,
	[SDL_CONTROLLER_BUTTON_LEFTSHOULDER]  = SDL2GC_BTN_L1,
	[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = SDL2GC_BTN_R1,
	[SDL_CONTROLLER_BUTTON_DPAD_UP]       = SDL2GC_DPAD_UP,
	[SDL_CONTROLLER_BUTTON_DPAD_DOWN]     = SDL2GC_DPAD_DOWN,
	[SDL_CONTROLLER_BUTTON_DPAD_LEFT]     = SDL2GC_DPAD_LEFT,
	[SDL_CONTROLLER_BUTTON_DPAD_RIGHT]    = SDL2GC_DPAD_RIGHT,
};

static const struct menu_keymap key_pbtn_map[] = {
	{ SDL2GC_DPAD_UP,      PBTN_UP },
	{ SDL2GC_DPAD_DOWN,    PBTN_DOWN },
	{ SDL2GC_DPAD_LEFT,    PBTN_LEFT },
	{ SDL2GC_DPAD_RIGHT,   PBTN_RIGHT },
	{ SDL2GC_BTN_CROSS,    PBTN_MOK },
	{ SDL2GC_BTN_CIRCLE,   PBTN_MBACK },
	{ SDL2GC_BTN_TRIANGLE, PBTN_MA2 },
	{ SDL2GC_BTN_SQUARE,   PBTN_MA3 },
	{ SDL2GC_BTN_L1,       PBTN_L },
	{ SDL2GC_BTN_R1,       PBTN_R },
	{ SDL2GC_BTN_PS,       PBTN_MENU },
};
#define KEY_PBTN_MAP_SIZE (sizeof(key_pbtn_map) / sizeof(key_pbtn_map[0]))

static struct in_sdl2gc_state *pads[SDL2GC_MAX_PADS];
static int pad_count;
static int joysticks_seen;
static unsigned int last_reprobe_check;
static const char * const *mapping_files;
static void (*pads_changed_cb)(int pad_count);
static int menu_opened_player;

static keybits_t get_keybits(struct in_sdl2gc_state *state)
{
	SDL_GameController *pad = state->pad;
	keybits_t bits = 0;
	int i, v;

	for (i = SDL_CONTROLLER_BUTTON_A; i < SDL_CONTROLLER_BUTTON_MAX; i++) {
		int button = in_sdl2gc_key_map[i];
		if (button == -1)
			continue;
		if (SDL_GameControllerGetButton(pad, i))
			SET_BIT(bits, button);
	}

	// a pad without a Guide button: Select+Start together is it (and neither on its own)
	if (!state->has_ps && CHECK_BIT(bits, SDL2GC_BTN_SELECT) && CHECK_BIT(bits, SDL2GC_BTN_START)) {
		SET_BIT(bits, SDL2GC_BTN_PS);
		CLEAR_BIT(bits, SDL2GC_BTN_SELECT);
		CLEAR_BIT(bits, SDL2GC_BTN_START);
	}

	// analog triggers as L2/R2
	v = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
	if (v > TRIGGER_DEADZONE)
		SET_BIT(bits, SDL2GC_BTN_L2);
	v = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
	if (v > TRIGGER_DEADZONE)
		SET_BIT(bits, SDL2GC_BTN_R2);

	return bits;
}

static void close_pads(void)
{
	int i;

	for (i = 0; i < pad_count; i++) {
		if (pads[i] == NULL)
			continue;
		if (pads[i]->pad != NULL && SDL_GameControllerGetAttached(pads[i]->pad))
			SDL_GameControllerClose(pads[i]->pad);
		// the state itself is freed by in_sdl2gc_free() when input.c drops the device
		pads[i] = NULL;
	}
	pad_count = 0;
}

/* a pad came or went: input.c re-probes every driver, which frees and re-registers ours */
static void check_and_reprobe(void)
{
	unsigned int now = SDL_GetTicks();
	int n;

	if (now - last_reprobe_check < REPROBE_INTERVAL_MS)
		return;
	last_reprobe_check = now;

	n = SDL_NumJoysticks();
	if (n == joysticks_seen)
		return;
	printf(IN_SDL2GC_PREFIX " controllers changed (%d -> %d), re-probing\n", joysticks_seen, n);
	SDL_PumpEvents();
	SDL_FlushEvents(SDL_JOYAXISMOTION, SDL_CONTROLLERDEVICEREMAPPED);
	in_probe();
}

static void in_sdl2gc_probe(const in_drv_t *drv)
{
	const struct in_pdata *pdata = drv->pdata;
	const char * const *key_names = in_sdl2gc_keys;
	struct in_sdl2gc_state *state;
	char name[128];
	int idx, n;

	if (pdata != NULL && pdata->key_names)
		key_names = pdata->key_names;

	close_pads();
	if (mapping_files != NULL) {
		static int mappings_loaded;
		const char * const *f;
		for (f = mapping_files; !mappings_loaded && *f != NULL; f++) {
			int ret = SDL_GameControllerAddMappingsFromFile(*f);
			if (ret >= 0) {
				printf(IN_SDL2GC_PREFIX " %d mappings from %s\n", ret, *f);
				mappings_loaded = 1;
			}
		}
	}

	n = joysticks_seen = SDL_NumJoysticks();
	for (idx = 0; idx < n && pad_count < SDL2GC_MAX_PADS; idx++) {
		SDL_GameController *pad;
		SDL_GameControllerButtonBind bind;
		char guid[64];

		if (!SDL_IsGameController(idx)) {
			printf(IN_SDL2GC_PREFIX " joystick %d \"%s\" has no controller mapping, ignored\n",
				idx, SDL_JoystickNameForIndex(idx));
			continue;
		}
		pad = SDL_GameControllerOpen(idx);
		if (pad == NULL)
			continue;
		state = calloc(1, sizeof(*state));
		if (state == NULL) {
			SDL_GameControllerClose(pad);
			break;
		}
		state->drv = drv;
		state->pad = pad;
		state->instance = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad));
		bind = SDL_GameControllerGetBindForButton(pad, SDL_CONTROLLER_BUTTON_GUIDE);
		state->has_ps = bind.bindType != SDL_CONTROLLER_BINDTYPE_NONE;
		bind = SDL_GameControllerGetBindForAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
		state->has_analog = bind.bindType != SDL_CONTROLLER_BINDTYPE_NONE;
		state->player = pad_count + 1;

		// one name per player, whatever the pad: a config's binds for "sdl2gc:pad 1" apply to whichever
		// controller is plugged in first
		snprintf(name, sizeof(name), IN_SDL2GC_PREFIX "pad %d", state->player);
		in_register(name, -1, state, SDL2GC_NBUTTONS, key_names, 0);
		state->dev_id = in_name_to_id(name);

		SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(pad)),
			guid, sizeof(guid));
		printf(IN_SDL2GC_PREFIX " player %d: \"%s\" (%s) guide=%d analog=%d\n", state->player,
			SDL_GameControllerName(pad), guid, state->has_ps, state->has_analog);

		pads[pad_count++] = state;
	}

	if (pads_changed_cb != NULL)
		pads_changed_cb(pad_count);
}

static void in_sdl2gc_free(void *drv_data)
{
	struct in_sdl2gc_state *state = drv_data;
	int i;

	if (state == NULL)
		return;
	for (i = 0; i < SDL2GC_MAX_PADS; i++)
		if (pads[i] == state)
			pads[i] = NULL;
	if (state->pad != NULL && SDL_GameControllerGetAttached(state->pad))
		SDL_GameControllerClose(state->pad);
	free(state);
}

static const char * const *
in_sdl2gc_get_key_names(const in_drv_t *drv, int *count)
{
	const struct in_pdata *pdata = drv->pdata;
	*count = SDL2GC_NBUTTONS;

	if (pdata != NULL && pdata->key_names)
		return pdata->key_names;
	return in_sdl2gc_keys;
}

static int in_sdl2gc_update(void *drv_data, const int *binds, int *result)
{
	struct in_sdl2gc_state *state = drv_data;
	keybits_t mask;
	int bit, b;

	check_and_reprobe();
	if (state->pad == NULL)
		return -1;
	SDL_GameControllerUpdate();
	state->keystate = get_keybits(state);

	if (CHECK_BIT(state->keystate, SDL2GC_BTN_PS))
		menu_opened_player = state->player;

	for (mask = state->keystate, bit = 0; mask != 0; bit++, mask >>= 1) {
		if ((mask & 1) == 0)
			continue;
		for (b = 0; b < IN_BINDTYPE_COUNT; b++) {
			int v = binds[IN_BIND_OFFS(bit, b)];
			// player 2's pad buttons are the upper half of the PLAYER12 word
			if (b == IN_BINDTYPE_PLAYER12 && state->player == 2)
				v <<= 16;
			result[b] |= v;
		}
	}
	return 0;
}

static int in_sdl2gc_update_keycode(void *drv_data, int *is_down)
{
	struct in_sdl2gc_state *state = drv_data;
	keybits_t val, diff;
	int i;

	check_and_reprobe();
	if (state->pad == NULL)
		return -1;
	// the menu is driven by the pad that opened it (or player 1 when a key did)
	if (menu_opened_player != 0 && menu_opened_player != state->player)
		return -1;

	SDL_GameControllerUpdate();
	val = get_keybits(state);
	CLEAR_BIT(val, SDL2GC_BTN_PS); // no Home in the menu, it would reopen it

	diff = val ^ state->menu_old;
	if (diff == 0)
		return -1;

	/* take one bit only */
	for (i = 0; i < (int)(sizeof(diff) * 8); i++)
		if (diff & (1UL << i))
			break;

	state->menu_old ^= 1UL << i;
	if (is_down)
		*is_down = !!(val & (1UL << i));
	return i;
}

static int in_sdl2gc_menu_translate(void *drv_data, int keycode, char *charcode)
{
	size_t i;

	if (keycode < 0) {
		/* menu -> kc */
		keycode = -keycode;
		for (i = 0; i < KEY_PBTN_MAP_SIZE; i++)
			if (key_pbtn_map[i].pbtn == keycode)
				return key_pbtn_map[i].key;
	} else {
		for (i = 0; i < KEY_PBTN_MAP_SIZE; i++)
			if (key_pbtn_map[i].key == keycode)
				return key_pbtn_map[i].pbtn;
	}

	return 0;
}

static int in_sdl2gc_update_analog(void *drv_data, int axis_id, int *result)
{
	static const int axes[SDL2GC_NAXES] = {
		SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY,
		SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY,
	};
	struct in_sdl2gc_state *state = drv_data;
	int val;

	if (state->pad == NULL || !state->has_analog)
		return -1;
	if ((unsigned int)axis_id >= SDL2GC_NAXES)
		return -1;

	// update() ran SDL_GameControllerUpdate() just before this
	val = SDL_GameControllerGetAxis(state->pad, axes[axis_id]) * IN_ABS_RANGE / 32767;
	if (abs(val) < STICK_DEADZONE)
		val = 0;
	*result = val;
	return 0;
}

static int in_sdl2gc_get_config(void *drv_data, enum in_cfg_opt what, int *val)
{
	switch (what) {
	case IN_CFG_ABS_AXIS_COUNT:
		*val = SDL2GC_NAXES;
		break;
	default:
		return -1;
	}
	return 0;
}

static const in_drv_t in_sdl2gc_drv = {
	.prefix         = IN_SDL2GC_PREFIX,
	.probe          = in_sdl2gc_probe,
	.free           = in_sdl2gc_free,
	.get_key_names  = in_sdl2gc_get_key_names,
	.get_config     = in_sdl2gc_get_config,
	.update_analog  = in_sdl2gc_update_analog,
	.update         = in_sdl2gc_update,
	.update_keycode = in_sdl2gc_update_keycode,
	.menu_translate = in_sdl2gc_menu_translate,
};

int in_sdl2gc_init(const struct in_pdata *pdata, const char * const *mapping_files_,
	void (*pads_changed)(int pad_count))
{
	const struct in_default_bind *defbinds = in_sdl2gc_defbinds;

	if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
		fprintf(stderr, IN_SDL2GC_PREFIX " SDL_InitSubSystem failed: %s\n", SDL_GetError());
		return -1;
	}
	// polled, not event-driven: the keyboard driver owns the event queue
	SDL_JoystickEventState(SDL_IGNORE);
	SDL_GameControllerEventState(SDL_IGNORE);

	mapping_files = mapping_files_;
	pads_changed_cb = pads_changed;
	if (pdata != NULL && pdata->defbinds != NULL)
		defbinds = pdata->defbinds;
	in_register_driver(&in_sdl2gc_drv, defbinds, NULL, pdata);
	return 0;
}

int in_sdl2gc_dev_id(int player)
{
	int i;

	for (i = 0; i < pad_count; i++)
		if (pads[i] != NULL && pads[i]->player == player)
			return pads[i]->dev_id;
	return -1;
}

int in_sdl2gc_pad_count(void)
{
	return pad_count;
}

int in_sdl2gc_rumble(int player, unsigned short low, unsigned short high, unsigned int ms)
{
#if SDL_VERSION_ATLEAST(2, 0, 9)
	int i;

	for (i = 0; i < pad_count; i++)
		if (pads[i] != NULL && pads[i]->player == player && pads[i]->pad != NULL)
			return SDL_GameControllerRumble(pads[i]->pad, low, high, ms);
#endif
	return -1;
}
