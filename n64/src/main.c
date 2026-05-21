/*
 * main.c — Joypad Tester N64 entry point + mode dispatcher.
 *
 * Boot order:
 *   1. timer + display + joypad init.
 *   2. console (text-mode renderer used by all current modes).
 *   3. Options menu init.
 *   4. Active mode's enter() called.
 *   5. Frame loop:
 *      a. joypad_poll().
 *      b. screensaver_idle_tick().
 *      c. jt_options_menu_update() — Start+D-pad-Down toggles.
 *      d. apply_mode_switch() if the menu confirmed a new mode.
 *      e. If screensaver active, screensaver_render().
 *         Else if menu visible, jt_options_menu_draw().
 *         Else current mode's update() + draw().
 *
 * Modes live in src/modes/<name>.{c,h} and register a const jt_mode_t
 * entry in mode_table[] below. See src/app.h for the contract and
 * src/ui/options_menu.{c,h} for the picker.
 *
 * Copyright (c) 2026 Robert Dale Smith
 * MIT License -- see ../LICENSE.md
 */

#include <libdragon.h>

#include "app.h"
#include "screensaver.h"
#include "modes/tester.h"
#include "ui/options_menu.h"
#include "modes/about.h"
#include "modes/cpak.h"
#include "modes/gbc.h"
#include "modes/snap.h"
#include "modes/tester.h"

jt_mode_id_t        jt_current_mode = JT_MODE_TESTER;
static jt_mode_id_t pending_mode    = JT_MODE_TESTER;

static const jt_mode_t * const mode_table[JT_MODE_COUNT] = {
    [JT_MODE_TESTER] = &jt_mode_tester,
    [JT_MODE_CPAK]   = &jt_mode_cpak,
    [JT_MODE_GBC]    = &jt_mode_gbc,
    [JT_MODE_SNAP]   = &jt_mode_snap,
    [JT_MODE_ABOUT]  = &jt_mode_about,
};

void jt_request_mode(jt_mode_id_t next)
{
    if (next < 0 || next >= JT_MODE_COUNT) return;
    pending_mode = next;
}

bool jt_any_input_this_frame(void)
{
    JOYPAD_PORT_FOREACH (port) {
        joypad_buttons_t held = joypad_get_buttons_held(port);
        if (held.a || held.b || held.x || held.y || held.l || held.r ||
            held.z || held.start ||
            held.d_up || held.d_down || held.d_left || held.d_right ||
            held.c_up || held.c_down || held.c_left || held.c_right) {
            return true;
        }
        joypad_inputs_t in = joypad_get_inputs(port);
        if (joypad_get_style(port) == JOYPAD_STYLE_MOUSE) {
            /* Mouse: stick_x/stick_y are per-frame deltas (signed),
             * not analog deflection. Any non-zero delta = motion. */
            if (in.stick_x != 0 || in.stick_y != 0) return true;
        } else {
            /* Analog stick / trigger deadzone -- N64 stick centres
             * around 0 with ~80 max so 8 comfortably ignores noise. */
            const int DEAD = 8;
            if (in.stick_x  > DEAD || in.stick_x  < -DEAD) return true;
            if (in.stick_y  > DEAD || in.stick_y  < -DEAD) return true;
            if (in.cstick_x > DEAD || in.cstick_x < -DEAD) return true;
            if (in.cstick_y > DEAD || in.cstick_y < -DEAD) return true;
            if (in.analog_l > DEAD || in.analog_r > DEAD)  return true;
        }
    }
    /* GBA-via-link-cable presses don't surface through libdragon's
     * joypad inputs; the tester module owns that polling, so ask it. */
    if (jt_tester_gba_input_active()) return true;
    if (jt_tester_kbd_input_active()) return true;
    return false;
}

static void apply_mode_switch(void)
{
    if (pending_mode == jt_current_mode) return;
    if (mode_table[jt_current_mode]->leave) {
        mode_table[jt_current_mode]->leave();
    }
    jt_current_mode = pending_mode;
    if (mode_table[jt_current_mode]->enter) {
        mode_table[jt_current_mode]->enter();
    }
}

int main(void)
{
    timer_init();
    /* 320x240 is N64's standard non-interlaced framebuffer. Each fb
     * pixel = 2 TV dots horizontally (anamorphic); screensaver
     * compensates by 2x-scaling its sprite at render time. */
    display_init(RESOLUTION_320x240, DEPTH_32_BPP, 2, GAMMA_NONE,
                 FILTERS_RESAMPLE);
    /* display_init starts the video signal immediately, but the
     * framebuffers contain uninitialised VRAM (which renders as a red
     * flash on the N64's VI output). Paint both buffers black before
     * the rest of init runs so nothing leaks on cold boot. */
    for (int i = 0; i < 2; i++) {
        surface_t *s = display_get();
        graphics_fill_screen(s, graphics_make_color(0, 0, 0, 0xff));
        display_show(s);
    }
    joypad_init();
    console_init();
    console_set_render_mode(RENDER_MANUAL);
    console_set_debug(false);
    jt_options_menu_init();

    if (mode_table[jt_current_mode]->enter) {
        mode_table[jt_current_mode]->enter();
    }

    while (1) {
        joypad_poll();
        /* Scan the RandNet keyboard every frame, before the idle check
         * and regardless of mode/screensaver, so typing both prevents
         * the screensaver and wakes it. */
        jt_tester_poll_keyboard();
        screensaver_idle_tick(jt_any_input_this_frame());
        jt_options_menu_update();
        apply_mode_switch();

        if (screensaver_active()) {
            screensaver_render();
        } else if (jt_options_menu_visible()) {
            jt_options_menu_draw();
        } else {
            if (mode_table[jt_current_mode]->update) {
                mode_table[jt_current_mode]->update();
            }
            if (mode_table[jt_current_mode]->draw) {
                mode_table[jt_current_mode]->draw();
            }
        }
    }
}
