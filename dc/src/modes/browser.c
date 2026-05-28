/*
 * browser.c — VMU File Manager.
 *
 * Three views:
 *   VIEW_VMUS    top level: one row per detected VMU showing its BIOS
 *                menu icon (ICONDATA_VMS), custom color, save count and
 *                free blocks. A = browse its saves, Y = edit options.
 *   VIEW_SAVES   the selected VMU's save list with icon thumbnails.
 *                A = extract to editor, Y = apply selected icon, B = back.
 *   VIEW_OPTIONS per-VMU settings: Real Mode (3D BIOS) toggle, custom
 *                color (enable + RGBA), and set the VMU clock to the
 *                console time. B = back.
 *
 * Read-only on game saves; only ICONDATA_VMS / the VMU root block (via
 * the KOS custom-color + datetime APIs) are written, and only from the
 * options view or an explicit apply.
 */
#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/vmu.h>
#include <dc/video.h>
#include <dc/vmufs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "browser.h"
#include "../app.h"
#include "../ui/bfont_util.h"
#include "../canvas/canvas.h"   /* default palette for the editor base icon */
#include "../vms/vms.h"
#include "../vms/apply.h"
#include "../library/library.h"
#include "../library/gen_fallback_vmu_icon.h"
#include "../library/gen_bios_icons.h"
#include "../ports/ports.h"

#define MAX_ENTRIES 64
#define MAX_VMUS    (JT_NUM_PORTS * JT_NUM_SLOTS)
#define MAX_FRAMES  3

typedef struct {
    int      port;
    int      slot;
    char     filename[13];   /* 12 + NUL (FAT name) */
    char     name[17];       /* VMS short description, 16 + NUL */
    char     description[33];/* VMS long description, 32 + NUL */
    int      blocks;
    jt_icon_t frames[MAX_FRAMES];
    int      frame_count;
    uint16_t anim_speed;
    bool     icon_valid;
} browser_entry_t;

typedef struct {
    int       port;
    int       slot;
    jt_icon_t bios_icon;     /* decoded ICONDATA_VMS (BIOS menu icon) */
    bool      bios_icon_valid;
    bool      has_icondata;
    bool      real_mode;     /* ICONDATA real-mode (3D BIOS) flag */
    bool      custom_color;  /* custom BIOS color enabled */
    uint8_t   cr, cg, cb, ca;
    /* Preset BIOS icon shape (root-block icon_shape, 0-relative). Used
     * as the fallback display when the VMU has no ICONDATA_VMS -- the
     * Dreamcast BIOS shows one of these built-in 124 icons in that
     * case. Add BFONT_ICON_VMUICON to get a bfont_find_icon() index. */
    uint8_t   icon_shape;
    int       free_blocks;
    int       save_count;
} vmu_info_t;

typedef enum {
    VIEW_VMUS = 0,
    VIEW_SAVES,
    VIEW_OPTIONS,
    VIEW_ICON_ACTIONS,
    VIEW_SAVE_DETAILS,
    VIEW_SAVE_DELETE_CONFIRM,
} browser_view_t;

/* Options rows. Settings only -- the clock-set action is bound to Y
 * (immediate) since A inside the modal commits the staged settings. */
enum {
    OPT_ICON_SHAPE = 0,
    OPT_CUSTOM_COLOR,
    OPT_COL_R,
    OPT_COL_G,
    OPT_COL_B,
    OPT_COL_A,
    OPT_REAL_MODE,
    OPT_COUNT
};

/* Staged copy of the VMU's editable options while the modal is open.
 * L/R adjusts these locally; A commits everything to the VMU at once,
 * B discards. Matches the palette-editor flow. */
static bool    staged_real_mode;
static bool    staged_custom_color;
static uint8_t staged_cr, staged_cg, staged_cb, staged_ca;
static uint8_t staged_icon_shape;   /* 0-relative BIOS preset shape (0 == VMU icon) */

static browser_entry_t entries[MAX_ENTRIES];
static int entry_count = 0;

static vmu_info_t vmus[MAX_VMUS];
static int vmu_count = 0;
static int vmu_sel = 0;

static browser_view_t view = VIEW_VMUS;

/* Saves view: indices into entries[] belonging to the selected VMU. */
static int filtered[MAX_ENTRIES];
static int filtered_count = 0;
static int save_sel = 0;
static int save_scroll = 0;
static int vmu_scroll = 0;          /* top row of the VMU list window */
#define VMU_VISIBLE 6               /* VMU rows that fit above the footer */

/* Options view selection. */
static int opt_sel = 0;
static int icon_action_sel = 0;   /* VIEW_ICON_ACTIONS chooser cursor */

static bool needs_refresh = true;
static float anim_time_s = 0.0f;
static uint32_t last_btns = 0;

/* Transient status line. */
static char status_msg[64] = {0};
static unsigned status_frames = 0;

static uint32_t aggregate_pad_buttons(void);   /* forward decl */
static void fill_rect(int x, int y, int w, int h, uint16_t color);
static void commit_staged_options(void);       /* defined with the options view */

/* Editor handoff. */
static jt_icon_t pending_to_editor;
static bool      pending_pickup = false;

void jt_browser_push_to_editor(const jt_icon_t *icon)
{
    if (!icon) return;
    memcpy(&pending_to_editor, icon, sizeof(pending_to_editor));
    pending_pickup = true;
}

bool jt_browser_consume_pending(jt_icon_t *out)
{
    if (!pending_pickup) return false;
    *out = pending_to_editor;
    pending_pickup = false;
    return true;
}

/* "Change icon" base: the target VMU's current icon, stashed when a
 * change starts so the editor can open pre-seeded with it if the Icon
 * Library turns out to be empty. Only handed to the editor on demand
 * (push), so picking a library entry instead leaves no stale pickup. */
static jt_icon_t change_base;
static bool      change_base_valid = false;

void jt_browser_push_change_base(void)
{
    if (change_base_valid) jt_browser_push_to_editor(&change_base);
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

static void set_status(const char *msg)
{
    strncpy(status_msg, msg, sizeof(status_msg) - 1);
    status_msg[sizeof(status_msg) - 1] = '\0';
    status_frames = 180;
}

/* Copy a fixed-length VMS header text field into a NUL-terminated string,
 * keeping only printable ASCII (VMS descriptions are space-padded, and
 * Japanese Shift-JIS bytes can't be drawn by bfont) and trimming trailing
 * spaces. dst must hold len+1 bytes. */
static void sanitize_text(char *dst, const uint8_t *src, int len)
{
    int j = 0;
    for (int i = 0; i < len; i++) {
        uint8_t c = src[i];
        if (c == 0) break;
        if (c >= 0x20 && c <= 0x7E) dst[j++] = (char)c;
    }
    while (j > 0 && dst[j - 1] == ' ') j--;
    dst[j] = '\0';
}

static void browser_enter(void)
{
    needs_refresh = true;
    view = VIEW_VMUS;
    vmu_sel = 0;
    save_sel = 0;
    save_scroll = 0;
    vmu_scroll = 0;
    opt_sel = 0;
    last_btns = aggregate_pad_buttons();
}

static void browser_leave(void) { /* nothing */ }

/* (Re-)scan every detected VMU: BIOS icon + custom color + free blocks,
 * and aggregate all save dirents into entries[]. */
static void refresh(void)
{
    vmu_count = 0;
    entry_count = 0;

    for (int p = 0; p < JT_NUM_PORTS; p++) {
        for (int s = 0; s < JT_NUM_SLOTS; s++) {
            if (jt_ports[p].slots[s].kind != JT_SLOT_VMU) continue;
            maple_device_t *dev = maple_enum_dev(p, s + 1);
            if (!dev || !dev->valid) continue;
            if (vmu_count >= MAX_VMUS) break;

            vmu_info_t *v = &vmus[vmu_count++];
            memset(v, 0, sizeof(*v));
            v->port = p;
            v->slot = s;
            v->free_blocks = vmufs_free_blocks(dev);

            /* Custom BIOS color + preset icon shape, both read straight
             * from the root block (KOS only ships setters). The
             * custom_color[] order is blue, green, red, alpha. */
            vmu_root_t root;
            if (vmufs_root_read(dev, &root) == 0) {
                v->custom_color = (root.use_custom != 0);
                v->cb = root.custom_color[0];
                v->cg = root.custom_color[1];
                v->cr = root.custom_color[2];
                v->ca = root.custom_color[3];
                /* Stored 0-relative (0 == VMU icon), matching what
                 * vmu_set_icon_shape writes. Displayed faithfully; the
                 * Options modal lets the user change it. */
                v->icon_shape = (uint8_t)root.icon_shape;
            }

            /* BIOS menu icon (ICONDATA_VMS). */
            void *raw = NULL;
            int   sz = 0;
            if (vmufs_read(dev, "ICONDATA_VMS", &raw, &sz) == 0 && raw) {
                v->has_icondata = true;
                if (jt_vms_decode_icondata(raw, sz, &v->bios_icon)) {
                    v->bios_icon_valid = true;
                    v->real_mode = v->bios_icon.real_mode_flag;
                }
                free(raw);
            }

            /* Saves on this VMU. */
            vmu_dir_t *dirents = NULL;
            int        dcount  = 0;
            if (vmufs_readdir(dev, &dirents, &dcount) == 0) {
                for (int i = 0; i < dcount; i++) {
                    if (dirents[i].filetype == 0) continue;
                    v->save_count++;
                    if (entry_count >= MAX_ENTRIES) continue;

                    browser_entry_t *e = &entries[entry_count++];
                    e->port = p;
                    e->slot = s;
                    memcpy(e->filename, dirents[i].filename, 12);
                    e->filename[12] = '\0';
                    e->blocks = dirents[i].filesize;
                    e->icon_valid = false;
                    e->frame_count = 0;
                    e->anim_speed = 0;
                    e->name[0] = e->description[0] = '\0';

                    void *fraw = NULL;
                    int   fsz  = 0;
                    if (vmufs_read_dirent(dev, &dirents[i], &fraw, &fsz) == 0 && fraw) {
                        const uint8_t *bytes = (const uint8_t *)fraw;
                        bool is_icondata = (memcmp(e->filename, "ICONDATA_VMS", 12) == 0);
                        /* VMS header text lives at hdroff blocks in; 0x00 is
                         * the 16-char name, 0x10 the 32-char description.
                         * ICONDATA_VMS reuses 0x10 for binary offsets, so it
                         * only has the name. */
                        size_t hb = (size_t)dirents[i].hdroff * 512;
                        if ((size_t)fsz >= hb + 0x10)
                            sanitize_text(e->name, bytes + hb + 0x00, 16);
                        if (!is_icondata && (size_t)fsz >= hb + 0x30)
                            sanitize_text(e->description, bytes + hb + 0x10, 32);
                        if (is_icondata) {
                            if (jt_vms_decode_icondata(bytes, fsz, &e->frames[0])) {
                                e->icon_valid = true;
                                e->frame_count = 1;
                            }
                        } else {
                            unsigned n = jt_vms_save_icon_count(bytes, fsz);
                            if (n > MAX_FRAMES) n = MAX_FRAMES;
                            for (unsigned f = 0; f < n; f++) {
                                if (jt_vms_extract_save_icon(bytes, fsz, f, &e->frames[f]))
                                    e->frame_count++;
                            }
                            if (e->frame_count > 0) e->icon_valid = true;
                            if (fsz >= 0x44)
                                e->anim_speed = (uint16_t)(bytes[0x42] | (bytes[0x43] << 8));
                        }
                        free(fraw);
                    }
                }
                free(dirents);
            }
        }
    }

    if (vmu_sel >= vmu_count) vmu_sel = 0;
    needs_refresh = false;
}

/* Build the filtered save-index list for the currently-selected VMU. */
static void build_filtered(void)
{
    filtered_count = 0;
    if (vmu_sel < 0 || vmu_sel >= vmu_count) return;
    int vp = vmus[vmu_sel].port, vs = vmus[vmu_sel].slot;
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].port == vp && entries[i].slot == vs)
            filtered[filtered_count++] = i;
    }
    if (save_sel >= filtered_count) save_sel = 0;
    save_scroll = 0;
}

/* ----- thumbnail blit (nearest-neighbour scale of full 32x32) ----- */
/* Render one of the Dreamcast BIOS's built-in 32x32 VMU icons (the
 * "icon shape" stored in the root block) at the given size, in the same
 * VMU-LCD palette as our mono thumb. bfont_find_icon returns the data
 * flipped both vertically and horizontally, so we unflip while sampling. */
static void draw_preset_icon(int sx, int sy, uint8_t shape, int size_px)
{
    const uint8_t *data = bfont_find_icon((bfont_vmu_icon_t)
                                          (BFONT_ICON_VMUICON + shape));
    /* Detect the all-zeros case -- emulators (notably Flycast) ship a
     * stub BIOS font that omits the VMU icon section, so bfont_find_icon
     * returns a valid pointer into all-zero memory. On real hardware
     * the icon data is in the BIOS ROM and this loop short-circuits
     * quickly on the first set bit. */
    bool empty = true;
    if (data) {
        for (int i = 0; i < 128; i++) if (data[i]) { empty = false; break; }
    }
    if (!data || empty) {
        /* No BIOS icon (emulator with a stub font). Use the bundled
         * preset-icon set for this shape, else the generic VMU
         * silhouette. All stored upright -- rendered directly below. */
        if (shape < 124)
            data = &bios_icons_mono[shape * 128];
        else
            data = fallback_vmu_icon_mono;
    }
    /* Render at integer scale so 32-px source pixels map cleanly --
     * size_px / 32 rounded down, centered with LCD-bg margin. Avoids
     * the uneven duplication that a 32->36 nearest-neighbor pass
     * produces on a 32-px source. bfont VMU icons are stored UPRIGHT
     * (row-major, MSB-first per byte -- same as the ICONDATA mono that
     * draw_thumb renders), so no row/col flip here; an earlier H+V
     * unflip rotated the real BIOS icons 180 degrees on hardware. */
    const uint16_t on_px  = JT_RGB565( 29,  71, 129);
    const uint16_t off_px = JT_RGB565(138, 248, 219);
    int scale = size_px / 32;
    if (scale < 1) scale = 1;
    int actual = 32 * scale;
    int pad_x  = (size_px - actual) / 2;
    int pad_y  = (size_px - actual) / 2;
    fill_rect(sx, sy, size_px, size_px, off_px);
    for (int sy_off = 0; sy_off < 32; sy_off++) {
        int row = sy_off;
        for (int sx_off = 0; sx_off < 32; sx_off++) {
            int col = sx_off;
            uint8_t byte = data[row * 4 + (col >> 3)];
            if (!((byte >> (7 - (col & 7))) & 1)) continue;
            int px0 = sx + pad_x + sx_off * scale;
            int py0 = sy + pad_y + sy_off * scale;
            for (int by = 0; by < scale; by++) {
                int py = py0 + by;
                if (py >= 480) break;
                for (int bx = 0; bx < scale; bx++) {
                    int px = px0 + bx;
                    if (px >= 640) break;
                    vram_s[py * 640 + px] = on_px;
                }
            }
        }
    }
}

static void draw_thumb(int sx, int sy, const jt_icon_t *icon, int size_px)
{
    for (int dy = 0; dy < size_px; dy++) {
        for (int dx = 0; dx < size_px; dx++) {
            int sxp = dx * JT_CANVAS_W / size_px;
            int syp = dy * JT_CANVAS_H / size_px;
            if (sxp >= JT_CANVAS_W) sxp = JT_CANVAS_W - 1;
            if (syp >= JT_CANVAS_H) syp = JT_CANVAS_H - 1;
            uint16_t argb;
            if (icon->has_color_icon) {
                uint8_t idx = icon->color_indices[syp * JT_CANVAS_W + sxp] & 0x0F;
                uint8_t r, g, b, a;
                jt_palette_unpack(icon->palette[idx], &r, &g, &b, &a);
                argb = JT_RGB565(r, g, b);
            } else {
                int p = syp * JT_CANVAS_W + sxp;
                bool on = (icon->mono_bits[p / 8] >> (7 - (p % 8))) & 1;
                argb = on ? 0x0000 : 0xFFFF;
            }
            if (sx + dx < 640 && sy + dy < 480)
                vram_s[(sy + dy) * 640 + (sx + dx)] = argb;
        }
    }
}

static void fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0 || y < 0 || x + w > 640 || y + h > 480) return;
    for (int j = 0; j < h; j++) {
        uint16_t *row = vram_s + (y + j) * 640 + x;
        for (int i = 0; i < w; i++) row[i] = color;
    }
}

/* ===================== actions ===================== */

static maple_device_t *sel_vmu_dev(void)
{
    if (vmu_sel < 0 || vmu_sel >= vmu_count) return NULL;
    return maple_enum_dev(vmus[vmu_sel].port, vmus[vmu_sel].slot + 1);
}

static void set_clock_to_console(void)
{
    maple_device_t *dev = sel_vmu_dev();
    if (!dev || !dev->valid) { set_status("VMU disconnected"); return; }
    jt_show_busy("Setting VMU clock...");
    if (vmu_set_datetime(dev, time(NULL)) == 0) set_status("VMU clock set");
    else set_status("Clock set failed");
}

/* Delete the VMU's custom icon (ICONDATA_VMS); it then falls back to the
 * built-in preset shape, just like a freshly formatted card. */
static void remove_custom_icon(void)
{
    maple_device_t *dev = sel_vmu_dev();
    if (!dev || !dev->valid) { set_status("VMU disconnected"); return; }
    jt_show_busy("Removing icon...");
    if (vmufs_delete(dev, "ICONDATA_VMS") == 0) set_status("Custom icon removed");
    else set_status("Remove failed");
    needs_refresh = true;
}

/* Editor base for a change/set: the VMU's current custom icon if it has
 * one, else its built-in preset shape stamped onto a default-palette
 * canvas (so the editor opens with the BIOS icon as an editable base). */
static void build_base_icon(jt_icon_t *out, const vmu_info_t *v)
{
    if (v->bios_icon_valid) { *out = v->bios_icon; return; }
    jt_canvas_t tmp;
    jt_canvas_init(&tmp);
    jt_canvas_to_icon(&tmp, out);       /* default palette, blank canvas */
    const uint8_t *mono = (v->icon_shape < 124)
                          ? &bios_icons_mono[v->icon_shape * 128]
                          : fallback_vmu_icon_mono;
    memcpy(out->mono_bits, mono, sizeof(out->mono_bits));
}

/* Begin "change/set custom icon": stash the editor base, commit staged
 * option edits (so they survive the mode switch), set the apply target,
 * and open the Icon Library to pick -- or, if empty, the editor seeded
 * with that base. */
static void begin_change_icon(vmu_info_t *v)
{
    build_base_icon(&change_base, v);
    change_base_valid = true;
    commit_staged_options();
    jt_apply_set_pending_target(v->port, v->slot);
    jt_request_mode(JT_MODE_LIB_BROWSER);
}

/* Copy the VMU's current settings into the staged_* working set so the
 * modal edits start from the live values. */
static void load_staged_from_vmu(void)
{
    vmu_info_t *v = &vmus[vmu_sel];
    staged_real_mode    = v->real_mode;
    staged_custom_color = v->custom_color;
    staged_cr = v->cr; staged_cg = v->cg; staged_cb = v->cb; staged_ca = v->ca;
    staged_icon_shape   = v->icon_shape;
}

/* Commit the staged options to the VMU: write Real Mode into ICONDATA
 * (auto-creating one if the VMU has none yet) and push custom-color
 * enable + RGBA via maple. Called on A inside the options modal. */
static void commit_staged_options(void)
{
    vmu_info_t *v = &vmus[vmu_sel];
    maple_device_t *dev = sel_vmu_dev();
    if (!dev || !dev->valid) { set_status("VMU disconnected"); return; }
    jt_show_busy("Saving VMU options...");

    /* Real Mode change requires an ICONDATA file -- auto-create one
     * from the baked Joypad-logo default if missing. */
    if (staged_real_mode != v->real_mode) {
        if (staged_real_mode && !v->has_icondata) {
            if (jt_vmu_ensure_icondata(v->port, v->slot) != 0) {
                set_status("Could not create ICONDATA");
                return;
            }
            /* Reload the fresh ICONDATA we just wrote. */
            void *raw = NULL; int sz = 0;
            if (vmufs_read(dev, "ICONDATA_VMS", &raw, &sz) == 0 && raw) {
                if (jt_vms_decode_icondata(raw, sz, &v->bios_icon)) {
                    v->bios_icon_valid = true;
                    v->has_icondata = true;
                }
                free(raw);
            }
        }
        if (v->bios_icon_valid) {
            jt_icon_t icon = v->bios_icon;
            icon.real_mode_flag = staged_real_mode;
            uint8_t buf[JT_VMS_ICONDATA_SIZE];
            if (!jt_vms_encode_icondata(&icon, buf) ||
                vmufs_write(dev, "ICONDATA_VMS", buf, sizeof(buf),
                            VMUFS_OVERWRITE) != 0) {
                set_status("ICONDATA write failed");
                return;
            }
            v->bios_icon = icon;
            v->real_mode = staged_real_mode;
        }
    }

    /* Color changes (enable bit + RGBA payload). */
    bool col_changed = (staged_custom_color != v->custom_color) ||
                       (staged_custom_color && (staged_cr != v->cr ||
                                                staged_cg != v->cg ||
                                                staged_cb != v->cb ||
                                                staged_ca != v->ca));
    if (col_changed) {
        vmu_use_custom_color(dev, staged_custom_color ? 1 : 0);
        if (staged_custom_color) {
            vmu_set_custom_color(dev, staged_cr, staged_cg, staged_cb, staged_ca);
        }
        v->custom_color = staged_custom_color;
        v->cr = staged_cr; v->cg = staged_cg;
        v->cb = staged_cb; v->ca = staged_ca;
    }

    /* BIOS preset icon shape. vmu_set_icon_shape takes the absolute bfont
     * value and stores it 0-relative in the root block -- matching how we
     * read it back. */
    if (staged_icon_shape != v->icon_shape) {
        vmu_set_icon_shape(dev, (uint8_t)(BFONT_ICON_VMUICON + staged_icon_shape));
        v->icon_shape = staged_icon_shape;
    }

    set_status("Options saved");
}

/* ===================== update ===================== */

static void update_vmus(uint32_t edges)
{
    if (edges & CONT_DPAD_UP)   if (vmu_sel > 0) vmu_sel--;
    if (edges & CONT_DPAD_DOWN) if (vmu_sel < vmu_count - 1) vmu_sel++;
    if (edges & CONT_X)         needs_refresh = true;
    if (vmu_count > 0) {
        if (edges & CONT_A) { build_filtered(); view = VIEW_SAVES; }
        if (edges & CONT_Y) { opt_sel = 0; load_staged_from_vmu(); view = VIEW_OPTIONS; }
    }
    /* Keep the selection inside the visible window (scrolls when there
     * are more VMUs than fit above the footer). */
    if (vmu_sel < vmu_scroll) vmu_scroll = vmu_sel;
    if (vmu_sel >= vmu_scroll + VMU_VISIBLE) vmu_scroll = vmu_sel - VMU_VISIBLE + 1;
}

static void update_saves(uint32_t edges)
{
    if (edges & CONT_B) { view = VIEW_VMUS; return; }
    if (filtered_count > 0) {
        if (edges & CONT_DPAD_UP)   if (save_sel > 0) save_sel--;
        if (edges & CONT_DPAD_DOWN) if (save_sel < filtered_count - 1) save_sel++;
        browser_entry_t *e = &entries[filtered[save_sel]];
        /* A: open details popup. (Library save is special -- A drills
         * into the Icon Library view since the file is a packed archive,
         * not a single icon.) */
        if (edges & CONT_A) {
            if (strcmp(e->filename, JT_LIBRARY_FILENAME) == 0) {
                jt_request_mode(JT_MODE_LIB_BROWSER);
                return;
            }
            view = VIEW_SAVE_DETAILS;
            return;
        }
        /* Y: extract to editor (replaces the old A behaviour). */
        if ((edges & CONT_Y) && e->icon_valid) {
            jt_browser_push_to_editor(&e->frames[0]);
            jt_request_mode(JT_MODE_VMU_EDITOR);
            return;
        }
        /* X: delete (with a yes/no confirm modal). */
        if (edges & CONT_X) {
            view = VIEW_SAVE_DELETE_CONFIRM;
            return;
        }
    }
    const int VISIBLE = 9;
    if (save_sel < save_scroll) save_scroll = save_sel;
    if (save_sel >= save_scroll + VISIBLE) save_scroll = save_sel - VISIBLE + 1;
}

static void update_save_details(uint32_t edges)
{
    /* Read-only info popup; A or B closes back to the saves list. */
    if (edges & (CONT_A | CONT_B)) view = VIEW_SAVES;
}

static void update_save_delete_confirm(uint32_t edges)
{
    if (edges & CONT_B) { view = VIEW_SAVES; return; }
    if (edges & CONT_A) {
        if (save_sel >= 0 && save_sel < filtered_count) {
            browser_entry_t *e = &entries[filtered[save_sel]];
            maple_device_t *dev = maple_enum_dev(e->port, e->slot + 1);
            jt_show_busy("Deleting save...");
            if (dev && dev->valid &&
                vmufs_delete(dev, e->filename) == 0) {
                set_status("Deleted");
                needs_refresh = true;
            } else {
                set_status("Delete failed");
            }
        }
        view = VIEW_SAVES;
    }
}

static void update_options(uint32_t edges)
{
    if (edges & CONT_B) { view = VIEW_VMUS; return; }
    if (edges & CONT_DPAD_UP)   if (opt_sel > 0) opt_sel--;
    if (edges & CONT_DPAD_DOWN) if (opt_sel < OPT_COUNT - 1) opt_sel++;

    /* L/R adjusts whatever the cursor's on: toggles for the bool rows,
     * +/-16 steps for the color channels. */
    int delta = 0;
    if (edges & CONT_DPAD_LEFT)  delta = -1;
    if (edges & CONT_DPAD_RIGHT) delta = +1;
    if (delta != 0) {
        switch (opt_sel) {
            case OPT_REAL_MODE:    staged_real_mode    = !staged_real_mode;    break;
            case OPT_CUSTOM_COLOR: staged_custom_color = !staged_custom_color; break;
            case OPT_COL_R: case OPT_COL_G: case OPT_COL_B: case OPT_COL_A: {
                uint8_t *ch = (opt_sel == OPT_COL_R) ? &staged_cr :
                              (opt_sel == OPT_COL_G) ? &staged_cg :
                              (opt_sel == OPT_COL_B) ? &staged_cb : &staged_ca;
                int nv = (int)*ch + delta * 16;
                if (nv < 0)   nv = 0;
                if (nv > 255) nv = 255;
                *ch = (uint8_t)nv;
                break;
            }
            case OPT_ICON_SHAPE: {
                int n = (int)staged_icon_shape + delta;
                if (n < 0)   n = 123;   /* wrap through the 124 shapes */
                if (n > 123) n = 0;
                staged_icon_shape = (uint8_t)n;
                break;
            }
        }
    }

    /* Y = set VMU clock to console time. Immediate side-effect; not
     * part of the A/B save/cancel flow. */
    if (edges & CONT_Y) set_clock_to_console();

    /* A on the VMU Icon row: with a custom icon, drill into the chooser
     * (Change / Remove / Back); without one, "Set Custom" is the only
     * action, so go straight to the Library. Any other row commits +
     * closes. Both icon paths commit staged edits first so they survive
     * the mode switch. */
    if (edges & CONT_A) {
        if (opt_sel == OPT_ICON_SHAPE) {
            vmu_info_t *v = &vmus[vmu_sel];
            if (v->bios_icon_valid) {
                icon_action_sel = 0;
                view = VIEW_ICON_ACTIONS;
            } else {
                begin_change_icon(v);   /* "Set Custom" -> Library/Editor */
            }
        } else {
            commit_staged_options();
            view = VIEW_VMUS;
        }
    }
}

/* VMU Icon chooser (avatar-style): Change routes to the Icon Library to
 * pick a saved icon (or the Editor if the library is empty); Remove
 * deletes the custom ICONDATA so the card reverts to its built-in shape.
 * Change/Remove also commit any staged color/real-mode/shape edits so
 * they aren't lost on the mode switch. */
static void update_icon_actions(uint32_t edges)
{
    vmu_info_t *v = &vmus[vmu_sel];
    bool has_custom = v->bios_icon_valid;
    int n = has_custom ? 3 : 2;          /* Change, [Remove], Back */

    if (edges & CONT_DPAD_UP)   if (icon_action_sel > 0) icon_action_sel--;
    if (edges & CONT_DPAD_DOWN) if (icon_action_sel < n - 1) icon_action_sel++;
    if (edges & CONT_B) { view = VIEW_OPTIONS; return; }
    if (edges & CONT_A) {
        /* Without a custom icon the middle "Remove" row is absent, so
         * cursor index 1 maps to Back. */
        int action = (!has_custom && icon_action_sel == 1) ? 2 : icon_action_sel;
        if (action == 0) {                /* Change */
            begin_change_icon(v);
        } else if (action == 1) {         /* Remove */
            commit_staged_options();
            remove_custom_icon();
            view = VIEW_OPTIONS;
        } else {                          /* Back */
            view = VIEW_OPTIONS;
        }
    }
}

static void browser_update(float dt)
{
    anim_time_s += dt;
    if (needs_refresh) {
        jt_show_busy("Reading VMU...");
        refresh();
        if (view == VIEW_SAVES) build_filtered();
    }

    uint32_t btns = aggregate_pad_buttons();
    uint32_t edges = btns & ~last_btns;

    switch (view) {
        case VIEW_VMUS:                  update_vmus(edges);    break;
        case VIEW_SAVES:                 update_saves(edges);   break;
        case VIEW_OPTIONS:               update_options(edges); break;
        case VIEW_ICON_ACTIONS:          update_icon_actions(edges);          break;
        case VIEW_SAVE_DETAILS:          update_save_details(edges);          break;
        case VIEW_SAVE_DELETE_CONFIRM:   update_save_delete_confirm(edges);   break;
    }

    if (status_frames > 0) status_frames--;
    last_btns = btns;
}

/* ===================== draw ===================== */

static void draw_vmus(void)
{
    jt_text_centered(24, JT_COL_YELLOW, JT_COL_BLACK, "VMU File Manager");

    if (vmu_count == 0) {
        jt_text_centered(220, JT_COL_WHITE, JT_COL_BLACK,
                         "No VMU detected on any port.");
        jt_text_centered(252, JT_COL_GREY, JT_COL_BLACK,
                         "Plug in a VMU and press X to refresh.");
        jt_text_centered(428, JT_COL_GREEN, JT_COL_BLACK, "Start: options menu");
        return;
    }

    for (int k = 0; k < VMU_VISIBLE && vmu_scroll + k < vmu_count; k++) {
        int i = vmu_scroll + k;
        vmu_info_t *v = &vmus[i];
        /* 54-px row pitch holding a 48-px content band. The two text
         * lines sit a FULL glyph height (24 px) apart: jt_text paints an
         * opaque per-glyph background, so a smaller gap lets the second
         * line overwrite the bottom of the first ("Port" row looked
         * clipped). 6 px of gap to the next row. Rows are positioned by
         * window slot k so the list scrolls when there are more VMUs
         * than fit. */
        int y = 52 + k * 54;
        bool sel = (i == vmu_sel);
        uint16_t fg = sel ? JT_COL_YELLOW : JT_COL_WHITE;
        uint16_t bg = sel ? JT_RGB565(30, 30, 30) : JT_COL_BLACK;
        if (sel) fill_rect(4, y, 632, 48, JT_RGB565(30, 30, 30));

        /* BIOS icon thumbnail wrapped with a 3px colored border that
         * matches the VMU's BIOS color (or a neutral grey when no
         * custom color is set). Border + thumb fit inside the row's
         * vertical extent (y..y+41) -- the thumb sits 3px inside the
         * border on every side. */
        /* Icon: 36-px thumbnail in a 3-px colored border (42 px total),
         * vertically centered in the 48-px band. */
        const int BORDER = 3;
        int iy = y + 3;
        uint16_t bc = v->custom_color ? JT_RGB565(v->cr, v->cg, v->cb)
                                      : JT_RGB565(120, 120, 120);
        fill_rect(10 - BORDER, iy, 36 + 2 * BORDER, 36 + 2 * BORDER, bc);
        if (v->bios_icon_valid)
            draw_thumb(10, iy + BORDER, &v->bios_icon, 36);
        else
            /* No ICONDATA -- show the VMU's selected preset shape, the
             * same one the Dreamcast BIOS file manager renders. */
            draw_preset_icon(10, iy + BORDER, v->icon_shape, 36);
        jt_text(56, y, fg, bg, "Port %c%d", 'A' + v->port, v->slot + 1);
        jt_text(360, y, v->real_mode ? JT_COL_GREEN : JT_COL_GREY, bg,
                "3D:%s", v->real_mode ? "on" : "off");
        jt_text(56, y + 24, JT_COL_GREY, bg,
                "%d saves   %d blks free", v->save_count, v->free_blocks);
    }

    /* Up/down arrows when the list scrolls past the visible window. */
    if (vmu_scroll > 0)
        jt_text(604, 50, JT_COL_GREY, JT_COL_BLACK, "^");
    if (vmu_scroll + VMU_VISIBLE < vmu_count)
        jt_text(604, 372, JT_COL_GREY, JT_COL_BLACK, "v");

    if (status_frames > 0)
        jt_text_centered(380, JT_COL_GREEN, JT_COL_BLACK, "%s", status_msg);
    jt_text_centered(404, JT_COL_GREY, JT_COL_BLACK,
                     "A: saves   Y: options   X: refresh");
    jt_text_centered(428, JT_COL_GREEN, JT_COL_BLACK, "Start: options menu");
}

static void draw_saves(void)
{
    vmu_info_t *v = &vmus[vmu_sel];
    jt_text_centered(24, JT_COL_YELLOW, JT_COL_BLACK,
                     "Port %c%d - Saves", 'A' + v->port, v->slot + 1);

    if (filtered_count == 0) {
        jt_text_centered(220, JT_COL_WHITE, JT_COL_BLACK, "No saves on this VMU.");
        jt_text_centered(428, JT_COL_GREEN, JT_COL_BLACK, "B: back   X: refresh");
        return;
    }

    /* Thumbs rendered at native 32x32 (no downscale); 9 rows fit
     * comfortably above the footer at y=380. */
    int row_h = 36;
    for (int row = 0; row < 9 && save_scroll + row < filtered_count; row++) {
        int fi = save_scroll + row;
        browser_entry_t *e = &entries[filtered[fi]];
        int y = 52 + row * row_h;
        uint16_t fg = (fi == save_sel) ? JT_COL_YELLOW : JT_COL_WHITE;
        const char *marker = (fi == save_sel) ? ">" : " ";

        int frame = 0;
        if (e->icon_valid && e->frame_count > 1) {
            float per = (e->anim_speed > 0) ? (float)e->anim_speed / 60.0f : 0.5f;
            if (per < 0.05f) per = 0.05f;
            frame = (int)(anim_time_s / per) % e->frame_count;
        }
        if (e->icon_valid) draw_thumb(8, y, &e->frames[frame], 32);
        else fill_rect(8, y, 32, 32, JT_COL_GREY);

        const char *anim = (e->frame_count > 1) ? "~" : " ";
        jt_text(48, y + 4, fg, JT_COL_BLACK, "%s %s%-12s %3d blk",
                marker, anim, e->filename, e->blocks);
    }

    if (status_frames > 0)
        jt_text_centered(380, JT_COL_GREEN, JT_COL_BLACK, "%s", status_msg);
    /* The library save (VMUICONS.VMS) opens into the Icon Library; every
     * other save opens a details popup. Label A to match the selection. */
    bool sel_is_lib = (filtered_count > 0 &&
                       strcmp(entries[filtered[save_sel]].filename,
                              JT_LIBRARY_FILENAME) == 0);
    jt_text_centered(404, JT_COL_GREY, JT_COL_BLACK,
                     sel_is_lib ? "A: open      Y: extract   X: delete   B: back"
                                : "A: details   Y: extract   X: delete   B: back");
    jt_text_centered(428, JT_COL_GREEN, JT_COL_BLACK, "Start: options menu");
}

/* Palette-editor-style modal: centered box, RGBA sliders, live swatch.
 * Two bool rows (Real Mode, Custom Color) + four channel rows. L/R
 * adjusts staged values; A commits everything, B discards. */
static void draw_options(void)
{
    vmu_info_t *v = &vmus[vmu_sel];

    /* Box centered horizontally, comfortably within the CRT-safe band. */
    const int x = 100, y = 64, w = 440, h = 384;
    fill_rect(x, y, w, h, JT_COL_BLACK);
    fill_rect(x, y, w, 2, JT_COL_YELLOW);
    fill_rect(x, y + h - 2, w, 2, JT_COL_YELLOW);
    fill_rect(x, y, 2, h, JT_COL_YELLOW);
    fill_rect(x + w - 2, y, 2, h, JT_COL_YELLOW);

    jt_text(x + 12, y + 8, JT_COL_YELLOW, JT_COL_BLACK,
            "VMU %c%d OPTIONS", 'A' + v->port, v->slot + 1);

    /* VMU preview (top-right): how the card shows in the Dreamcast file
     * manager -- the custom-color border (the color you're editing) around
     * the *effective* icon. A custom ICONDATA_VMS overrides the preset
     * BIOS shape on real hardware, so when one exists the preview shows it
     * and the preset index below is marked overridden. */
    {
        const int PV = 64, PB = 6;
        int psx = x + w - 12 - (PV + 2 * PB);
        int psy = y + 34;
        uint16_t pbc = staged_custom_color
                       ? JT_RGB565(staged_cr, staged_cg, staged_cb)
                       : JT_RGB565(120, 120, 120);
        fill_rect(psx, psy, PV + 2 * PB, PV + 2 * PB, pbc);
        if (v->bios_icon_valid)
            draw_thumb(psx + PB, psy + PB, &v->bios_icon, PV);
        else
            draw_preset_icon(psx + PB, psy + PB, staged_icon_shape, PV);
    }

    /* Row layout. y_row[i] is the baseline for row i. */
    int row_y[OPT_COUNT];
    for (int i = 0; i < OPT_COUNT; i++) row_y[i] = y + 46 + i * 30;

    /* Bool toggle rows. */
    {
        int ly = row_y[OPT_REAL_MODE];
        uint16_t fg = (opt_sel == OPT_REAL_MODE) ? JT_COL_YELLOW : JT_COL_WHITE;
        jt_text(x + 12, ly, fg, JT_COL_BLACK,
                "Real Mode (3D BIOS):  %s", staged_real_mode ? "ON " : "off");
    }
    {
        int ly = row_y[OPT_CUSTOM_COLOR];
        uint16_t fg = (opt_sel == OPT_CUSTOM_COLOR) ? JT_COL_YELLOW : JT_COL_WHITE;
        jt_text(x + 12, ly, fg, JT_COL_BLACK,
                "Custom BIOS color:    %s", staged_custom_color ? "ON " : "off");
    }

    /* RGBA sliders. Bar = 16 cells x 14px = 224px wide. Each cell
     * represents 16 raw values (16*16 = 256), matching the L/R step. */
    const char *labels[4] = { "R", "G", "B", "A" };
    uint8_t      vals[4]  = { staged_cr, staged_cg, staged_cb, staged_ca };
    for (int c = 0; c < 4; c++) {
        int row = OPT_COL_R + c;
        int ly  = row_y[row];
        bool active = staged_custom_color;
        uint16_t lc = (row == opt_sel) ? JT_COL_YELLOW : JT_COL_WHITE;
        if (!active) lc = JT_COL_GREY;
        jt_text(x + 12, ly, lc, JT_COL_BLACK, "%s", labels[c]);
        int cell_v = vals[c] / 16;
        for (int cell = 0; cell < 16; cell++) {
            int cx = x + 32 + cell * 14;
            int cy = ly + 4;
            uint16_t bg = (!active)         ? JT_RGB565(30, 30, 30) :
                          (cell == cell_v)  ? lc :
                          (cell <  cell_v)  ? JT_RGB565(80, 80, 80) :
                                              JT_RGB565(30, 30, 30);
            fill_rect(cx, cy, 12, 14, bg);
        }
        jt_text(x + 32 + 16 * 14 + 6, ly, JT_COL_GREY, JT_COL_BLACK,
                "%3d", vals[c]);
    }

    /* VMU icon. A custom ICONDATA_VMS is the "avatar"; the preset index
     * is only the fallback shown when there's none -- so we only surface
     * the index when no custom icon is set. */
    {
        int ly = row_y[OPT_ICON_SHAPE];
        bool custom = v->bios_icon_valid;
        uint16_t fg = (opt_sel == OPT_ICON_SHAPE) ? JT_COL_YELLOW
                    : custom ? JT_COL_GREY : JT_COL_WHITE;
        if (custom)
            jt_text(x + 12, ly, fg, JT_COL_BLACK, "VMU Icon:  Custom");
        else
            jt_text(x + 12, ly, fg, JT_COL_BLACK, "VMU Icon:  #%d", staged_icon_shape);
    }

    /* Status line + control hints, anchored above the bottom border. */
    if (status_frames > 0)
        jt_text(x + 12, y + h - 90, JT_COL_GREEN, JT_COL_BLACK, "%s", status_msg);
    /* A is contextual: on the VMU Icon row it opens the icon chooser;
     * everywhere else it commits + closes. Keep A/B fixed on the bottom
     * line and just relabel A, so the footer never says "A: save" while A
     * actually opens the icon menu. */
    bool on_icon = (opt_sel == OPT_ICON_SHAPE);
    jt_text(x + 12, y + h - 60, JT_COL_GREY, JT_COL_BLACK,
            on_icon ? "L/R: change shape   Y: set clock"
                    : "L/R: adjust         Y: set clock");
    jt_text(x + 12, y + h - 30, JT_COL_GREY, JT_COL_BLACK,
            on_icon ? (v->bios_icon_valid ? "A: change icon...   B: cancel"
                                          : "A: set custom       B: cancel")
                    : "A: save             B: cancel");
}

/* Render the mono channel of an icon as a VMU-LCD-style mini thumbnail
 * (navy "on" on green-cyan bg) so the details popup shows what the
 * VMU's physical LCD would look like. */
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
            uint16_t pixel = on ? JT_RGB565(29, 71, 129)
                                : JT_RGB565(138, 248, 219);
            if (sx + dx < 640 && sy + dy < 480)
                vram_s[(sy + dy) * 640 + (sx + dx)] = pixel;
        }
    }
}

static void draw_save_details(void)
{
    if (save_sel < 0 || save_sel >= filtered_count) { view = VIEW_SAVES; return; }
    browser_entry_t *e = &entries[filtered[save_sel]];

    const int x = 140, y = 92, w = 360, h = 280;
    fill_rect(x, y, w, h, JT_COL_BLACK);
    fill_rect(x, y, w, 2, JT_COL_YELLOW);
    fill_rect(x, y + h - 2, w, 2, JT_COL_YELLOW);
    fill_rect(x, y, 2, h, JT_COL_YELLOW);
    fill_rect(x + w - 2, y, 2, h, JT_COL_YELLOW);

    /* Running cursor so absent text fields don't leave gaps. Rows are a
     * full 24-px glyph height apart so opaque backgrounds don't clip. */
    int ty = y + 8;
    jt_text(x + 12, ty, JT_COL_YELLOW, JT_COL_BLACK, "SAVE DETAILS"); ty += 28;

    jt_text(x + 12, ty, JT_COL_GREY, JT_COL_BLACK, "File: %-12s", e->filename);
    ty += 24;
    if (e->name[0]) {        /* VMS 16-char name (header 0x00) */
        jt_text(x + 12, ty, JT_COL_WHITE, JT_COL_BLACK, "Name: %s", e->name);
        ty += 24;
    }
    if (e->description[0]) { /* VMS 32-char description (header 0x10), trimmed to width */
        jt_text(x + 12, ty, JT_COL_GREY, JT_COL_BLACK, "%.28s", e->description);
        ty += 24;
    }
    jt_text(x + 12, ty, JT_COL_GREY, JT_COL_BLACK,
            "Port %c%d", 'A' + e->port, e->slot + 1);
    ty += 28;

    /* Color + mono thumbs side-by-side; stats stacked in the right column. */
    if (e->icon_valid) draw_thumb(x + 12, ty, &e->frames[0], 32);
    else               fill_rect(x + 12, ty, 32, 32, JT_COL_GREY);
    if (e->icon_valid) draw_mono_thumb(x + 52, ty, &e->frames[0], 32);
    else               fill_rect(x + 52, ty, 32, 32, JT_RGB565(138, 248, 219));
    jt_text(x + 96, ty,      JT_COL_GREY, JT_COL_BLACK, "Size:   %d blk", e->blocks);
    jt_text(x + 96, ty + 24, JT_COL_GREY, JT_COL_BLACK, "Frames: %d", e->frame_count);
    int block_h = 48;
    if (e->frame_count > 1) {
        jt_text(x + 96, ty + 48, JT_COL_GREY, JT_COL_BLACK,
                "Anim:   %u ticks", (unsigned)e->anim_speed);
        block_h = 72;
    }
    ty += block_h;

    bool is_lib = (strcmp(e->filename, JT_LIBRARY_FILENAME) == 0);
    bool is_ico = (strcmp(e->filename, "ICONDATA_VMS")        == 0);
    const char *kind = is_ico ? "BIOS icon"      :
                       is_lib ? "Joypad library" :
                                "Game save";
    jt_text(x + 12, ty, JT_COL_GREY, JT_COL_BLACK, "Type: %s", kind);

    /* Footnote clears the 2-px bottom border (24-px glyphs). */
    jt_text(x + 12, y + h - 32, JT_COL_GREY, JT_COL_BLACK, "A or B: close");
}

static void draw_save_delete_confirm(void)
{
    if (save_sel < 0 || save_sel >= filtered_count) { view = VIEW_SAVES; return; }
    browser_entry_t *e = &entries[filtered[save_sel]];

    const int x = 140, y = 170, w = 360, h = 140;
    fill_rect(x, y, w, h, JT_COL_BLACK);
    fill_rect(x, y, w, 2, JT_COL_RED);
    fill_rect(x, y + h - 2, w, 2, JT_COL_RED);
    fill_rect(x, y, 2, h, JT_COL_RED);
    fill_rect(x + w - 2, y, 2, h, JT_COL_RED);

    jt_text(x + 12, y + 8,  JT_COL_RED,   JT_COL_BLACK, "DELETE SAVE?");
    jt_text(x + 12, y + 40, JT_COL_WHITE, JT_COL_BLACK, "%s", e->filename);
    jt_text(x + 12, y + 60, JT_COL_GREY,  JT_COL_BLACK,
            "Port %c%d   %d blocks",
            'A' + e->port, e->slot + 1, e->blocks);
    jt_text(x + 12, y + 88, JT_COL_GREY,  JT_COL_BLACK,
            "This cannot be undone.");
    jt_text(x + 12, y + h - 24, JT_COL_GREY, JT_COL_BLACK,
            "A: yes, delete    B: cancel");
}

/* Small chooser drawn over the options modal. */
static void draw_icon_actions(void)
{
    vmu_info_t *v = &vmus[vmu_sel];
    bool has_custom = v->bios_icon_valid;

    const int w = 300, h = 168, x = (640 - w) / 2, y = (480 - h) / 2;
    fill_rect(x, y, w, h, JT_COL_BLACK);
    fill_rect(x, y, w, 2, JT_COL_YELLOW);
    fill_rect(x, y + h - 2, w, 2, JT_COL_YELLOW);
    fill_rect(x, y, 2, h, JT_COL_YELLOW);
    fill_rect(x + w - 2, y, 2, h, JT_COL_YELLOW);

    jt_text(x + 16, y + 12, JT_COL_YELLOW, JT_COL_BLACK, "Custom Icon");

    const char *labels[3];
    int n;
    if (has_custom) { labels[0] = "Change icon"; labels[1] = "Remove icon";
                      labels[2] = "Back"; n = 3; }
    else            { labels[0] = "Change icon"; labels[1] = "Back"; n = 2; }

    for (int i = 0; i < n; i++) {
        int ly = y + 50 + i * 30;
        uint16_t fg = (i == icon_action_sel) ? JT_COL_YELLOW : JT_COL_WHITE;
        jt_text(x + 22, ly, fg, JT_COL_BLACK, "%s%s",
                (i == icon_action_sel) ? "> " : "  ", labels[i]);
    }
    jt_text(x + 16, y + h - 26, JT_COL_GREY, JT_COL_BLACK, "A: select   B: back");
}

static void browser_draw(void)
{
    switch (view) {
        case VIEW_VMUS:                  draw_vmus();    break;
        case VIEW_SAVES:                 draw_saves();   break;
        case VIEW_OPTIONS:               draw_options(); break;
        case VIEW_ICON_ACTIONS:          draw_options(); draw_icon_actions(); break;
        case VIEW_SAVE_DETAILS:          draw_saves(); draw_save_details(); break;
        case VIEW_SAVE_DELETE_CONFIRM:   draw_saves(); draw_save_delete_confirm(); break;
    }
}

const jt_mode_t jt_mode_browser = {
    .name   = "VMU File Manager",
    .enter  = browser_enter,
    .leave  = browser_leave,
    .update = browser_update,
    .draw   = browser_draw,
};
