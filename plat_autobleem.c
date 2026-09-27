/*
 * AutoBleem's video for libpicofe - see plat_autobleem.h.
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

#include "plat_autobleem.h"

/* The GL we use, declared here and loaded through SDL_GL_GetProcAddress(): no GL headers or GL library at
 * build time, so the same file builds against any sysroot, and the one symbol set serves a GLES context
 * and a desktop GL 2.1 one alike (everything below is in both). */
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLbitfield;
typedef unsigned char GLboolean;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef char GLchar;

#if defined(_WIN32)
#define GLAPI_ __stdcall
#else
#define GLAPI_
#endif

#define GL_FALSE_                 0
#define GL_TRIANGLE_STRIP_        0x0005
#define GL_SRC_ALPHA_             0x0302
#define GL_ONE_MINUS_SRC_ALPHA_   0x0303
#define GL_CULL_FACE_             0x0B44
#define GL_DEPTH_TEST_            0x0B71
#define GL_BLEND_                 0x0BE2
#define GL_SCISSOR_TEST_          0x0C11
#define GL_UNPACK_ALIGNMENT_      0x0CF5
#define GL_PACK_ALIGNMENT_        0x0D05
#define GL_TEXTURE_2D_            0x0DE1
#define GL_UNSIGNED_BYTE_         0x1401
#define GL_FLOAT_                 0x1406
#define GL_RGB_                   0x1907
#define GL_RGBA_                  0x1908
#define GL_VENDOR_                0x1F00
#define GL_RENDERER_              0x1F01
#define GL_VERSION_               0x1F02
#define GL_NEAREST_               0x2600
#define GL_LINEAR_                0x2601
#define GL_TEXTURE_MAG_FILTER_    0x2800
#define GL_TEXTURE_MIN_FILTER_    0x2801
#define GL_TEXTURE_WRAP_S_        0x2802
#define GL_TEXTURE_WRAP_T_        0x2803
#define GL_COLOR_BUFFER_BIT_      0x4000
#define GL_CLAMP_TO_EDGE_         0x812F
#define GL_UNSIGNED_SHORT_5_6_5_  0x8363
#define GL_TEXTURE0_              0x84C0
#define GL_FRAGMENT_SHADER_       0x8B30
#define GL_VERTEX_SHADER_         0x8B31
#define GL_COMPILE_STATUS_        0x8B81
#define GL_LINK_STATUS_           0x8B82
#define GL_SHADING_LANGUAGE_VERSION_ 0x8B8C
#define GL_FRAMEBUFFER_COMPLETE_  0x8CD5
#define GL_COLOR_ATTACHMENT0_     0x8CE0
#define GL_FRAMEBUFFER_           0x8D40

#define GL_FUNCS(X) \
  X(void, ActiveTexture, (GLenum)) \
  X(void, AttachShader, (GLuint, GLuint)) \
  X(void, BindAttribLocation, (GLuint, GLuint, const GLchar *)) \
  X(void, BindFramebuffer, (GLenum, GLuint)) \
  X(void, BindTexture, (GLenum, GLuint)) \
  X(void, BlendFunc, (GLenum, GLenum)) \
  X(GLenum, CheckFramebufferStatus, (GLenum)) \
  X(void, Clear, (GLbitfield)) \
  X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
  X(void, CompileShader, (GLuint)) \
  X(GLuint, CreateProgram, (void)) \
  X(GLuint, CreateShader, (GLenum)) \
  X(void, DeleteFramebuffers, (GLsizei, const GLuint *)) \
  X(void, DeleteProgram, (GLuint)) \
  X(void, DeleteShader, (GLuint)) \
  X(void, DeleteTextures, (GLsizei, const GLuint *)) \
  X(void, Disable, (GLenum)) \
  X(void, DisableVertexAttribArray, (GLuint)) \
  X(void, DrawArrays, (GLenum, GLint, GLsizei)) \
  X(void, Enable, (GLenum)) \
  X(void, EnableVertexAttribArray, (GLuint)) \
  X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
  X(void, GenFramebuffers, (GLsizei, GLuint *)) \
  X(void, GenTextures, (GLsizei, GLuint *)) \
  X(GLenum, GetError, (void)) \
  X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
  X(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
  X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
  X(void, GetShaderiv, (GLuint, GLenum, GLint *)) \
  X(const GLubyte *, GetString, (GLenum)) \
  X(GLint, GetUniformLocation, (GLuint, const GLchar *)) \
  X(void, LinkProgram, (GLuint)) \
  X(void, PixelStorei, (GLenum, GLint)) \
  X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
  X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
  X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
  X(void, TexParameteri, (GLenum, GLenum, GLint)) \
  X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
  X(void, Uniform1i, (GLint, GLint)) \
  X(void, Uniform2f, (GLint, GLfloat, GLfloat)) \
  X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
  X(void, UseProgram, (GLuint)) \
  X(void, VertexAttrib4f, (GLuint, GLfloat, GLfloat, GLfloat, GLfloat)) \
  X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
  X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))

#define GL_DECL(ret, name, args) ret (GLAPI_ *name) args;
static struct { GL_FUNCS(GL_DECL) } gl;
#undef GL_DECL

SDL_Window *plat_ab_window;
int plat_ab_win_w, plat_ab_win_h;
void (*plat_ab_resize_cb)(int w, int h);
void (*plat_ab_quit_cb)(void);

static SDL_GLContext context;
static int is_gles;               /* the context is GLES (else desktop GL 2.1): the shaders' #version */
static int windowed_w, windowed_h;
static int fullscreen;
static SDL_atomic_t frame_count;  /* presents so far; shot_want/shot_serial: a debug driver's readback */
static SDL_atomic_t shot_want, shot_serial;
static int frame_cache_on;
static SDL_mutex *frame_cache_lock;
static SDL_Surface *frame_cache;  /* the last frame read back, ARGB8888 */
static void (*hud_cb)(int screen_w, int screen_h);

/* the frame, 1x as the app gives it */
static GLuint frame_tex;
static int frame_w, frame_h;
/* the smoothing pass's output */
static GLuint smooth_fbo, smooth_tex;
static int smooth_w, smooth_h, smooth_ok;
/* the scanlines: 1 x the screen's height, RGBA, stretched across */
static GLuint scan_tex;
static int scan_dark, scan_bright, scan_alpha;
static int scan_tex_h, scan_tex_dark, scan_tex_bright, scan_tex_alpha;
/* the HUD's slots */
static struct {
  GLuint tex;
  int cap_w, cap_h, w, h;
} hud[PLAT_AB_HUD_SLOTS];

/* a compiled pass */
typedef struct {
  const struct plat_ab_shader *key;   /* NULL: the built-in copy */
  GLuint prog;
  int failed;
  GLint mvp, texture, texture_size, input_size, output_size, frame_count, frame_direction;
} Program;

#define MAX_PROGRAMS 24
static Program programs[MAX_PROGRAMS];
static Program blit;                    /* the plain copy: the menu, the scanlines, the HUD, a missing filter */
static const struct plat_ab_shader *pipe_smooth, *pipe_filter;
static unsigned int pass_frame;

/* our own copy pass: one read per pixel, the input texture's filter decides nearest or bilinear */
static const char blit_src[] =
  "#if defined(VERTEX)\n"
  "attribute vec4 VertexCoord;\n"
  "attribute vec4 TexCoord;\n"
  "varying vec2 uv;\n"
  "uniform mat4 MVPMatrix;\n"
  "void main() { gl_Position = MVPMatrix * VertexCoord; uv = TexCoord.xy; }\n"
  "#elif defined(FRAGMENT)\n"
  "#ifdef GL_ES\n"
  "precision mediump float;\n"
  "#endif\n"
  "varying vec2 uv;\n"
  "uniform sampler2D Texture;\n"
  "void main() { gl_FragColor = texture2D(Texture, uv); }\n"
  "#endif\n";

/* VertexCoord is 0..1 with y = 0 at the top of what is drawn; into a texture (an FBO) the rows stay in
 * memory order (row 0 = the picture's top, as uploaded), onto the screen they are turned the right way up */
static const GLfloat mvp_texture[16] = { 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, -1, -1, 0, 1 };
static const GLfloat mvp_screen[16] = { 2, 0, 0, 0, 0, -2, 0, 0, 0, 0, 1, 0, -1, 1, 0, 1 };
static const GLfloat quad_full[16] = { 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1 };

void plat_ab_set_hud_cb(void (*cb)(int screen_w, int screen_h))
{
  hud_cb = cb;
}

static void update_output_size(void)
{
  int w = 0, h = 0;
  SDL_GL_GetDrawableSize(plat_ab_window, &w, &h);
  if (w > 0 && h > 0)
    plat_ab_win_w = w, plat_ab_win_h = h;
}

/* SDL_ShowCursor(SDL_DISABLE) alone leaves the pointer on the screen with the KMSDRM backend (a Pi):
 * relative mouse mode is what hides it there, and it keeps the pointer inside the window on a desktop.
 * The absolute position in_sdl2 reads for a lightgun is still kept by SDL from the relative motion. */
static void plat_ab_show_cursor(int show)
{
  int r1 = SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE);
  int r2 = SDL_SetRelativeMouseMode(show ? SDL_FALSE : SDL_TRUE);
  if (plat_ab_window != NULL)
    SDL_SetWindowGrab(plat_ab_window, show ? SDL_FALSE : SDL_TRUE);
  fprintf(stderr, "plat_ab: cursor %s (ShowCursor %d, relative %d: %s)\n", show ? "shown" : "hidden",
    r1, r2, r2 == 0 ? "ok" : SDL_GetError());
}

/***************************************************************************************************
 * shaders
 ***************************************************************************************************/

static GLuint compile_shader(GLenum type, const char *src, const char *name)
{
  const char *parts[3];
  GLuint s;
  GLint ok = 0;

  parts[0] = is_gles ? "#version 100\n" : "#version 120\n";
  parts[1] = type == GL_VERTEX_SHADER_ ? "#define VERTEX\n" : "#define FRAGMENT\n";
  parts[2] = src;
  s = gl.CreateShader(type);
  gl.ShaderSource(s, 3, parts, NULL);
  gl.CompileShader(s);
  gl.GetShaderiv(s, GL_COMPILE_STATUS_, &ok);
  if (!ok) {
    char log[1024];
    log[0] = 0;
    gl.GetShaderInfoLog(s, sizeof(log), NULL, log);
    fprintf(stderr, "plat_ab: %s: the %s shader does not compile:\n%s\n", name,
      type == GL_VERTEX_SHADER_ ? "vertex" : "fragment", log);
    gl.DeleteShader(s);
    return 0;
  }
  return s;
}

static int build_program(Program *p, const char *src, const char *name)
{
  GLuint vs, fs;
  GLint ok = 0;
  Uint32 t0 = SDL_GetTicks();

  p->failed = 1;
  vs = compile_shader(GL_VERTEX_SHADER_, src, name);
  fs = vs ? compile_shader(GL_FRAGMENT_SHADER_, src, name) : 0;
  if (!vs || !fs) {
    if (vs)
      gl.DeleteShader(vs);
    return -1;
  }
  p->prog = gl.CreateProgram();
  gl.AttachShader(p->prog, vs);
  gl.AttachShader(p->prog, fs);
  gl.BindAttribLocation(p->prog, 0, "VertexCoord");
  gl.BindAttribLocation(p->prog, 1, "TexCoord");
  gl.BindAttribLocation(p->prog, 2, "COLOR");
  gl.LinkProgram(p->prog);
  gl.DeleteShader(vs);
  gl.DeleteShader(fs);
  gl.GetProgramiv(p->prog, GL_LINK_STATUS_, &ok);
  if (!ok) {
    char log[1024];
    log[0] = 0;
    gl.GetProgramInfoLog(p->prog, sizeof(log), NULL, log);
    fprintf(stderr, "plat_ab: %s: the program does not link:\n%s\n", name, log);
    gl.DeleteProgram(p->prog);
    p->prog = 0;
    return -1;
  }
  p->mvp = gl.GetUniformLocation(p->prog, "MVPMatrix");
  p->texture = gl.GetUniformLocation(p->prog, "Texture");
  p->texture_size = gl.GetUniformLocation(p->prog, "TextureSize");
  p->input_size = gl.GetUniformLocation(p->prog, "InputSize");
  p->output_size = gl.GetUniformLocation(p->prog, "OutputSize");
  p->frame_count = gl.GetUniformLocation(p->prog, "FrameCount");
  p->frame_direction = gl.GetUniformLocation(p->prog, "FrameDirection");
  p->failed = 0;
  fprintf(stderr, "plat_ab: shader %s ready (%u ms)\n", name, (unsigned)(SDL_GetTicks() - t0));
  return 0;
}

/* the program for a pass, compiled the first time it is asked for; NULL if it does not compile */
static Program *get_program(const struct plat_ab_shader *sh)
{
  int i;

  if (sh == NULL)
    return blit.failed ? NULL : &blit;
  for (i = 0; i < MAX_PROGRAMS; i++) {
    if (programs[i].key == sh)
      return programs[i].failed ? NULL : &programs[i];
    if (programs[i].key == NULL) {
      programs[i].key = sh;
      build_program(&programs[i], sh->src, sh->name);
      return programs[i].failed ? NULL : &programs[i];
    }
  }
  return NULL;
}

void plat_ab_set_pipeline(const struct plat_ab_shader *smooth, const struct plat_ab_shader *filter)
{
  if (smooth != pipe_smooth || filter != pipe_filter)
    fprintf(stderr, "plat_ab: pipeline %s -> %s\n", smooth ? smooth->name : "-",
      filter ? filter->name : "linear");
  pipe_smooth = smooth;
  pipe_filter = filter;
}

/* one textured quad with a pass: tex sampled by `linear`, drawn into the viewport already set */
static void draw_pass(const Program *p, GLuint tex, int linear, int in_w, int in_h, int out_w, int out_h,
                      const GLfloat *mvp, const GLfloat *vc, const GLfloat *tc)
{
  gl.UseProgram(p->prog);
  gl.ActiveTexture(GL_TEXTURE0_);
  gl.BindTexture(GL_TEXTURE_2D_, tex);
  gl.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, linear ? GL_LINEAR_ : GL_NEAREST_);
  gl.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, linear ? GL_LINEAR_ : GL_NEAREST_);
  if (p->mvp >= 0)
    gl.UniformMatrix4fv(p->mvp, 1, GL_FALSE_, mvp);
  if (p->texture >= 0)
    gl.Uniform1i(p->texture, 0);
  if (p->texture_size >= 0)
    gl.Uniform2f(p->texture_size, (GLfloat)in_w, (GLfloat)in_h);
  if (p->input_size >= 0)
    gl.Uniform2f(p->input_size, (GLfloat)in_w, (GLfloat)in_h);
  if (p->output_size >= 0)
    gl.Uniform2f(p->output_size, (GLfloat)out_w, (GLfloat)out_h);
  if (p->frame_count >= 0)
    gl.Uniform1i(p->frame_count, (GLint)pass_frame);
  if (p->frame_direction >= 0)
    gl.Uniform1i(p->frame_direction, 1);
  gl.VertexAttribPointer(0, 4, GL_FLOAT_, GL_FALSE_, 0, vc);
  gl.VertexAttribPointer(1, 4, GL_FLOAT_, GL_FALSE_, 0, tc);
  gl.DrawArrays(GL_TRIANGLE_STRIP_, 0, 4);
}

static GLuint new_texture(void)
{
  GLuint t = 0;
  gl.GenTextures(1, &t);
  gl.BindTexture(GL_TEXTURE_2D_, t);
  gl.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
  gl.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
  gl.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_S_, GL_CLAMP_TO_EDGE_);
  gl.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_T_, GL_CLAMP_TO_EDGE_);
  return t;
}

/***************************************************************************************************
 * init
 ***************************************************************************************************/

static int load_gl(void)
{
#define GL_LOAD(ret, name, args) \
  gl.name = (ret (GLAPI_ *) args)SDL_GL_GetProcAddress("gl" #name); \
  if (gl.name == NULL) { fprintf(stderr, "plat_ab: no gl" #name "\n"); return -1; }
  GL_FUNCS(GL_LOAD)
#undef GL_LOAD
  return 0;
}

/* a window with a GL context: GLES 2.0 (the console's and the Pi's, any GLES 3.x answers it), else desktop
 * GL 2.1 - the window is made again for the second try, since SDL picks EGL or the desktop's GL library
 * when the window is made */
static int create_window(const char *title, int w, int h, Uint32 wflags, int gles)
{
  SDL_GL_ResetAttributes();
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 6);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
  if (gles) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  } else {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  }
  plat_ab_window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
    wflags | SDL_WINDOW_OPENGL);
  if (plat_ab_window == NULL) {
    fprintf(stderr, "plat_ab: SDL_CreateWindow (%s) failed: %s\n", gles ? "GLES" : "GL", SDL_GetError());
    return -1;
  }
  context = SDL_GL_CreateContext(plat_ab_window);
  if (context == NULL) {
    fprintf(stderr, "plat_ab: no %s context: %s\n", gles ? "GLES 2" : "GL 2.1", SDL_GetError());
    SDL_DestroyWindow(plat_ab_window);
    plat_ab_window = NULL;
    return -1;
  }
  is_gles = gles;
  return 0;
}

int plat_ab_init(const char *title, int w, int h, int fullscreen_, int vsync)
{
  Uint32 wflags = SDL_WINDOW_ALLOW_HIGHDPI;
  int ret, i;

  ret = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  if (ret != 0) {
    fprintf(stderr, "plat_ab: SDL_Init failed: %s\n", SDL_GetError());
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
      fprintf(stderr, "plat_ab: got the %s video driver - the display is not free yet, retrying\n", drv);
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

  if (create_window(title, w, h, wflags, 1) != 0 && create_window(title, w, h, wflags, 0) != 0)
    goto fail;
  if (load_gl() != 0)
    goto fail;
  if (SDL_GL_SetSwapInterval(vsync ? 1 : 0) != 0)
    fprintf(stderr, "plat_ab: swap interval %d: %s\n", vsync ? 1 : 0, SDL_GetError());
  update_output_size();

  printf("plat_ab: %s video, %s, %s, GLSL %s, output %dx%d%s%s\n", SDL_GetCurrentVideoDriver(),
    (const char *)gl.GetString(GL_RENDERER_), (const char *)gl.GetString(GL_VERSION_),
    (const char *)gl.GetString(GL_SHADING_LANGUAGE_VERSION_), plat_ab_win_w, plat_ab_win_h,
    vsync ? " (vsync)" : "", fullscreen ? " fullscreen" : "");
  {
    // the display's current mode against the largest it offers (is the output at the panel's native size?)
    SDL_DisplayMode cur, best;
    int di = SDL_GetWindowDisplayIndex(plat_ab_window), n;
    if (di >= 0 && SDL_GetCurrentDisplayMode(di, &cur) == 0) {
      best = cur;
      n = SDL_GetNumDisplayModes(di);
      for (i = 0; i < n; i++) {
        SDL_DisplayMode m;
        if (SDL_GetDisplayMode(di, i, &m) == 0 && m.w * m.h > best.w * best.h)
          best = m;
      }
      printf("plat_ab: display mode %dx%d@%d, largest offered %dx%d@%d\n",
        cur.w, cur.h, cur.refresh_rate, best.w, best.h, best.refresh_rate);
    }
  }

  gl.Disable(GL_DEPTH_TEST_);
  gl.Disable(GL_CULL_FACE_);
  gl.Disable(GL_SCISSOR_TEST_);
  gl.Disable(GL_BLEND_);
  gl.BlendFunc(GL_SRC_ALPHA_, GL_ONE_MINUS_SRC_ALPHA_);
  gl.PixelStorei(GL_UNPACK_ALIGNMENT_, 1);
  gl.PixelStorei(GL_PACK_ALIGNMENT_, 1);
  gl.EnableVertexAttribArray(0);
  gl.EnableVertexAttribArray(1);
  gl.DisableVertexAttribArray(2);
  gl.VertexAttrib4f(2, 1.0f, 1.0f, 1.0f, 1.0f);   /* a libretro shader's COLOR */
  if (build_program(&blit, blit_src, "copy") != 0)
    goto fail;
  frame_tex = new_texture();
  smooth_tex = new_texture();
  gl.GenFramebuffers(1, &smooth_fbo);
  scan_tex = new_texture();

  plat_ab_show_cursor(!fullscreen);
  return 0;

fail:
  plat_ab_finish();
  return -1;
}

void plat_ab_finish(void)
{
  int i;

  if (context != NULL) {
    for (i = 0; i < MAX_PROGRAMS; i++) {
      if (programs[i].prog)
        gl.DeleteProgram(programs[i].prog);
      memset(&programs[i], 0, sizeof(programs[i]));
    }
    if (blit.prog)
      gl.DeleteProgram(blit.prog);
    memset(&blit, 0, sizeof(blit));
    for (i = 0; i < PLAT_AB_HUD_SLOTS; i++) {
      if (hud[i].tex)
        gl.DeleteTextures(1, &hud[i].tex);
      memset(&hud[i], 0, sizeof(hud[i]));
    }
    if (smooth_fbo)
      gl.DeleteFramebuffers(1, &smooth_fbo);
    if (frame_tex)
      gl.DeleteTextures(1, &frame_tex);
    if (smooth_tex)
      gl.DeleteTextures(1, &smooth_tex);
    if (scan_tex)
      gl.DeleteTextures(1, &scan_tex);
    smooth_fbo = frame_tex = smooth_tex = scan_tex = 0;
    frame_w = frame_h = smooth_w = smooth_h = scan_tex_h = 0;
    SDL_GL_DeleteContext(context);
    context = NULL;
  }
  if (plat_ab_window != NULL) {
    SDL_DestroyWindow(plat_ab_window);
    plat_ab_window = NULL;
  }
  SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
}

int plat_ab_set_fullscreen(int on)
{
  int ret;

  on = !!on;
  if (on == fullscreen)
    return 0;
  ret = SDL_SetWindowFullscreen(plat_ab_window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  if (ret != 0) {
    fprintf(stderr, "plat_ab: SDL_SetWindowFullscreen failed: %s\n", SDL_GetError());
    return -1;
  }
  fullscreen = on;
  if (!on)
    SDL_SetWindowSize(plat_ab_window, windowed_w, windowed_h);
  plat_ab_show_cursor(!on);
  update_output_size();
  if (plat_ab_resize_cb != NULL)
    plat_ab_resize_cb(plat_ab_win_w, plat_ab_win_h);
  return 0;
}

int plat_ab_is_fullscreen(void)
{
  return fullscreen;
}

void plat_ab_set_title(const char *title)
{
  SDL_SetWindowTitle(plat_ab_window, title);
}

void plat_ab_clear(void)
{
  if (context == NULL)
    return;
  gl.BindFramebuffer(GL_FRAMEBUFFER_, 0);
  gl.Viewport(0, 0, plat_ab_win_w, plat_ab_win_h);
  gl.ClearColor(0, 0, 0, 1);
  gl.Clear(GL_COLOR_BUFFER_BIT_);
  SDL_GL_SwapWindow(plat_ab_window);
}

/***************************************************************************************************
 * readback
 ***************************************************************************************************/

/* the back buffer (before the swap) into an ARGB8888 surface of the window's size, the right way up */
static int read_screen(SDL_Surface *s)
{
  static Uint8 *buf;
  static size_t buf_size;
  size_t need = (size_t)s->w * s->h * 4;
  int x, y;

  if (buf_size < need) {
    Uint8 *n = realloc(buf, need);
    if (n == NULL)
      return -1;
    buf = n;
    buf_size = need;
  }
  gl.ReadPixels(0, 0, s->w, s->h, GL_RGBA_, GL_UNSIGNED_BYTE_, buf);
  for (y = 0; y < s->h; y++) {
    const Uint8 *src = buf + (size_t)(s->h - 1 - y) * s->w * 4;
    Uint32 *dst = (Uint32 *)((Uint8 *)s->pixels + (size_t)y * s->pitch);
    for (x = 0; x < s->w; x++, src += 4)
      dst[x] = 0xff000000u | ((Uint32)src[0] << 16) | ((Uint32)src[1] << 8) | src[2];
  }
  return 0;
}

/* PLAT_SDL2_SHOT=<file.bmp> in the environment: what is about to be presented, saved every 5 s
 * (PLAT_SDL2_SHOT_MS; the variables keep their old names) - for looking at a display one cannot see (a Pi over ssh); the emulator's own
 * screenshot is the PSX frame before scaling and effects */
static void plat_ab_debug_shot(void)
{
  static const char *path;
  static int checked;
  static Uint32 last, interval = 5000;
  static int count;
  Uint32 now;
  SDL_Surface *s;

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
  s = SDL_CreateRGBSurfaceWithFormat(0, plat_ab_win_w, plat_ab_win_h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (s == NULL)
    return;
  if (read_screen(s) == 0) {
    // a %d in the name numbers the frames (PLAT_SDL2_SHOT=/tmp/shot%d.bmp), else the file is rewritten
    char name[512];
    snprintf(name, sizeof(name), path, count++);
    SDL_SaveBMP(s, name);
  }
  SDL_FreeSurface(s);
}

/* The frame a debug driver asks for (see the header): a readback costs a GPU sync, so it happens only
 * when one was asked for, in the present that follows the request. The driver thread waits for the
 * serial to move and then writes the surface out under the lock. */
static void plat_ab_shot_take(void)
{
  int w = plat_ab_win_w, h = plat_ab_win_h;

  if (!frame_cache_on || SDL_AtomicGet(&shot_want) == 0)
    return;
  SDL_LockMutex(frame_cache_lock);
  if (frame_cache != NULL && (frame_cache->w != w || frame_cache->h != h)) {
    SDL_FreeSurface(frame_cache);
    frame_cache = NULL;
  }
  if (frame_cache == NULL)
    frame_cache = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (frame_cache != NULL && read_screen(frame_cache) != 0)
    fprintf(stderr, "plat_ab: no memory for a %dx%d readback\n", w, h);
  SDL_UnlockMutex(frame_cache_lock);
  SDL_AtomicSet(&shot_want, 0);
  SDL_AtomicAdd(&shot_serial, 1);
}

void plat_ab_frame_cache(int on)
{
  if (on && frame_cache_lock == NULL)
    frame_cache_lock = SDL_CreateMutex();
  frame_cache_on = on && frame_cache_lock != NULL;
}

unsigned int plat_ab_frame_count(void)
{
  return (unsigned int)SDL_AtomicGet(&frame_count);
}

unsigned int plat_ab_shot_request(void)
{
  SDL_AtomicSet(&shot_want, 1);
  return (unsigned int)SDL_AtomicGet(&shot_serial);
}

unsigned int plat_ab_shot_serial(void)
{
  return (unsigned int)SDL_AtomicGet(&shot_serial);
}

int plat_ab_shot_save(const char *path)
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

/***************************************************************************************************
 * scanlines and the HUD
 ***************************************************************************************************/

void plat_ab_set_scanlines(int dark, int bright, int alpha)
{
  scan_dark = dark < 0 ? 0 : dark;
  scan_bright = bright < 1 ? 1 : bright;
  scan_alpha = alpha < 0 ? 0 : alpha > 255 ? 255 : alpha;
}

/* the scanlines are the screen's: dark rows then bright rows from the top, in screen pixels, over the
 * whole screen whatever the picture's size or shape; one 1 x H texture, remade only when the screen's
 * height, the pattern or the alpha changes */
static void draw_scanlines(void)
{
  int h = plat_ab_win_h;

  if (scan_tex_h != h || scan_tex_dark != scan_dark || scan_tex_bright != scan_bright ||
      scan_tex_alpha != scan_alpha) {
    Uint8 *px = calloc((size_t)h, 4);   /* transparent black */
    int y, period = scan_dark + scan_bright;
    if (px == NULL)
      return;
    for (y = 0; y < h; y++)
      if (y % period < scan_dark)
        px[y * 4 + 3] = (Uint8)scan_alpha;
    gl.BindTexture(GL_TEXTURE_2D_, scan_tex);
    gl.TexImage2D(GL_TEXTURE_2D_, 0, GL_RGBA_, 1, h, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, px);
    free(px);
    scan_tex_h = h;
    scan_tex_dark = scan_dark;
    scan_tex_bright = scan_bright;
    scan_tex_alpha = scan_alpha;
    fprintf(stderr, "plat_ab: scanlines %d dark + %d bright over %d rows, alpha %d\n", scan_dark, scan_bright,
      h, scan_alpha);
  }
  draw_pass(&blit, scan_tex, 0, 1, h, plat_ab_win_w, h, mvp_screen, quad_full, quad_full);
}

int plat_ab_hud_image(int slot, const Uint32 *argb, int w, int h)
{
  Uint8 *px;
  int i;

  if (context == NULL || slot < 0 || slot >= PLAT_AB_HUD_SLOTS || w <= 0 || h <= 0)
    return -1;
  px = malloc((size_t)w * h * 4);
  if (px == NULL)
    return -1;
  for (i = 0; i < w * h; i++) {
    px[i * 4 + 0] = (Uint8)(argb[i] >> 16);
    px[i * 4 + 1] = (Uint8)(argb[i] >> 8);
    px[i * 4 + 2] = (Uint8)argb[i];
    px[i * 4 + 3] = (Uint8)(argb[i] >> 24);
  }
  if (hud[slot].tex == 0)
    hud[slot].tex = new_texture();
  gl.BindTexture(GL_TEXTURE_2D_, hud[slot].tex);
  if (w > hud[slot].cap_w || h > hud[slot].cap_h) {
    int cw = w > hud[slot].cap_w ? w : hud[slot].cap_w, ch = h > hud[slot].cap_h ? h : hud[slot].cap_h;
    gl.TexImage2D(GL_TEXTURE_2D_, 0, GL_RGBA_, cw, ch, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, NULL);
    hud[slot].cap_w = cw;
    hud[slot].cap_h = ch;
  }
  gl.TexSubImage2D(GL_TEXTURE_2D_, 0, 0, 0, w, h, GL_RGBA_, GL_UNSIGNED_BYTE_, px);
  free(px);
  hud[slot].w = w;
  hud[slot].h = h;
  return 0;
}

void plat_ab_hud_draw(int slot, const SDL_Rect *r)
{
  GLfloat vc[16], tc[16], x0, y0, x1, y1, u, v;

  if (slot < 0 || slot >= PLAT_AB_HUD_SLOTS || hud[slot].tex == 0 || r == NULL)
    return;
  x0 = (GLfloat)r->x / plat_ab_win_w;
  y0 = (GLfloat)r->y / plat_ab_win_h;
  x1 = (GLfloat)(r->x + r->w) / plat_ab_win_w;
  y1 = (GLfloat)(r->y + r->h) / plat_ab_win_h;
  u = (GLfloat)hud[slot].w / hud[slot].cap_w;
  v = (GLfloat)hud[slot].h / hud[slot].cap_h;
#define V4(a, i, x, y) a[i] = x, a[i + 1] = y, a[i + 2] = 0, a[i + 3] = 1
  V4(vc, 0, x0, y0); V4(vc, 4, x1, y0); V4(vc, 8, x0, y1); V4(vc, 12, x1, y1);
  V4(tc, 0, 0, 0); V4(tc, 4, u, 0); V4(tc, 8, 0, v); V4(tc, 12, u, v);
#undef V4
  draw_pass(&blit, hud[slot].tex, 0, hud[slot].cap_w, hud[slot].cap_h, r->w, r->h, mvp_screen, vc, tc);
}

/***************************************************************************************************
 * present
 ***************************************************************************************************/

static void upload_frame(const void *rgb565, int w, int h, int pitch)
{
  gl.ActiveTexture(GL_TEXTURE0_);
  gl.BindTexture(GL_TEXTURE_2D_, frame_tex);
  if (w != frame_w || h != frame_h) {
    gl.TexImage2D(GL_TEXTURE_2D_, 0, GL_RGB_, w, h, 0, GL_RGB_, GL_UNSIGNED_SHORT_5_6_5_, NULL);
    frame_w = w;
    frame_h = h;
  }
  if (pitch == w)
    gl.TexSubImage2D(GL_TEXTURE_2D_, 0, 0, 0, w, h, GL_RGB_, GL_UNSIGNED_SHORT_5_6_5_, rgb565);
  else {
    // GLES 2 has no row length: a row at a time (only a frame narrower than its buffer comes here)
    const Uint16 *p = rgb565;
    int y;
    for (y = 0; y < h; y++)
      gl.TexSubImage2D(GL_TEXTURE_2D_, 0, 0, y, w, 1, GL_RGB_, GL_UNSIGNED_SHORT_5_6_5_, p + (size_t)y * pitch);
  }
}

/* the smoothing pass's target at kw x kh; 0 if the FBO cannot be made (smoothing is then left out) */
static int smooth_target(int kw, int kh)
{
  if (kw == smooth_w && kh == smooth_h)
    return smooth_ok;
  gl.BindTexture(GL_TEXTURE_2D_, smooth_tex);
  gl.TexImage2D(GL_TEXTURE_2D_, 0, GL_RGB_, kw, kh, 0, GL_RGB_, GL_UNSIGNED_SHORT_5_6_5_, NULL);
  gl.BindFramebuffer(GL_FRAMEBUFFER_, smooth_fbo);
  gl.FramebufferTexture2D(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D_, smooth_tex, 0);
  smooth_ok = gl.CheckFramebufferStatus(GL_FRAMEBUFFER_) == GL_FRAMEBUFFER_COMPLETE_;
  gl.BindFramebuffer(GL_FRAMEBUFFER_, 0);
  smooth_w = kw;
  smooth_h = kh;
  fprintf(stderr, "plat_ab: smoothing target %dx%d%s\n", kw, kh, smooth_ok ? "" : " - incomplete, smoothing off");
  return smooth_ok;
}

int plat_ab_present(const void *rgb565, int w, int h, int pitch, const SDL_Rect *dst)
{
  int sw = plat_ab_win_w, sh = plat_ab_win_h;

  if (context == NULL)
    return -1;
  upload_frame(rgb565, w, h, pitch);
  gl.Disable(GL_BLEND_);
  pass_frame++;

  if (dst == NULL) {
    // the menu: its buffer is the window's size, 1:1
    gl.BindFramebuffer(GL_FRAMEBUFFER_, 0);
    gl.Viewport(0, 0, sw, sh);
    draw_pass(&blit, frame_tex, 0, w, h, sw, sh, mvp_screen, quad_full, quad_full);
  } else {
    const struct plat_ab_shader *fsh = pipe_filter;
    Program *smooth = pipe_smooth != NULL ? get_program(pipe_smooth) : NULL;
    Program *filter = get_program(fsh);
    GLuint in = frame_tex;
    int in_w = w, in_h = h;

    if (filter == NULL) {   // a filter that does not compile: a plain bilinear copy
      filter = &blit;
      fsh = NULL;
    }
    if (smooth != NULL && pipe_smooth->scale > 0 &&
        smooth_target(w * pipe_smooth->scale, h * pipe_smooth->scale)) {
      gl.BindFramebuffer(GL_FRAMEBUFFER_, smooth_fbo);
      gl.Viewport(0, 0, smooth_w, smooth_h);
      draw_pass(smooth, frame_tex, pipe_smooth->linear, w, h, smooth_w, smooth_h, mvp_texture, quad_full,
        quad_full);
      in = smooth_tex;
      in_w = smooth_w;
      in_h = smooth_h;
    }
    gl.BindFramebuffer(GL_FRAMEBUFFER_, 0);
    gl.Viewport(0, 0, sw, sh);
    gl.ClearColor(0, 0, 0, 1);
    gl.Clear(GL_COLOR_BUFFER_BIT_);
    gl.Viewport(dst->x, sh - dst->y - dst->h, dst->w, dst->h);
    draw_pass(filter, in, fsh != NULL ? fsh->linear : 1, in_w, in_h, dst->w, dst->h, mvp_screen, quad_full,
      quad_full);

    gl.Viewport(0, 0, sw, sh);
    gl.Enable(GL_BLEND_);
    if (scan_dark > 0 && scan_alpha > 0)
      draw_scanlines();
    if (hud_cb != NULL)
      hud_cb(sw, sh);
    gl.Disable(GL_BLEND_);
  }
  plat_ab_debug_shot();
  plat_ab_shot_take();
  SDL_AtomicAdd(&frame_count, 1);
  SDL_GL_SwapWindow(plat_ab_window);
  return 0;
}

void plat_ab_event_handler(void *event_)
{
  SDL_Event *event = event_;

  switch (event->type) {
  case SDL_WINDOWEVENT:
    switch (event->window.event) {
    case SDL_WINDOWEVENT_SIZE_CHANGED:
    case SDL_WINDOWEVENT_RESIZED:
      update_output_size();
      if (plat_ab_resize_cb != NULL)
        plat_ab_resize_cb(plat_ab_win_w, plat_ab_win_h);
      break;
    default:
      break;
    }
    break;
  case SDL_QUIT:
    if (plat_ab_quit_cb != NULL)
      plat_ab_quit_cb();
    break;
  default:
    break;
  }
}

// vim:shiftwidth=2:expandtab
