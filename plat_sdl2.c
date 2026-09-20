/*
 * SDL2 video for libpicofe - see plat_sdl2.h.
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
#include <SDL.h>

#include "plat_sdl2.h"

SDL_Window *plat_sdl2_window;
SDL_Renderer *plat_sdl2_renderer;
int plat_sdl2_win_w, plat_sdl2_win_h;
void (*plat_sdl2_resize_cb)(int w, int h);
void (*plat_sdl2_quit_cb)(void);

static SDL_Texture *texture;
static int tex_w, tex_h, tex_linear = -1;
static SDL_Texture *target;     /* the integer-prescaled frame (sharp filter, or scanlines) */
static int target_w, target_h;
static int windowed_w, windowed_h;
static int scan_rows, scan_thickness, scan_alpha;
static int fullscreen;

static void update_output_size(void)
{
  int w = 0, h = 0;
  SDL_GetRendererOutputSize(plat_sdl2_renderer, &w, &h);
  if (w > 0 && h > 0)
    plat_sdl2_win_w = w, plat_sdl2_win_h = h;
}

/* SDL_ShowCursor(SDL_DISABLE) alone leaves the pointer on the screen with the KMSDRM backend (a Pi):
 * relative mouse mode is what hides it there, and it keeps the pointer inside the window on a desktop.
 * The absolute position in_sdl2 reads for a lightgun is still kept by SDL from the relative motion. */
static void plat_sdl2_show_cursor(int show)
{
  int r1 = SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE);
  int r2 = SDL_SetRelativeMouseMode(show ? SDL_FALSE : SDL_TRUE);
  if (plat_sdl2_window != NULL)
    SDL_SetWindowGrab(plat_sdl2_window, show ? SDL_FALSE : SDL_TRUE);
  fprintf(stderr, "plat_sdl2: cursor %s (ShowCursor %d, relative %d: %s)\n", show ? "shown" : "hidden",
    r1, r2, r2 == 0 ? "ok" : SDL_GetError());
}

int plat_sdl2_init(const char *title, int w, int h, int fullscreen_, int vsync)
{
  Uint32 wflags = SDL_WINDOW_ALLOW_HIGHDPI;
  Uint32 rflags = SDL_RENDERER_ACCELERATED;
  int ret;

  ret = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  if (ret != 0) {
    fprintf(stderr, "plat_sdl2: SDL_Init failed: %s\n", SDL_GetError());
    return -1;
  }
  // a display that is not free yet (the launcher's window, or the previous emulator, still holds the DRM
  // master for a few seconds after it is gone) makes SDL fall back to a driver that shows nothing: wait
  // for the real one instead, unless that driver was asked for
  {
    const char *drv = SDL_GetCurrentVideoDriver(), *want = getenv("SDL_VIDEODRIVER");
    int tries = 0;
    while (drv != NULL && (strcmp(drv, "offscreen") == 0 || strcmp(drv, "dummy") == 0) &&
           (want == NULL || strcmp(want, drv) != 0) && tries++ < 12) {
      fprintf(stderr, "plat_sdl2: got the %s video driver - the display is not free yet, retrying\n", drv);
      SDL_QuitSubSystem(SDL_INIT_VIDEO);
      SDL_Delay(500);
      if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        break;
      drv = SDL_GetCurrentVideoDriver();
    }
  }

  windowed_w = w;
  windowed_h = h;
  fullscreen = fullscreen_;
  if (fullscreen)
    wflags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  else
    wflags |= SDL_WINDOW_RESIZABLE;
  if (vsync)
    rflags |= SDL_RENDERER_PRESENTVSYNC;

  plat_sdl2_window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
    w, h, wflags);
  if (plat_sdl2_window == NULL) {
    fprintf(stderr, "plat_sdl2: SDL_CreateWindow failed: %s\n", SDL_GetError());
    goto fail;
  }

  plat_sdl2_renderer = SDL_CreateRenderer(plat_sdl2_window, -1, rflags);
  if (plat_sdl2_renderer == NULL) {
    fprintf(stderr, "plat_sdl2: no accelerated renderer (%s), trying any\n", SDL_GetError());
    plat_sdl2_renderer = SDL_CreateRenderer(plat_sdl2_window, -1, 0);
  }
  if (plat_sdl2_renderer == NULL) {
    fprintf(stderr, "plat_sdl2: SDL_CreateRenderer failed: %s\n", SDL_GetError());
    goto fail;
  }
  update_output_size();

  {
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(plat_sdl2_renderer, &info) == 0)
      printf("plat_sdl2: %s video, %s renderer%s, output %dx%d%s\n",
        SDL_GetCurrentVideoDriver(), info.name,
        (info.flags & SDL_RENDERER_PRESENTVSYNC) ? " (vsync)" : "",
        plat_sdl2_win_w, plat_sdl2_win_h, fullscreen ? " fullscreen" : "");
  }
  {
    // the display's current mode against the largest it offers (is the output at the panel's native size?)
    SDL_DisplayMode cur, best;
    int di = SDL_GetWindowDisplayIndex(plat_sdl2_window), n, i;
    if (di >= 0 && SDL_GetCurrentDisplayMode(di, &cur) == 0) {
      best = cur;
      n = SDL_GetNumDisplayModes(di);
      for (i = 0; i < n; i++) {
        SDL_DisplayMode m;
        if (SDL_GetDisplayMode(di, i, &m) == 0 && m.w * m.h > best.w * best.h)
          best = m;
      }
      printf("plat_sdl2: display mode %dx%d@%d, largest offered %dx%d@%d\n",
        cur.w, cur.h, cur.refresh_rate, best.w, best.h, best.refresh_rate);
    }
  }

  plat_sdl2_show_cursor(!fullscreen);
  return 0;

fail:
  plat_sdl2_finish();
  return -1;
}

void plat_sdl2_finish(void)
{
  if (texture != NULL) {
    SDL_DestroyTexture(texture);
    texture = NULL;
  }
  if (target != NULL) {
    SDL_DestroyTexture(target);
    target = NULL;
  }
  if (plat_sdl2_renderer != NULL) {
    SDL_DestroyRenderer(plat_sdl2_renderer);
    plat_sdl2_renderer = NULL;
  }
  if (plat_sdl2_window != NULL) {
    SDL_DestroyWindow(plat_sdl2_window);
    plat_sdl2_window = NULL;
  }
  SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
}

int plat_sdl2_set_fullscreen(int on)
{
  int ret;

  on = !!on;
  if (on == fullscreen)
    return 0;
  ret = SDL_SetWindowFullscreen(plat_sdl2_window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  if (ret != 0) {
    fprintf(stderr, "plat_sdl2: SDL_SetWindowFullscreen failed: %s\n", SDL_GetError());
    return -1;
  }
  fullscreen = on;
  if (!on)
    SDL_SetWindowSize(plat_sdl2_window, windowed_w, windowed_h);
  plat_sdl2_show_cursor(!on);
  update_output_size();
  if (plat_sdl2_resize_cb != NULL)
    plat_sdl2_resize_cb(plat_sdl2_win_w, plat_sdl2_win_h);
  return 0;
}

int plat_sdl2_is_fullscreen(void)
{
  return fullscreen;
}

void plat_sdl2_set_title(const char *title)
{
  SDL_SetWindowTitle(plat_sdl2_window, title);
}

void plat_sdl2_clear(void)
{
  SDL_SetRenderDrawColor(plat_sdl2_renderer, 0, 0, 0, 255);
  SDL_RenderClear(plat_sdl2_renderer);
  SDL_RenderPresent(plat_sdl2_renderer);
}

/* PLAT_SDL2_SHOT=<file.bmp> in the environment: what the renderer is about to present, saved every 5 s
 * (PLAT_SDL2_SHOT_MS) -
 * for looking at a display one cannot see (a Pi over ssh); the emulator's own screenshot is the PSX frame
 * before scaling and effects */
static void plat_sdl2_debug_shot(void)
{
  static const char *path;
  static int checked;
  static Uint32 last, interval = 5000;
  static int count;
  Uint32 now;
  SDL_Surface *s;
  int w, h;

  if (!checked) {
    path = getenv("PLAT_SDL2_SHOT");
    // PLAT_SDL2_SHOT_MS: how often, for catching a menu screen (the default is easy on a Pi's tmpfs)
    if (getenv("PLAT_SDL2_SHOT_MS") != NULL && atoi(getenv("PLAT_SDL2_SHOT_MS")) > 0)
      interval = atoi(getenv("PLAT_SDL2_SHOT_MS"));
    checked = 1;
  }
  if (path == NULL)
    return;
  now = SDL_GetTicks();
  if (last != 0 && now - last < interval)
    return;
  last = now;
  if (SDL_GetRendererOutputSize(plat_sdl2_renderer, &w, &h) != 0)
    return;
  s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (s == NULL)
    return;
  if (SDL_RenderReadPixels(plat_sdl2_renderer, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0) {
    // a %d in the name numbers the frames (PLAT_SDL2_SHOT=/tmp/shot%d.bmp), else the file is rewritten
    char name[512];
    snprintf(name, sizeof(name), path, count++);
    SDL_SaveBMP(s, name);
  }
  else
    fprintf(stderr, "plat_sdl2: RenderReadPixels: %s\n", SDL_GetError());
  SDL_FreeSurface(s);
}

void plat_sdl2_set_scanlines(int rows, int thickness, int alpha)
{
  scan_rows = rows;
  scan_thickness = thickness < 1 ? 1 : thickness > 3 ? 3 : thickness;
  scan_alpha = alpha < 0 ? 0 : alpha > 255 ? 255 : alpha;
}

/* a translucent black band at the bottom of every emulated row, where that row lands on the screen */
static void draw_scanlines(const SDL_Rect *dst)
{
  float row = (float)dst->h / scan_rows;
  float band = row * scan_thickness / 4.0f;
  SDL_Rect r;
  int i;

  if (band < 1.0f)
    band = 1.0f;
  if (band > row - 1.0f && row > 2.0f)
    band = row - 1.0f;
  SDL_SetRenderDrawBlendMode(plat_sdl2_renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(plat_sdl2_renderer, 0, 0, 0, (Uint8)scan_alpha);
  r.x = dst->x;
  r.w = dst->w;
  r.h = (int)(band + 0.5f);
  if (r.h < 1)
    r.h = 1;
  for (i = 1; i <= scan_rows; i++) {
    r.y = dst->y + (int)(i * row - band + 0.5f);
    SDL_RenderFillRect(plat_sdl2_renderer, &r);
  }
  SDL_SetRenderDrawBlendMode(plat_sdl2_renderer, SDL_BLENDMODE_NONE);
}

/* the render target the frame is prescaled into by a whole factor, (re)made to k*w x k*h; NULL if the
 * renderer cannot (then the frame goes to the screen directly) */
static SDL_Texture *prescale_target(int w, int h, int kx, int ky)
{
  if (target != NULL && target_w == kx * w && target_h == ky * h)
    return target;
  if (target != NULL)
    SDL_DestroyTexture(target);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
  target = SDL_CreateTexture(plat_sdl2_renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_TARGET, kx * w, ky * h);
  if (target == NULL) {
    fprintf(stderr, "plat_sdl2: no %dx%d render target: %s\n", kx * w, ky * h, SDL_GetError());
    return NULL;
  }
#if SDL_VERSION_ATLEAST(2, 0, 12)
  SDL_SetTextureScaleMode(target, SDL_ScaleModeLinear);
#endif
  target_w = kx * w;
  target_h = ky * h;
  fprintf(stderr, "plat_sdl2: prescale %dx%d -> %dx%d (%dx, %dx)\n", w, h, target_w, target_h, kx, ky);
  return target;
}

int plat_sdl2_present(const void *rgb565, int w, int h, int pitch, const SDL_Rect *dst, int filter)
{
  int linear = filter == PLAT_SDL2_FILTER_LINEAR;
  SDL_Texture *tg = NULL;

  if (dst != NULL && (filter == PLAT_SDL2_FILTER_SHARP || (scan_rows > 0 && scan_alpha > 0))) {
    // per axis, the whole factor that still fits the destination (a 512x240 frame in a 4:3 layer is
    // 1.9x wide and 3x tall: 1x and 3x); with only scanlines to draw and no room, 1x keeps them at the
    // source's rows
    int kx = dst->w / w, ky = dst->h / h;
    if (kx < 1) kx = 1;
    if (ky < 1) ky = 1;
    if (kx > 8) kx = 8;
    if (ky > 8) ky = 8;
    tg = prescale_target(w, h, kx, ky);
  }
  if (tg != NULL)
    linear = 0;   // nearest into the target; the target's own mode does the final pass
  if (texture == NULL || tex_w != w || tex_h != h || tex_linear != linear) {
    if (texture != NULL)
      SDL_DestroyTexture(texture);
    // the scale quality is read when the texture is made (SDL_SetTextureScaleMode is 2.0.12+)
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, linear ? "linear" : "nearest");
    texture = SDL_CreateTexture(plat_sdl2_renderer, SDL_PIXELFORMAT_RGB565,
      SDL_TEXTUREACCESS_STREAMING, w, h);
    if (texture == NULL) {
      fprintf(stderr, "plat_sdl2: SDL_CreateTexture %dx%d failed: %s\n", w, h, SDL_GetError());
      return -1;
    }
#if SDL_VERSION_ATLEAST(2, 0, 12)
    // the hint is honoured at creation by every renderer; this is the explicit way where it exists
    SDL_SetTextureScaleMode(texture, linear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
#endif
    if (tex_linear != linear)
      fprintf(stderr, "plat_sdl2: %s filter\n", linear ? "linear" : "nearest");
    tex_w = w;
    tex_h = h;
    tex_linear = linear;
  }

  if (SDL_UpdateTexture(texture, NULL, rgb565, pitch * 2) != 0) {
    fprintf(stderr, "plat_sdl2: SDL_UpdateTexture failed: %s\n", SDL_GetError());
    return -1;
  }
  if (tg != NULL) {
    // pass 1: the frame into the target by the whole factor, the scanlines at its whole rows
    SDL_Rect full = { 0, 0, target_w, target_h };
    SDL_SetRenderTarget(plat_sdl2_renderer, tg);
    SDL_RenderCopy(plat_sdl2_renderer, texture, NULL, &full);
    if (scan_rows > 0 && scan_alpha > 0)
      draw_scanlines(&full);
    SDL_SetRenderTarget(plat_sdl2_renderer, NULL);
#if SDL_VERSION_ATLEAST(2, 0, 12)
    // pass 2's filter: bilinear for linear/sharp, nearest for "off" (the user asked for no smoothing)
    SDL_SetTextureScaleMode(tg, filter == PLAT_SDL2_FILTER_OFF ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
#endif
  }
  // a frame that does not cover the window would leave the last one's edges otherwise, and the
  // backbuffer is not kept across presents on every driver: clear on every present
  SDL_SetRenderDrawColor(plat_sdl2_renderer, 0, 0, 0, 255);
  SDL_RenderClear(plat_sdl2_renderer);
  SDL_RenderCopy(plat_sdl2_renderer, tg != NULL ? tg : texture, NULL, dst);
  if (tg == NULL && dst != NULL && scan_rows > 0 && scan_alpha > 0)
    draw_scanlines(dst);
  plat_sdl2_debug_shot();
  SDL_RenderPresent(plat_sdl2_renderer);
  return 0;
}

void plat_sdl2_event_handler(void *event_)
{
  SDL_Event *event = event_;

  switch (event->type) {
  case SDL_WINDOWEVENT:
    switch (event->window.event) {
    case SDL_WINDOWEVENT_SIZE_CHANGED:
    case SDL_WINDOWEVENT_RESIZED:
      update_output_size();
      if (plat_sdl2_resize_cb != NULL)
        plat_sdl2_resize_cb(plat_sdl2_win_w, plat_sdl2_win_h);
      break;
    default:
      break;
    }
    break;
  case SDL_QUIT:
    if (plat_sdl2_quit_cb != NULL)
      plat_sdl2_quit_cb();
    break;
  default:
    break;
  }
}

// vim:shiftwidth=2:expandtab
