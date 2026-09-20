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

/* PLAT_SDL2_SHOT=<file.bmp> in the environment: what the renderer is about to present, saved every 5 s -
 * for looking at a display one cannot see (a Pi over ssh); the emulator's own screenshot is the PSX frame
 * before scaling and effects */
static void plat_sdl2_debug_shot(void)
{
  static const char *path;
  static int checked;
  static Uint32 last;
  static int count;
  Uint32 now;
  SDL_Surface *s;
  int w, h;

  if (!checked) {
    path = getenv("PLAT_SDL2_SHOT");
    checked = 1;
  }
  if (path == NULL)
    return;
  now = SDL_GetTicks();
  if (last != 0 && now - last < 5000)
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

int plat_sdl2_present(const void *rgb565, int w, int h, int pitch, const SDL_Rect *dst, int linear)
{
  linear = !!linear;
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
  // a frame that does not cover the window would leave the last one's edges otherwise, and the
  // backbuffer is not kept across presents on every driver: clear on every present
  SDL_SetRenderDrawColor(plat_sdl2_renderer, 0, 0, 0, 255);
  SDL_RenderClear(plat_sdl2_renderer);
  SDL_RenderCopy(plat_sdl2_renderer, texture, NULL, dst);
  if (dst != NULL && scan_rows > 0 && scan_alpha > 0)
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
