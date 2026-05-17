/*
 * browser.c — save browser mode.
 *
 * Aggregates saves across all connected VMUs into a single scrollable
 * list. For each save we read enough bytes (header + first icon
 * frame) to decode a thumbnail, then render the list with thumbnails
 * inline. Selecting a save offers Extract -> Editor, Extract ->
 * Library, or Apply -> ICONDATA_VMS as actions.
 *
 * Read-only on game saves: the source save is never written to. The
 * Apply target may be the source VMU or a different one (port pick).
 */
#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/vmu.h>
#include <dc/video.h>
#include <dc/vmufs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "browser.h"
#include "../app.h"
#include "../ui/bfont_util.h"
#include "../vms/vms.h"
#include "../vms/apply.h"
#include "../library/library.h"
#include "../ports/ports.h"

#define MAX_ENTRIES 64
#define THUMB_PX    32

typedef struct {
    int      port;
    int      slot;
    char     filename[13];   /* 12 + NUL */
    int      blocks;         /* file size in VMU blocks */
    jt_icon_t icon;          /* decoded frame 0 + palette */
    bool     icon_valid;
} browser_entry_t;

static browser_entry_t entries[MAX_ENTRIES];
static int entry_count = 0;
static int selected = 0;
static int scroll_top = 0;
static bool needs_refresh = true;

/* Last button state for edge detection. */
static uint32_t last_btns = 0;

/* Editor-side hook: defined here for now, the editor mode imports it
 * via jt_browser_push_to_editor. The actual handoff uses a globally
 * shared canvas in canvas.c via the editor; we just expose the entry
 * the user picked. */
static jt_icon_t pending_to_editor;
static bool      pending_pickup = false;

void jt_browser_push_to_editor(const jt_icon_t *icon)
{
    if (!icon) return;
    memcpy(&pending_to_editor, icon, sizeof(pending_to_editor));
    pending_pickup = true;
}

/* Called by the editor mode in its enter() so it can pull a freshly-
 * extracted icon. Returns true if there was one ready, then clears. */
bool jt_browser_consume_pending(jt_icon_t *out)
{
    if (!pending_pickup) return false;
    *out = pending_to_editor;
    pending_pickup = false;
    return true;
}

static void browser_enter(void)
{
    needs_refresh = true;
    selected = 0;
    scroll_top = 0;
}

static void browser_leave(void) { /* nothing */ }

/* (Re-)read the save list from every detected VMU. Slow op — minute
 * worth of maple reads possible if many VMUs each have many saves.
 * Triggered on mode enter and on user demand (X). */
static void refresh_entries(void)
{
    entry_count = 0;
    for (int p = 0; p < JT_NUM_PORTS && entry_count < MAX_ENTRIES; p++) {
        for (int s = 0; s < JT_NUM_SLOTS && entry_count < MAX_ENTRIES; s++) {
            if (jt_ports[p].slots[s].kind != JT_SLOT_VMU) continue;
            maple_device_t *dev = maple_enum_dev(p, s + 1);
            if (!dev || !dev->valid) continue;

            vmu_dir_t *dirents = NULL;
            int        dcount = 0;
            if (vmufs_readdir(dev, &dirents, &dcount) != 0) continue;

            for (int i = 0; i < dcount && entry_count < MAX_ENTRIES; i++) {
                if (dirents[i].filetype == 0) continue;
                browser_entry_t *e = &entries[entry_count++];
                e->port = p;
                e->slot = s;
                memcpy(e->filename, dirents[i].filename, 12);
                e->filename[12] = '\0';
                e->blocks = dirents[i].filesize;
                e->icon_valid = false;

                /* Decode a thumbnail by reading just enough for the
                 * header + 1 icon frame (0x80 + 512 = 0x280 = 640
                 * bytes). vmufs_read_dirent doesn't let us partial-
                 * read, so we pull the whole file. Worth it for the
                 * preview UX; refresh is gated to mode enter / X. */
                void *raw = NULL;
                int   sz = 0;
                if (vmufs_read_dirent(dev, &dirents[i], &raw, &sz) == 0 && raw) {
                    /* ICONDATA_VMS lives outside the regular game-save
                     * format -- decode via the icondata path. */
                    if (memcmp(e->filename, "ICONDATA_VMS", 12) == 0) {
                        e->icon_valid = jt_vms_decode_icondata(raw, sz, &e->icon);
                    } else {
                        e->icon_valid = jt_vms_extract_save_icon(raw, sz, 0, &e->icon);
                    }
                    free(raw);
                }
            }
            free(dirents);
        }
    }
    needs_refresh = false;
}

static uint32_t aggregate_pad_buttons(void)
{
    uint32_t btns = 0;
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        if (jt_ports[p].present && jt_ports[p].style == JT_STYLE_PAD) {
            btns |= jt_ports[p].pad.buttons;
        }
    }
    return btns;
}

static void perform_apply(int target_port, int target_slot)
{
    if (entry_count == 0 || selected < 0 || selected >= entry_count) return;
    browser_entry_t *e = &entries[selected];
    if (!e->icon_valid) return;
    /* Best-effort apply; result printed in the status line. */
    (void)jt_apply_icondata(&e->icon, target_port, target_slot);
}

static void perform_extract_to_editor(void)
{
    if (entry_count == 0 || selected < 0 || selected >= entry_count) return;
    browser_entry_t *e = &entries[selected];
    if (!e->icon_valid) return;
    jt_browser_push_to_editor(&e->icon);
    jt_request_mode(JT_MODE_VMU_EDITOR);
}

static void browser_update(float dt)
{
    (void)dt;
    if (needs_refresh) refresh_entries();

    uint32_t btns = aggregate_pad_buttons();
    uint32_t edges = btns & ~last_btns;

    if (edges & CONT_DPAD_UP)   if (selected > 0) selected--;
    if (edges & CONT_DPAD_DOWN) if (selected < entry_count - 1) selected++;
    if (edges & CONT_X)         needs_refresh = true;
    if (edges & CONT_A)         perform_extract_to_editor();
    if (edges & CONT_Y) {
        /* Apply to the source VMU (port/slot of the selected entry).
         * Target picker UI is a v0.2.x polish item; v0.2.0 applies to
         * the source as the obvious default. */
        if (entry_count > 0) {
            perform_apply(entries[selected].port, entries[selected].slot);
        }
    }
    /* Keep selection visible in viewport. */
    const int VISIBLE_ROWS = 14;
    if (selected < scroll_top) scroll_top = selected;
    if (selected >= scroll_top + VISIBLE_ROWS) scroll_top = selected - VISIBLE_ROWS + 1;

    last_btns = btns;
}

static void draw_thumb(int sx, int sy, const jt_icon_t *icon, int size_px)
{
    /* Tiny thumbnail. 32x32 icon -> draw 1 framebuffer pixel per icon
     * pixel for size_px=32, or scale-down/up via integer step. Keep
     * it simple: 32 source pixels -> 32 dest pixels (1:1) when size_px=32. */
    int step = JT_CANVAS_W / size_px;
    if (step < 1) step = 1;
    for (int dy = 0; dy < size_px; dy++) {
        for (int dx = 0; dx < size_px; dx++) {
            int sxp = dx * step;
            int syp = dy * step;
            uint16_t argb = 0;
            if (icon->has_color_icon) {
                uint8_t idx = icon->color_indices[syp * JT_CANVAS_W + sxp] & 0x0F;
                uint8_t r, g, b, a;
                jt_palette_unpack(icon->palette[idx], &r, &g, &b, &a);
                argb = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            } else {
                int p = syp * JT_CANVAS_W + sxp;
                bool on = (icon->mono_bits[p / 8] >> (7 - (p % 8))) & 1;
                argb = on ? 0x0000 : 0xFFFF;
            }
            if (sx + dx < 640 && sy + dy < 480) {
                vram_s[(sy + dy) * 640 + (sx + dx)] = argb;
            }
        }
    }
}

static void browser_draw(void)
{
    jt_text_centered(8, JT_COL_YELLOW, JT_COL_BLACK, "VMU Save Browser");

    if (entry_count == 0) {
        jt_text_centered(220, JT_COL_WHITE, JT_COL_BLACK,
                         "No saves found on any detected VMU.");
        jt_text_centered(252, JT_COL_GREY, JT_COL_BLACK,
                         "Plug in a VMU and press X to refresh.");
        jt_text_centered(456, JT_COL_GREEN, JT_COL_BLACK,
                         "Hold Start+Down for options menu");
        return;
    }

    /* List rows starting at y=40, 28px stride for the 24px-tall thumb
     * + a few px breathing room. */
    int row_h = 28;
    for (int row = 0; row < 14 && scroll_top + row < entry_count; row++) {
        browser_entry_t *e = &entries[scroll_top + row];
        int idx = scroll_top + row;
        int y = 40 + row * row_h;
        uint16_t fg = (idx == selected) ? JT_COL_YELLOW : JT_COL_WHITE;
        const char *marker = (idx == selected) ? ">" : " ";

        /* Thumbnail. */
        if (e->icon_valid) draw_thumb(8, y - 2, &e->icon, 24);
        else {
            /* placeholder box */
            for (int yy = 0; yy < 24; yy++)
                for (int xx = 0; xx < 24; xx++)
                    vram_s[(y - 2 + yy) * 640 + (8 + xx)] = JT_COL_GREY;
        }
        jt_text(40, y, fg, JT_COL_BLACK,
                "%s %c%d %-12s %3d blk",
                marker, 'A' + e->port, e->slot + 1,
                e->filename, e->blocks);
    }

    /* Footer instructions. */
    jt_text_centered(420, JT_COL_GREY, JT_COL_BLACK,
                     "Up/Down select   A: extract to editor   Y: apply to source");
    jt_text_centered(444, JT_COL_GREY, JT_COL_BLACK,
                     "X: refresh list");
    jt_text_centered(468, JT_COL_GREEN, JT_COL_BLACK,
                     "Hold Start+Down for options menu");
}

const jt_mode_t jt_mode_browser = {
    .name   = "Save Browser",
    .enter  = browser_enter,
    .leave  = browser_leave,
    .update = browser_update,
    .draw   = browser_draw,
};
