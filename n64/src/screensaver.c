/*
 * screensaver.c — idle-detect + bouncing-logo renderer.
 *
 * State model: an idle-frame counter that the main loop pokes every
 * frame. Once it crosses IDLE_FRAMES (~5 s at 60 fps) the renderer
 * is considered "active" and takes over the framebuffer; any
 * meaningful input resets the counter to zero and the screensaver
 * stops.
 *
 * Bounce mechanics: integer x/y position with ±1 px/frame velocity,
 * walls collide on screen edges, colour index advances every wall
 * hit. Matches the GBA / PC Engine versions.
 *
 * Copyright (c) 2026 Robert Dale Smith
 * MIT License -- see ../LICENSE.md
 */

#include "screensaver.h"
#include "gen_logo.h"

#include <libdragon.h>
#include <limits.h>
#include <stdint.h>

/* Match main.c's display_init resolution. If you bump that, also bump
 * these. */
#define SCREEN_W       320
#define SCREEN_H       240
/* Logo dimensions come from gen_logo.h (regenerate via
 * buildtools/make_logo.py if you swap the asset). The PNG is
 * intentionally wider than tall (76x64) to compensate for N64
 * anamorphic 2:1 horizontal stretch. */

/* 60 fps * 30 s -- matches the GBA / PC Engine / GameCube screensavers. */
#define IDLE_FRAMES    (60 * 30)

/* Canonical 7-colour cycle: red, green, yellow, blue, magenta, cyan,
 * white -- same order as the GBA + PC Engine + GameCube
 * screensavers. The GameCube version's `cycle_yuv[]` is the
 * historical source; we re-express in libdragon's 32bpp RGBA32
 * via the graphics_make_color() helper. */
static const uint32_t LOGO_COLORS[] = {
    0xff0000ff,  /* red       */
    0x00ff00ff,  /* green     */
    0xffff00ff,  /* yellow    */
    0x0000ffff,  /* blue      */
    0xff00ffff,  /* magenta   */
    0x00ffffff,  /* cyan      */
    0xffffffff,  /* white     */
};
#define CYCLE_LEN ((int)(sizeof(LOGO_COLORS) / sizeof(LOGO_COLORS[0])))

static int idle_frames = 0;

static int logo_x   = 100;
static int logo_y   = 80;
static int logo_dx  = +1;
static int logo_dy  = +1;
static int color_ix = 0;

void screensaver_idle_tick(bool any_input)
{
    if (any_input) {
        if (idle_frames >= IDLE_FRAMES) {
            /* Was active and just got input -- reset bounce state so
             * the next entry starts fresh in the centre. */
            logo_x = 100; logo_y = 80;
            logo_dx = +1; logo_dy = +1;
            color_ix = 0;
        }
        idle_frames = 0;
    } else {
        if (idle_frames < INT32_MAX) idle_frames++;
    }
}

bool screensaver_active(void)
{
    return idle_frames >= IDLE_FRAMES;
}

void screensaver_wake(void)
{
    idle_frames = 0;
}

void screensaver_render(void)
{
    surface_t *surf = display_get();
    /* Query the actual surface dimensions rather than trusting the
     * SCREEN_W/SCREEN_H compile-time constants -- some libdragon
     * display modes return a wider/taller surface than the nominal
     * resolution_t (e.g. internally line-doubled buffers). The
     * SCREEN_W constant is now just a fallback for the bounce
     * boundary if surf->width is somehow zero. */
    int screen_w = surf->width  ? surf->width  : SCREEN_W;
    int screen_h = surf->height ? surf->height : SCREEN_H;

    /* Advance position; bounce off the four walls and cycle colour on
     * each hit. */
    logo_x += logo_dx;
    logo_y += logo_dy;

    /* Horizontal bounce limits account for the 2x horizontal scale
     * applied at render time (see render loop). */
    bool bounced = false;
    int  logo_w_disp = LOGO_W * 2;
    if (logo_x <= 0)                     { logo_x = 0;                    logo_dx = +1; bounced = true; }
    if (logo_x >= screen_w - logo_w_disp){ logo_x = screen_w - logo_w_disp; logo_dx = -1; bounced = true; }
    if (logo_y <= 0)                     { logo_y = 0;                    logo_dy = +1; bounced = true; }
    if (logo_y >= screen_h - LOGO_H)     { logo_y = screen_h - LOGO_H;    logo_dy = -1; bounced = true; }
    if (bounced) {
        color_ix = (color_ix + 1) % CYCLE_LEN;
    }
    /* libdragon's graphics_make_color packs an RGBA32 into the format
     * the active surface needs. Since our LOGO_COLORS are already
     * RGBA32 we re-unpack + re-pack to be format-agnostic. */
    uint32_t  bg   = graphics_make_color(0, 0, 0, 255);
    uint8_t   r    = (LOGO_COLORS[color_ix] >> 24) & 0xff;
    uint8_t   g    = (LOGO_COLORS[color_ix] >> 16) & 0xff;
    uint8_t   b    = (LOGO_COLORS[color_ix] >>  8) & 0xff;
    uint32_t  fg   = graphics_make_color(r, g, b, 255);

    graphics_fill_screen(surf, bg);

    /* N64's 320x240 framebuffer is anamorphic: each fb pixel ends up
     * roughly half as wide as it is tall on a 4:3 NTSC display, so
     * we double each logo pixel horizontally to keep the silhouette's
     * source aspect ratio intact on screen. Scaling on the fly is
     * cheaper than maintaining a pre-scaled asset and lets us swap
     * the PNG without re-running a build-time stretch. */
    for (int row = 0; row < LOGO_H; row++) {
        const uint8_t *row_bytes = &gen_logo[row * LOGO_W];
        for (int col = 0; col < LOGO_W; col++) {
            if (row_bytes[col]) {
                int x = logo_x + col * 2;
                graphics_draw_pixel(surf, x,     logo_y + row, fg);
                graphics_draw_pixel(surf, x + 1, logo_y + row, fg);
            }
        }
    }

    display_show(surf);
}
