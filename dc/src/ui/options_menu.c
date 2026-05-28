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
#include "osk.h"
#include "../app.h"
#include "../ports/ports.h"
#include "../video/mode.h"

/* Menu box geometry. Single source of truth -- both the draw routine
 * and main.c's on-close wipe reference these so the cleared region
 * always matches what was painted (a smaller wipe leaves the menu's
 * border / title / footer behind as artifacts). */
#define MENU_BOX_X 130
#define MENU_BOX_Y 110
#define MENU_BOX_W 380
#define MENU_BOX_H 260

static bool     visible = false;
static int      hover   = 0;
static bool     last_dpad_down = false;
static bool     last_dpad_up   = false;
static bool     last_a         = false;
static bool     last_b         = false;
static bool     last_start     = false;
static bool     last_combo     = false;
/* Set for one frame whenever the menu closes; main.c reads this to
 * skip the mode->update call on that frame, otherwise the confirming
 * Start press leaks into the newly-active mode's input handler. */
static bool     just_closed    = false;
/* Held after the menu closes until the confirm/cancel buttons (A / B /
 * Start) are fully released. main.c suppresses mode input while this is
 * set so the A press that selected a menu item doesn't keep firing into
 * the newly-loaded (or re-selected) mode as a click -- e.g. the File
 * Manager would otherwise open the first file the instant the menu
 * closes. just_closed only covers the single close frame; the user
 * holds A across several frames, so a release-gated lock is needed. */
static bool     post_close_lock = false;
/* Cooldown frames after the menu opens during which confirm/cancel
 * are ignored. Handles cases where the user's Start press for "open"
 * gets briefly re-detected as a confirm (controller bounce, fast
 * re-tap, keyboard auto-repeat, etc). 9 frames ≈ 150ms at 60Hz. */
#define OPEN_COOLDOWN_FRAMES 9
static int      open_cooldown  = 0;

/* Display order matches the jt_mode_id_t enum order. Keep them in
 * sync — selecting menu position N requests jt_mode_id_t(N). */
static const char *mode_names[JT_MODE_COUNT] = {
    [JT_MODE_TESTER]      = "Controller Tester",
    [JT_MODE_BROWSER]     = "VMU File Manager",
    [JT_MODE_VMU_EDITOR]  = "VMU Icon Editor",
    [JT_MODE_LIB_BROWSER] = "VMU Icon Library",
    [JT_MODE_ABOUT]       = "About",
};

void jt_options_menu_init(void)
{
    visible = false;
    hover = (int)jt_current_mode;
}

bool jt_options_menu_visible(void) { return visible; }
bool jt_options_menu_just_closed(void) { return just_closed; }
bool jt_options_menu_input_locked(void) { return post_close_lock; }

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
    bool was_visible = visible;

    /* While the on-screen keyboard is up, Start is the OSK's "Done"
     * key — must not also trigger the options menu. */
    if (jt_osk_visible()) {
        last_combo = false;
        last_dpad_down = last_dpad_up = last_a = last_b = last_start = false;
        just_closed = false;
        return;
    }

    /* Open hotkey: in the Controller Tester, Start alone is a button
     * the user is trying to test, so we require Start+Down to open.
     * In every other mode Start is unused at rest, so Start alone is
     * enough — much cheaper to press, especially with one hand on the
     * mouse. */
    uint32_t open_mask = (jt_current_mode == JT_MODE_TESTER)
                      ? (CONT_START | CONT_DPAD_DOWN)
                      : CONT_START;

    if (!visible) {
        /* Drain the post-close input lock: hold it until the buttons
         * that interact with the menu (A confirm / B cancel / Start
         * opener) are all released, so the press that closed the menu
         * doesn't bleed into the mode as a fresh edge. */
        if (post_close_lock &&
            !any_pad_holds(CONT_A) &&
            !any_pad_holds(CONT_B) &&
            !any_pad_holds(CONT_START)) {
            post_close_lock = false;
        }
        /* Detect press edge of the open combo. */
        bool combo_now = any_pad_holds(open_mask);
        if (combo_now && !last_combo) {
            visible = true;
            hover = (int)jt_current_mode;
            /* Mark Start as already pressed so the open-press doesn't
             * immediately re-fire as a confirm on the next frame.
             * Same for the combo so a continued hold doesn't retoggle. */
            last_start = true;
            open_cooldown = OPEN_COOLDOWN_FRAMES;
        }
        last_combo = combo_now;
        last_dpad_down = last_dpad_up = last_a = last_b = false;
        just_closed = false;
        return;
    }
    if (open_cooldown > 0) open_cooldown--;
    /* While visible, keep last_combo synced so re-pressing the open
     * combo doesn't immediately retrigger an open after a close. */
    last_combo = any_pad_holds(open_mask);

    /* Menu nav. D-pad up/down cycles selection. A or Start confirms
     * the highlighted option (Enter on a keyboard typically maps to
     * Start in Flycast's controller emulation). B cancels and closes
     * without picking. */
    if (any_pad_pressed(CONT_DPAD_DOWN, &last_dpad_down)) {
        hover = (hover + 1) % JT_MODE_COUNT;
    }
    if (any_pad_pressed(CONT_DPAD_UP, &last_dpad_up)) {
        hover = (hover + JT_MODE_COUNT - 1) % JT_MODE_COUNT;
    }
    /* A confirms; B cancels. Start is intentionally NOT a confirm
     * key — it's the opener (alone in non-tester modes, Start+Down
     * in Tester) and conflating opener+confirm caused the menu to
     * "open and immediately close" when bouncing/fast presses
     * tripped the same Start signal twice. */
    bool a_edge = any_pad_pressed(CONT_A, &last_a);
    bool b_edge = any_pad_pressed(CONT_B, &last_b);
    /* Still track Start edges so opener debouncing stays in sync. */
    (void)any_pad_pressed(CONT_START, &last_start);
    if (open_cooldown == 0) {
        if (a_edge) {
            jt_request_mode((jt_mode_id_t)hover);
            visible = false;
        }
        if (b_edge) {
            visible = false;
        }
    }
    just_closed = (was_visible && !visible);
    if (just_closed) post_close_lock = true;
}

/* Direct-VRAM rect fill (same primitive editor/osk use). */
#include <dc/video.h>
static void fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0 || y < 0 || x + w > 640 || y + h > 480) return;
    for (int j = 0; j < h; j++) {
        uint16_t *row = vram_s + (y + j) * 640 + x;
        for (int i = 0; i < w; i++) row[i] = color;
    }
}

void jt_options_menu_draw(void)
{
    if (!visible) return;

    /* Full redraw every frame onto the double-buffered back buffer. */
    const int x = MENU_BOX_X, y = MENU_BOX_Y, w = MENU_BOX_W, h = MENU_BOX_H;

    /* Solid opaque backdrop + 2px yellow border. */
    fill_rect(x, y, w, h, JT_COL_BLACK);
    fill_rect(x, y, w, 2, JT_COL_YELLOW);
    fill_rect(x, y + h - 2, w, 2, JT_COL_YELLOW);
    fill_rect(x, y, 2, h, JT_COL_YELLOW);
    fill_rect(x + w - 2, y, 2, h, JT_COL_YELLOW);

    jt_text_centered(y + 6, JT_COL_YELLOW, JT_COL_BLACK, "-- OPTIONS --");
    for (int i = 0; i < JT_MODE_COUNT; i++) {
        uint16_t fg = (i == hover) ? JT_COL_YELLOW : JT_COL_WHITE;
        const char *marker = (i == hover) ? ">" : " ";
        jt_text(x + 16, y + 40 + i * 32, fg, JT_COL_BLACK,
                "%s %s", marker, mode_names[i]);
    }
    jt_text(x + 16, y + h - 28, JT_COL_GREY, JT_COL_BLACK,
            "A: confirm    B: cancel");
}
