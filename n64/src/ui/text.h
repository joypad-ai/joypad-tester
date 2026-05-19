/*
 * ui/text.h — per-segment-coloured text rendering on top of
 * libdragon's 8x8 default font, mimicking what console_* does but
 * letting each draw call carry its own foreground colour.
 *
 * Used by tester mode (which needs per-section colour groupings to
 * match the GameCube tester's SetFgColor blocks). Other modes still
 * use libdragon's console_* directly for now -- it's simpler when
 * colour isn't needed.
 *
 * Each helper writes to the surface returned by display_get; callers
 * are responsible for display_show after they finish drawing.
 */
#ifndef N64_UI_TEXT_H
#define N64_UI_TEXT_H

#include <stdint.h>
#include <libdragon.h>

#define TXT_GLYPH_W 8
#define TXT_GLYPH_H 8

/* Shared palette so every mode renders the title row + bottom footer
 * the same way (per CLAUDE.md's UI/UX conventions for the repo). */
#define JT_COL_TITLE   0xfff040ff  /* yellow -- page title + menu hover */
#define JT_COL_FOOTER  0x80ff80ff  /* green  -- bottom "Start: options menu" hint */
#define JT_COL_LABEL   0xc0c0c0ff  /* light grey -- field labels */
#define JT_COL_VALUE   0xffffffff  /* white  -- field values */
#define JT_COL_DIM     0x606060ff  /* dark grey -- disconnected / unheld */
#define JT_COL_HELD    0xffe040ff  /* yellow accent -- held button cells */
#define JT_COL_ACTIVE  0x80ff80ff  /* green  -- active / connected */
#define JT_COL_ERROR   0xff6060ff  /* red    -- error states */
#define JT_COL_CYAN    0x40e0e0ff  /* cyan   -- secondary highlight (About credits etc.) */
#define JT_COL_BG      0x000000ff  /* opaque black backdrop */

/* Plot a coloured run of text. Returns x just past the last glyph,
 * so callers can chain draw_text calls along the same row without
 * recomputing widths. */
int txt_draw(surface_t *surf, int x, int y, uint32_t fg, const char *s);

/* printf-style variant. Format into a 128-byte stack buffer, then
 * delegate to txt_draw. */
int txt_drawf(surface_t *surf, int x, int y, uint32_t fg,
              const char *fmt, ...);

/* Centred-text helper: lays out a single line so its midpoint lands
 * at the given screen-x. Same as `txt_draw` otherwise. */
int txt_draw_centered(surface_t *surf, int y, uint32_t fg, const char *s,
                      int screen_w);

/* Draw the standard green "Start: options menu" (or other) hint at the
 * bottom of the screen. Y is chosen to clear typical N64 TV overscan;
 * pages that hand-rolled a `screen_h - 16` line tended to get clipped
 * on real hardware. */
void txt_draw_footer(surface_t *surf, const char *s);

#endif
