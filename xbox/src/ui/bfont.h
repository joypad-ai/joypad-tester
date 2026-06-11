/*
 * bfont.h -- positioned colored text + filled rects on the Xbox
 * framebuffer.
 *
 * Same shape as dc/'s text helper: jt_text(x, y, fg, bg, fmt, ...) and
 * jt_text_centered(y, fg, bg, fmt, ...) place text by absolute pixel
 * coords using the public-domain unscii-16 font baked into the binary
 * (8x16 glyphs, Latin-1 supplement). bg = transparent if 0.
 *
 * Direct framebuffer writes -- bypasses pbkit so it composes with
 * gen_logo.h sprite blits and arbitrary rect fills the same way the
 * dc/ tester does. Caller is responsible for vsync (XVideoWaitForVBlank
 * in main.c's frame loop).
 */
#ifndef JT_XBOX_BFONT_H
#define JT_XBOX_BFONT_H

#include <stdbool.h>
#include <stdint.h>

#define JT_FONT_W 8
#define JT_FONT_H 16

/* One-time double-buffer init: allocate the back framebuffer (front
 * is whatever XVideoSetMode left displayed) and cache size info.
 * Returns false on allocation failure -- caller should bail. */
bool jt_video_init(void);

/* Vsync + swap front<->back. Call once per frame after all draws so
 * the back buffer becomes visible and a fresh back is ready for the
 * next frame's writes. */
void jt_video_present(void);

/* Re-cache framebuffer dimensions if the video mode changed. Safe to
 * call every frame; cheap. */
void jt_video_refresh(void);

/* Active framebuffer surface size (640x480 in our setup, but we read
 * what XVideoGetMode actually reports so a future mode change keeps
 * centered widgets correctly aligned). */
int jt_video_width(void);
int jt_video_height(void);

/* Fill the entire surface in `color` -- used at the top of each frame
 * so single-buffered draws self-overpaint cleanly. */
void jt_clear(uint32_t color);

/* Filled rectangle in `color`. Pixels outside the surface are
 * clipped. Used for button highlights + the options-menu border. */
void jt_fill_rect(int x, int y, int w, int h, uint32_t color);

/* Positioned colored text. bg = 0 means transparent (only the glyph's
 * set pixels are written; the surrounding pixels keep whatever's
 * underneath). Returns the x coord just past the last glyph so chained
 * calls can lay out side-by-side. */
int jt_text(int x, int y, uint32_t fg, uint32_t bg, const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));

/* Same as jt_text, centered horizontally against jt_video_width(). */
int jt_text_centered(int y, uint32_t fg, uint32_t bg, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

/* Blit a 1-bit silhouette mask (MSB-first per byte, row-major) at
 * (px, py). Lit bits become `color`; cleared bits are left untouched
 * (transparent over whatever's already in the framebuffer). Matches
 * the gen_logo.h packing make_logo.py produces. */
void jt_blit_mask(const uint8_t *mask, int mask_w, int mask_h,
                  int bytes_per_row, int px, int py, uint32_t color);

#endif /* JT_XBOX_BFONT_H */
