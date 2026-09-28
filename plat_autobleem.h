/*
 * AutoBleem's video for libpicofe: one window with a GL context (GLES 2.0 asked for, a desktop GL 2.1 context when
 * there is none), the frame drawn by our own GL passes - SDL keeps only the window, the context and the
 * input. The AutoBleem platform layer (plat_sdl.c is SDL 1.2 and stays as it is).
 *
 * This work is licensed under the terms of any of these licenses (at your option):
 *  - GNU GPL, version 2 or later.
 *  - GNU LGPL, version 2.1 or later.
 *  - MAME license.
 * See the COPYING file in the top-level directory.
 */
#ifndef LIBPICOFE_PLAT_AUTOBLEEM_H
#define LIBPICOFE_PLAT_AUTOBLEEM_H

#include <SDL.h>

extern SDL_Window *plat_ab_window;
/* the drawable's size in pixels (a fullscreen-desktop window is the display's) */
extern int plat_ab_win_w, plat_ab_win_h;
/* called from plat_ab_event_handler(): the output size changed; the app closed the window */
extern void (*plat_ab_resize_cb)(int w, int h);
extern void (*plat_ab_quit_cb)(void);

/* fullscreen: 1 = a borderless window over the whole desktop, 0 = a w x h window (resizable) */
int  plat_ab_init(const char *title, int w, int h, int fullscreen, int vsync);
void plat_ab_finish(void);
int  plat_ab_set_fullscreen(int on);
int  plat_ab_is_fullscreen(void);
/* the output mode: 1 when the display lists a w x h mode; plat_ab_set_output_mode(w, h) switches to it in
 * fullscreen (0, 0 = the display's own mode, the desktop's), sizes the window when windowed, and calls
 * plat_ab_resize_cb. -1 when the display has no such mode (nothing changed). */
int  plat_ab_has_mode(int w, int h);
int  plat_ab_set_output_mode(int w, int h);
void plat_ab_set_title(const char *title);

/* A shader pass, in libretro's single-file GLSL shape: the source is compiled twice, once with VERTEX and
 * once with FRAGMENT defined, after a #version line this file adds (100 on GLES, 120 on desktop GL). It gets
 * the attributes VertexCoord and TexCoord (vec4, 0..1, TexCoord's y = 0 is the picture's top), the uniforms
 * MVPMatrix, Texture, TextureSize, InputSize, OutputSize, FrameCount and FrameDirection. The app keeps the
 * struct alive (it is the program cache's key). */
struct plat_ab_shader {
  const char *name;
  const char *src;
  int scale;   /* a smoothing pass: it draws into a texture scale x its input; the filter pass: 0 */
  int linear;  /* 1: the pass samples its input bilinearly */
};
/* the passes of the next presents with a dst: smooth (NULL = none) into a texture, then filter (NULL = a
 * plain bilinear copy) into dst; a pass that does not compile is left out, with a line on stderr */
void plat_ab_set_pipeline(const struct plat_ab_shader *smooth, const struct plat_ab_shader *filter);

/* Upload a w x h RGB565 frame (pitch in pixels) and present it. With dst: the pipeline's passes put it into
 * dst (the rest of the screen is black), then the scanlines and the HUD go over the whole screen. With
 * dst == NULL (the menu, a buffer the window's size): copied 1:1 over the whole window, nothing over it. */
int  plat_ab_present(const void *rgb565, int w, int h, int pitch, const SDL_Rect *dst);
/* scanlines over the whole screen in screen rows: dark rows, then bright rows, repeated from the top; dark
 * = 0 for none; alpha 0..255 how dark */
void plat_ab_set_scanlines(int dark, int bright, int alpha);
/* present a black frame */
void plat_ab_clear(void);

/* The HUD: images the app puts in slots and draws at screen pixels over everything else. A slot's texture
 * is made at its first image and updated in place afterwards (it only grows, never shrinks), so nothing is
 * made or freed while a game runs once the HUD's images have been seen; argb is w x h ARGB8888, 0 = done.
 * plat_ab_hud_draw() is for the HUD callback only - it draws the slot's last image into r, scaled
 * nearest. The callback runs in every present with a dst, after the scanlines and before the debug
 * shot/screenshot readback, with the screen's size. */
#define PLAT_AB_HUD_SLOTS 16
int  plat_ab_hud_image(int slot, const Uint32 *argb, int w, int h);
void plat_ab_hud_draw(int slot, const SDL_Rect *r);
void plat_ab_set_hud_cb(void (*cb)(int screen_w, int screen_h));

/* What a debug driver on a thread of its own needs (the app's, ours is frontend/ab/ab_debug.c): how many
 * frames were presented, and the last one as a file. A readback is a GPU sync, so one happens only where it
 * is asked for: plat_ab_shot_request() returns the serial to wait past, the next present reads the frame
 * back before its swap, plat_ab_shot_serial() moves, and plat_ab_shot_save() writes that frame as a BMP
 * (0 = written). All four are safe from another thread; nothing is read back until plat_ab_frame_cache(1)
 * turns the cache on. */
void plat_ab_frame_cache(int on);
unsigned int plat_ab_frame_count(void);
unsigned int plat_ab_shot_request(void);
unsigned int plat_ab_shot_serial(void);
int  plat_ab_shot_save(const char *path);
/* the caller's event loop hands every event it does not consume to this */
void plat_ab_event_handler(void *event);

#endif
