/*
 * options_menu.c — modal mode picker.
 *
 * Triggered by Start+Down on any pad. While visible, captures D-pad
 * input for navigation (A confirms, Start dismisses). Modes register
 * themselves with the registry in main.c; this file just renders the
 * list and asks main.c for a mode switch when the user confirms.
 */
#include <dc/maple/controller.h>

#include "options_menu.h"
#include "bfont_util.h"
#include "../app.h"
#include "../ports/ports.h"
#include "../video/mode.h"

static bool     visible = false;
static int      hover   = 0;
static bool     last_dpad_down = false;
static bool     last_dpad_up   = false;
static bool     last_a         = false;
static bool     last_combo     = false;

static const char *mode_names[JT_MODE_COUNT] = {
    "Controller Tester",
    "VMU Icon Editor",
    "VMU Save Browser",
    "About"
};

void jt_options_menu_init(void)
{
    visible = false;
    hover = (int)jt_current_mode;
}

bool jt_options_menu_visible(void) { return visible; }

static bool any_pad_holds(uint32_t mask)
{
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        if (jt_ports[p].present && jt_ports[p].style == JT_STYLE_PAD &&
            (jt_ports[p].pad.buttons & mask) == mask) {
            return true;
        }
    }
    return false;
}

static bool any_pad_pressed(uint32_t btn, bool *last)
{
    bool now = false;
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        if (jt_ports[p].present && jt_ports[p].style == JT_STYLE_PAD &&
            (jt_ports[p].pad.buttons & btn)) {
            now = true;
            break;
        }
    }
    bool edge = (now && !*last);
    *last = now;
    return edge;
}

void jt_options_menu_update(float dt)
{
    (void)dt;

    /* Hotkey: Start + Down opens it; Start alone (released after
     * combo) closes. */
    bool combo_now = any_pad_holds(CONT_START | CONT_DPAD_DOWN);
    if (combo_now && !last_combo) {
        visible = !visible;
        if (visible) hover = (int)jt_current_mode;
    }
    last_combo = combo_now;

    if (!visible) {
        last_dpad_down = last_dpad_up = last_a = false;
        return;
    }

    if (any_pad_pressed(CONT_DPAD_DOWN, &last_dpad_down)) {
        hover = (hover + 1) % JT_MODE_COUNT;
    }
    if (any_pad_pressed(CONT_DPAD_UP, &last_dpad_up)) {
        hover = (hover + JT_MODE_COUNT - 1) % JT_MODE_COUNT;
    }
    if (any_pad_pressed(CONT_A, &last_a)) {
        jt_request_mode((jt_mode_id_t)hover);
        visible = false;
    }
}

void jt_options_menu_draw(void)
{
    if (!visible) return;

    /* Hand-drawn box. 380x220 centered. We're not dimming the
     * underlying view here (would need a framebuffer-blend pass);
     * relying on the box outline + filled background instead. */
    const int x = 130, y = 130, h = 220;
    /* Border + body via repeated text. Replacing with a real PVR
     * quad path is a v0.2 polish item; bfont fills are enough to
     * land the scaffold. */
    jt_text(x, y, JT_COL_YELLOW, JT_COL_BLACK,
            "+---- OPTIONS ----------------+");
    for (int i = 0; i < JT_MODE_COUNT; i++) {
        uint16_t fg = (i == hover) ? JT_COL_YELLOW : JT_COL_WHITE;
        const char *marker = (i == hover) ? ">" : " ";
        jt_text(x + 16, y + 40 + i * 32, fg, JT_COL_BLACK,
                "%s %s", marker, mode_names[i]);
    }
    jt_text(x + 16, y + 40 + JT_MODE_COUNT * 32 + 16, JT_COL_GREY, JT_COL_BLACK,
            "Video: %s", jt_cable_name(jt_video_cable()));
    jt_text(x + 16, y + h - 32, JT_COL_GREY, JT_COL_BLACK,
            "[Start+Down] close");
}
