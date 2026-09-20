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
static int fullscreen;

static void update_output_size(void)
{
  int w = 0, h = 0;
  SDL_GetRendererOutputSize(plat_sdl2_renderer, &w, &h);
  if (w > 0 && h > 0)
    plat_sdl2_win_w = w, plat_sdl2_win_h = h;
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

  SDL_ShowCursor(fullscreen ? SDL_DISABLE : SDL_ENABLE);
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
  SDL_ShowCursor(on ? SDL_DISABLE : SDL_ENABLE);
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
