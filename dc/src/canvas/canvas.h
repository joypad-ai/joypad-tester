/*
 * canvas.h — 32x32 color + mono drawing surfaces with undo.
 *
 * The editor mode owns one jt_canvas_t. Drawing tools (paint/erase/
 * fill/picker) mutate the canvas via the API below; every mutating
 * op is recorded so undo can rewind. The canvas knows nothing about
 * KOS, the framebuffer, or the cursor — those are wired together in
 * the editor mode. This keeps canvas testable in isolation.
 */
#ifndef JT_CANVAS_HDR
#define JT_CANVAS_HDR

#include <stdint.h>
#include <stdbool.h>

#include "../vms/vms.h"   /* jt_icon_t carries the same pixel data */

#define JT_UNDO_DEPTH 32

typedef enum {
    JT_LAYER_COLOR = 0,
    JT_LAYER_MONO,
} jt_canvas_layer_t;

/* Matches the web app's tool set: just two modes. The "primary vs
 * secondary color" concept (A vs B button) replaces the older erase /
 * pick / swap tools — there's no transparent state in a VMU icon, so
 * erasing is just painting with a different color. */
typedef enum {
    JT_TOOL_DRAW = 0,     /* single-pixel set with primary/secondary */
    JT_TOOL_FILL,         /* flood fill with primary/secondary */
    JT_TOOL_COUNT
} jt_tool_t;

/* A single undo record. We snapshot the full canvas because the
 * editor mutations are mostly small and the canvas is tiny (1056
 * bytes); a full snapshot ring is simpler than per-op deltas and
 * keeps undo O(1) work per step. */
typedef struct {
    uint8_t color_indices[JT_CANVAS_W * JT_CANVAS_H];
    uint8_t mono_bits[JT_CANVAS_W * JT_CANVAS_H / 8];
    uint16_t palette[JT_PALETTE_ENTRIES];
} jt_canvas_snapshot_t;

typedef struct {
    /* Live canvas state. Same layout as jt_icon_t -> easy round-trip. */
    uint16_t palette[JT_PALETTE_ENTRIES];
    uint8_t  color_indices[JT_CANVAS_W * JT_CANVAS_H];
    uint8_t  mono_bits[JT_CANVAS_W * JT_CANVAS_H / 8];

    /* Mono palette toggle state — one bool per color-palette index.
     * Mirrors the web app's mono-palette panel: toggling index N flips
     * every mono pixel whose color_index == N to the toggle state.
     * jt_canvas_mono_toggle_palette() applies a flip + cascade. */
    bool mono_palette_states[JT_PALETTE_ENTRIES];

    /* Active editor state. */
    jt_canvas_layer_t layer;            /* color or mono (which pane has focus) */
    jt_tool_t         tool;
    uint8_t           primary_color;    /* 0..15, painted by A / left-click */
    uint8_t           secondary_color;  /* 0..15, painted by B / right-click */
    bool              real_mode_flag;
    char              description[16];

    /* Undo ring. head points at the most recently stashed snapshot;
     * count tracks how many entries are live (<= JT_UNDO_DEPTH). */
    jt_canvas_snapshot_t undo[JT_UNDO_DEPTH];
    int undo_head;
    int undo_count;

    /* Redo ring. Undo stashes the pre-undo state here so it can be
     * re-applied; any fresh edit (push_undo) clears it. */
    jt_canvas_snapshot_t redo[JT_UNDO_DEPTH];
    int redo_head;
    int redo_count;
} jt_canvas_t;

/* Initialize with the 16-color default palette and a blank canvas. */
void jt_canvas_init(jt_canvas_t *c);

/* Load/save against the shared jt_icon_t structure. */
void jt_canvas_from_icon(jt_canvas_t *c, const jt_icon_t *icon);
void jt_canvas_to_icon  (const jt_canvas_t *c, jt_icon_t *icon);

/* Stash a snapshot before any mutation. Tools call this on first
 * action of a stroke; subsequent same-stroke pixels don't push more. */
void jt_canvas_push_undo(jt_canvas_t *c);
bool jt_canvas_undo(jt_canvas_t *c);
bool jt_canvas_redo(jt_canvas_t *c);

/* Single-pixel paint (color layer). Coordinates are canvas-space
 * (0..31). Out-of-range coords are silently ignored. */
void jt_canvas_paint_color(jt_canvas_t *c, int x, int y, uint8_t color_idx);

/* Bucket flood-fill from (x, y), replacing all 4-connected pixels of
 * the same source value with color_idx (color layer) or `on` (mono).
 * Pushes one undo snapshot. No-op if already at the target value. */
void jt_canvas_fill_color(jt_canvas_t *c, int x, int y, uint8_t color_idx);
void jt_canvas_mono_fill (jt_canvas_t *c, int x, int y, bool on);

/* Read a pixel as the editor would render it (mapping color index
 * via palette, or mono bit -> black/white). Returns ARGB1555. Used
 * by the editor's renderer and by thumbnail decoders. */
uint16_t jt_canvas_pixel_argb1555(const jt_canvas_t *c, int x, int y);

/* Mono-bit accessor. The layout in mono_bits is MSB-first per byte,
 * 32 bits per row. */
bool jt_canvas_mono_get(const jt_canvas_t *c, int x, int y);
void jt_canvas_mono_set(jt_canvas_t *c, int x, int y, bool on);

/* Flip mono_palette_states[idx] and cascade the new state to every
 * mono pixel whose color_index == idx. Pushes one undo snapshot. */
void jt_canvas_mono_toggle_palette(jt_canvas_t *c, int idx);

/* Recompute mono_palette_states[] from the current mono_bits +
 * color_indices: a palette index is considered "on" only if every
 * color-canvas pixel of that index has its mono bit on. Called after
 * direct mono painting so the toggle row stays in sync. */
void jt_canvas_mono_sync_palette(jt_canvas_t *c);

/* Bulk operations matching the web app's per-canvas buttons. */
void jt_canvas_mono_invert(jt_canvas_t *c);
void jt_canvas_color_reset(jt_canvas_t *c);   /* reload default palette, all white */
void jt_canvas_mono_reset(jt_canvas_t *c);    /* clear mono bits + palette states */

#endif /* JT_CANVAS_HDR */
