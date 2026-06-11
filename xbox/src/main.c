/*
 * main.c -- mode dispatcher for the OG Xbox tester.
 *
 * No pbkit (we don't need accelerated drawing for a text UI); just
 * the nxdk hal video layer + XVideoGetFB-based direct framebuffer
 * writes via ui/bfont.c. Vsync via XVideoWaitForVBlank.
 *
 * Loop order matches dc/'s main.c:
 *   ports_poll -> screensaver_tick -> options_menu_update ->
 *   (skip mode update if menu is visible / just_closed /
 *    input-locked / screensaver active) ->
 *   apply_mode_switch -> clear back buffer + draw -> vblank.
 */
#include <hal/video.h>
#include <hal/debug.h>
#include <hal/xbox.h>
#include <SDL.h>
#include <windows.h>

#include "app.h"
#include "ports/ports.h"
#include "ports/accessories.h"
#include "ui/bfont.h"
#include "ui/colors.h"
#include "ui/options_menu.h"
#include "ui/screensaver.h"
#include "modes/tester.h"
#include "modes/about.h"

jt_mode_id_t jt_current_mode = JT_MODE_TESTER;
static jt_mode_id_t pending_mode = JT_MODE_TESTER;

void jt_request_mode(jt_mode_id_t id)
{
    if (id < 0 || id >= JT_MODE_COUNT) return;
    pending_mode = id;
}

static const jt_mode_t *mode_table[JT_MODE_COUNT];

static void apply_mode_switch(void)
{
    if (pending_mode == jt_current_mode) return;
    if (mode_table[jt_current_mode]->leave)
        mode_table[jt_current_mode]->leave();
    jt_current_mode = pending_mode;
    if (mode_table[jt_current_mode]->enter)
        mode_table[jt_current_mode]->enter();
}

static void wait_then_reboot(void)
{
    Sleep(5000);
    XReboot();
}

int main(void)
{
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);

    /* Register USB Mass Storage + Audio Class drivers BEFORE SDL_Init
     * brings up the USB stack. SDL's joystick init does a 500ms
     * enumeration warmup, and we want all class drivers in place
     * during that warmup so pre-attached MUs and headsets find their
     * handler -- otherwise they only show up after a hot-plug. */
    jt_accessories_register_class_drivers();

    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        debugPrint("SDL_Init failed: %s\n", SDL_GetError());
        wait_then_reboot();
        return 1;
    }

    mode_table[JT_MODE_TESTER] = &jt_mode_tester;
    mode_table[JT_MODE_ABOUT]  = &jt_mode_about;

    /* Double-buffer init (allocates the second framebuffer). On
     * failure we silently fall through to single-buffer mode -- the
     * tester still renders, just with tearing visible. */
    jt_video_init();
    jt_ports_init();
    jt_options_menu_init();
    jt_screensaver_init();
    if (mode_table[jt_current_mode]->enter)
        mode_table[jt_current_mode]->enter();

    DWORD last_ticks = GetTickCount();

    for (;;) {
        DWORD now = GetTickCount();
        float dt = (now - last_ticks) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        last_ticks = now;

        jt_video_refresh();
        jt_accessories_tick();
        jt_ports_poll();
        jt_screensaver_tick(dt);
        jt_options_menu_update(dt);

        bool skip_mode = jt_options_menu_visible() ||
                         jt_options_menu_just_closed() ||
                         jt_options_menu_input_locked() ||
                         jt_screensaver_active() ||
                         jt_screensaver_consume_wake();
        if (!skip_mode) {
            mode_table[jt_current_mode]->update(dt);
        }

        apply_mode_switch();

        /* Render into the back buffer, then present (vsync + flip).
         * Double-buffered draws are tearing-free regardless of how
         * long the draw takes, so we don't need the §3.8 single-
         * buffer mitigations (opaque-overpaint, before-vsync, etc.). */
        jt_clear(JT_COL_BLACK);
        if (jt_screensaver_active()) {
            jt_screensaver_draw();
        } else {
            mode_table[jt_current_mode]->draw();
            if (jt_options_menu_visible()) jt_options_menu_draw();
        }
        jt_video_present();
    }

    /* Unreached. */
    SDL_Quit();
    return 0;
}
