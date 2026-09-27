/*
 * A standalone smoke test for plat_sdl2's HUD overlay (EMU-15): a callback set with
 * plat_sdl2_set_hud_cb() must run after the frame and the scanlines, never before them - the bug this
 * guards against is a HUD element baked into the emulated frame, which the scanline overlay (or any
 * scaling) then dims or hides. The mechanism is the same whatever the callback draws - a battery icon
 * (part 1) or a text notice such as hud_msg/FPS/CPU load (part 2, frontend/ab/ab_hud_text.c in the
 * pcsx-abnxt repo, tested separately there since it needs that repo's own font data) - so this test proves
 * it once, generically, with a plain fill standing in for "a HUD element" (case 3) and, since part 2, a
 * thin bar standing in for "a notice" specifically (case 6). Runs headless (SDL_VIDEODRIVER=dummy, a
 * software renderer), no display needed. Not part of a CMake build (libpicofe has none of its own) - the
 * AutoBleem-side tests in frontend/ab/test_*.c are the same kind of thing: compile and run directly.
 *
 *   gcc -o test_hud_overlay plat_sdl2.c test_hud_overlay.c $(pkg-config --cflags --libs sdl2) && ./test_hud_overlay
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

static int failures;

static void expect(int cond, const char *what)
{
	if (!cond) {
		printf("FAIL: %s\n", what);
		failures++;
	} else {
		printf("ok:   %s\n", what);
	}
}

static int hud_calls;
static SDL_Rect hud_last_dst;

/* case 6/7's callback: a "notice" - a small element tucked into one corner of dst, the shape a text line
 * like hud_msg/FPS/CPU load actually is (frontend/ab/ab_hud_text.c in the pcsx-abnxt repo draws real text
 * the same way: a texture, copied to a small rect in a corner of dst, never SDL_RenderFillRect). */
static void test_notice_cb(SDL_Renderer *renderer, const SDL_Rect *dst)
{
	Uint32 blue = 0xff0000ff; /* ARGB8888, opaque blue */
	SDL_Texture *tex;
	SDL_Rect r;

	tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 1, 1);
	if (tex == NULL)
		return;
	SDL_UpdateTexture(tex, NULL, &blue, 4);
	r.x = dst->x;
	r.y = dst->y + dst->h - 16;
	r.w = 32;
	r.h = 16;
	SDL_RenderCopy(renderer, tex, NULL, &r);
	SDL_DestroyTexture(tex);
}

/* the callback under test: fills the whole dst with a solid, distinctive color via a small texture
 * (never SDL_RenderFillRect - see the repo's CLAUDE.md: SDL's GLES2 on the console draws a fill rect
 * only 1 px high) */
static void test_hud_cb(SDL_Renderer *renderer, const SDL_Rect *dst)
{
	Uint32 green = 0xff00ff00; /* ARGB8888, opaque green */
	SDL_Texture *tex;

	hud_calls++;
	hud_last_dst = *dst;

	tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 1, 1);
	if (tex == NULL)
		return;
	SDL_UpdateTexture(tex, NULL, &green, 4);
	SDL_RenderCopy(renderer, tex, NULL, dst);
	SDL_DestroyTexture(tex);
}

/* the pixel at (x,y) of whatever is currently in the renderer's backbuffer, as 0xRRGGBB (alpha dropped) */
static Uint32 read_pixel(int x, int y)
{
	Uint32 px = 0;
	SDL_Rect r = { x, y, 1, 1 };
	if (SDL_RenderReadPixels(plat_sdl2_renderer, &r, SDL_PIXELFORMAT_RGB888, &px, 4) != 0) {
		fprintf(stderr, "SDL_RenderReadPixels failed: %s\n", SDL_GetError());
		return 0xdeadbe;
	}
	return px & 0xffffff;
}

/* int argc, char *argv[] even though neither is used: SDL2's pkg-config passes -Dmain=SDL_main, so this
 * literally becomes SDL_main() - which SDL_main.h declares with that signature, not (void). */
int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	const int W = 64, H = 64;
	SDL_Rect dst = { 0, 0, W, H };
	unsigned short frame[8 * 8];
	int i;

	/* SDL_setenv, not POSIX setenv: MinGW (the Windows build) has no setenv, and SDL_setenv has
	 * been in SDL since 2.0.0 - see frontend/plat_sdl2.c's use of it in the pcsx-abnxt repo. */
	SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
	SDL_setenv("SDL_RENDER_DRIVER", "software", 1);

	if (plat_sdl2_init("test_hud_overlay", W, H, 0, 0) != 0) {
		fprintf(stderr, "plat_sdl2_init failed - cannot run this test here\n");
		return 1;
	}

	for (i = 0; i < 8 * 8; i++)
		frame[i] = 0xffff; /* RGB565 white */

	/* 1) no HUD callback registered, no scanlines: the white frame goes through untouched */
	plat_sdl2_set_scanlines(0, 1, 0);
	plat_sdl2_present(frame, 8, 8, 8, &dst, PLAT_SDL2_FILTER_OFF);
	expect(read_pixel(32, 32) == 0xffffff, "no scanlines, no HUD: frame shows through white");

	/* 2) heavy scanlines (alpha 255) over a small dst: the 240-line overlay is dense enough at 64 px
	 * that it ends up fully opaque over the whole area - the bug this reproduces (EMU-15): a HUD baked
	 * into the frame before present() is completely hidden under them. */
	plat_sdl2_set_scanlines(1, 3, 255);
	plat_sdl2_present(frame, 8, 8, 8, &dst, PLAT_SDL2_FILTER_OFF);
	expect(read_pixel(32, 32) == 0x000000, "heavy scanlines, no HUD cb: white frame fully hidden (the bug)");

	/* 3) the same heavy scanlines, but the HUD is drawn through the new callback instead of being baked
	 * into the frame: it must come out on top, not dimmed or hidden - the fix. */
	hud_calls = 0;
	plat_sdl2_set_hud_cb(test_hud_cb);
	plat_sdl2_present(frame, 8, 8, 8, &dst, PLAT_SDL2_FILTER_OFF);
	expect(hud_calls == 1, "HUD callback: called once per present with a dst");
	expect(hud_last_dst.w == W && hud_last_dst.h == H, "HUD callback: given the same dst as present()");
	expect(read_pixel(32, 32) == 0x00ff00, "heavy scanlines, HUD cb: HUD wins - not dimmed or hidden");

	/* 4) a present with dst == NULL (the menu's own full-window layer) does not call the HUD back -
	 * there is nothing under it for a HUD to sit "over" */
	hud_calls = 0;
	plat_sdl2_present(frame, 8, 8, 8, NULL, PLAT_SDL2_FILTER_OFF);
	expect(hud_calls == 0, "dst == NULL: HUD callback not called");

	/* 5) clearing the callback stops it being called */
	plat_sdl2_set_hud_cb(NULL);
	hud_calls = 0;
	plat_sdl2_present(frame, 8, 8, 8, &dst, PLAT_SDL2_FILTER_OFF);
	expect(hud_calls == 0, "HUD callback cleared: not called any more");

	/* 6) EMU-15 part 2's own bug, reproduced the same way: a "notice" - a small element in one corner of
	 * the frame, not the plain full-dst fill case 2/3 used - is exactly as vulnerable as the battery icon
	 * was to being baked into the source frame before present(). Bake a notice into the bottom-left 4x2
	 * texels of the 8x8 source frame (RGB565 blue); at the 8x scale this test's 64x64 dst gives an 8x8
	 * source, that lands in dst's bottom-left 32x16 px block - exactly where a real hud_msg/FPS line would
	 * sit (frontend/ab/ab_hud_text.c positions its text there too). */
	plat_sdl2_set_scanlines(1, 3, 255);
	for (i = 0; i < 8 * 8; i++)
		frame[i] = 0xffff; /* white background */
	{
		int nx, ny;
		for (ny = 6; ny < 8; ny++)
			for (nx = 0; nx < 4; nx++)
				frame[ny * 8 + nx] = 0x001f; /* RGB565 blue */
	}
	plat_sdl2_present(frame, 8, 8, 8, &dst, PLAT_SDL2_FILTER_OFF);
	expect(read_pixel(16, 56) == 0x000000,
		"heavy scanlines, notice baked into the frame: hidden under them (the bug)");

	/* 7) the same notice, in the same corner, drawn through the HUD callback instead of baked into the
	 * frame: it must come out on top of the scanlines, exactly where a real notice would - the fix. */
	plat_sdl2_set_hud_cb(test_notice_cb);
	for (i = 0; i < 8 * 8; i++)
		frame[i] = 0xffff; /* plain white frame this time - nothing baked in */
	plat_sdl2_present(frame, 8, 8, 8, &dst, PLAT_SDL2_FILTER_OFF);
	expect(read_pixel(16, 56) == 0x0000ff,
		"heavy scanlines, notice via HUD cb: visible, not dimmed or hidden");
	plat_sdl2_set_hud_cb(NULL);

	plat_sdl2_finish();

	printf(failures == 0 ? "\nAll tests passed.\n" : "\n%d test(s) FAILED.\n", failures);
	return failures == 0 ? 0 : 1;
}
