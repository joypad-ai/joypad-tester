/*
 * options_menu.c -- modal mode picker overlay.
 *
 * PORTING.md §3.5 box: solid JT_COL_BLACK fill, 2-px JT_COL_TITLE
 * border, centered against jt_video_width(). Items:
 *
 *     Controller Tester
 *     About
 *
 * Tester first, About last (PORTING.md §3.5 + §4).
 *
 * D-pad up/down moves the cursor; A confirms (jt_request_mode + close);
 * B closes without switching. Open-cooldown holds A/B for 6 frames
 * after open so the same press that opened the menu can't immediately
 * confirm it.
 */
#include "options_menu.h"

#include <stddef.h>
#include <hal/xbox.h>

#include "bfont.h"
#include "colors.h"
#include "../app.h"
#include "../ports/ports.h"

/* Menu layout: the first JT_MODE_COUNT rows are mode switches; the
 * trailing rows are actions (currently just "Return to Dashboard",
 * which calls XLaunchXBE(NULL) -- same path as the in-game-reset
 * combo in tester.c). Keep mode rows first so cursor < JT_MODE_COUNT
 * stays a valid jt_mode_id_t cast. */
#define ACTION_RETURN_DASH (JT_MODE_COUNT + 0)
#define MENU_ITEM_COUNT    (JT_MODE_COUNT + 1)

static bool visible;
static bool just_closed_flag;
static int  cursor;
static int  open_cooldown;
static uint32_t prev_btns_aggregate;

static const char *items[MENU_ITEM_COUNT] = {
    "Controller Tester",
    "About",
    "Return to Dashboard",
};

void jt_options_menu_init(void)
{
    visible = false;
    just_closed_flag = false;
    cursor = 0;
    open_cooldown = 0;
    prev_btns_aggregate = 0;
}

void jt_options_menu_open(void)
{
    if (visible) return;
    visible = true;
    cursor = (int)jt_current_mode;
    open_cooldown = 6;
}

bool jt_options_menu_visible(void)        { return visible; }
bool jt_options_menu_just_closed(void)    { return just_closed_flag; }
bool jt_options_menu_input_locked(void)   { return open_cooldown > 0; }

static uint32_t aggregate_buttons(void)
{
    uint32_t b = 0;
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        if (jt_ports[p].present && jt_ports[p].style == JT_STYLE_PAD)
            b |= jt_ports[p].pad.buttons;
    }
    return b;
}

void jt_options_menu_update(float dt)
{
    (void)dt;
    just_closed_flag = false;
    if (open_cooldown > 0) open_cooldown--;
    if (!visible) {
        prev_btns_aggregate = aggregate_buttons();
        return;
    }

    uint32_t btns  = aggregate_buttons();
    uint32_t edges = btns & ~prev_btns_aggregate;
    prev_btns_aggregate = btns;

    if (edges & JT_BTN_DPAD_UP) {
        cursor = (cursor - 1 + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
    }
    if (edges & JT_BTN_DPAD_DOWN) {
        cursor = (cursor + 1) % MENU_ITEM_COUNT;
    }
    if (open_cooldown == 0 && (edges & JT_BTN_A)) {
        if (cursor == ACTION_RETURN_DASH) {
            /* Same XLaunchXBE(NULL) path as the in-game-reset combo in
             * tester.c -- nxdk persists LDT_LAUNCH_DASHBOARD across the
             * reset so the BIOS hands control back to the dashboard. */
            XLaunchXBE(NULL);
            /* unreached */
        }
        jt_request_mode((jt_mode_id_t)cursor);
        visible = false;
        just_closed_flag = true;
    } else if (open_cooldown == 0 && (edges & JT_BTN_B)) {
        visible = false;
        just_closed_flag = true;
    }
}

void jt_options_menu_draw(void)
{
    if (!visible) return;

    int w = jt_video_width();
    int h = jt_video_height();
    int box_w = 320;
    int row_h = 24;
    int box_h = 32 + MENU_ITEM_COUNT * row_h + 16;
    int x = (w - box_w) / 2;
    int y = (h - box_h) / 2;

    /* 2-px yellow border + black interior. */
    jt_fill_rect(x - 2, y - 2, box_w + 4, box_h + 4, JT_COL_TITLE);
    jt_fill_rect(x, y, box_w, box_h, JT_COL_BLACK);

    jt_text(x + 16, y + 10, JT_COL_TITLE, 0, "Options");

    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        int ry = y + 32 + i * row_h;
        uint32_t fg = (i == cursor) ? JT_COL_HELD : JT_COL_LABEL;
        const char *cursor_glyph = (i == cursor) ? ">" : " ";
        jt_text(x + 16, ry, fg, 0, "%s %s", cursor_glyph, items[i]);
    }
}
