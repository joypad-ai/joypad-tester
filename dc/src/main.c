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
#include <dc/biosfont.h>
#include <arch/timer.h>
#include <string.h>

#include "app.h"
#include "video/mode.h"
#include "ports/ports.h"
#include "input/cursor.h"
#include "ui/options_menu.h"
#include "modes/tester.h"
#include "modes/vmu_editor.h"
#include "modes/browser.h"
#include "modes/about.h"

KOS_INIT_FLAGS(INIT_DEFAULT | INIT_MALLOCSTATS);

jt_mode_id_t jt_current_mode = JT_MODE_TESTER;
static jt_mode_id_t pending_mode = JT_MODE_TESTER;

/* Mode registry. Index matches jt_mode_id_t. New modes append here
 * and to the enum in app.h. */
static const jt_mode_t * const mode_table[JT_MODE_COUNT] = {
    [JT_MODE_TESTER]     = &jt_mode_tester,
    [JT_MODE_VMU_EDITOR] = &jt_mode_vmu_editor,
    [JT_MODE_BROWSER]    = &jt_mode_browser,
    [JT_MODE_ABOUT]      = &jt_mode_about,
};

void jt_request_mode(jt_mode_id_t next)
{
    if (next < 0 || next >= JT_MODE_COUNT) return;
    pending_mode = next;
}

static void clear_framebuffer(void)
{
    /* Solid black background. Called once at boot + once per mode
     * switch. Not every frame — opaque-mode bfont repaints its own
     * background per glyph, and a per-frame memset on a single-
     * buffered framebuffer causes flicker (the clear races the beam). */
    memset(vram_s, 0, 640 * 480 * sizeof(uint16_t));
}

static void apply_mode_switch(void)
{
    if (pending_mode == jt_current_mode) return;
    if (mode_table[jt_current_mode]->leave) {
        mode_table[jt_current_mode]->leave();
    }
    jt_current_mode = pending_mode;
    /* Wipe the framebuffer once on the transition so the new mode
     * doesn't inherit stale text from the old one's layout. */
    clear_framebuffer();
    if (mode_table[jt_current_mode]->enter) {
        mode_table[jt_current_mode]->enter();
    }
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

    /* One-time clear so the boot framebuffer doesn't show garbage.
     * Subsequent frames don't memset — opaque-mode bfont repaints
     * its own glyph backgrounds, and a per-frame full-buffer clear
     * causes single-buffer flicker. */
    clear_framebuffer();

    /* Enter the default mode. */
    if (mode_table[jt_current_mode]->enter) {
        mode_table[jt_current_mode]->enter();
    }

    /* arch_timer_gettime() returns struct timespec since KOS boot.
     * We convert to a float seconds delta for the per-frame update. */
    struct timespec last_ts = arch_timer_gettime();
    bool last_menu_visible = false;

    for (;;) {
        struct timespec now_ts = arch_timer_gettime();
        float dt = (now_ts.tv_sec - last_ts.tv_sec) +
                   (now_ts.tv_nsec - last_ts.tv_nsec) / 1.0e9f;
        if (dt > 0.1f) dt = 0.1f;   /* clamp to avoid huge steps on first frame */
        last_ts = now_ts;

        jt_ports_poll();
        jt_cursor_update(dt);
        jt_options_menu_update(dt);

        if (!jt_options_menu_visible()) {
            mode_table[jt_current_mode]->update(dt);
        }

        /* Apply any mode switch *between* update and draw so the
         * outgoing mode's update completes cleanly. */
        apply_mode_switch();

        /* Wait for vblank BEFORE drawing. With a single-buffered
         * framebuffer (the default for vid_set_mode), writes are
         * visible immediately — drawing during active scan tears.
         * Drawing right after vblank means the beam is in retrace
         * (or hasn't reached our pixels yet) and bfont's small per-
         * glyph writes land before the scan catches up. */
        vid_waitvbl();

        /* When the options menu opens or closes, clear the framebuffer
         * once so we don't see stale pixels from the prior layer. */
        bool menu_now = jt_options_menu_visible();
        if (menu_now != last_menu_visible) {
            clear_framebuffer();
            last_menu_visible = menu_now;
        }

        /* Only one layer redraws per frame. Drawing both the mode and
         * the menu means they race on overlapping pixels in single-
         * buffer mode (menu box overlaps Port B/C rows -> flicker). */
        if (menu_now) {
            jt_options_menu_draw();
        } else {
            mode_table[jt_current_mode]->draw();
        }
    }

    return 0;
}
