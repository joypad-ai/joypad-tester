/*
 * screensaver.c — bouncing silhouette-logo screensaver.
 *
 * Matches the joypad-tester convention used by gcn / gba / pce:
 *   - track idle time across all maple ports.
 *   - after ~30s with no input, clear the screen and start bouncing
 *     the Joypad logo silhouette.
 *   - the logo is a 1-bit mask (gen_logo.h, baked from
 *     assets/logo.png by buildtools/make_logo.py); each set bit gets
 *     rendered in the active cycle color, clear bits stay black.
 *   - on each wall hit, flip the relevant velocity component and
 *     advance the 7-color cycle.
 *   - any input wakes; main.c then resumes the underlying mode draw,
 *     which is responsible for repainting its own UI.
 */
#include <kos.h>
#include <dc/video.h>
#include <string.h>

#include "screensaver.h"
#include "bfont_util.h"
#include "gen_logo.h"
#include "../ports/ports.h"

/* 30 seconds at 60 fps -- matches the PCE tester's threshold. */
#define IDLE_FRAMES 1800

static int  idle_counter = 0;
static bool active = false;
static int  x, y, dx, dy;
static int  color_idx = 0;
static int  last_x = -1000;
static int  last_y = -1000;
/* Pending-wake flag: set the single frame we deactivate, consumed by
 * main.c to clear leftover logo pixels + nudge mode caches. */
static bool wake_pending = false;

/* 7-color cycle. Walls advance the index. Skips dim blues that read
 * poorly on consumer CRTs. */
static const uint16_t cycle[7] = {
    JT_COL_RED,
    JT_COL_GREEN,
    JT_COL_YELLOW,
    JT_COL_CYAN,
    JT_COL_WHITE,
    JT_RGB565(255, 128,   0),  /* orange */
    JT_RGB565(255,  80, 200),  /* hot pink */
};

void jt_screensaver_init(void)
{
    idle_counter = 0;
    active = false;
    color_idx = 0;
    /* Start in the upper-left quadrant with a velocity that puts the
     * logo on a diagonal trajectory at ~2 px/frame. */
    x = 64; y = 80;
    dx = 2;  dy = 2;
    last_x = -1000;
    last_y = -1000;
}

bool jt_screensaver_active(void) { return active; }

bool jt_screensaver_consume_wake(void)
{
    bool r = wake_pending;
    wake_pending = false;
    return r;
}

static bool any_user_input(void)
{
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        jt_port_state_t *port = &jt_ports[p];
        if (!port->present) continue;

        if (port->style == JT_STYLE_PAD) {
            if (port->pad.buttons != 0) return true;
            int sx = port->pad.stick_x, sy = port->pad.stick_y;
            if (sx > 20 || sx < -20 || sy > 20 || sy < -20) return true;
            if (port->pad.trig_l > 20 || port->pad.trig_r > 20) return true;
        }
        if (port->style == JT_STYLE_MOUSE) {
            if (port->mouse.dx || port->mouse.dy) return true;
            if (port->mouse.buttons != 0) return true;
        }
        if (port->style == JT_STYLE_KEYBOARD) {
            for (size_t i = 0; i < sizeof(port->kbd.scancodes); i++) {
                if (port->kbd.scancodes[i]) return true;
            }
            if (port->kbd.modifiers) return true;
        }
    }
    return false;
}

/* Blit the logo mask at (px, py) in `color`. Mask-clear pixels are
 * left untouched (main.c already cleared the back buffer to black this
 * frame), so we only write the lit pixels. */
static void blit_logo(int px, int py, uint16_t color)
{
    for (int row = 0; row < LOGO_H; row++) {
        int sy = py + row;
        if (sy < 0 || sy >= 480) continue;
        const unsigned char *mask_row = logo_mask + row * LOGO_BYTES_PER_ROW;
        uint16_t *dst_row = vram_s + sy * 640;
        for (int col = 0; col < LOGO_W; col++) {
            int sx = px + col;
            if (sx < 0 || sx >= 640) continue;
            unsigned char byte = mask_row[col >> 3];
            if (byte & (0x80 >> (col & 7))) dst_row[sx] = color;
        }
    }
}

void jt_screensaver_tick(float dt)
{
    (void)dt;
    bool input = any_user_input();

    if (input) {
        if (active) {
            active = false;
            idle_counter = 0;
            wake_pending = true;
        }
        idle_counter = 0;
        return;
    }

    idle_counter++;
    if (!active && idle_counter >= IDLE_FRAMES) {
        active = true;
    }
}

void jt_screensaver_draw(void)
{
    if (!active) return;

    /* Step + bounce. Each wall hit flips the relevant component and
     * advances the cycle index. The back buffer is cleared every frame
     * by main.c, so we just draw the logo at its new position. */
    x += dx;
    y += dy;
    if (x < 0) { x = 0; dx = -dx; color_idx = (color_idx + 1) % 7; }
    if (x + LOGO_W > 640) { x = 640 - LOGO_W; dx = -dx; color_idx = (color_idx + 1) % 7; }
    if (y < 0) { y = 0; dy = -dy; color_idx = (color_idx + 1) % 7; }
    if (y + LOGO_H > 480) { y = 480 - LOGO_H; dy = -dy; color_idx = (color_idx + 1) % 7; }

    blit_logo(x, y, cycle[color_idx]);
}
