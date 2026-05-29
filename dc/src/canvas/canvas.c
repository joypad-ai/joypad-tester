/*
 * canvas.c — drawing surface state + tools + undo ring.
 *
 * Snapshot-based undo: every mutation pushes the full canvas (1056
 * bytes) onto a 32-deep ring. Simpler than per-op deltas, and the
 * memory footprint is trivial (32 * 1056 = ~33KB) for a homebrew
 * with 16MB of RAM.
 */
#include <string.h>

#include "canvas.h"

/* Default palette — same 16 colors the JS app uses (basic CGA-ish
 * primary set + a darker secondary tier). Each entry stored in the
 * on-disc packing so writing out doesn't need a conversion step. */
static const struct { uint8_t r, g, b, a; } default_palette[JT_PALETTE_ENTRIES] = {
    {0,   0,   0,   255}, /* black   */
    {255, 255, 255, 255}, /* white   */
    {255, 0,   0,   255}, /* red     */
    {0,   255, 0,   255}, /* green   */
    {0,   0,   255, 255}, /* blue    */
    {255, 255, 0,   255}, /* yellow  */
    {0,   255, 255, 255}, /* cyan    */
    {255, 0,   255, 255}, /* magenta */
    {187, 187, 187, 255}, /* silver  */
    {136, 136, 136, 255}, /* grey    */
    {136, 0,   0,   255}, /* maroon  */
    {136, 136, 0,   255}, /* olive   */
    {0,   136, 0,   255}, /* dgreen  */
    {136, 0,   136, 255}, /* purple  */
    {0,   136, 136, 255}, /* teal    */
    {0,   0,   136, 255}, /* navy    */
};

static inline int idx_for(int x, int y) { return y * JT_CANVAS_W + x; }
static inline bool in_bounds(int x, int y)
{
    return x >= 0 && x < JT_CANVAS_W && y >= 0 && y < JT_CANVAS_H;
}

void jt_canvas_init(jt_canvas_t *c)
{
    memset(c, 0, sizeof(*c));
    for (int i = 0; i < JT_PALETTE_ENTRIES; i++) {
        c->palette[i] = jt_palette_pack(default_palette[i].r,
                                        default_palette[i].g,
                                        default_palette[i].b,
                                        default_palette[i].a);
    }
    /* Blank canvas = all white (palette index 1), all mono pixels off. */
    memset(c->color_indices, 1, sizeof(c->color_indices));
    c->layer = JT_LAYER_COLOR;
    c->tool = JT_TOOL_DRAW;
    /* Match the web app's defaults: primary = index 0 (black, left
     * click), secondary = index 1 (white, right click). */
    c->primary_color = 0;
    c->secondary_color = 1;
    c->real_mode_flag = false;
}

void jt_canvas_from_icon(jt_canvas_t *c, const jt_icon_t *icon)
{
    /* Preserve undo ring across loads — the user can undo a load. */
    jt_canvas_push_undo(c);
    memcpy(c->palette,       icon->palette,       sizeof(c->palette));
    memcpy(c->color_indices, icon->color_indices, sizeof(c->color_indices));
    memcpy(c->mono_bits,     icon->mono_bits,     sizeof(c->mono_bits));
    memcpy(c->description,   icon->description,   sizeof(c->description));
    c->real_mode_flag = icon->real_mode_flag;
}

void jt_canvas_to_icon(const jt_canvas_t *c, jt_icon_t *icon)
{
    memset(icon, 0, sizeof(*icon));
    memcpy(icon->palette,       c->palette,       sizeof(c->palette));
    memcpy(icon->color_indices, c->color_indices, sizeof(c->color_indices));
    memcpy(icon->mono_bits,     c->mono_bits,     sizeof(c->mono_bits));
    memcpy(icon->description,   c->description,   sizeof(c->description));
    icon->has_color_icon = true;   /* editor always emits both layers */
    icon->real_mode_flag = c->real_mode_flag;
}

static void snap_save(jt_canvas_snapshot_t *s, const jt_canvas_t *c)
{
    memcpy(s->color_indices, c->color_indices, sizeof(c->color_indices));
    memcpy(s->mono_bits,     c->mono_bits,     sizeof(c->mono_bits));
    memcpy(s->palette,       c->palette,       sizeof(c->palette));
}

static void snap_load(const jt_canvas_snapshot_t *s, jt_canvas_t *c)
{
    memcpy(c->color_indices, s->color_indices, sizeof(c->color_indices));
    memcpy(c->mono_bits,     s->mono_bits,     sizeof(c->mono_bits));
    memcpy(c->palette,       s->palette,       sizeof(c->palette));
}

void jt_canvas_push_undo(jt_canvas_t *c)
{
    c->undo_head = (c->undo_head + 1) % JT_UNDO_DEPTH;
    snap_save(&c->undo[c->undo_head], c);
    if (c->undo_count < JT_UNDO_DEPTH) c->undo_count++;
    /* A fresh edit invalidates any forward (redo) history. */
    c->redo_count = 0;
}

bool jt_canvas_undo(jt_canvas_t *c)
{
    if (c->undo_count == 0) return false;
    /* Stash the current (post-edit) state so redo can return to it. */
    c->redo_head = (c->redo_head + 1) % JT_UNDO_DEPTH;
    snap_save(&c->redo[c->redo_head], c);
    if (c->redo_count < JT_UNDO_DEPTH) c->redo_count++;
    /* Revert to the most recent pre-edit snapshot. */
    snap_load(&c->undo[c->undo_head], c);
    c->undo_head = (c->undo_head - 1 + JT_UNDO_DEPTH) % JT_UNDO_DEPTH;
    c->undo_count--;
    return true;
}

bool jt_canvas_redo(jt_canvas_t *c)
{
    if (c->redo_count == 0) return false;
    /* Push the current state back onto the undo ring (without clearing
     * redo, so push_undo's reset doesn't fire). */
    c->undo_head = (c->undo_head + 1) % JT_UNDO_DEPTH;
    snap_save(&c->undo[c->undo_head], c);
    if (c->undo_count < JT_UNDO_DEPTH) c->undo_count++;
    /* Re-apply the most recently undone snapshot. */
    snap_load(&c->redo[c->redo_head], c);
    c->redo_head = (c->redo_head - 1 + JT_UNDO_DEPTH) % JT_UNDO_DEPTH;
    c->redo_count--;
    return true;
}

bool jt_canvas_mono_get(const jt_canvas_t *c, int x, int y)
{
    if (!in_bounds(x, y)) return false;
    int p = idx_for(x, y);
    return (c->mono_bits[p / 8] >> (7 - (p % 8))) & 1;
}

void jt_canvas_mono_set(jt_canvas_t *c, int x, int y, bool on)
{
    if (!in_bounds(x, y)) return;
    int p = idx_for(x, y);
    uint8_t mask = (uint8_t)(1u << (7 - (p % 8)));
    if (on) c->mono_bits[p / 8] |=  mask;
    else    c->mono_bits[p / 8] &= (uint8_t)~mask;
}

void jt_canvas_paint_color(jt_canvas_t *c, int x, int y, uint8_t color_idx)
{
    if (!in_bounds(x, y)) return;
    c->color_indices[idx_for(x, y)] = color_idx & 0x0F;
}

/* Iterative flood fill (4-connected). 32x32 = 1024 cells max, so the
 * fixed-size stack is plenty. */
void jt_canvas_mono_fill(jt_canvas_t *c, int x, int y, bool on)
{
    if (!in_bounds(x, y)) return;
    jt_canvas_push_undo(c);
    bool target = jt_canvas_mono_get(c, x, y);
    if (target == on) return;
    int stack_x[1024], stack_y[1024];
    int sp = 0;
    stack_x[sp] = x; stack_y[sp] = y; sp++;
    while (sp > 0) {
        sp--;
        int cx = stack_x[sp], cy = stack_y[sp];
        if (!in_bounds(cx, cy)) continue;
        if (jt_canvas_mono_get(c, cx, cy) != target) continue;
        jt_canvas_mono_set(c, cx, cy, on);
        if (sp + 4 <= 1024) {
            stack_x[sp] = cx + 1; stack_y[sp] = cy;     sp++;
            stack_x[sp] = cx - 1; stack_y[sp] = cy;     sp++;
            stack_x[sp] = cx;     stack_y[sp] = cy + 1; sp++;
            stack_x[sp] = cx;     stack_y[sp] = cy - 1; sp++;
        }
    }
}

void jt_canvas_fill_color(jt_canvas_t *c, int x, int y, uint8_t color_idx)
{
    if (!in_bounds(x, y)) return;
    jt_canvas_push_undo(c);
    uint8_t target = c->color_indices[idx_for(x, y)] & 0x0F;
    uint8_t replace_with = color_idx & 0x0F;
    if (target == replace_with) return;
    int stack_x[1024], stack_y[1024];
    int sp = 0;
    stack_x[sp] = x; stack_y[sp] = y; sp++;
    while (sp > 0) {
        sp--;
        int cx = stack_x[sp], cy = stack_y[sp];
        if (!in_bounds(cx, cy)) continue;
        if ((c->color_indices[idx_for(cx, cy)] & 0x0F) != target) continue;
        c->color_indices[idx_for(cx, cy)] = replace_with;
        if (sp + 4 <= 1024) {
            stack_x[sp] = cx + 1; stack_y[sp] = cy;     sp++;
            stack_x[sp] = cx - 1; stack_y[sp] = cy;     sp++;
            stack_x[sp] = cx;     stack_y[sp] = cy + 1; sp++;
            stack_x[sp] = cx;     stack_y[sp] = cy - 1; sp++;
        }
    }
}

void jt_canvas_mono_toggle_palette(jt_canvas_t *c, int idx)
{
    if (idx < 0 || idx >= JT_PALETTE_ENTRIES) return;
    jt_canvas_push_undo(c);
    c->mono_palette_states[idx] = !c->mono_palette_states[idx];
    bool on = c->mono_palette_states[idx];
    /* Cascade: every mono pixel whose color index matches takes the
     * new state. Matches web app's toggleMonoPaletteIndex(). */
    for (int p = 0; p < JT_CANVAS_W * JT_CANVAS_H; p++) {
        if ((c->color_indices[p] & 0x0F) == idx) {
            int by = p / 8, sh = 7 - (p % 8);
            if (on) c->mono_bits[by] |=  (uint8_t)(1u << sh);
            else    c->mono_bits[by] &= (uint8_t)~(1u << sh);
        }
    }
}

void jt_canvas_mono_sync_palette(jt_canvas_t *c)
{
    for (int idx = 0; idx < JT_PALETTE_ENTRIES; idx++) {
        bool has_any = false;
        bool all_on = true;
        for (int p = 0; p < JT_CANVAS_W * JT_CANVAS_H; p++) {
            if ((c->color_indices[p] & 0x0F) != idx) continue;
            has_any = true;
            bool on = (c->mono_bits[p / 8] >> (7 - (p % 8))) & 1;
            if (!on) { all_on = false; break; }
        }
        c->mono_palette_states[idx] = has_any && all_on;
    }
}

void jt_canvas_mono_invert(jt_canvas_t *c)
{
    jt_canvas_push_undo(c);
    for (size_t i = 0; i < sizeof(c->mono_bits); i++) {
        c->mono_bits[i] = (uint8_t)~c->mono_bits[i];
    }
    jt_canvas_mono_sync_palette(c);
}

void jt_canvas_color_reset(jt_canvas_t *c)
{
    jt_canvas_push_undo(c);
    for (int i = 0; i < JT_PALETTE_ENTRIES; i++) {
        c->palette[i] = jt_palette_pack(default_palette[i].r,
                                        default_palette[i].g,
                                        default_palette[i].b,
                                        default_palette[i].a);
    }
    memset(c->color_indices, 1, sizeof(c->color_indices)); /* all white */
    c->primary_color = 0;
    c->secondary_color = 1;
    /* Mono cascade: nothing to set true (since palette states are all
     * tracked but cascading would zero them based on color). Keep
     * existing mono. */
}

void jt_canvas_mono_reset(jt_canvas_t *c)
{
    jt_canvas_push_undo(c);
    memset(c->mono_bits, 0, sizeof(c->mono_bits));
    memset(c->mono_palette_states, 0, sizeof(c->mono_palette_states));
}

uint16_t jt_canvas_pixel_argb1555(const jt_canvas_t *c, int x, int y)
{
    if (!in_bounds(x, y)) return 0;
    if (c->layer == JT_LAYER_COLOR) {
        uint8_t idx = c->color_indices[idx_for(x, y)] & 0x0F;
        uint8_t r, g, b, a;
        jt_palette_unpack(c->palette[idx], &r, &g, &b, &a);
        /* Convert 8888 -> 1555. Alpha is binary (>=128 = opaque). */
        uint16_t out = 0;
        out |= ((a >= 128) ? 0x8000 : 0);
        out |= (uint16_t)((r >> 3) & 0x1F) << 10;
        out |= (uint16_t)((g >> 3) & 0x1F) << 5;
        out |= (uint16_t)((b >> 3) & 0x1F);
        return out;
    } else {
        /* Mono: on = opaque black, off = opaque white. */
        return jt_canvas_mono_get(c, x, y) ? 0x8000 : 0xFFFF;
    }
}
