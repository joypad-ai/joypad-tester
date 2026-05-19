/*
 * options_menu.c — modal screen picker, dc/-style.
 *
 * Visual: solid black box with a 2-px yellow border, centered. Yellow
 * `-- OPTIONS --` title bar inside the box, then one row per screen
 * (yellow with a `>` marker on the hovered row, white otherwise),
 * then a grey `A: confirm    B: cancel` footer hint.
 *
 * Open hotkey:
 *   - Tester mode: Start + D-Pad Down (Start alone is a controller
 *     input you'd want to test).
 *   - Any other mode: Start alone.
 * Inside the menu: D-pad navigates, A confirms, B cancels.
 *
 * Matches the dc/src/ui/options_menu.c implementation including the
 * open-cooldown trick that prevents the opening Start press from
 * leaking into the menu's input handler as a confirm.
 */

#include "options_menu.h"
#include "text.h"
#include "../app.h"

#include <libdragon.h>
#include <string.h>

#define MENU_Y     32
#define MENU_W     280
/* Box height is content-derived so the footer hugs the last row
 * instead of floating in ~60px of dead space. Breakdown:
 *   28 -> top border + title row + gap to first item
 *   16 * JT_MODE_COUNT -> mode rows
 *   24 -> gap + footer row + bottom padding/border
 * If the mode list grows, the box auto-sizes. */
#define MENU_H     (28 + JT_MODE_COUNT * 16 + 24)
#define BORDER_PX  2

#define COL_BG        0x000000ff
#define COL_BORDER    0xfff040ff   /* yellow */
#define COL_TITLE     0xfff040ff
#define COL_HOVER     0xfff040ff
#define COL_ITEM      0xffffffff
#define COL_FOOTER    0x808080ff   /* grey */

#define OPEN_COOLDOWN_FRAMES 9

static bool visible    = false;
static int  hover      = 0;
static bool last_combo = false;
static bool last_down  = false;
static bool last_up    = false;
static bool last_a     = false;
static bool last_b     = false;
static int  open_cooldown = 0;

static const char *screen_names[JT_MODE_COUNT] = {
    [JT_MODE_TESTER] = "Controller Tester",
    [JT_MODE_CPAK]   = "Controller Pak Browser",
    [JT_MODE_GBC]    = "Game Boy Camera Viewer",
    [JT_MODE_SNAP]   = "Snap Station Test",
    [JT_MODE_ABOUT]  = "About",
};

void jt_options_menu_init(void)
{
    visible    = false;
    hover      = (int)jt_current_mode;
    last_combo = false;
    last_down  = false;
    last_up    = false;
    last_a     = false;
    last_b     = false;
    open_cooldown = 0;
}

bool jt_options_menu_visible(void) { return visible; }

/* True iff any pad currently HOLDS the given combination. */
static bool any_holds_start_down(void)
{
    JOYPAD_PORT_FOREACH (port) {
        joypad_buttons_t h = joypad_get_buttons_held(port);
        if (h.start && h.d_down) return true;
    }
    return false;
}

static bool any_holds_start(void)
{
    JOYPAD_PORT_FOREACH (port) {
        if (joypad_get_buttons_held(port).start) return true;
    }
    return false;
}

static bool any_pressed_edge(bool *last, bool now)
{
    bool edge = now && !*last;
    *last = now;
    return edge;
}

static bool any_holds_d_down(void)
{
    JOYPAD_PORT_FOREACH (port) {
        if (joypad_get_buttons_held(port).d_down) return true;
    }
    return false;
}
static bool any_holds_d_up(void)
{
    JOYPAD_PORT_FOREACH (port) {
        if (joypad_get_buttons_held(port).d_up) return true;
    }
    return false;
}
static bool any_holds_a(void)
{
    JOYPAD_PORT_FOREACH (port) {
        if (joypad_get_buttons_held(port).a) return true;
    }
    return false;
}
static bool any_holds_b(void)
{
    JOYPAD_PORT_FOREACH (port) {
        if (joypad_get_buttons_held(port).b) return true;
    }
    return false;
}

void jt_options_menu_update(void)
{
    /* Open combo depends on the active screen. Tester requires
     * Start+Down because Start alone is a button under test; every
     * other screen treats bare Start as the open trigger. */
    bool open_now = (jt_current_mode == JT_MODE_TESTER)
                    ? any_holds_start_down()
                    : (any_holds_start() && !any_holds_d_down());

    if (!visible) {
        if (open_now && !last_combo) {
            visible = true;
            hover = (int)jt_current_mode;
            open_cooldown = OPEN_COOLDOWN_FRAMES;
            /* Pre-arm the edge-detect state so the opening Start /
             * D-down doesn't immediately re-fire as a confirm or
             * navigation in this same frame's body. */
            last_a    = any_holds_a();
            last_b    = any_holds_b();
            last_down = any_holds_d_down();
            last_up   = any_holds_d_up();
        }
        last_combo = open_now;
        return;
    }
    if (open_cooldown > 0) open_cooldown--;
    last_combo = open_now;

    /* Nav. */
    if (any_pressed_edge(&last_down, any_holds_d_down()))
        hover = (hover + 1) % JT_MODE_COUNT;
    if (any_pressed_edge(&last_up, any_holds_d_up()))
        hover = (hover + JT_MODE_COUNT - 1) % JT_MODE_COUNT;

    if (open_cooldown == 0) {
        if (any_pressed_edge(&last_a, any_holds_a())) {
            jt_request_mode((jt_mode_id_t)hover);
            visible = false;
            return;
        }
        if (any_pressed_edge(&last_b, any_holds_b())) {
            visible = false;
            return;
        }
    } else {
        /* During cooldown, keep edge-state in sync so we don't
         * register a confirm/cancel the instant the cooldown ends. */
        last_a = any_holds_a();
        last_b = any_holds_b();
    }
}

static void fill_rect(surface_t *surf, int x, int y, int w, int h, uint32_t rgba)
{
    uint8_t r = (rgba >> 24) & 0xff;
    uint8_t g = (rgba >> 16) & 0xff;
    uint8_t b = (rgba >>  8) & 0xff;
    uint8_t a = (rgba      ) & 0xff;
    graphics_draw_box(surf, x, y, w, h, graphics_make_color(r, g, b, a));
}

void jt_options_menu_draw(void)
{
    if (!visible) return;
    surface_t *surf = display_get();

    /* Centre the menu against the actual surface width rather than
     * assuming 320 -- some libdragon display modes return a wider
     * buffer, and a hardcoded 320 reference makes the menu drift
     * left in those modes. */
    int screen_w = surf->width ? surf->width : 320;
    int menu_x   = (screen_w - MENU_W) / 2;

    /* Solid backdrop. Don't full-clear -- preserve whatever the
     * underlying mode drew so the menu reads as an overlay. */
    fill_rect(surf, menu_x, MENU_Y, MENU_W, MENU_H, COL_BG);
    /* 2-px yellow border. */
    fill_rect(surf, menu_x,                  MENU_Y,                      MENU_W,    BORDER_PX, COL_BORDER);
    fill_rect(surf, menu_x,                  MENU_Y + MENU_H - BORDER_PX, MENU_W,    BORDER_PX, COL_BORDER);
    fill_rect(surf, menu_x,                  MENU_Y,                      BORDER_PX, MENU_H,    COL_BORDER);
    fill_rect(surf, menu_x + MENU_W - BORDER_PX, MENU_Y,                  BORDER_PX, MENU_H,    COL_BORDER);

    /* Title centred against the actual screen, not the menu box, so
     * it lands on the box's centerline regardless of menu offset. */
    txt_draw_centered(surf, MENU_Y + 8, COL_TITLE, "-- OPTIONS --", screen_w);

    /* Mode rows. */
    int row_y = MENU_Y + 28;
    for (int i = 0; i < JT_MODE_COUNT; i++) {
        bool hov = (i == hover);
        const char *marker = hov ? ">" : " ";
        uint32_t fg = hov ? COL_HOVER : COL_ITEM;
        char line[40];
        snprintf(line, sizeof(line), "%s %s", marker, screen_names[i]);
        txt_draw(surf, menu_x + 16, row_y, fg, line);
        row_y += 16;
    }

    /* Footer hint inside the box. */
    txt_draw(surf, menu_x + 16, MENU_Y + MENU_H - 16, COL_FOOTER,
             "A: confirm    B: cancel");

    display_show(surf);
}
