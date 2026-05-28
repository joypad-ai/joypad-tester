/*
 * bfont_util.h — text rendering helpers around KOS's bfont.
 *
 * KOS ships a Sega-licensed BIOS font (`bfont`) at fixed sizes. We
 * wrap it with a printf-style call that writes into the 640x480
 * RGB565 framebuffer at given pixel coords, plus a centered-string
 * helper for headings.
 */
#ifndef JT_BFONT_UTIL_H
#define JT_BFONT_UTIL_H

#include <stdint.h>

#define JT_RGB565(r, g, b) (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))

#define JT_COL_WHITE   JT_RGB565(255, 255, 255)
#define JT_COL_BLACK   JT_RGB565(0,   0,   0)
#define JT_COL_YELLOW  JT_RGB565(255, 220, 0)
#define JT_COL_CYAN    JT_RGB565(120, 220, 255)
#define JT_COL_GREEN   JT_RGB565(100, 220, 120)
#define JT_COL_GREY    JT_RGB565(140, 140, 140)
#define JT_COL_RED     JT_RGB565(255, 100, 100)

/* Print formatted text at (x, y). Uses bfont's 12x24 cell. */
void jt_text(int x, int y, uint16_t fg, uint16_t bg, const char *fmt, ...);
/* Same but centered horizontally on the screen at row y. */
void jt_text_centered(int y, uint16_t fg, uint16_t bg, const char *fmt, ...);

/* Paint a centered "busy" box with `msg` and flip it to the screen
 * immediately. Call this right before a blocking maple/VMU operation
 * (which stalls the frame loop) so the user sees activity instead of a
 * frozen frame. */
void jt_show_busy(const char *msg);

#endif
