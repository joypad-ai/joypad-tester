/*
 * screensaver.c -- bouncing logo silhouette + 7-color cycle.
 *
 * Same convention dc/'s screensaver follows: dt-based motion (was
 * a per-frame bug on dc/ -- on uncapped emulators the logo flew off
 * the screen). 120 px/s diagonal velocity ~= the 2 px/frame at 60 Hz
 * the legacy ports used. Wall hits flip the relevant component and
 * advance the cycle index.
 *
 * Input check reuses ports.h state -- any pad button, any stick past
 * a deadzone, or any trigger past a small threshold wakes the saver.
 * PORTING.md §3.4 also requires accessory hot-plug events to count
 * as activity; on xbox a pad isn't a pass-through accessory the way
 * a GBA-link is, so the controller poll path is sufficient.
 */
#include "screensaver.h"

#include <string.h>

#include "bfont.h"
#include "colors.h"
#include "gen_logo.h"
#include "../ports/ports.h"
#include "../ports/accessories.h"

#define IDLE_SECONDS 120.0f

static float idle_time;
static bool  active;
static float x, y, vx, vy;
static int   color_idx;
static bool  wake_pending;

static const uint32_t cycle[7] = {
    JT_RGBA(0xFF, 0x40, 0x40, 0xFF),   /* red    */
    JT_RGBA(0x40, 0xFF, 0x40, 0xFF),   /* green  */
    JT_RGBA(0xFF, 0xE0, 0x40, 0xFF),   /* yellow */
    JT_RGBA(0x40, 0xE0, 0xE0, 0xFF),   /* cyan   */
    JT_RGBA(0xFF, 0xFF, 0xFF, 0xFF),   /* white  */
    JT_RGBA(0xFF, 0x80, 0x00, 0xFF),   /* orange */
    JT_RGBA(0xFF, 0x50, 0xC8, 0xFF),   /* hot pink */
};

void jt_screensaver_init(void)
{
    idle_time = 0.0f;
    active = false;
    color_idx = 0;
    x = 64.0f;  y = 80.0f;
    vx = 120.0f; vy = 120.0f;
    wake_pending = false;
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
    /* PORTING.md §3.4: "no input must include every input path --
     * pad buttons, analog past a deadzone, mouse deltas (no
     * deadzone), and pass-through devices like a link-cable
     * controller." For OG Xbox that's pad + USB keyboard + USB
     * mouse + DVD remote + Steel Battalion -- every device class the
     * tester surfaces needs to feed the idle-reset check. */
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        const jt_port_state_t *port = &jt_ports[p];
        if (!port->present) continue;

        if (port->style == JT_STYLE_PAD) {
            if (port->pad.buttons) return true;
            if (port->pad.stick_lx >  6000 || port->pad.stick_lx < -6000) return true;
            if (port->pad.stick_ly >  6000 || port->pad.stick_ly < -6000) return true;
            if (port->pad.stick_rx >  6000 || port->pad.stick_rx < -6000) return true;
            if (port->pad.stick_ry >  6000 || port->pad.stick_ry < -6000) return true;
            if (port->pad.trig_l > 20 || port->pad.trig_r > 20) return true;
            /* Analog-button pressure too -- a held A-button on a Duke
             * sends pressure even when the digital bit is taken away
             * (xemu sometimes does this with keyboard mappings). */
            if (port->pad.abtn_a || port->pad.abtn_b ||
                port->pad.abtn_x || port->pad.abtn_y ||
                port->pad.abtn_black || port->pad.abtn_white) return true;
        }
    }

    /* Walk every chassis port for non-pad accessory input. These
     * devices SDL doesn't surface as gamepads, so we query
     * accessories.c directly instead of waiting for jt_ports state. */
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        /* Keyboard: any held modifier or any non-zero scancode counts.
         * (USB HID Usage 0x01 is the ErrorRollOver state -- still
         * counts; the user is clearly mashing keys.) */
        uint8_t mods = 0;
        uint8_t keys[6] = {0};
        if (jt_accessory_keyboard_at_port(p, &mods, keys)) {
            if (mods) return true;
            for (int i = 0; i < 6; i++) if (keys[i]) return true;
        }
        /* Mouse: any button or nonzero delta or wheel scroll. */
        uint8_t mbtns = 0;
        int8_t mdx = 0, mdy = 0, mw = 0;
        if (jt_accessory_mouse_at_port(p, &mbtns, &mdx, &mdy, &mw)) {
            if (mbtns || mdx || mdy || mw) return true;
        }
        /* DVD remote: a recent button press. timeElapsed is ms since
         * last IR pulse; treat <1500 ms as "active". */
        uint16_t code = 0, since = 0;
        if (jt_accessory_dvd_remote_state(p, &code, &since)) {
            if (code != 0 && since < 1500) return true;
        }
        /* Steel Battalion: any button in either group. */
        uint32_t sb_a = 0, sb_b = 0;
        int8_t sb_gear = 0;
        if (jt_accessory_steel_battalion_state(p, &sb_a, &sb_b, &sb_gear)) {
            if (sb_a || sb_b) return true;
        }
    }
    return false;
}

void jt_screensaver_tick(float dt)
{
    if (any_user_input()) {
        if (active) {
            active = false;
            wake_pending = true;
        }
        idle_time = 0.0f;
        return;
    }

    idle_time += dt;
    if (!active && idle_time >= IDLE_SECONDS) active = true;

    if (!active) return;

    int w = jt_video_width();
    int h = jt_video_height();
    if (w <= LOGO_W || h <= LOGO_H) return;

    x += vx * dt;
    y += vy * dt;
    if (x < 0)             { x = 0;            vx = -vx; color_idx = (color_idx + 1) % 7; }
    if (x + LOGO_W > w)    { x = w - LOGO_W;   vx = -vx; color_idx = (color_idx + 1) % 7; }
    if (y < 0)             { y = 0;            vy = -vy; color_idx = (color_idx + 1) % 7; }
    if (y + LOGO_H > h)    { y = h - LOGO_H;   vy = -vy; color_idx = (color_idx + 1) % 7; }
}

void jt_screensaver_draw(void)
{
    if (!active) return;
    jt_clear(JT_COL_BLACK);
    jt_blit_mask(logo_mask, LOGO_W, LOGO_H, LOGO_BYTES_PER_ROW,
                 (int)x, (int)y, cycle[color_idx]);
}
