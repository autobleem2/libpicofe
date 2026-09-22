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
static SDL_Texture *target;     /* the integer-prescaled frame (sharp filter) */
static int target_w, target_h;
static int windowed_w, windowed_h;
static int scan_on, scan_thickness, scan_alpha;
static SDL_Texture *scan_tex;   /* the bands as one ARGB overlay the size of dst, blended over the frame */
static int scan_tex_w, scan_tex_h, scan_tex_band, scan_tex_alpha;
static int fullscreen;
static SDL_atomic_t frame_count;  /* presents so far; shot_want/shot_serial: a debug driver's readback */
static SDL_atomic_t shot_want, shot_serial;
static int frame_cache_on;
static SDL_mutex *frame_cache_lock;
static SDL_Surface *frame_cache;  /* the last frame read back, ARGB8888 */

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
  if (scan_tex != NULL) {
    SDL_DestroyTexture(scan_tex);
    scan_tex = NULL;
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

/* The frame a debug driver asks for (see the header): a readback costs a GPU sync, so it happens only
 * when one was asked for, in the present that follows the request. The driver thread waits for the
 * serial to move and then writes the surface out under the lock. */
static void plat_sdl2_shot_take(void)
{
  int w, h;

  if (!frame_cache_on || SDL_AtomicGet(&shot_want) == 0)
    return;
  if (SDL_GetRendererOutputSize(plat_sdl2_renderer, &w, &h) != 0)
    return;
  SDL_LockMutex(frame_cache_lock);
  if (frame_cache != NULL && (frame_cache->w != w || frame_cache->h != h)) {
    SDL_FreeSurface(frame_cache);
    frame_cache = NULL;
  }
  if (frame_cache == NULL)
    frame_cache = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (frame_cache != NULL &&
      SDL_RenderReadPixels(plat_sdl2_renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
        frame_cache->pixels, frame_cache->pitch) != 0)
    fprintf(stderr, "plat_sdl2: RenderReadPixels: %s\n", SDL_GetError());
  SDL_UnlockMutex(frame_cache_lock);
  SDL_AtomicSet(&shot_want, 0);
  SDL_AtomicAdd(&shot_serial, 1);
}

void plat_sdl2_frame_cache(int on)
{
  if (on && frame_cache_lock == NULL)
    frame_cache_lock = SDL_CreateMutex();
  frame_cache_on = on && frame_cache_lock != NULL;
}

unsigned int plat_sdl2_frame_count(void)
{
  return (unsigned int)SDL_AtomicGet(&frame_count);
}

unsigned int plat_sdl2_shot_request(void)
{
  SDL_AtomicSet(&shot_want, 1);
  return (unsigned int)SDL_AtomicGet(&shot_serial);
}

unsigned int plat_sdl2_shot_serial(void)
{
  return (unsigned int)SDL_AtomicGet(&shot_serial);
}

int plat_sdl2_shot_save(const char *path)
{
  int ret = -1;

  if (frame_cache_lock == NULL)
    return -1;
  SDL_LockMutex(frame_cache_lock);
  if (frame_cache != NULL)
    ret = SDL_SaveBMP(frame_cache, path);
  SDL_UnlockMutex(frame_cache_lock);
  return ret;
}

void plat_sdl2_set_scanlines(int on, int thickness, int alpha)
{
  scan_on = on;
  scan_thickness = thickness < 1 ? 1 : thickness > 3 ? 3 : thickness;
  scan_alpha = alpha < 0 ? 0 : alpha > 255 ? 255 : alpha;
}

/* The scanlines are a property of the screen, as a CRT's are: SCAN_LINES of them over the picture's
 * height whatever the game's mode (a 480-line screen or a PAL frame shows through the same lines), each
 * a translucent black band at the bottom of its line - at 720p 3 px lines with 1-2 px bands, at 1080p
 * 4.5 px lines (spaced 4/5 alternately, as they must be) with 1-3 px bands. The band is thickness/4 of
 * the line, 1 px at least, and the line keeps a pixel of picture. They are drawn over the frame after
 * it has been scaled to the screen, and nothing is scaled after them - that second scale is what made
 * them uneven before.
 * The overlay is one ARGB texture the size of the picture, made on the CPU when the size, thickness or
 * level changes and blended over the frame in a single copy - pcsx-ab's RGBA scanline image, rather than
 * a fill rect per line (which SDL 2.0.12's GLES2 renderer on the console drew 1 px high whatever was
 * asked). */
#define SCAN_LINES 240

static void draw_scanlines(const SDL_Rect *dst)
{
  int rows = SCAN_LINES, band;

  band = (dst->h * scan_thickness / 4 + rows / 2) / rows;   /* round(line * thickness / 4) */
  if (band < 1)
    band = 1;
  if (dst->h >= rows * 2 && band > dst->h / rows - 1)
    band = dst->h / rows - 1;
  if (scan_tex == NULL || scan_tex_w != dst->w || scan_tex_h != dst->h ||
      scan_tex_band != band || scan_tex_alpha != scan_alpha) {
    Uint32 *px = calloc((size_t)dst->w * dst->h, 4);   /* transparent black */
    Uint32 dark = (Uint32)scan_alpha << 24;             /* ARGB: black at the bands' alpha */
    int i, y, x;

    if (scan_tex != NULL) {
      SDL_DestroyTexture(scan_tex);
      scan_tex = NULL;
    }
    if (px == NULL)
      return;
    for (i = 1; i <= rows; i++) {
      int y1 = dst->h * i / rows;   /* the row's last screen line, exactly */
      for (y = y1 - band; y < y1; y++) {
        Uint32 *row = px + (size_t)y * dst->w;
        if (y < 0)
          continue;
        for (x = 0; x < dst->w; x++)
          row[x] = dark;
      }
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    scan_tex = SDL_CreateTexture(plat_sdl2_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
      dst->w, dst->h);
    if (scan_tex == NULL || SDL_UpdateTexture(scan_tex, NULL, px, dst->w * 4) != 0 ||
        SDL_SetTextureBlendMode(scan_tex, SDL_BLENDMODE_BLEND) != 0) {
      fprintf(stderr, "plat_sdl2: no %dx%d scanline overlay: %s\n", dst->w, dst->h, SDL_GetError());
      if (scan_tex != NULL)
        SDL_DestroyTexture(scan_tex);
      scan_tex = NULL;
      free(px);
      return;
    }
    free(px);
    scan_tex_w = dst->w;
    scan_tex_h = dst->h;
    scan_tex_band = band;
    scan_tex_alpha = scan_alpha;
    fprintf(stderr, "plat_sdl2: scanlines: %d lines over %d px, %d px bands, alpha %d\n", rows, dst->h, band, scan_alpha);
  }
  SDL_RenderCopy(plat_sdl2_renderer, scan_tex, NULL, dst);
}

/* The render target the sharp filter prescales the frame into by a whole factor, (re)made to k*w x k*h;
 * NULL if the renderer cannot (then the frame goes to the screen directly, as with the linear filter).
 * It is 8888, native to every renderer, so it is never a wrapper around a native texture - and a texture's
 * scale mode is only ever set through the hint at its creation, never with SDL_SetTextureScaleMode(): in
 * SDL 2.0.12 (the PlayStation Classic's) that call runs the renderer's hook on a wrapper texture
 * (RGB565 on the GLES2 renderer, say), which has no driver data to dereference; 2.0.14 fixed it. */
static SDL_Texture *prescale_target(int w, int h, int kx, int ky)
{
  if (target != NULL && target_w == kx * w && target_h == ky * h)
    return target;
  if (target != NULL)
    SDL_DestroyTexture(target);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
  target = SDL_CreateTexture(plat_sdl2_renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_TARGET, kx * w, ky * h);
  if (target == NULL) {
    fprintf(stderr, "plat_sdl2: no %dx%d render target: %s\n", kx * w, ky * h, SDL_GetError());
    return NULL;
  }
  target_w = kx * w;
  target_h = ky * h;
  fprintf(stderr, "plat_sdl2: prescale %dx%d -> %dx%d (%dx, %dx)\n", w, h, target_w, target_h, kx, ky);
  return target;
}

/* The frame to the screen, in one scale: the RGB565 frame goes into a streaming texture whose scale mode
 * is the filter (off = nearest, linear = bilinear), that texture is drawn into dst by the renderer, and
 * the scanlines go over the result at screen rows (draw_scanlines). The backbuffer is the screen-sized
 * buffer of pcsx-ab's chain; what is outside dst (the bands of a 4:3 picture) is free for anything drawn
 * before the present. The sharp filter alone has a pass before that: nearest into a whole-multiple render
 * target, which is then drawn into dst bilinearly, so the pixels' edges stay crisp and only the
 * remainder of the scale is smoothed - at a screen that is an exact multiple (720p for a 240-line frame)
 * it is the same picture as off. A 2x-enhanced frame is just a bigger texture on the same path. */
int plat_sdl2_present(const void *rgb565, int w, int h, int pitch, const SDL_Rect *dst, int filter)
{
  int linear = filter == PLAT_SDL2_FILTER_LINEAR;
  SDL_Texture *tg = NULL;

  if (dst != NULL && filter == PLAT_SDL2_FILTER_SHARP) {
    // per axis, the whole factor that still fits the destination (a 512x240 frame in a 4:3 layer is
    // 1.9x wide and 3x tall: 1x and 3x)
    int kx = dst->w / w, ky = dst->h / h;
    if (kx < 1) kx = 1;
    if (ky < 1) ky = 1;
    if (kx > 8) kx = 8;
    if (ky > 8) ky = 8;
    tg = prescale_target(w, h, kx, ky);
  }
  if (texture == NULL || tex_w != w || tex_h != h || tex_linear != linear) {
    if (texture != NULL)
      SDL_DestroyTexture(texture);
    // the scale quality is read when the texture is made (and only then - see prescale_target())
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, linear ? "linear" : "nearest");
    texture = SDL_CreateTexture(plat_sdl2_renderer, SDL_PIXELFORMAT_RGB565,
      SDL_TEXTUREACCESS_STREAMING, w, h);
    if (texture == NULL) {
      fprintf(stderr, "plat_sdl2: SDL_CreateTexture %dx%d failed: %s\n", w, h, SDL_GetError());
      return -1;
    }
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
    // sharp: the frame into the target by the whole factor, nearest
    SDL_Rect full = { 0, 0, target_w, target_h };
    SDL_SetRenderTarget(plat_sdl2_renderer, tg);
    SDL_RenderCopy(plat_sdl2_renderer, texture, NULL, &full);
    SDL_SetRenderTarget(plat_sdl2_renderer, NULL);
  }
  // a frame that does not cover the window would leave the last one's edges otherwise, and the
  // backbuffer is not kept across presents on every driver: clear on every present
  SDL_SetRenderDrawColor(plat_sdl2_renderer, 0, 0, 0, 255);
  SDL_RenderClear(plat_sdl2_renderer);
  SDL_RenderCopy(plat_sdl2_renderer, tg != NULL ? tg : texture, NULL, dst);
  if (dst != NULL && scan_on && scan_alpha > 0)
    draw_scanlines(dst);
  plat_sdl2_debug_shot();
  plat_sdl2_shot_take();
  SDL_AtomicAdd(&frame_count, 1);
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
