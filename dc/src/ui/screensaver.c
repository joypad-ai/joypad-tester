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
#include <dc/maple.h>
#include <dc/maple/vmu.h>
#include <string.h>

#include "screensaver.h"
#include "bfont_util.h"
#include "gen_logo.h"
#include "../ports/ports.h"
#include "../library/gen_library_icon.h"   /* joypad_logo_icondata_full */
#include "../vms/vms.h"
#include "../vms/apply.h"                  /* jt_vmu_show_stored_icon */

/* 30 seconds of idle before the screensaver kicks in. dt-based so the
 * threshold doesn't depend on framerate (was IDLE_FRAMES = 30 * 60). */
#define IDLE_SECONDS 30.0f

static float idle_time = 0.0f;
static bool  active = false;
/* Position + velocity are dt-based: x/y in pixels (float so subpixel
 * accumulation doesn't drop frames at high fps), vx/vy in pixels/sec.
 * 120 px/s == the original 2 px/frame at 60 fps, but now constant regardless
 * of the actual frame rate (which can spike on Flycast without throttle
 * and was making the logo fly across the screen). */
static float x, y, vx, vy;
static int   color_idx = 0;
static int  last_x = -1000;
static int  last_y = -1000;
/* Pending-wake flag: set the single frame we deactivate, consumed by
 * main.c to clear leftover logo pixels + nudge mode caches. */
static bool wake_pending = false;

/* VMU-LCD mirror of the screensaver. While active we render the joypad
 * mono icon at a bounce offset onto every present VMU LCD; the LCD's
 * 48x32 area only has 16px of horizontal slack and a few px vertical
 * (the joypad mono has empty rows top/bottom we clip into), so the LCD
 * trajectory ends up a small diagonal mirroring the screen's. We only
 * push when the integer LCD position changes, which naturally caps
 * maple writes to a handful per second per VMU. On wake we restore each
 * VMU to its stored icon (jt_vmu_show_stored_icon) so the LCD doesn't
 * stick on the last screensaver frame while the mode catches up. */
static jt_icon_t lcd_joypad_icon;
static bool      lcd_joypad_decoded = false;
static int       last_lcd_x = -1000;
static int       last_lcd_y = -1000;

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
    idle_time = 0.0f;
    active = false;
    color_idx = 0;
    /* Upper-left start; diagonal at 120 px/s (matches the previous
     * 2 px/frame at 60 fps). */
    x = 64.0f; y = 80.0f;
    vx = 120.0f; vy = 120.0f;
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

static void lcd_decode_joypad_once(void)
{
    if (lcd_joypad_decoded) return;
    lcd_joypad_decoded = jt_vms_decode_icondata(
        joypad_logo_icondata_full, sizeof(joypad_logo_icondata_full),
        &lcd_joypad_icon);
}

/* Render the 32x32 joypad mono into 48x32 LCD native format (MSB-first
 * per byte, 180-deg rotated) with arbitrary (lcd_x, lcd_y) placement.
 * Off-LCD bitmap pixels are clipped; the mono's empty top/bottom rows
 * absorb small negative/positive y offsets without visible loss. */
static void lcd_render_joypad_at(uint8_t out[48 * 32 / 8],
                                 int lcd_x, int lcd_y)
{
    memset(out, 0, 48 * 32 / 8);
    if (!lcd_joypad_decoded) return;
    const uint8_t *mono = lcd_joypad_icon.mono_bits;
    for (int r = 0; r < 32; r++) {
        for (int c = 0; c < 32; c++) {
            int p = r * 32 + c;
            if (!((mono[p / 8] >> (7 - (p % 8))) & 1)) continue;
            int lx = lcd_x + c, ly = lcd_y + r;
            if (lx < 0 || lx >= 48 || ly < 0 || ly >= 32) continue;
            int dx = 47 - lx, dy = 31 - ly;
            out[dy * 6 + (dx / 8)] |= (uint8_t)(0x80 >> (dx % 8));
        }
    }
}

static void lcd_push_joypad_to_all_vmus(int lcd_x, int lcd_y)
{
    lcd_decode_joypad_once();
    uint8_t native[48 * 32 / 8];
    lcd_render_joypad_at(native, lcd_x, lcd_y);
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        for (int s = 0; s < JT_NUM_SLOTS; s++) {
            if (jt_ports[p].slots[s].kind != JT_SLOT_VMU) continue;
            if (!jt_ports[p].slots[s].has_lcd) continue;
            maple_device_t *dev = maple_enum_dev(p, s + 1);
            if (!dev || !dev->valid) continue;
            vmu_draw_lcd(dev, native);
        }
    }
}

static void lcd_restore_all_vmus(void)
{
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        for (int s = 0; s < JT_NUM_SLOTS; s++) {
            if (jt_ports[p].slots[s].kind != JT_SLOT_VMU) continue;
            if (!jt_ports[p].slots[s].has_lcd) continue;
            jt_vmu_show_stored_icon(p, s);
        }
    }
}

void jt_screensaver_tick(float dt)
{
    bool input = any_user_input();

    if (input) {
        if (active) {
            active = false;
            idle_time = 0.0f;
            wake_pending = true;
            /* Restore each VMU's natural icon so the LCD doesn't sit on
             * the last bouncing-logo frame while the underlying mode
             * catches up (browser only re-pushes on enter / refresh). */
            lcd_restore_all_vmus();
            last_lcd_x = -1000;
            last_lcd_y = -1000;
        }
        idle_time = 0.0f;
        return;
    }

    idle_time += dt;
    if (!active && idle_time >= IDLE_SECONDS) {
        active = true;
        /* Force the first VMU-LCD push regardless of where the bounce
         * happens to start. */
        last_lcd_x = -1000;
        last_lcd_y = -1000;
    }

    /* Step + bounce in tick so motion is dt-based: at any frame rate the
     * logo moves at vx/vy pixels per second instead of vx/vy per frame.
     * Each wall hit flips the relevant component and advances the cycle
     * index. The back buffer is cleared every frame by main.c, so the
     * draw side just blits at the current position. */
    if (active) {
        x += vx * dt;
        y += vy * dt;
        if (x < 0)                { x = 0;              vx = -vx; color_idx = (color_idx + 1) % 7; }
        if (x + LOGO_W > 640)     { x = 640 - LOGO_W;   vx = -vx; color_idx = (color_idx + 1) % 7; }
        if (y < 0)                { y = 0;              vy = -vy; color_idx = (color_idx + 1) % 7; }
        if (y + LOGO_H > 480)     { y = 480 - LOGO_H;   vy = -vy; color_idx = (color_idx + 1) % 7; }

        /* Mirror the screen bounce onto every present VMU LCD. The
         * 32x32 joypad mono on a 48x32 LCD has only 16px horizontal
         * slack and ~5px vertical slack (we shift into the empty
         * top/bottom rows of the bitmap), so the LCD trajectory is a
         * compressed version of the screen one. Push only when the
         * integer position changes, which naturally caps maple writes
         * to a handful per second per VMU. */
        float max_sx = (float)(640 - LOGO_W);
        float max_sy = (float)(480 - LOGO_H);
        int lcd_x = (int)(x * 16.0f / max_sx);
        int lcd_y = (int)(y * 5.0f  / max_sy) - 2;
        if (lcd_x < 0)  lcd_x = 0;
        if (lcd_x > 16) lcd_x = 16;
        if (lcd_y < -2) lcd_y = -2;
        if (lcd_y > 3)  lcd_y = 3;
        if (lcd_x != last_lcd_x || lcd_y != last_lcd_y) {
            lcd_push_joypad_to_all_vmus(lcd_x, lcd_y);
            last_lcd_x = lcd_x;
            last_lcd_y = lcd_y;
        }
    }
}

void jt_screensaver_draw(void)
{
    if (!active) return;
    blit_logo((int)x, (int)y, cycle[color_idx]);
}
