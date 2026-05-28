/*
 * bfont_util.c — printf wrapper around bfont_draw_str.
 *
 * bfont_draw_str writes one glyph at a time into a uint16_t buffer
 * (the framebuffer) at given byte stride. Our framebuffer is the
 * one KOS sets up via vid_set_mode(); vram_s is the canonical
 * 16bpp pointer.
 */
#include <kos.h>
#include <dc/biosfont.h>
#include <dc/video.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "bfont_util.h"

#define BUF_SIZE 256

static void draw_at(int x, int y, uint16_t fg, uint16_t bg, const char *s)
{
    bfont_set_foreground_color(fg);
    bfont_set_background_color(bg);
    /* vram_s points at the active framebuffer; bfont_draw_str
     * advances the pointer one cell per glyph. Opaque mode (the
     * third arg = 1) is critical: it paints `bg` behind each glyph,
     * which means we don't have to memset the framebuffer each
     * frame. Without it, stale text would underdraw the new text;
     * with a per-frame memset we'd get visible single-buffer
     * flicker (black flash → drawn → black flash). Opaque text on
     * a one-time-cleared framebuffer is flicker-free. */
    bfont_draw_str(vram_s + (y * 640) + x, 640, 1, s);
}

void jt_text(int x, int y, uint16_t fg, uint16_t bg, const char *fmt, ...)
{
    char buf[BUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    draw_at(x, y, fg, bg, buf);
}

void jt_text_centered(int y, uint16_t fg, uint16_t bg, const char *fmt, ...)
{
    char buf[BUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    /* bfont glyph width is BFONT_THIN_WIDTH (12 px) for ASCII. */
    int w = (int)strlen(buf) * BFONT_THIN_WIDTH;
    int x = (640 - w) / 2;
    if (x < 0) x = 0;
    draw_at(x, y, fg, bg, buf);
}

void jt_show_busy(const char *msg)
{
    /* Centered box drawn straight onto the back buffer + flipped now, so
     * it's on screen while the caller blocks the frame loop on a maple
     * read/write. We can't animate during the stall, but a steady
     * "working" panel tells the user it isn't frozen. */
    const int w = 300, h = 64, x = (640 - w) / 2, y = (480 - h) / 2;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            vram_s[(y + j) * 640 + (x + i)] = JT_COL_BLACK;
    for (int i = 0; i < w; i++) {
        vram_s[y * 640 + x + i]             = JT_COL_YELLOW;
        vram_s[(y + 1) * 640 + x + i]       = JT_COL_YELLOW;
        vram_s[(y + h - 2) * 640 + x + i]   = JT_COL_YELLOW;
        vram_s[(y + h - 1) * 640 + x + i]   = JT_COL_YELLOW;
    }
    for (int j = 0; j < h; j++) {
        vram_s[(y + j) * 640 + x]           = JT_COL_YELLOW;
        vram_s[(y + j) * 640 + x + 1]       = JT_COL_YELLOW;
        vram_s[(y + j) * 640 + x + w - 2]   = JT_COL_YELLOW;
        vram_s[(y + j) * 640 + x + w - 1]   = JT_COL_YELLOW;
    }
    jt_text_centered(y + 20, JT_COL_WHITE, JT_COL_BLACK, msg);
    vid_flip(-1);
    vid_waitvbl();
}
