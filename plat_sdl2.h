/*
 * SDL2 video for libpicofe: one window, one accelerated renderer, RGB565 frames uploaded to a streaming
 * texture. The AutoBleem platform layer (plat_sdl.c is SDL 1.2 and stays as it is).
 *
 * This work is licensed under the terms of any of these licenses (at your option):
 *  - GNU GPL, version 2 or later.
 *  - GNU LGPL, version 2.1 or later.
 *  - MAME license.
 * See the COPYING file in the top-level directory.
 */
#ifndef LIBPICOFE_PLAT_SDL2_H
#define LIBPICOFE_PLAT_SDL2_H

#include <SDL.h>

extern SDL_Window *plat_sdl2_window;
extern SDL_Renderer *plat_sdl2_renderer;
/* the renderer's output size in pixels (a fullscreen-desktop window is the display's) */
extern int plat_sdl2_win_w, plat_sdl2_win_h;
/* called from plat_sdl2_event_handler(): the output size changed; the app closed the window */
extern void (*plat_sdl2_resize_cb)(int w, int h);
extern void (*plat_sdl2_quit_cb)(void);

/* fullscreen: 1 = a borderless window over the whole desktop, 0 = a w x h window (resizable) */
int  plat_sdl2_init(const char *title, int w, int h, int fullscreen, int vsync);
void plat_sdl2_finish(void);
int  plat_sdl2_set_fullscreen(int on);
int  plat_sdl2_is_fullscreen(void);
void plat_sdl2_set_title(const char *title);

/* Upload a w x h RGB565 frame (pitch in pixels) and present it into dst (NULL = the whole window;
 * the window is cleared first, what is outside dst is black). The frame is scaled to dst by the
 * renderer in one pass with the filter (PLAT_SDL2_FILTER_*: nearest, bilinear, or "sharp" = nearest
 * to a whole multiple in a render target, then bilinear for the remainder - crisp pixels, no shimmer),
 * and the scanlines are drawn over the scaled picture at the screen rows each emulated row occupies -
 * nothing is scaled after them, so they are exact at any output size. */
#define PLAT_SDL2_FILTER_OFF    0
#define PLAT_SDL2_FILTER_LINEAR 1
#define PLAT_SDL2_FILTER_SHARP  2
int  plat_sdl2_present(const void *rgb565, int w, int h, int pitch, const SDL_Rect *dst, int filter);
/* scanlines drawn over the next presents, at output resolution: 240 lines over the picture's height,
 * a property of the screen like a CRT's (the same whatever the game's mode, filter or internal
 * resolution), each a dark band at its bottom; on = 0 for none, thickness 1..3 (quarters of a line, at
 * least a pixel), alpha 0..255 how dark */
void plat_sdl2_set_scanlines(int on, int thickness, int alpha);
/* present a black frame */
void plat_sdl2_clear(void);
/* the caller's event loop hands every event it does not consume to this */
void plat_sdl2_event_handler(void *event);

#endif
