/*
 * main.c — Joypad Tester Dreamcast entry point.
 *
 * Boot order:
 *   1. KOS init (video, maple, fs_romdisk).
 *   2. jt_video_init() — cable detect + mode set (VGA first).
 *   3. jt_ports_init() / jt_cursor_init().
 *   4. Frame loop:
 *      a. ports_poll() snapshots maple state.
 *      b. cursor_update() integrates pad/mouse motion.
 *      c. options_menu_update() (Start+Down toggles).
 *      d. current mode's update().
 *      e. clear framebuffer + current mode's draw() + menu overlay.
 *      f. vsync.
 *
 * KOS_INIT_FLAGS macro is the canonical KOS pattern for declaring
 * which subsystems get auto-initialized at runtime entry.
 */
#include <kos.h>
#include <dc/video.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/biosfont.h>
#include <arch/arch.h>
#include <arch/timer.h>
#include <string.h>

#include "app.h"
#include "video/mode.h"
#include "ports/ports.h"
#include "input/cursor.h"
#include "ui/options_menu.h"
#include "ui/screensaver.h"
#include "modes/tester.h"
#include "modes/vmu_editor.h"
#include "modes/browser.h"
#include "modes/lib_browser.h"
#include "modes/about.h"

KOS_INIT_FLAGS(INIT_DEFAULT | INIT_MALLOCSTATS);

jt_mode_id_t jt_current_mode = JT_MODE_TESTER;
static jt_mode_id_t pending_mode = JT_MODE_TESTER;

/* Mode registry. Index matches jt_mode_id_t. New modes append here
 * and to the enum in app.h. */
static const jt_mode_t * const mode_table[JT_MODE_COUNT] = {
    [JT_MODE_TESTER]      = &jt_mode_tester,
    [JT_MODE_VMU_EDITOR]  = &jt_mode_vmu_editor,
    [JT_MODE_BROWSER]     = &jt_mode_browser,
    [JT_MODE_LIB_BROWSER] = &jt_mode_lib_browser,
    [JT_MODE_ABOUT]       = &jt_mode_about,
};

void jt_request_mode(jt_mode_id_t next)
{
    if (next < 0 || next >= JT_MODE_COUNT) return;
    pending_mode = next;
}

static void clear_framebuffer(void)
{
    /* Clear the current (hidden) back buffer to black. Called every
     * frame before the full redraw -- with double buffering the back
     * buffer holds a stale frame, so it must be wiped. No beam race:
     * the buffer being cleared is not the one on screen. */
    memset(vram_s, 0, 640 * 480 * sizeof(uint16_t));
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

/* Dreamcast soft-reset combo: Start + A + B + X + Y on any controller.
 * Returns true while the full combo is held on at least one pad. The
 * caller debounces with a hold timer so a momentary all-buttons press
 * (expected on a controller tester) doesn't reset out from under you. */
static bool reset_combo_held(void)
{
    const uint32_t combo = CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y;
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        if (jt_ports[p].present && jt_ports[p].style == JT_STYLE_PAD &&
            (jt_ports[p].pad.buttons & combo) == combo)
            return true;
    }
    return false;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    /* KOS init runs before main() via KOS_INIT_FLAGS; explicit
     * subsystem inits beyond that go here. */
    jt_video_init();
    jt_ports_init();
    jt_cursor_init(320, 240);
    jt_options_menu_init();
    jt_screensaver_init();

    /* Clear the boot buffer so the first visible frame isn't garbage.
     * The render loop clears every frame from here on. */
    clear_framebuffer();

    /* Enter the default mode. */
    if (mode_table[jt_current_mode]->enter) {
        mode_table[jt_current_mode]->enter();
    }

    /* arch_timer_gettime() returns struct timespec since KOS boot.
     * We convert to a float seconds delta for the per-frame update. */
    struct timespec last_ts = arch_timer_gettime();
    int reset_hold = 0;   /* frames the Start+ABXY combo has been held */

    for (;;) {
        struct timespec now_ts = arch_timer_gettime();
        float dt = (now_ts.tv_sec - last_ts.tv_sec) +
                   (now_ts.tv_nsec - last_ts.tv_nsec) / 1.0e9f;
        if (dt > 0.1f) dt = 0.1f;   /* clamp to avoid huge steps on first frame */
        last_ts = now_ts;

        jt_ports_poll();

        /* Standard DC soft reset: hold Start+A+B+X+Y ~1s -> BIOS menu. */
        if (reset_combo_held()) {
            if (++reset_hold >= 60) arch_menu();
        } else {
            reset_hold = 0;
        }
        jt_cursor_update(dt);
        jt_screensaver_tick(dt);
        jt_options_menu_update(dt);

        /* Skip mode-update when the menu is visible OR when it just
         * closed this frame, or while the post-close input lock holds.
         * Otherwise the confirming A/Start press would leak through into
         * the newly-active mode's input handler. */
        if (!jt_options_menu_visible() && !jt_options_menu_just_closed()
            && !jt_options_menu_input_locked()
            && !jt_screensaver_active()) {
            mode_table[jt_current_mode]->update(dt);
        }

        /* Apply any mode switch *between* update and draw so the
         * outgoing mode's update completes cleanly. */
        apply_mode_switch();

        /* Double-buffered render: every frame draws a FULL screen to the
         * hidden back buffer, then vid_flip() swaps it in at vblank.
         * vram_s already points at the back buffer (vid_flip repointed
         * it last frame). Clear it first since it holds a stale frame. */
        clear_framebuffer();

        if (jt_screensaver_active()) {
            jt_screensaver_draw();
        } else {
            mode_table[jt_current_mode]->draw();
            /* The options menu is a real overlay now -- draw it on top
             * of the underlying mode in the same frame. */
            if (jt_options_menu_visible()) {
                jt_options_menu_draw();
            }
        }

        /* Display this buffer at the next vblank and repoint vram_s at
         * the next hidden buffer. vid_waitvbl paces to the refresh and
         * ensures the flip latched before we draw the next frame. */
        vid_flip(-1);
        vid_waitvbl();
    }

    return 0;
}
