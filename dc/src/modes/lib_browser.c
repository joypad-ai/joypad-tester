/*
 * lib_browser.c — read-back UI for VMUICONS.VMS library saves.
 *
 * For each detected VMU, attempts to load its library file and lists
 * every entry as a scrollable row with thumbnail + name + flags.
 *
 * Actions:
 *   A — load the selected entry into the editor canvas (switches mode)
 *   B + confirm — delete the entry from its source library save
 *   X — refresh (re-scan all VMUs)
 */
#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/vmu.h>
#include <dc/video.h>
#include <stdio.h>
#include <string.h>

#include "lib_browser.h"
#include "browser.h"        /* push-to-editor handoff */
#include "../app.h"
#include "../ui/bfont_util.h"
#include "../vms/apply.h"   /* pending "change icon" target */
#include "../library/library.h"
#include "../ports/ports.h"

#define MAX_ENTRIES_SHOWN 96

typedef struct {
    int                 port;
    int                 slot;
    int                 lib_index;     /* index into the source library */
    jt_library_entry_t  entry;          /* full copy for thumbnail + load */
} flat_entry_t;

static flat_entry_t flat[MAX_ENTRIES_SHOWN];
static int          flat_count = 0;
static int          selected = 0;
static int          scroll_top = 0;
static bool         needs_refresh = true;
static bool         confirm_delete = false;
static uint32_t     last_btns = 0;

/* "Set on a VMU" target picker, used when A is pressed while browsing
 * without a pending change target from the File Manager. */
static bool         picking = false;
static int          pick_sel = 0;
static struct { int8_t port, slot; } picks[JT_NUM_PORTS * JT_NUM_SLOTS];
static int          pick_count = 0;

static uint32_t aggregate_pad_buttons(void);   /* forward decl */

static void enter_mode(void)
{
    needs_refresh = true;
    selected = 0;
    scroll_top = 0;
    confirm_delete = false;
    last_btns = aggregate_pad_buttons();
}
static void leave_mode(void)  { }

static void refresh(void)
{
    flat_count = 0;
    for (int p = 0; p < JT_NUM_PORTS && flat_count < MAX_ENTRIES_SHOWN; p++) {
        for (int s = 0; s < JT_NUM_SLOTS && flat_count < MAX_ENTRIES_SHOWN; s++) {
            if (jt_ports[p].slots[s].kind != JT_SLOT_VMU) continue;
            maple_device_t *dev = maple_enum_dev(p, s + 1);
            if (!dev || !dev->valid) continue;

            jt_library_t lib;
            int rc = jt_library_load(dev, &lib);
            if (rc != 0) continue;   /* no library on this VMU */

            for (int i = 0; i < lib.entry_count && flat_count < MAX_ENTRIES_SHOWN; i++) {
                flat[flat_count].port      = p;
                flat[flat_count].slot      = s;
                flat[flat_count].lib_index = i;
                flat[flat_count].entry     = lib.entries[i];
                flat_count++;
            }
        }
    }
    needs_refresh = false;
}

static uint32_t aggregate_pad_buttons(void)
{
    uint32_t b = 0;
    for (int p = 0; p < JT_NUM_PORTS; p++)
        if (jt_ports[p].present && jt_ports[p].style == JT_STYLE_PAD)
            b |= jt_ports[p].pad.buttons;
    return b;
}

static void delete_selected(void)
{
    if (selected < 0 || selected >= flat_count) return;
    flat_entry_t *fe = &flat[selected];
    maple_device_t *dev = maple_enum_dev(fe->port, fe->slot + 1);
    if (!dev) return;
    jt_show_busy("Deleting...");
    jt_library_t lib;
    if (jt_library_load(dev, &lib) != 0) return;
    if (fe->lib_index >= lib.entry_count) return;
    /* Shift entries down. */
    for (int i = fe->lib_index; i < lib.entry_count - 1; i++) {
        lib.entries[i] = lib.entries[i + 1];
    }
    lib.entry_count--;
    jt_library_save(dev, &lib);
    needs_refresh = true;
    confirm_delete = false;
    if (selected >= flat_count - 1) selected--;
    if (selected < 0) selected = 0;
}

static void load_selected_to_editor(void)
{
    if (selected < 0 || selected >= flat_count) return;
    flat_entry_t *fe = &flat[selected];
    /* Push WITH source so the editor's "Save" overwrites this entry in
     * place (vs "Save As", which creates a new one). */
    jt_browser_push_to_editor_src(&fe->entry.icon,
                                  fe->port, fe->slot, fe->lib_index,
                                  fe->entry.name);
    jt_request_mode(JT_MODE_VMU_EDITOR);
}

/* "Change icon" flow: the file manager set a target VMU before sending
 * us here. Apply the picked icon straight to it and return. If there are
 * no saved icons to pick, hand off to the editor to create one (the
 * editor has its own apply/target flow). */
static void apply_to_pending_target(void)
{
    if (flat_count == 0) {
        /* Nothing to pick -- open the editor seeded with the target VMU's
         * current icon so the user edits from it. */
        jt_browser_push_change_base();
        jt_apply_clear_pending_target();
        jt_request_mode(JT_MODE_VMU_EDITOR);
        return;
    }
    if (selected < 0 || selected >= flat_count) return;
    int port, slot;
    if (jt_apply_take_pending_target(&port, &slot)) {
        jt_show_busy("Setting icon...");
        jt_apply_icondata(&flat[selected].entry.icon, port, slot);
        jt_request_mode(JT_MODE_BROWSER);
    }
}

/* Enumerate connected VMUs as "set" destinations. */
static void build_pick_list(void)
{
    pick_count = 0;
    for (int p = 0; p < JT_NUM_PORTS; p++)
        for (int s = 0; s < JT_NUM_SLOTS; s++) {
            maple_device_t *d = maple_enum_dev(p, s + 1);
            if (d && d->valid && (d->info.functions & MAPLE_FUNC_MEMCARD)) {
                picks[pick_count].port = (int8_t)p;
                picks[pick_count].slot = (int8_t)s;
                pick_count++;
            }
        }
}

/* A = "Set": write the selected icon to a VMU. If the File Manager sent
 * us here for a specific VMU, apply straight to it; otherwise open a
 * picker to choose the destination. */
static void do_set(void)
{
    /* Pending target (came from a VMU's Custom Icon flow) is handled
     * first -- apply_to_pending_target also covers the empty-library
     * case by routing to the editor. */
    if (jt_apply_has_pending_target()) { apply_to_pending_target(); return; }
    if (selected < 0 || selected >= flat_count) return;
    build_pick_list();
    if (pick_count == 0) return;
    pick_sel = 0;
    picking = true;
}

static void update_mode(float dt)
{
    (void)dt;
    if (needs_refresh) { jt_show_busy("Reading VMUs..."); refresh(); }
    uint32_t btns = aggregate_pad_buttons();
    uint32_t edges = btns & ~last_btns;

    if (confirm_delete) {
        if (edges & CONT_A) delete_selected();
        if (edges & CONT_B) confirm_delete = false;
        last_btns = btns;
        return;
    }

    if (picking) {   /* choosing which VMU to set the icon on */
        if (edges & CONT_DPAD_UP)   if (pick_sel > 0) pick_sel--;
        if (edges & CONT_DPAD_DOWN) if (pick_sel < pick_count - 1) pick_sel++;
        if (edges & CONT_A) {
            jt_show_busy("Setting icon...");
            jt_apply_icondata(&flat[selected].entry.icon,
                              picks[pick_sel].port, picks[pick_sel].slot);
            picking = false;
            jt_request_mode(JT_MODE_BROWSER);
        }
        if (edges & CONT_B) picking = false;
        last_btns = btns;
        return;
    }

    if (edges & CONT_DPAD_UP)   if (selected > 0) selected--;
    if (edges & CONT_DPAD_DOWN) if (selected < flat_count - 1) selected++;
    /* A = Set (apply to a VMU), Y = Edit (load into the editor), X =
     * delete (confirm), B = back to the File Manager. */
    if (edges & CONT_A)         do_set();
    if (edges & CONT_Y) {
        jt_apply_clear_pending_target();
        load_selected_to_editor();
    }
    if (edges & CONT_X)         confirm_delete = (flat_count > 0);
    if (edges & CONT_B) {
        jt_apply_clear_pending_target();
        jt_request_mode(JT_MODE_BROWSER);
    }

    const int VISIBLE = 9;
    if (selected < scroll_top) scroll_top = selected;
    if (selected >= scroll_top + VISIBLE) scroll_top = selected - VISIBLE + 1;

    last_btns = btns;
}

/* Render the mono channel of an icon as a VMU-LCD-styled thumbnail:
 * dark navy "on" bits against a green-cyan background, matching the
 * editor's mono pane so library rows show how the icon will look on
 * the physical VMU screen. */
static void draw_mono_thumb(int sx, int sy, const jt_icon_t *icon, int size_px)
{
    for (int dy = 0; dy < size_px; dy++) {
        for (int dx = 0; dx < size_px; dx++) {
            int sxp = dx * JT_CANVAS_W / size_px;
            int syp = dy * JT_CANVAS_H / size_px;
            if (sxp >= JT_CANVAS_W) sxp = JT_CANVAS_W - 1;
            if (syp >= JT_CANVAS_H) syp = JT_CANVAS_H - 1;
            int p = syp * JT_CANVAS_W + sxp;
            bool on = (icon->mono_bits[p / 8] >> (7 - (p % 8))) & 1;
            uint16_t pixel = on ? JT_RGB565(29, 71, 129)    /* navy on */
                                : JT_RGB565(138, 248, 219); /* LCD green */
            if (sx + dx < 640 && sy + dy < 480) {
                vram_s[(sy + dy) * 640 + (sx + dx)] = pixel;
            }
        }
    }
}

static void draw_thumb(int sx, int sy, const jt_icon_t *icon, int size_px)
{
    /* Nearest-neighbour downscale of the full 32x32 icon into size_px
     * (fractional ratio; an integer step truncated to 1 and cropped to
     * the top-left 24x24). */
    for (int dy = 0; dy < size_px; dy++) {
        for (int dx = 0; dx < size_px; dx++) {
            int sxp = dx * JT_CANVAS_W / size_px;
            int syp = dy * JT_CANVAS_H / size_px;
            if (sxp >= JT_CANVAS_W) sxp = JT_CANVAS_W - 1;
            if (syp >= JT_CANVAS_H) syp = JT_CANVAS_H - 1;
            uint16_t pixel;
            if (icon->has_color_icon) {
                uint8_t idx = icon->color_indices[syp * JT_CANVAS_W + sxp] & 0x0F;
                uint8_t r, g, b, a;
                jt_palette_unpack(icon->palette[idx], &r, &g, &b, &a);
                pixel = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            } else {
                int p = syp * JT_CANVAS_W + sxp;
                bool on = (icon->mono_bits[p / 8] >> (7 - (p % 8))) & 1;
                pixel = on ? 0x0000 : 0xFFFF;
            }
            if (sx + dx < 640 && sy + dy < 480) {
                vram_s[(sy + dy) * 640 + (sx + dx)] = pixel;
            }
        }
    }
}

static void draw_mode(void)
{
    /* Track empty -> populated transition so the "No entries" placeholder
     * text doesn't ghost behind freshly-loaded list rows. */
    static bool was_empty = true;
    bool is_empty = (flat_count == 0);
    if (was_empty && !is_empty) {
        /* Clear the placeholder area + list region in one wipe. */
        for (int y = 200; y < 320; y++) {
            uint16_t *p = vram_s + y * 640;
            for (int x = 0; x < 640; x++) p[x] = 0;
        }
    }
    was_empty = is_empty;

    bool changing = jt_apply_has_pending_target();
    jt_text_centered(24, JT_COL_YELLOW, JT_COL_BLACK,
                     changing ? "Choose VMU Icon" : "VMU Icon Library");

    if (flat_count == 0) {
        jt_text_centered(220, JT_COL_WHITE, JT_COL_BLACK,
                         "No library entries found on any VMU.");
        if (changing) {
            jt_text_centered(252, JT_COL_GREY, JT_COL_BLACK,
                             "A: create one in the Editor    B: cancel");
        } else {
            jt_text_centered(252, JT_COL_GREY, JT_COL_BLACK,
                             "Use Editor's Save button to add an entry.");
            jt_text_centered(284, JT_COL_GREY, JT_COL_BLACK, "B: back");
        }
        jt_text_centered(428, JT_COL_GREEN, JT_COL_BLACK,
                         "Start: options menu");
        return;
    }

    /* Native 32x32 color + mono thumbs side-by-side; 9 rows fit
     * comfortably above the footer at y=380. */
    int row_h = 36;
    for (int row = 0; row < 9 && scroll_top + row < flat_count; row++) {
        int idx = scroll_top + row;
        flat_entry_t *fe = &flat[idx];
        int y = 52 + row * row_h;
        uint16_t fg = (idx == selected) ? JT_COL_YELLOW : JT_COL_WHITE;
        const char *marker = (idx == selected) ? ">" : " ";
        draw_thumb(8,  y, &fe->entry.icon, 32);    /* color thumb */
        draw_mono_thumb(44, y, &fe->entry.icon, 32); /* mono thumb (VMU LCD style) */
        const char *bkup = (fe->entry.flags & JT_LIB_FLAG_BACKUP) ? "*" : " ";
        const char *rmd  = (fe->entry.flags & JT_LIB_FLAG_REALMODE) ? "R" : " ";
        jt_text(84, y + 4, fg, JT_COL_BLACK,
                "%s %c%d %s%s %-16s",
                marker, 'A' + fe->port, fe->slot + 1,
                bkup, rmd, fe->entry.name);
    }

    if (confirm_delete) {
        const int x = 140, y = 180, w = 360, h = 96;
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++)
                vram_s[(y + j) * 640 + (x + i)] = JT_COL_BLACK;
        /* 2px yellow border, matching the other modals. */
        for (int i = 0; i < w; i++) {
            vram_s[y * 640 + (x + i)]             = JT_COL_YELLOW;
            vram_s[(y + 1) * 640 + (x + i)]       = JT_COL_YELLOW;
            vram_s[(y + h - 2) * 640 + (x + i)]   = JT_COL_YELLOW;
            vram_s[(y + h - 1) * 640 + (x + i)]   = JT_COL_YELLOW;
        }
        for (int j = 0; j < h; j++) {
            vram_s[(y + j) * 640 + x]             = JT_COL_YELLOW;
            vram_s[(y + j) * 640 + (x + 1)]       = JT_COL_YELLOW;
            vram_s[(y + j) * 640 + (x + w - 2)]   = JT_COL_YELLOW;
            vram_s[(y + j) * 640 + (x + w - 1)]   = JT_COL_YELLOW;
        }
        jt_text_centered(y + 12, JT_COL_RED, JT_COL_BLACK,
                         "Delete this library entry?");
        jt_text_centered(y + 48, JT_COL_WHITE, JT_COL_BLACK,
                         "A: confirm   B: cancel");
    }

    if (picking) {   /* choose which VMU to set the selected icon on */
        int rows = pick_count > 0 ? pick_count : 1;
        const int w = 300, h = 80 + rows * 26;
        const int x = (640 - w) / 2, y = (480 - h) / 2;
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++)
                vram_s[(y + j) * 640 + (x + i)] = JT_COL_BLACK;
        for (int i = 0; i < w; i++) {
            vram_s[y * 640 + x + i]           = JT_COL_YELLOW;
            vram_s[(y + 1) * 640 + x + i]     = JT_COL_YELLOW;
            vram_s[(y + h - 2) * 640 + x + i] = JT_COL_YELLOW;
            vram_s[(y + h - 1) * 640 + x + i] = JT_COL_YELLOW;
        }
        for (int j = 0; j < h; j++) {
            vram_s[(y + j) * 640 + x]         = JT_COL_YELLOW;
            vram_s[(y + j) * 640 + x + 1]     = JT_COL_YELLOW;
            vram_s[(y + j) * 640 + x + w - 2] = JT_COL_YELLOW;
            vram_s[(y + j) * 640 + x + w - 1] = JT_COL_YELLOW;
        }
        jt_text(x + 16, y + 12, JT_COL_YELLOW, JT_COL_BLACK, "Set icon on VMU:");
        for (int i = 0; i < pick_count; i++) {
            uint16_t fg = (i == pick_sel) ? JT_COL_YELLOW : JT_COL_WHITE;
            jt_text(x + 24, y + 40 + i * 26, fg, JT_COL_BLACK, "%sPort %c%d",
                    (i == pick_sel) ? "> " : "  ",
                    'A' + picks[i].port, picks[i].slot + 1);
        }
        jt_text(x + 16, y + 40 + rows * 26 + 8, JT_COL_GREY, JT_COL_BLACK,
                "A: set   B: cancel");
    }

    jt_text_centered(380, JT_COL_GREY, JT_COL_BLACK,
                     "A: Set   Y: Edit   X: delete   B: back");
    jt_text_centered(404, JT_COL_GREY, JT_COL_BLACK,
                     "* = auto-backup    R = Real Mode");
    jt_text_centered(428, JT_COL_GREEN, JT_COL_BLACK,
                     "Start: options menu");
}

const jt_mode_t jt_mode_lib_browser = {
    .name   = "VMU Icon Library",
    .enter  = enter_mode,
    .leave  = leave_mode,
    .update = update_mode,
    .draw   = draw_mode,
};
