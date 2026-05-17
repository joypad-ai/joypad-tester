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

typedef enum {
    JT_TOOL_PAINT = 0,    /* single-pixel set */
    JT_TOOL_ERASE,        /* single-pixel clear (color 0 / mono off) */
    JT_TOOL_FILL,         /* flood fill */
    JT_TOOL_PICK,         /* sample pixel value into current color */
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

    /* Active editor state. */
    jt_canvas_layer_t layer;            /* color or mono */
    jt_tool_t         tool;
    uint8_t           current_color;    /* 0..15, color-layer index */
    bool              real_mode_flag;
    char              description[16];

    /* Undo ring. head points at the most recently stashed snapshot;
     * count tracks how many entries are live (<= JT_UNDO_DEPTH). */
    jt_canvas_snapshot_t undo[JT_UNDO_DEPTH];
    int undo_head;
    int undo_count;
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

/* Single-pixel ops. Coordinates are canvas-space (0..31). Out-of-range
 * coords are silently ignored. set_pixel writes the current_color
 * (or mono on/off per layer). erase_pixel writes 0 / off. */
void jt_canvas_set_pixel  (jt_canvas_t *c, int x, int y);
void jt_canvas_erase_pixel(jt_canvas_t *c, int x, int y);

/* Bucket fill — same-value flood from (x, y) until value boundary
 * (or canvas edge). Pushes a single undo snapshot at entry. */
void jt_canvas_fill(jt_canvas_t *c, int x, int y);

/* Sample whatever the pixel at (x, y) is into current_color. No
 * mutation, no undo. */
void jt_canvas_pick(jt_canvas_t *c, int x, int y);

/* Read a pixel as the editor would render it (mapping color index
 * via palette, or mono bit -> black/white). Returns ARGB1555. Used
 * by the editor's renderer and by thumbnail decoders. */
uint16_t jt_canvas_pixel_argb1555(const jt_canvas_t *c, int x, int y);

/* Mono-bit accessor. The layout in mono_bits is MSB-first per byte,
 * 32 bits per row. */
bool jt_canvas_mono_get(const jt_canvas_t *c, int x, int y);
void jt_canvas_mono_set(jt_canvas_t *c, int x, int y, bool on);

#endif /* JT_CANVAS_HDR */
