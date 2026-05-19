/*
 * ui/text.c — see header.
 */

#include "text.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

int txt_draw(surface_t *surf, int x, int y, uint32_t fg, const char *s)
{
    /* graphics_set_color expects the surface's native packed-pixel
     * format, not raw RGBA8888. Re-pack via graphics_make_color so
     * the colour stays correct across surface format choices. */
    uint8_t r = (fg >> 24) & 0xff;
    uint8_t g = (fg >> 16) & 0xff;
    uint8_t b = (fg >>  8) & 0xff;
    uint8_t a = (fg      ) & 0xff;
    uint32_t native_fg = graphics_make_color(r, g, b, a);
    uint32_t native_bg = graphics_make_color(0, 0, 0, 0);  /* transparent */
    graphics_set_color(native_fg, native_bg);
    graphics_draw_text(surf, x, y, s);
    return x + (int)strlen(s) * TXT_GLYPH_W;
}

int txt_drawf(surface_t *surf, int x, int y, uint32_t fg,
              const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return txt_draw(surf, x, y, fg, buf);
}

int txt_draw_centered(surface_t *surf, int y, uint32_t fg, const char *s,
                      int screen_w)
{
    int width = (int)strlen(s) * TXT_GLYPH_W;
    int x = (screen_w - width) / 2;
    if (x < 0) x = 0;
    return txt_draw(surf, x, y, fg, s);
}

void txt_draw_footer(surface_t *surf, const char *s)
{
    int screen_w = surf->width  ? surf->width  : 320;
    int screen_h = surf->height ? surf->height : 240;
    /* 20 = 12 (margin) + 8 (glyph) -- mirrors the tester page, which
     * is the one place the footer never got clipped by overscan. */
    txt_draw_centered(surf, screen_h - 20, JT_COL_FOOTER, s, screen_w);
}
