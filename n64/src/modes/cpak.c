/*
 * cpak.c — Controller Pak note browser.
 *
 * When a port has a Controller Pak attached, runs libdragon's
 * validate_mempak + iterates get_mempak_entry across the 16 note
 * slots. Renders the note list (vendor / game_id / region / blocks /
 * name) so users can see whether the pak is intact and what saves it
 * holds. Cached per port -- re-reads only when an accessory swap is
 * detected.
 *
 * Renders via ui/text helpers (matches the rest of the modes for
 * consistent title colour, centring, and footer placement).
 */

#include "cpak.h"
#include "../ui/text.h"

#include <libdragon.h>
#include <string.h>
#include <stdio.h>

#define CPAK_MAX_ENTRIES 16

typedef struct {
    bool                 attempted;
    bool                 valid;
    int                  count;
    entry_structure_t    notes[CPAK_MAX_ENTRIES];
} cpak_cache_t;

static cpak_cache_t cache[JOYBUS_PORT_COUNT];
static joypad_accessory_type_t prev_acc[JOYBUS_PORT_COUNT];

static void cpak_read(int port)
{
    cache[port].attempted = true;
    cache[port].valid     = false;
    cache[port].count     = 0;

    if (validate_mempak(port) != 0) {
        return;
    }
    cache[port].valid = true;

    for (int e = 0; e < CPAK_MAX_ENTRIES; e++) {
        entry_structure_t ent = {0};
        if (get_mempak_entry(port, e, &ent) == 0 && ent.valid) {
            cache[port].notes[cache[port].count++] = ent;
        }
    }
}

static void cpak_update(void)
{
    JOYPAD_PORT_FOREACH (port) {
        joypad_accessory_type_t acc = joypad_get_accessory_type(port);
        if (acc != prev_acc[port]) {
            memset(&cache[port], 0, sizeof(cache[port]));
        }
        prev_acc[port] = acc;
        if (acc == JOYPAD_ACCESSORY_TYPE_CONTROLLER_PAK
            && !cache[port].attempted) {
            cpak_read(port);
        }
    }
}

static void cpak_draw(void)
{
    surface_t *surf = display_get();
    graphics_fill_screen(surf, graphics_make_color(0, 0, 0, 0xff));

    int screen_w = surf->width  ? surf->width  : 320;
    int screen_h = surf->height ? surf->height : 240;
    int y = 12;

    /* Title: same yellow + centred as every other page. */
    txt_draw_centered(surf, y, JT_COL_TITLE, "Controller Pak Browser", screen_w);
    y += 24;

    /* Content table -- left edge picks the widest line and centers
     * around that so per-port lists line up even with short text. */
    const int CONTENT_W = 40 * TXT_GLYPH_W;
    int x = (screen_w - CONTENT_W) / 2;
    if (x < 16) x = 16;

    bool any = false;
    JOYPAD_PORT_FOREACH (port) {
        if (joypad_get_accessory_type(port) != JOYPAD_ACCESSORY_TYPE_CONTROLLER_PAK) {
            continue;
        }
        any = true;
        char header[64];
        if (!cache[port].attempted) {
            snprintf(header, sizeof(header), "Port %d  (reading...)", port + 1);
            txt_draw(surf, x, y, JT_COL_LABEL, header);
            y += TXT_GLYPH_H + 6;
            continue;
        }
        if (!cache[port].valid) {
            snprintf(header, sizeof(header),
                     "Port %d  (pak header invalid -- needs format)",
                     port + 1);
            txt_draw(surf, x, y, JT_COL_ERROR, header);
            y += TXT_GLYPH_H + 6;
            continue;
        }
        snprintf(header, sizeof(header), "Port %d  %d notes",
                 port + 1, cache[port].count);
        txt_draw(surf, x, y, JT_COL_LABEL, header);
        y += TXT_GLYPH_H + 4;
        for (int i = 0; i < cache[port].count; i++) {
            const entry_structure_t *n = &cache[port].notes[i];
            char clean[19];
            int  j;
            for (j = 0; j < 18 && n->name[j]; j++) {
                clean[j] = (n->name[j] >= 0x20 && n->name[j] < 0x7f)
                           ? n->name[j] : '?';
            }
            clean[j] = '\0';
            char line[64];
            snprintf(line, sizeof(line),
                     "  %2d: %-18s  blk:%2u  reg:%c  game:%04x",
                     i + 1, clean, n->blocks, n->region, n->game_id);
            txt_draw(surf, x, y, JT_COL_VALUE, line);
            y += TXT_GLYPH_H + 2;
        }
        y += 6;
    }
    if (!any) {
        txt_draw_centered(surf, y, JT_COL_DIM,
                          "No Controller Pak detected on any port.",
                          screen_w);
        y += TXT_GLYPH_H + 6;
        txt_draw_centered(surf, y, JT_COL_DIM,
                          "Insert one and the contents will load.",
                          screen_w);
    }

    /* Footer hint: shared helper places it inside overscan. */
    (void)screen_h;
    txt_draw_footer(surf, "Start: options menu");

    display_show(surf);
}

const jt_mode_t jt_mode_cpak = {
    .name   = "CPak",
    .enter  = NULL,
    .leave  = NULL,
    .update = cpak_update,
    .draw   = cpak_draw,
};
