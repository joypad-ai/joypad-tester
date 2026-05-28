/*
 * vmu_editor.c — two-pane VMU icon editor matching the web app's UX.
 *
 * Layout (640x480, 6x canvas zoom -> 192px square each):
 *   +-- y=8 ---- title bar -------------------------------------+
 *   |  COLOR (left, x=20)            MONO (right, x=240)        |
 *   |   canvas 192x192               canvas 192x192             |
 *   |   palette 4x4 swatches         palette 4x4 toggle states  |
 *   |   [Reset]                      [Reset] [Invert]           |
 *   +-- y=400 status / hints / BIOS 3D Mode flag -----------------+
 *
 * Two canvases visible at once. The "active pane" is whichever the
 * cursor is hovering over. A paints with the PRIMARY color, B paints
 * with the SECONDARY color (mirrors the web app's left-click /
 * right-click model). Mono pane: A = on, B = off. There is no "erase"
 * verb -- VMU icons have no transparent state, so erasing is just
 * painting with whatever the secondary color happens to be.
 *
 * Mono palette panel: 16 toggle cells, one per color-palette index.
 * Click a cell -> jt_canvas_mono_toggle_palette() flips every mono
 * pixel of that color. That's the color->silhouette translation
 * shortcut from the web app.
 *
 * Reset / Invert buttons live below their respective panes.
 */
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/maple/mouse.h>
#include <dc/biosfont.h>
#include <dc/video.h>
#include <stdio.h>
#include <string.h>

#include "vmu_editor.h"
#include "browser.h"
#include "../app.h"
#include "../ui/bfont_util.h"
#include "../ui/osk.h"
#include "../canvas/canvas.h"
#include "../vms/apply.h"
#include "../library/library.h"
#include "../ports/ports.h"
#include <time.h>
#include "../input/cursor.h"
#include "../video/mode.h"

/* From browser.c -- pulls any pending icon the user just extracted. */
bool jt_browser_consume_pending(jt_icon_t *out);

#define ZOOM         6                          /* 32 * 6 = 192 */
#define PANE_SIZE    (JT_CANVAS_W * ZOOM)       /* 192 */

#define COLOR_X      24
#define MONO_X       240
/* CANVAS_Y pushed down + inter-section gaps tightened (12->8) so the
 * whole editor (header .. footer) fits inside the CRT-safe band
 * (~y=24..452); overscan was clipping the title and footer. */
#define CANVAS_Y     52

#define COLOR_PAL_Y  (CANVAS_Y + PANE_SIZE + 8)
#define MONO_PAL_Y   COLOR_PAL_Y
#define BUTTON_Y     (COLOR_PAL_Y + 4 * 22 + 8)

#define SWATCH_W     22

/* Document-level buttons (Save / Name) live in the right-hand status
 * column, stacked below the status text, so they read as a separate
 * group from the per-pane Reset/Invert buttons. Real Mode is now a
 * VMU-level setting in the file manager -- apply preserves the prior
 * flag automatically, so the editor doesn't need its own toggle. */
#define DOCBTN_X     450
#define DOCBTN_W     176
#define DOCBTN_SAVE_Y 288
#define DOCBTN_NAME_Y 320

static jt_canvas_t canvas;
static bool initialized = false;

/* Forward declarations for helpers used by editor_enter before their
 * definitions appear in this file. */
static uint32_t aggregate_pad_buttons(void);

static uint32_t last_btns = 0;
static bool     last_action_button = false;     /* A held last frame */
static bool     last_b_button      = false;     /* B held last frame */

/* D-pad cursor-nudge auto-repeat: per-direction held-frame counters
 * (order: Up, Down, Left, Right). DELAY = frames before auto-repeat
 * begins (~270ms @ 60Hz); RATE = frames between repeats (~66ms). */
#define DPAD_REPEAT_DELAY 16
#define DPAD_REPEAT_RATE  4
static int dpad_hold[4] = {0, 0, 0, 0};

/* Unified save flow. The Save button drives a two-step wizard:
 *   stage 1 (SAVE_PICK_VMU)    -> choose which connected VMU
 *   stage 2 (SAVE_PICK_ACTION) -> "Apply as current icon" (ICONDATA_VMS)
 *                                 vs "Store in icon library" (VMUICONS.VMS)
 *   stage 3 (name OSK)         -> only on the library-store path
 * On completion the user is dropped into the File Manager (apply) or the
 * Icon Library (store) to see the result. */
typedef enum {
    SAVE_NONE = 0,
    SAVE_PICK_VMU,
    SAVE_PICK_ACTION,
} save_stage_t;
static save_stage_t  save_stage   = SAVE_NONE;
static int           picker_idx   = 0;      /* selection in current stage */
static int           save_vmu_port = -1, save_vmu_slot = -1;  /* chosen VMU */
static char          apply_result_text[80] = {0};
static unsigned      apply_result_frames = 0;
static int           apply_target_count = 0;
static struct { int port, slot; } apply_targets[JT_NUM_PORTS * JT_NUM_SLOTS];

/* While the library store is in flight after the name OSK, remember
 * which VMU to write to once the name comes back. */
static int  pending_save_port = -1;
static int  pending_save_slot = -1;
static bool save_awaiting_name = false;

/* Palette editor overlay state. Active when palette_edit_idx >= 0;
 * channel 0..3 = R, G, B, A (each 4 bits = 0..15). */
static int palette_edit_idx = -1;
static int palette_edit_channel = 0;
static uint8_t palette_edit_rgba[4];   /* current R, G, B, A */
static uint16_t palette_edit_snapshot;  /* original packed value for cancel */

static void rebuild_apply_targets(void)
{
    apply_target_count = 0;
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        for (int s = 0; s < JT_NUM_SLOTS; s++) {
            if (jt_ports[p].slots[s].kind == JT_SLOT_VMU) {
                apply_targets[apply_target_count].port = p;
                apply_targets[apply_target_count].slot = s;
                apply_target_count++;
            }
        }
    }
    if (picker_idx >= apply_target_count) picker_idx = 0;
}

/* Open the palette editor on a specific palette index. */
static void open_palette_editor(int idx)
{
    if (idx < 0 || idx >= JT_PALETTE_ENTRIES) return;
    palette_edit_idx = idx;
    palette_edit_channel = 0;
    palette_edit_snapshot = canvas.palette[idx];
    /* Unpack to 4-bit-per-channel form so the editor works in the
     * native on-disc resolution (no conversion-back rounding). */
    uint8_t r, g, b, a;
    jt_palette_unpack(canvas.palette[idx], &r, &g, &b, &a);
    palette_edit_rgba[0] = (uint8_t)(r / 17);   /* 0..15 */
    palette_edit_rgba[1] = (uint8_t)(g / 17);
    palette_edit_rgba[2] = (uint8_t)(b / 17);
    palette_edit_rgba[3] = (uint8_t)(a / 17);
}

static void palette_editor_apply_live(void)
{
    uint8_t r4 = palette_edit_rgba[0] & 0x0F;
    uint8_t g4 = palette_edit_rgba[1] & 0x0F;
    uint8_t b4 = palette_edit_rgba[2] & 0x0F;
    uint8_t a4 = palette_edit_rgba[3] & 0x0F;
    canvas.palette[palette_edit_idx] = jt_palette_pack(r4 * 17, g4 * 17,
                                                       b4 * 17, a4 * 17);
}

static void palette_editor_commit(void)
{
    /* Re-snapshot for undo (we replaced palette_edit_snapshot up
     * front so the live preview never pushes spurious undo entries). */
    jt_canvas_push_undo(&canvas);
    /* The undo snapshot just took the LIVE state. Need the pre-edit
     * value in the snapshot for proper rollback. Stash the live, set
     * the snapshot, push, restore live. */
    uint16_t live = canvas.palette[palette_edit_idx];
    canvas.palette[palette_edit_idx] = palette_edit_snapshot;
    /* The push_undo above already snapshotted the LIVE state; we
     * actually want the snapshot to contain pre-edit. Easiest: pop
     * the just-pushed snapshot, re-push with the pre-edit value. */
    canvas.undo_count--;     /* undo the push */
    canvas.undo_head = (canvas.undo_head - 1 + JT_UNDO_DEPTH) % JT_UNDO_DEPTH;
    jt_canvas_push_undo(&canvas);
    canvas.palette[palette_edit_idx] = live;
    palette_edit_idx = -1;
}

static void palette_editor_cancel(void)
{
    canvas.palette[palette_edit_idx] = palette_edit_snapshot;
    palette_edit_idx = -1;
}

/* Write canvas to the library save on a VMU. */
static void save_to_library(int port, int slot, const char *entry_name)
{
    maple_device_t *dev = maple_enum_dev(port, slot + 1);
    if (!dev || !dev->valid) {
        snprintf(apply_result_text, sizeof(apply_result_text),
                 "%c%d: VMU disconnected", 'A' + port, slot + 1);
        apply_result_frames = 180;
        return;
    }
    jt_library_t lib;
    int rc = jt_library_load(dev, &lib);
    if (rc == -1) {
        jt_library_init(&lib);
    } else if (rc < 0) {
        snprintf(apply_result_text, sizeof(apply_result_text),
                 "%c%d: Library load failed (%d)", 'A' + port, slot + 1, rc);
        apply_result_frames = 180;
        return;
    }
    jt_library_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    const char *name = (entry_name && entry_name[0]) ? entry_name : "Untitled";
    strncpy(entry.name, name, JT_LIBRARY_NAME_LEN);
    strncpy(entry.description, canvas.description, JT_LIBRARY_DESC_LEN);
    entry.timestamp = (uint64_t)time(NULL);
    entry.flags = JT_LIB_FLAG_COLOR | JT_LIB_FLAG_MONO;
    if (canvas.real_mode_flag) entry.flags |= JT_LIB_FLAG_REALMODE;
    jt_canvas_to_icon(&canvas, &entry.icon);

    if (jt_library_append(&lib, &entry) != 0) {
        snprintf(apply_result_text, sizeof(apply_result_text),
                 "%c%d: Library full (cap %d)",
                 'A' + port, slot + 1, lib.capacity);
        apply_result_frames = 180;
        return;
    }
    jt_show_busy("Saving to library...");
    int wrc = jt_library_save(dev, &lib);
    snprintf(apply_result_text, sizeof(apply_result_text),
             "%c%d: %s",
             'A' + port, slot + 1,
             (wrc == 0) ? "Saved to library" : "Library write failed");
    apply_result_frames = 180;
}

/* ---- VMU LCD live mirror ----
 * While the editor is open, every connected VMU's physical LCD mirrors
 * the live mono canvas. Throttled to ~10Hz and gated on actual change
 * so we don't flood the maple bus. On leave we push each VMU's stored
 * ICONDATA icon back so the displays revert to their "natural" look. */
static uint8_t lcd_last_pushed_mono[128];
static bool    lcd_last_pushed_valid = false;
static int     lcd_push_cooldown = 0;

static void push_live_mono_to_vmus(void)
{
    if (lcd_push_cooldown > 0) { lcd_push_cooldown--; return; }
    if (lcd_last_pushed_valid &&
        memcmp(lcd_last_pushed_mono, canvas.mono_bits,
               sizeof(canvas.mono_bits)) == 0) return;

    for (int p = 0; p < JT_NUM_PORTS; p++) {
        for (int s = 0; s < JT_NUM_SLOTS; s++) {
            if (jt_ports[p].slots[s].kind != JT_SLOT_VMU) continue;
            if (!jt_ports[p].slots[s].has_lcd) continue;
            jt_vmu_show_mono_bits(p, s, canvas.mono_bits);
        }
    }
    memcpy(lcd_last_pushed_mono, canvas.mono_bits, sizeof(canvas.mono_bits));
    lcd_last_pushed_valid = true;
    lcd_push_cooldown = 6;   /* ~10Hz cap */
}

static void restore_stored_icons_to_vmus(void)
{
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        for (int s = 0; s < JT_NUM_SLOTS; s++) {
            if (jt_ports[p].slots[s].kind != JT_SLOT_VMU) continue;
            if (!jt_ports[p].slots[s].has_lcd) continue;
            jt_vmu_show_stored_icon(p, s);
        }
    }
    lcd_last_pushed_valid = false;
}

static void editor_enter(void)
{
    if (!initialized) {
        jt_canvas_init(&canvas);
        initialized = true;
    }
    jt_icon_t loaded;
    if (jt_browser_consume_pending(&loaded)) {
        jt_canvas_from_icon(&canvas, &loaded);
        jt_canvas_mono_sync_palette(&canvas);
    }

    /* Reset all modal/input state on enter. Otherwise a Start press
     * that confirmed the options menu (selecting "VMU Icon Editor")
     * would be seen as a fresh edge by this mode's update on the
     * next frame and open the apply picker spuriously. Also clears
     * any stale picker / palette-editor / OSK-awaiting state from a
     * previous visit. */
    save_stage = SAVE_NONE;
    picker_idx = 0;
    palette_edit_idx = -1;
    save_awaiting_name = false;
    pending_save_port = -1;
    pending_save_slot = -1;
    last_btns = aggregate_pad_buttons();
    last_action_button = (last_btns & CONT_A) || jt_cursor.button_a;
    last_b_button      = (last_btns & CONT_B) || jt_cursor.button_b;
    dpad_hold[0] = dpad_hold[1] = dpad_hold[2] = dpad_hold[3] = 0;
    /* Force the live mirror to push on the first frame in the editor. */
    lcd_last_pushed_valid = false;
    lcd_push_cooldown = 0;
}

static void editor_leave(void)
{
    /* Revert every connected VMU's LCD back to its stored ICONDATA
     * icon (or blank if none) so other modes see the "natural" state. */
    restore_stored_icons_to_vmus();
}

static uint32_t aggregate_pad_buttons(void)
{
    uint32_t b = 0;
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        if (jt_ports[p].present && jt_ports[p].style == JT_STYLE_PAD) {
            b |= jt_ports[p].pad.buttons;
        }
    }
    return b;
}

/* Region hit-test. Returns the kind of region under (x,y). */
typedef enum {
    R_NONE = 0,
    R_COLOR_CANVAS,
    R_MONO_CANVAS,
    R_COLOR_SWATCH,    /* arg = palette index */
    R_MONO_TOGGLE,     /* arg = palette index */
    R_BTN_COLOR_RESET,
    R_BTN_MONO_RESET,
    R_BTN_MONO_INVERT,
    R_BTN_SAVE,
    R_BTN_NAME,
} region_t;

static region_t region_at(int x, int y, int *cx, int *cy, int *arg)
{
    if (cx) *cx = 0;
    if (cy) *cy = 0;
    if (arg) *arg = 0;

    /* Canvases. */
    if (x >= COLOR_X && x < COLOR_X + PANE_SIZE &&
        y >= CANVAS_Y && y < CANVAS_Y + PANE_SIZE) {
        if (cx) *cx = (x - COLOR_X) / ZOOM;
        if (cy) *cy = (y - CANVAS_Y) / ZOOM;
        return R_COLOR_CANVAS;
    }
    if (x >= MONO_X && x < MONO_X + PANE_SIZE &&
        y >= CANVAS_Y && y < CANVAS_Y + PANE_SIZE) {
        if (cx) *cx = (x - MONO_X) / ZOOM;
        if (cy) *cy = (y - CANVAS_Y) / ZOOM;
        return R_MONO_CANVAS;
    }
    /* Color palette strip — 4 rows x 4 cols at x=COLOR_X. */
    if (x >= COLOR_X && x < COLOR_X + 4 * SWATCH_W &&
        y >= COLOR_PAL_Y && y < COLOR_PAL_Y + 4 * SWATCH_W) {
        int row = (y - COLOR_PAL_Y) / SWATCH_W;
        int col = (x - COLOR_X) / SWATCH_W;
        if (arg) *arg = row * 4 + col;
        return R_COLOR_SWATCH;
    }
    /* Mono toggle strip. */
    if (x >= MONO_X && x < MONO_X + 4 * SWATCH_W &&
        y >= MONO_PAL_Y && y < MONO_PAL_Y + 4 * SWATCH_W) {
        int row = (y - MONO_PAL_Y) / SWATCH_W;
        int col = (x - MONO_X) / SWATCH_W;
        if (arg) *arg = row * 4 + col;
        return R_MONO_TOGGLE;
    }
    /* Per-pane canvas-op buttons (under the panes). */
    if (y >= BUTTON_Y && y < BUTTON_Y + 24) {
        if (x >= COLOR_X && x < COLOR_X + 60)              return R_BTN_COLOR_RESET;
        if (x >= MONO_X && x < MONO_X + 60)                return R_BTN_MONO_RESET;
        if (x >= MONO_X + 72 && x < MONO_X + 144)          return R_BTN_MONO_INVERT;
    }
    /* Document-level buttons (right column, stacked). */
    if (x >= DOCBTN_X && x < DOCBTN_X + DOCBTN_W) {
        if (y >= DOCBTN_SAVE_Y && y < DOCBTN_SAVE_Y + 24) return R_BTN_SAVE;
        if (y >= DOCBTN_NAME_Y && y < DOCBTN_NAME_Y + 24) return R_BTN_NAME;
    }
    return R_NONE;
}

static void open_save_flow(void)
{
    rebuild_apply_targets();
    if (apply_target_count == 0) {
        snprintf(apply_result_text, sizeof(apply_result_text),
                 "No VMU detected -- plug one in to save.");
        apply_result_frames = 180;
        save_stage = SAVE_NONE;
        return;
    }
    save_stage = SAVE_PICK_VMU;
    picker_idx = 0;
}

static void editor_update(float dt)
{
    (void)dt;

    /* OSK takes input precedence. */
    if (jt_osk_visible()) {
        jt_osk_update(dt);
        char buf[JT_OSK_MAX_LEN + 1];
        if (jt_osk_consume_text(buf, sizeof(buf))) {
            if (save_awaiting_name) {
                /* Library-store path: write the entry, then drop the
                 * user into the Icon Library so they see it land. */
                save_to_library(pending_save_port, pending_save_slot, buf);
                save_awaiting_name = false;
                pending_save_port = -1;
                pending_save_slot = -1;
                jt_request_mode(JT_MODE_LIB_BROWSER);
            } else {
                /* Plain naming: update description. */
                strncpy(canvas.description, buf, sizeof(canvas.description) - 1);
                canvas.description[sizeof(canvas.description) - 1] = '\0';
            }
        }
        return;
    }

    uint32_t btns = aggregate_pad_buttons();
    uint32_t edges = btns & ~last_btns;

    /* Palette editor overlay takes precedence over everything else. */
    if (palette_edit_idx >= 0) {
        if (edges & CONT_DPAD_UP)    if (palette_edit_channel > 0) palette_edit_channel--;
        if (edges & CONT_DPAD_DOWN)  if (palette_edit_channel < 3) palette_edit_channel++;
        if (edges & CONT_DPAD_LEFT) {
            if (palette_edit_rgba[palette_edit_channel] > 0) {
                palette_edit_rgba[palette_edit_channel]--;
                palette_editor_apply_live();
            }
        }
        if (edges & CONT_DPAD_RIGHT) {
            if (palette_edit_rgba[palette_edit_channel] < 15) {
                palette_edit_rgba[palette_edit_channel]++;
                palette_editor_apply_live();
            }
        }
        if (edges & CONT_A) palette_editor_commit();
        if (edges & CONT_B) palette_editor_cancel();
        last_btns = btns;
        return;
    }

    if (save_stage != SAVE_NONE) {
        int max_idx = (save_stage == SAVE_PICK_VMU)
                    ? apply_target_count - 1 : 1;
        if (edges & CONT_DPAD_UP)   { if (picker_idx > 0)       picker_idx--; }
        if (edges & CONT_DPAD_DOWN) { if (picker_idx < max_idx) picker_idx++; }

        if (edges & CONT_A) {
            if (save_stage == SAVE_PICK_VMU) {
                /* Lock in the chosen VMU, advance to the action step. */
                save_vmu_port = apply_targets[picker_idx].port;
                save_vmu_slot = apply_targets[picker_idx].slot;
                save_stage = SAVE_PICK_ACTION;
                picker_idx = 0;
            } else { /* SAVE_PICK_ACTION */
                if (picker_idx == 0) {
                    /* Apply as the VMU's current icon (ICONDATA_VMS),
                     * then jump to the File Manager to view it. */
                    jt_icon_t icon;
                    jt_canvas_to_icon(&canvas, &icon);
                    jt_show_busy("Applying icon...");
                    jt_apply_result_t rr =
                        jt_apply_icondata(&icon, save_vmu_port, save_vmu_slot);
                    snprintf(apply_result_text, sizeof(apply_result_text),
                             "%c%d: %s", 'A' + save_vmu_port,
                             save_vmu_slot + 1, jt_apply_result_str(rr));
                    apply_result_frames = 180;
                    save_stage = SAVE_NONE;
                    jt_request_mode(JT_MODE_BROWSER);
                } else {
                    /* Store in the icon library: ask for a name first,
                     * then the OSK handler writes it + jumps to the
                     * Icon Library. Seed the OSK with the description. */
                    pending_save_port = save_vmu_port;
                    pending_save_slot = save_vmu_slot;
                    save_awaiting_name = true;
                    save_stage = SAVE_NONE;
                    jt_osk_begin("Library entry name",
                                 canvas.description, JT_LIBRARY_NAME_LEN);
                }
            }
        }
        if (edges & CONT_B) {
            /* Step back a stage; cancel entirely from the first step. */
            if (save_stage == SAVE_PICK_ACTION) {
                save_stage = SAVE_PICK_VMU;
                picker_idx = 0;
            } else {
                save_stage = SAVE_NONE;
            }
        }
        last_btns = btns;
        return;
    }

    /* Y cycles tool (Draw / Fill). */
    if (edges & CONT_Y) {
        canvas.tool = (jt_tool_t)((canvas.tool + 1) % JT_TOOL_COUNT);
    }

    /* D-pad nudges the drawing cursor one canvas cell (ZOOM px) per
     * press, with hold-to-repeat so you can glide across the canvas.
     * Replaces the old hidden hotkeys (color cycle / 3D toggle / Name
     * OSK) -- those all have on-screen buttons now. */
    {
        static const uint32_t dirs[4] = {
            CONT_DPAD_UP, CONT_DPAD_DOWN, CONT_DPAD_LEFT, CONT_DPAD_RIGHT
        };
        static const int dx[4] = { 0, 0, -ZOOM, ZOOM };
        static const int dy[4] = { -ZOOM, ZOOM, 0, 0 };
        for (int d = 0; d < 4; d++) {
            if (btns & dirs[d]) {
                int h = dpad_hold[d]++;
                /* Fire on the initial press, then again once the hold
                 * passes the delay, repeating at the faster rate. */
                bool fire = (h == 0) ||
                            (h >= DPAD_REPEAT_DELAY &&
                             ((h - DPAD_REPEAT_DELAY) % DPAD_REPEAT_RATE) == 0);
                if (fire) {
                    jt_cursor.x += dx[d];
                    jt_cursor.y += dy[d];
                    if (jt_cursor.x < 0)   jt_cursor.x = 0;
                    if (jt_cursor.x > 639) jt_cursor.x = 639;
                    if (jt_cursor.y < 0)   jt_cursor.y = 0;
                    if (jt_cursor.y > 479) jt_cursor.y = 479;
                }
            } else {
                dpad_hold[d] = 0;
            }
        }
    }

    /* Determine cursor region + apply paint / button actions. A = paint
     * with primary color (mono on), B = paint with secondary (mono off).
     * Matches the web app's left-vs-right-click model exactly. */
    bool a_now  = (btns & CONT_A) || jt_cursor.button_a;
    bool b_now  = (btns & CONT_B) || jt_cursor.button_b;
    bool a_edge = a_now && !last_action_button;
    bool b_edge = b_now && !last_b_button;

    int cx, cy, arg;
    region_t r = region_at(jt_cursor.x, jt_cursor.y, &cx, &cy, &arg);

    if (r == R_COLOR_CANVAS) {
        canvas.layer = JT_LAYER_COLOR;
        if (a_now) {
            if (a_edge) jt_canvas_push_undo(&canvas);
            if (canvas.tool == JT_TOOL_FILL) {
                if (a_edge) jt_canvas_fill_color(&canvas, cx, cy, canvas.primary_color);
            } else {
                jt_canvas_paint_color(&canvas, cx, cy, canvas.primary_color);
            }
        } else if (b_now) {
            if (b_edge) jt_canvas_push_undo(&canvas);
            if (canvas.tool == JT_TOOL_FILL) {
                if (b_edge) jt_canvas_fill_color(&canvas, cx, cy, canvas.secondary_color);
            } else {
                jt_canvas_paint_color(&canvas, cx, cy, canvas.secondary_color);
            }
        }
    } else if (r == R_MONO_CANVAS) {
        canvas.layer = JT_LAYER_MONO;
        if (a_now) {
            if (a_edge) jt_canvas_push_undo(&canvas);
            if (canvas.tool == JT_TOOL_FILL) {
                if (a_edge) jt_canvas_mono_fill(&canvas, cx, cy, true);
            } else {
                jt_canvas_mono_set(&canvas, cx, cy, true);
            }
            jt_canvas_mono_sync_palette(&canvas);
        } else if (b_now) {
            if (b_edge) jt_canvas_push_undo(&canvas);
            if (canvas.tool == JT_TOOL_FILL) {
                if (b_edge) jt_canvas_mono_fill(&canvas, cx, cy, false);
            } else {
                jt_canvas_mono_set(&canvas, cx, cy, false);
            }
            jt_canvas_mono_sync_palette(&canvas);
        }
    } else if (r == R_COLOR_SWATCH) {
        /* A = set primary, B = set secondary (same as web app's
         * left-click / right-click). X = open palette editor. Force
         * a static-UI redraw on color change so the old yellow/cyan
         * selection rings get wiped — relying on the canvas-state
         * dirty hash alone is fragile when only metadata bits flip. */
        if (a_edge) canvas.primary_color   = (uint8_t)(arg & 0x0F);
        if (b_edge) canvas.secondary_color = (uint8_t)(arg & 0x0F);
        if (edges & CONT_X) open_palette_editor(arg);
    } else if (r == R_MONO_TOGGLE && a_edge) {
        jt_canvas_mono_toggle_palette(&canvas, arg);
    } else if (a_edge) {
        switch (r) {
            case R_BTN_COLOR_RESET:  jt_canvas_color_reset(&canvas); break;
            case R_BTN_MONO_RESET:   jt_canvas_mono_reset(&canvas);  break;
            case R_BTN_MONO_INVERT:  jt_canvas_mono_invert(&canvas); break;
            case R_BTN_SAVE:         open_save_flow();               break;
            case R_BTN_NAME:
                jt_osk_begin("Description (16 chars max)",
                             canvas.description, 16);
                break;
            default: break;
        }
    }

    last_btns = btns;
    last_action_button = a_now;
    last_b_button      = b_now;
    if (apply_result_frames > 0) apply_result_frames--;

    /* Mirror the live mono canvas to every connected VMU's LCD. */
    push_live_mono_to_vmus();
}

/* Direct framebuffer rect fill (same as v0.2.0 helper). */
static void fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0 || y < 0 || x + w > 640 || y + h > 480) return;
    for (int j = 0; j < h; j++) {
        uint16_t *row = vram_s + (y + j) * 640 + x;
        for (int i = 0; i < w; i++) row[i] = color;
    }
}

/* Fill a rect with an ARGB color composited over a transparency
 * checkerboard, so palette alpha is actually visible. Opaque (a>=255)
 * is a plain solid fill; lower alpha blends toward a 4px light/dark
 * checker (a==0 shows the bare checker). */
static void fill_rect_argb(int x, int y, int w, int h,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (a >= 255) {
        fill_rect(x, y, w, h, JT_RGB565(r, g, b));
        return;
    }
    if (x < 0 || y < 0 || x + w > 640 || y + h > 480) return;
    int ia = 255 - a;
    for (int j = 0; j < h; j++) {
        int py = y + j;
        uint16_t *row = vram_s + py * 640;
        for (int i = 0; i < w; i++) {
            int px = x + i;
            uint8_t bg = (((px >> 2) ^ (py >> 2)) & 1) ? 0xB0 : 0x70;
            uint8_t cr = (uint8_t)((r * a + bg * ia) / 255);
            uint8_t cg = (uint8_t)((g * a + bg * ia) / 255);
            uint8_t cb = (uint8_t)((b * a + bg * ia) / 255);
            row[px] = JT_RGB565(cr, cg, cb);
        }
    }
}

static void draw_canvases(void)
{
    /* Color pane. */
    for (int y = 0; y < JT_CANVAS_H; y++) {
        for (int x = 0; x < JT_CANVAS_W; x++) {
            uint8_t idx = canvas.color_indices[y * JT_CANVAS_W + x] & 0x0F;
            uint8_t r, g, b, a;
            jt_palette_unpack(canvas.palette[idx], &r, &g, &b, &a);
            fill_rect_argb(COLOR_X + x * ZOOM, CANVAS_Y + y * ZOOM, ZOOM, ZOOM,
                           r, g, b, a);
        }
    }
    /* Mono pane. */
    for (int y = 0; y < JT_CANVAS_H; y++) {
        for (int x = 0; x < JT_CANVAS_W; x++) {
            int p = y * JT_CANVAS_W + x;
            bool on = (canvas.mono_bits[p / 8] >> (7 - (p % 8))) & 1;
            uint16_t col = on ? JT_RGB565(29, 71, 129) : JT_RGB565(138, 248, 219);
            fill_rect(MONO_X + x * ZOOM, CANVAS_Y + y * ZOOM, ZOOM, ZOOM, col);
        }
    }
    /* 2px borders. */
    fill_rect(COLOR_X - 2, CANVAS_Y - 2, PANE_SIZE + 4, 2, JT_COL_YELLOW);
    fill_rect(COLOR_X - 2, CANVAS_Y + PANE_SIZE, PANE_SIZE + 4, 2, JT_COL_YELLOW);
    fill_rect(COLOR_X - 2, CANVAS_Y - 2, 2, PANE_SIZE + 4, JT_COL_YELLOW);
    fill_rect(COLOR_X + PANE_SIZE, CANVAS_Y - 2, 2, PANE_SIZE + 4, JT_COL_YELLOW);
    fill_rect(MONO_X - 2, CANVAS_Y - 2, PANE_SIZE + 4, 2, JT_COL_WHITE);
    fill_rect(MONO_X - 2, CANVAS_Y + PANE_SIZE, PANE_SIZE + 4, 2, JT_COL_WHITE);
    fill_rect(MONO_X - 2, CANVAS_Y - 2, 2, PANE_SIZE + 4, JT_COL_WHITE);
    fill_rect(MONO_X + PANE_SIZE, CANVAS_Y - 2, 2, PANE_SIZE + 4, JT_COL_WHITE);
}

static void draw_color_palette(void)
{
    /* Swatches sit on a 22-pixel grid stride but each rendered cell is
     * 24x24 (1-pixel border on each side). Adjacent cells overlap by 1
     * pixel; whichever cell is drawn LAST wins on the shared edge. To
     * keep selection rings visible we draw all swatches first, then
     * paint the primary (yellow) and secondary (cyan) rings on top.
     * Matches the web app's stacked primary/secondary indicators. */
    for (int i = 0; i < JT_PALETTE_ENTRIES; i++) {
        uint8_t r, g, b, a;
        jt_palette_unpack(canvas.palette[i], &r, &g, &b, &a);
        int row = i / 4, col = i % 4;
        int sx = COLOR_X + col * SWATCH_W;
        int sy = COLOR_PAL_Y + row * SWATCH_W;
        fill_rect(sx - 1, sy - 1, SWATCH_W + 2, SWATCH_W + 2, JT_COL_BLACK);
        fill_rect_argb(sx + 1, sy + 1, SWATCH_W - 2, SWATCH_W - 2, r, g, b, a);
    }
    /* Selection overlays drawn after all swatches so neighbors can't
     * clip them. Secondary first, primary second: if both point at the
     * same swatch, the primary (yellow) wins visually. */
    int sec = canvas.secondary_color & 0x0F;
    int pri = canvas.primary_color   & 0x0F;
    int sec_sx = COLOR_X      + (sec % 4) * SWATCH_W;
    int sec_sy = COLOR_PAL_Y  + (sec / 4) * SWATCH_W;
    int pri_sx = COLOR_X      + (pri % 4) * SWATCH_W;
    int pri_sy = COLOR_PAL_Y  + (pri / 4) * SWATCH_W;
    fill_rect(sec_sx - 1, sec_sy - 1, SWATCH_W + 2, 1, JT_COL_CYAN);
    fill_rect(sec_sx - 1, sec_sy + SWATCH_W, SWATCH_W + 2, 1, JT_COL_CYAN);
    fill_rect(sec_sx - 1, sec_sy - 1, 1, SWATCH_W + 2, JT_COL_CYAN);
    fill_rect(sec_sx + SWATCH_W, sec_sy - 1, 1, SWATCH_W + 2, JT_COL_CYAN);
    fill_rect(pri_sx - 1, pri_sy - 1, SWATCH_W + 2, 1, JT_COL_YELLOW);
    fill_rect(pri_sx - 1, pri_sy + SWATCH_W, SWATCH_W + 2, 1, JT_COL_YELLOW);
    fill_rect(pri_sx - 1, pri_sy - 1, 1, SWATCH_W + 2, JT_COL_YELLOW);
    fill_rect(pri_sx + SWATCH_W, pri_sy - 1, 1, SWATCH_W + 2, JT_COL_YELLOW);
}

static void draw_mono_toggles(void)
{
    /* Match the web app's mono palette panel exactly:
     *   - cell background = VMU LCD green-gradient color (so the
     *     panel reads as "what the VMU LCD looks like", not as
     *     color swatches)
     *   - dark navy blue filled indicator = mono pixel on
     *   - 3-state visual computed from actual canvas pixel counts:
     *       all canvas pixels of this color have mono-on -> large
     *           filled square
     *       some but not all on -> smaller centered square (partial)
     *       none on -> no indicator (just the green background)
     *   - colors with zero canvas pixels show no indicator at all. */
    const uint16_t bg_vmu  = JT_RGB565(138, 248, 219); /* VMU LCD bg */
    const uint16_t fg_dot  = JT_RGB565(29,  71,  129); /* VMU pixel-on */
    const uint16_t bg_dark = JT_RGB565(70, 100, 90);   /* darker bg for "empty color" */

    for (int i = 0; i < JT_PALETTE_ENTRIES; i++) {
        int row = i / 4, col = i % 4;
        int sx = MONO_X + col * SWATCH_W;
        int sy = MONO_PAL_Y + row * SWATCH_W;

        /* Count how many canvas pixels use this color index, and how
         * many of those have mono-on. */
        int on_count = 0, total_count = 0;
        for (int p = 0; p < JT_CANVAS_W * JT_CANVAS_H; p++) {
            if ((canvas.color_indices[p] & 0x0F) != i) continue;
            total_count++;
            bool on = (canvas.mono_bits[p / 8] >> (7 - (p % 8))) & 1;
            if (on) on_count++;
        }

        /* Cell background. Greyer when the color has no canvas
         * presence so the user can see at a glance which palette
         * slots are unused on the color canvas. */
        uint16_t bg = (total_count > 0) ? bg_vmu : bg_dark;
        fill_rect(sx + 1, sy + 1, SWATCH_W - 2, SWATCH_W - 2, bg);

        if (total_count == 0) continue;

        if (on_count == total_count) {
            /* All on: large filled blue, near-full-cell. */
            fill_rect(sx + 2, sy + 2, SWATCH_W - 4, SWATCH_W - 4, fg_dot);
        } else if (on_count > 0) {
            /* Partial on: smaller centered blue indicator. */
            fill_rect(sx + 5, sy + 5, SWATCH_W - 10, SWATCH_W - 10, fg_dot);
        }
        /* else: all off -> show just the green background, no dot. */
    }
}

static void draw_button(int x, int y, int w, const char *label, bool on)
{
    uint16_t bg = on ? JT_COL_YELLOW : JT_RGB565(60, 60, 60);
    uint16_t fg = on ? JT_COL_BLACK : JT_COL_WHITE;
    fill_rect(x, y, w, 24, bg);
    fill_rect(x, y, w, 1, JT_COL_WHITE);
    fill_rect(x, y + 23, w, 1, JT_COL_WHITE);
    fill_rect(x, y, 1, 24, JT_COL_WHITE);
    fill_rect(x + w - 1, y, 1, 24, JT_COL_WHITE);
    int tx = x + (w - (int)strlen(label) * 12) / 2;
    jt_text(tx, y + 2, fg, bg, "%s", label);
}

static void draw_buttons(void)
{
    /* Per-pane canvas ops, under each pane. */
    draw_button(COLOR_X, BUTTON_Y, 60, "Reset", false);
    draw_button(MONO_X,  BUTTON_Y, 60, "Reset", false);
    draw_button(MONO_X + 72, BUTTON_Y, 72, "Invert", false);

    /* Document-level ops in the right column, stacked as a group. */
    draw_button(DOCBTN_X, DOCBTN_SAVE_Y, DOCBTN_W, "Save", false);
    draw_button(DOCBTN_X, DOCBTN_NAME_Y, DOCBTN_W, "Name", false);
}

static const char *tool_name(jt_tool_t t)
{
    switch (t) {
        case JT_TOOL_DRAW: return "Draw";
        case JT_TOOL_FILL: return "Fill";
        default:           return "?";
    }
}

static void draw_status(void)
{
    /* Right-column status from x=450, y=40. */
    int x = 450, y = CANVAS_Y;
    jt_text(x, y,       JT_COL_YELLOW, JT_COL_BLACK, "Tool:  %s", tool_name(canvas.tool));
    jt_text(x, y + 24,  JT_COL_YELLOW, JT_COL_BLACK, "Pri:   %02d", canvas.primary_color);
    jt_text(x, y + 48,  JT_COL_CYAN,   JT_COL_BLACK, "Sec:   %02d", canvas.secondary_color);
    jt_text(x, y + 72,  JT_COL_GREY,   JT_COL_BLACK, "Undo:  %d/%d",
            canvas.undo_count, JT_UNDO_DEPTH);
    jt_text(x, y + 96,  JT_COL_GREY,   JT_COL_BLACK, "Name:");
    jt_text(x, y + 120, JT_COL_CYAN,   JT_COL_BLACK, "%s",
            canvas.description[0] ? canvas.description : "(none)");

    int cx, cy, arg;
    region_t r = region_at(jt_cursor.x, jt_cursor.y, &cx, &cy, &arg);
    if (r == R_COLOR_CANVAS || r == R_MONO_CANVAS) {
        jt_text(x, y + 152, JT_COL_GREEN, JT_COL_BLACK, "Px:    %02d,%02d", cx, cy);
    } else {
        jt_text(x, y + 152, JT_COL_GREY,  JT_COL_BLACK, "Px:    --,--");
    }

    int vmu_count = 0;
    for (int p = 0; p < JT_NUM_PORTS; p++)
        for (int s = 0; s < JT_NUM_SLOTS; s++)
            if (jt_ports[p].slots[s].kind == JT_SLOT_VMU) vmu_count++;
    jt_text(x, y + 176, JT_COL_GREEN, JT_COL_BLACK, "VMUs:  %d", vmu_count);

}

/* Draw the cell-highlight ring (when over a canvas) + the arrow
 * pointer. With double buffering the whole frame is repainted each
 * tick, so there's nothing to save/restore -- just draw on top. */
static void draw_cursor_crosshair(void)
{
    int cx, cy, arg;
    region_t r = region_at(jt_cursor.x, jt_cursor.y, &cx, &cy, &arg);

    if (r == R_COLOR_CANVAS || r == R_MONO_CANVAS) {
        int base_x = (r == R_COLOR_CANVAS) ? COLOR_X : MONO_X;
        int sx = base_x + cx * ZOOM;
        int sy = CANVAS_Y + cy * ZOOM;
        fill_rect(sx - 1, sy - 1, ZOOM + 2, 1, JT_COL_YELLOW);
        fill_rect(sx - 1, sy + ZOOM, ZOOM + 2, 1, JT_COL_YELLOW);
        fill_rect(sx - 1, sy - 1, 1, ZOOM + 2, JT_COL_YELLOW);
        fill_rect(sx + ZOOM, sy - 1, 1, ZOOM + 2, JT_COL_YELLOW);
    }

    /* Screen-space arrow pointer. 9x14 classic mac/windows bitmap.
     * '.' = transparent, 'o' = white interior, 'X' = black outline. */
    static const char *arrow[14] = {
        "X........",  /* tip */
        "XX.......",
        "XoX......",
        "XooX.....",
        "XoooX....",
        "XooooX...",
        "XoooooX..",
        "XooooooX.",
        "XoooooooX",
        "XoooooXXX",
        "XooXooX..",
        "XoX.XooX.",
        "XX..XooX.",
        "X....XX..",
    };
    int px = jt_cursor.x, py = jt_cursor.y;
    for (int dy = 0; dy < 14; dy++) {
        for (int dx = 0; arrow[dy][dx] && dx < 9; dx++) {
            char c = arrow[dy][dx];
            int sx = px + dx, sy = py + dy;
            if (sx < 0 || sx >= 640 || sy < 0 || sy >= 480) continue;
            if (c == 'X')      vram_s[sy * 640 + sx] = JT_COL_BLACK;
            else if (c == 'o') vram_s[sy * 640 + sx] = JT_COL_WHITE;
        }
    }
}

static void draw_palette_editor(void)
{
    if (palette_edit_idx < 0) return;
    /* Modal centered at (140, 120, 360, 220). */
    const int x = 140, y = 120, w = 360, h = 220;
    fill_rect(x, y, w, h, JT_COL_BLACK);
    fill_rect(x, y, w, 2, JT_COL_YELLOW);
    fill_rect(x, y + h - 2, w, 2, JT_COL_YELLOW);
    fill_rect(x, y, 2, h, JT_COL_YELLOW);
    fill_rect(x + w - 2, y, 2, h, JT_COL_YELLOW);

    jt_text(x + 12, y + 8, JT_COL_YELLOW, JT_COL_BLACK,
            "EDIT PALETTE COLOR %02d", palette_edit_idx);

    /* Live preview swatch -- 4-bit channels scaled to 8-bit, composited
     * over the transparency checker so the alpha slider is visible. */
    uint8_t r8 = (uint8_t)((palette_edit_rgba[0] & 0x0F) * 17);
    uint8_t g8 = (uint8_t)((palette_edit_rgba[1] & 0x0F) * 17);
    uint8_t b8 = (uint8_t)((palette_edit_rgba[2] & 0x0F) * 17);
    uint8_t a8 = (uint8_t)((palette_edit_rgba[3] & 0x0F) * 17);
    fill_rect_argb(x + 12, y + 36, 48, 48, r8, g8, b8, a8);
    fill_rect(x + 11, y + 35, 50, 1, JT_COL_WHITE);
    fill_rect(x + 11, y + 84, 50, 1, JT_COL_WHITE);
    fill_rect(x + 11, y + 35, 1, 50, JT_COL_WHITE);
    fill_rect(x + 60, y + 35, 1, 50, JT_COL_WHITE);

    /* Channel sliders. */
    const char *labels[4] = { "R", "G", "B", "A" };
    for (int c = 0; c < 4; c++) {
        int ly = y + 40 + c * 32;
        uint16_t lc = (c == palette_edit_channel) ? JT_COL_YELLOW : JT_COL_WHITE;
        jt_text(x + 80, ly, lc, JT_COL_BLACK, "%s", labels[c]);
        /* 16-step bar: 15 cells of 16px each. */
        for (int v = 0; v < 16; v++) {
            int cx = x + 100 + v * 14;
            int cy = ly + 4;
            uint16_t bg = (v == palette_edit_rgba[c]) ? lc :
                          (v < palette_edit_rgba[c]) ? JT_RGB565(80, 80, 80) :
                                                       JT_RGB565(30, 30, 30);
            fill_rect(cx, cy, 12, 14, bg);
        }
        jt_text(x + 100 + 16 * 14 + 8, ly, JT_COL_GREY, JT_COL_BLACK,
                "%2d", palette_edit_rgba[c]);
    }

    /* Modal is 360px wide; bfont = 12px/char -> ~28 chars usable
     * inside the inner padding. Two short lines instead of one long.
     * Pulled up off the bottom border so opaque bfont doesn't paint
     * over the 2px frame at the bottom of the box. */
    jt_text(x + 12, y + h - 56, JT_COL_GREY, JT_COL_BLACK,
            "Up/Dn:channel  L/R:value");
    jt_text(x + 12, y + h - 32, JT_COL_GREY, JT_COL_BLACK,
            "A:save  B:cancel");
}

static void draw_picker(void)
{
    if (save_stage == SAVE_NONE) return;

    /* 400px box centered on the 640px screen (x=120..520) so the
     * longest line -- the action sub-captions -- fits with margin. */
    const int x = 120, y = 130, w = 400, h = 220;
    fill_rect(x, y, w, h, JT_COL_BLACK);
    fill_rect(x, y, w, 2, JT_COL_YELLOW);
    fill_rect(x, y + h - 2, w, 2, JT_COL_YELLOW);
    fill_rect(x, y, 2, h, JT_COL_YELLOW);
    fill_rect(x + w - 2, y, 2, h, JT_COL_YELLOW);

    if (save_stage == SAVE_PICK_VMU) {
        jt_text(x + 12, y + 10, JT_COL_YELLOW, JT_COL_BLACK, "SAVE - SELECT VMU");
        for (int i = 0; i < apply_target_count; i++) {
            uint16_t fg = (i == picker_idx) ? JT_COL_YELLOW : JT_COL_WHITE;
            const char *m = (i == picker_idx) ? ">" : " ";
            jt_text(x + 16, y + 48 + i * 28, fg, JT_COL_BLACK,
                    "%s Port %c, Slot %d",
                    m, 'A' + apply_targets[i].port,
                    apply_targets[i].slot + 1);
        }
        jt_text(x + 12, y + 190, JT_COL_GREY, JT_COL_BLACK,
                "A: next    B: cancel");
    } else { /* SAVE_PICK_ACTION */
        jt_text(x + 12, y + 10, JT_COL_YELLOW, JT_COL_BLACK,
                "SAVE TO VMU %c%d", 'A' + save_vmu_port, save_vmu_slot + 1);
        const char *opts[2] = {
            "Apply as current icon",
            "Store in icon library",
        };
        const char *sub[2] = {
            "set the VMU's BIOS icon",
            "add to VMUICONS archive",
        };
        for (int i = 0; i < 2; i++) {
            uint16_t fg = (i == picker_idx) ? JT_COL_YELLOW : JT_COL_WHITE;
            const char *m = (i == picker_idx) ? ">" : " ";
            jt_text(x + 16, y + 50 + i * 56, fg, JT_COL_BLACK, "%s %s", m, opts[i]);
            jt_text(x + 32, y + 74 + i * 56, JT_COL_GREY, JT_COL_BLACK, "%s", sub[i]);
        }
        jt_text(x + 12, y + 190, JT_COL_GREY, JT_COL_BLACK,
                "A: select    B: back");
    }
}

static bool any_modal_visible(void)
{
    return (save_stage != SAVE_NONE) || (palette_edit_idx >= 0);
}

static void editor_draw(void)
{
    /* Full redraw every frame onto the hidden back buffer (main.c
     * cleared it + flips after). No dirty flags / save-restore needed
     * with double buffering -- just paint the whole UI top to bottom. */

    /* OSK is a full-screen modal. */
    if (jt_osk_visible()) {
        jt_text_centered(24, JT_COL_YELLOW, JT_COL_BLACK, "Naming");
        jt_osk_draw();
        return;
    }

    /* Base editor UI. */
    jt_text_centered(24, JT_COL_YELLOW, JT_COL_BLACK, "VMU Icon Editor");
    draw_canvases();
    draw_color_palette();
    draw_mono_toggles();
    draw_buttons();
    draw_status();

    if (apply_result_frames > 0) {
        jt_text_centered(404, JT_COL_GREEN, JT_COL_BLACK,
                         "%s", apply_result_text);
    } else {
        jt_text_centered(404, JT_COL_GREY, JT_COL_BLACK,
                         "A primary  B secondary  Y tool  D-pad move");
    }
    jt_text_centered(428, JT_COL_GREEN, JT_COL_BLACK,
                     "Start: options menu");

    /* Modal overlays draw on top of the base UI. */
    if (any_modal_visible()) {
        draw_picker();
        draw_palette_editor();
    } else {
        /* Cursor only when no modal is capturing input. */
        draw_cursor_crosshair();
    }
}

const jt_mode_t jt_mode_vmu_editor = {
    .name   = "VMU Icon Editor",
    .enter  = editor_enter,
    .leave  = editor_leave,
    .update = editor_update,
    .draw   = editor_draw,
};
