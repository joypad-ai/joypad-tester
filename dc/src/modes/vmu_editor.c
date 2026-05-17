/*
 * vmu_editor.c — interactive VMU icon editor mode.
 *
 * Layout (640x480 progressive, 12px text cells):
 *   y= 8   title bar
 *   y= 32  canvas (32x32 cells, 10px per cell -> 320x320, at x=24)
 *   y= 32  palette strip + tool readout (right of canvas, x=370+)
 *   y=400  current desc + flags
 *   y=456  controls hint
 *
 * Input model: the unified cursor (jt_cursor) gives us a single
 * screen-space pointer driven by pad analog OR mouse. We translate
 * cursor -> canvas cell each frame; A paints, B erases, X picks, Y
 * cycles tools, L/R swap layers (color <-> mono), Start triggers Save.
 * The drawing/saving plumbing for ICONDATA_VMS / library lives in
 * other modules; this file is purely the editor UI.
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
#include "../ui/bfont_util.h"
#include "../canvas/canvas.h"
#include "../vms/apply.h"
#include "../ports/ports.h"
#include "../input/cursor.h"
#include "../video/mode.h"

/* From browser.c -- pulls any pending icon the user just extracted. */
bool jt_browser_consume_pending(jt_icon_t *out);

#define CANVAS_X       24
#define CANVAS_Y       40
#define ZOOM_PROGRESSIVE 10  /* 10x -> 320x320 pixel canvas */
#define ZOOM_INTERLACED  10  /* keep same; only line thicknesses change */

static jt_canvas_t canvas;
static bool initialized = false;

/* Edge-detection state. Buttons in jt_ports[].pad.buttons are raw —
 * we trigger one-shot actions (tool cycle, layer flip, undo) on edge. */
static uint32_t last_btns_or = 0;
static bool     last_mouse_a = false;

/* Apply-to-VMU state. The editor opens a tiny picker overlay when
 * the user hits Start: list of detected VMUs, A confirms write, B
 * cancels. Result string lingers a few frames after the action. */
static bool          apply_picker_visible = false;
static int           apply_picker_idx = 0;
static char          apply_result_text[64] = {0};
static unsigned      apply_result_frames = 0;
static int           apply_target_count = 0;
static struct { int port, slot; } apply_targets[JT_NUM_PORTS * JT_NUM_SLOTS];

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
    if (apply_picker_idx >= apply_target_count) apply_picker_idx = 0;
}

static void editor_enter(void)
{
    if (!initialized) {
        jt_canvas_init(&canvas);
        initialized = true;
    }
    /* Consume any icon the browser extracted on its way here. */
    jt_icon_t loaded;
    if (jt_browser_consume_pending(&loaded)) {
        jt_canvas_from_icon(&canvas, &loaded);
    }
}

static void editor_leave(void) { /* persistence handled by canvas state */ }

/* Aggregate button state across all connected pads, so the editor
 * doesn't care which port the user happens to be on. */
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

static int zoom(void)
{
    return jt_video_is_progressive() ? ZOOM_PROGRESSIVE : ZOOM_INTERLACED;
}

/* Translate the unified cursor's screen coords to canvas coords.
 * Returns false when the cursor isn't over the canvas region. */
static bool cursor_canvas_coords(int *cx, int *cy)
{
    int z = zoom();
    int x_rel = jt_cursor.x - CANVAS_X;
    int y_rel = jt_cursor.y - CANVAS_Y;
    if (x_rel < 0 || y_rel < 0) return false;
    *cx = x_rel / z;
    *cy = y_rel / z;
    return (*cx < JT_CANVAS_W && *cy < JT_CANVAS_H);
}

static void editor_update(float dt)
{
    (void)dt;
    uint32_t btns = aggregate_pad_buttons();
    /* Bits that JUST went from 0 -> 1 this frame. */
    uint32_t edges = btns & ~last_btns_or;

    /* Apply picker takes over input when visible. */
    if (apply_picker_visible) {
        if (edges & CONT_DPAD_UP)   if (apply_picker_idx > 0) apply_picker_idx--;
        if (edges & CONT_DPAD_DOWN) if (apply_picker_idx < apply_target_count - 1) apply_picker_idx++;
        if (edges & CONT_A && apply_target_count > 0) {
            jt_icon_t icon;
            jt_canvas_to_icon(&canvas, &icon);
            int port = apply_targets[apply_picker_idx].port;
            int slot = apply_targets[apply_picker_idx].slot;
            jt_apply_result_t r = jt_apply_icondata(&icon, port, slot);
            snprintf(apply_result_text, sizeof(apply_result_text),
                     "%c%d: %s", 'A' + port, slot + 1, jt_apply_result_str(r));
            apply_result_frames = 180;  /* ~3s at 60fps */
            apply_picker_visible = false;
        }
        if (edges & CONT_B) apply_picker_visible = false;
        last_btns_or = btns;
        return;
    }

    /* Start opens the apply picker. */
    if (edges & CONT_START) {
        rebuild_apply_targets();
        apply_picker_visible = (apply_target_count > 0);
        if (!apply_picker_visible) {
            snprintf(apply_result_text, sizeof(apply_result_text),
                     "No VMU detected -- plug one in to Apply.");
            apply_result_frames = 180;
        }
        last_btns_or = btns;
        return;
    }

    /* L/R triggers cycle the active layer between color and mono. */
    if (edges & CONT_DPAD_LEFT) {
        canvas.layer = (canvas.layer == JT_LAYER_COLOR) ? JT_LAYER_MONO : JT_LAYER_COLOR;
    }
    if (edges & CONT_DPAD_RIGHT) {
        canvas.layer = (canvas.layer == JT_LAYER_COLOR) ? JT_LAYER_MONO : JT_LAYER_COLOR;
    }
    /* D-pad up/down cycles the current color (color layer only). */
    if (canvas.layer == JT_LAYER_COLOR) {
        if (edges & CONT_DPAD_UP)   canvas.current_color = (canvas.current_color + JT_PALETTE_ENTRIES - 1) & 0x0F;
        if (edges & CONT_DPAD_DOWN) canvas.current_color = (canvas.current_color + 1) & 0x0F;
    }
    /* Y cycles tool. */
    if (edges & CONT_Y) {
        canvas.tool = (jt_tool_t)((canvas.tool + 1) % JT_TOOL_COUNT);
    }

    /* Paint while A held; erase while B held; pick on X edge; fill on
     * Start edge (the user has to commit). Stroke-undo: push one
     * snapshot per stroke (transition from "not painting" to
     * "painting"), not per pixel. */
    bool a_now = (btns & CONT_A) != 0 || jt_cursor.button_a;
    bool b_now = (btns & CONT_B) != 0 || jt_cursor.button_b;
    bool x_edge = (edges & CONT_X) != 0;
    bool a_edge = a_now && !last_mouse_a;

    int cx, cy;
    bool over_canvas = cursor_canvas_coords(&cx, &cy);

    if (over_canvas) {
        if (a_now) {
            if (a_edge) jt_canvas_push_undo(&canvas);
            if (canvas.tool == JT_TOOL_FILL && a_edge) {
                jt_canvas_fill(&canvas, cx, cy);
            } else if (canvas.tool == JT_TOOL_PICK && a_edge) {
                jt_canvas_pick(&canvas, cx, cy);
            } else {
                jt_canvas_set_pixel(&canvas, cx, cy);
            }
        } else if (b_now) {
            if (!last_mouse_a) jt_canvas_push_undo(&canvas);
            jt_canvas_erase_pixel(&canvas, cx, cy);
        } else if (x_edge) {
            jt_canvas_pick(&canvas, cx, cy);
        }
    }

    last_btns_or = btns;
    last_mouse_a = a_now;

    if (apply_result_frames > 0) apply_result_frames--;
}

static void draw_apply_picker(void)
{
    if (!apply_picker_visible) return;
    /* Centered modal at (170, 130, 300, 220). */
    const int x = 170, y = 130;
    jt_text(x, y, JT_COL_YELLOW, JT_COL_BLACK,
            "+- APPLY ICONDATA_VMS -+");
    if (apply_target_count == 0) {
        jt_text(x + 16, y + 40, JT_COL_RED, JT_COL_BLACK,
                "No VMU detected.");
    } else {
        for (int i = 0; i < apply_target_count; i++) {
            uint16_t fg = (i == apply_picker_idx) ? JT_COL_YELLOW : JT_COL_WHITE;
            const char *marker = (i == apply_picker_idx) ? ">" : " ";
            jt_text(x + 16, y + 40 + i * 28, fg, JT_COL_BLACK,
                    "%s Port %c, Slot %d",
                    marker,
                    'A' + apply_targets[i].port,
                    apply_targets[i].slot + 1);
        }
    }
    jt_text(x + 16, y + 200, JT_COL_GREY, JT_COL_BLACK,
            "A: write   B: cancel");
}

/* Render a single 1px-bordered solid block. Faster than bfont fills
 * for the canvas itself — we write directly into vram_s. */
static void fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0 || y < 0 || x + w > 640 || y + h > 480) return;
    for (int j = 0; j < h; j++) {
        uint16_t *row = vram_s + (y + j) * 640 + x;
        for (int i = 0; i < w; i++) row[i] = color;
    }
}

static void draw_canvas(void)
{
    int z = zoom();
    for (int y = 0; y < JT_CANVAS_H; y++) {
        for (int x = 0; x < JT_CANVAS_W; x++) {
            uint16_t argb = jt_canvas_pixel_argb1555(&canvas, x, y);
            /* Convert ARGB1555 -> RGB565 for the framebuffer. Strip
             * alpha; remap R/G/B widths (5->5, 5->6, 5->5). */
            uint16_t r5 = (argb >> 10) & 0x1F;
            uint16_t g5 = (argb >> 5)  & 0x1F;
            uint16_t b5 = (argb)       & 0x1F;
            uint16_t rgb565 = (uint16_t)((r5 << 11) | ((g5 << 1) << 5) | b5);
            fill_rect(CANVAS_X + x * z, CANVAS_Y + y * z, z, z, rgb565);
        }
    }
    /* 2px outer border around the canvas so the user can find it. */
    int cw = JT_CANVAS_W * z, ch = JT_CANVAS_H * z;
    fill_rect(CANVAS_X - 2, CANVAS_Y - 2, cw + 4, 2, JT_COL_YELLOW);
    fill_rect(CANVAS_X - 2, CANVAS_Y + ch, cw + 4, 2, JT_COL_YELLOW);
    fill_rect(CANVAS_X - 2, CANVAS_Y - 2, 2, ch + 4, JT_COL_YELLOW);
    fill_rect(CANVAS_X + cw, CANVAS_Y - 2, 2, ch + 4, JT_COL_YELLOW);
}

static void draw_palette_strip(void)
{
    /* Palette to the right of the canvas. 4 wide x 4 tall grid,
     * each swatch 24px square. */
    int z = zoom();
    int x0 = CANVAS_X + JT_CANVAS_W * z + 16;
    int y0 = CANVAS_Y;
    int sw = 24;
    int pad = 4;
    for (int i = 0; i < JT_PALETTE_ENTRIES; i++) {
        uint8_t r, g, b, a;
        jt_palette_unpack(canvas.palette[i], &r, &g, &b, &a);
        uint16_t rgb565 = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        int row = i / 4, col = i % 4;
        int sx = x0 + col * (sw + pad);
        int sy = y0 + row * (sw + pad);
        fill_rect(sx, sy, sw, sw, rgb565);
        /* Highlight current color with a yellow inset frame. */
        if (i == canvas.current_color) {
            fill_rect(sx - 2, sy - 2, sw + 4, 2, JT_COL_YELLOW);
            fill_rect(sx - 2, sy + sw, sw + 4, 2, JT_COL_YELLOW);
            fill_rect(sx - 2, sy - 2, 2, sw + 4, JT_COL_YELLOW);
            fill_rect(sx + sw, sy - 2, 2, sw + 4, JT_COL_YELLOW);
        }
    }
}

static const char *tool_name(jt_tool_t t)
{
    switch (t) {
        case JT_TOOL_PAINT: return "Paint";
        case JT_TOOL_ERASE: return "Erase";
        case JT_TOOL_FILL:  return "Fill";
        case JT_TOOL_PICK:  return "Pick";
        default:            return "?";
    }
}

static void draw_status(void)
{
    int z = zoom();
    int x0 = CANVAS_X + JT_CANVAS_W * z + 16;
    int y_status = CANVAS_Y + 4 * (24 + 4) + 16;

    jt_text(x0, y_status, JT_COL_WHITE, JT_COL_BLACK,
            "Layer: %s", canvas.layer == JT_LAYER_COLOR ? "Color" : "Mono");
    jt_text(x0, y_status + 24, JT_COL_WHITE, JT_COL_BLACK,
            "Tool:  %s", tool_name(canvas.tool));
    jt_text(x0, y_status + 48, JT_COL_WHITE, JT_COL_BLACK,
            "Color: %d", canvas.current_color);

    int cx, cy;
    if (cursor_canvas_coords(&cx, &cy)) {
        jt_text(x0, y_status + 72, JT_COL_CYAN, JT_COL_BLACK,
                "Px:    %02d,%02d", cx, cy);
    } else {
        jt_text(x0, y_status + 72, JT_COL_GREY, JT_COL_BLACK,
                "Px:    --,--");
    }
    jt_text(x0, y_status + 96, JT_COL_GREY, JT_COL_BLACK,
            "Undo:  %d/%d", canvas.undo_count, JT_UNDO_DEPTH);

    /* Count detected VMUs so the user knows whether Apply has a
     * target ready (the apply wiring lives in modes/about.c sibling
     * work coming in v0.2.x). */
    int vmu_count = 0;
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        for (int s = 0; s < JT_NUM_SLOTS; s++) {
            if (jt_ports[p].slots[s].kind == JT_SLOT_VMU) vmu_count++;
        }
    }
    jt_text(x0, y_status + 120, JT_COL_GREEN, JT_COL_BLACK,
            "VMUs:  %d", vmu_count);
}

static void draw_cursor(void)
{
    /* Outline crosshair around the cell the cursor is over. */
    int cx, cy;
    if (!cursor_canvas_coords(&cx, &cy)) return;
    int z = zoom();
    int sx = CANVAS_X + cx * z;
    int sy = CANVAS_Y + cy * z;
    /* 2px outline frame around the cell. */
    fill_rect(sx - 1, sy - 1, z + 2, 1, JT_COL_WHITE);
    fill_rect(sx - 1, sy + z, z + 2, 1, JT_COL_WHITE);
    fill_rect(sx - 1, sy - 1, 1, z + 2, JT_COL_WHITE);
    fill_rect(sx + z, sy - 1, 1, z + 2, JT_COL_WHITE);
}

static void editor_draw(void)
{
    jt_text_centered(8, JT_COL_YELLOW, JT_COL_BLACK, "VMU Icon Editor");

    draw_canvas();
    draw_palette_strip();
    draw_status();
    draw_cursor();
    draw_apply_picker();

    if (apply_result_frames > 0) {
        jt_text_centered(380, JT_COL_GREEN, JT_COL_BLACK, "%s", apply_result_text);
    }

    jt_text_centered(420, JT_COL_GREY, JT_COL_BLACK,
                     "A/L-click paint  B/R-click erase  X pick  Y tool  D-pad color");
    jt_text_centered(444, JT_COL_GREY, JT_COL_BLACK,
                     "L/R triggers: swap Color/Mono layer    Start: Apply to VMU");
    jt_text_centered(468, JT_COL_GREEN, JT_COL_BLACK,
                     "Hold Start+Down for options menu");
}

const jt_mode_t jt_mode_vmu_editor = {
    .name   = "VMU Icon Editor",
    .enter  = editor_enter,
    .leave  = editor_leave,
    .update = editor_update,
    .draw   = editor_draw,
};
