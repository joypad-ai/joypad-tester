/*
 * colors.h -- PORTING.md §3.1 color roles as Xbox framebuffer pixels.
 *
 * The framebuffer is 32-bit BGRA (XVideoSetMode bpp=32) -- nxdk debug.c
 * uses the same convention. Pack via JT_RGBA(r,g,b,a) and use the
 * named JT_COL_* roles in widgets so the palette stays in one place.
 */
#ifndef JT_XBOX_COLORS_H
#define JT_XBOX_COLORS_H

#include <stdint.h>

/* Channel order matches the nxdk framebuffer layout: 0xAARRGGBB
 * little-endian (byte 0 = B, byte 1 = G, byte 2 = R, byte 3 = A). */
#define JT_RGBA(r, g, b, a) \
    ((uint32_t)((((uint32_t)(a) & 0xFFu) << 24) | \
                (((uint32_t)(r) & 0xFFu) << 16) | \
                (((uint32_t)(g) & 0xFFu) <<  8) | \
                 ((uint32_t)(b) & 0xFFu)))

/* PORTING.md §3.1 role palette. Literal values match the reference
 * RGBA table from the spec exactly. */
#define JT_COL_TITLE   JT_RGBA(0xFF, 0xF0, 0x40, 0xFF)   /* yellow         */
#define JT_COL_FOOTER  JT_RGBA(0x80, 0xFF, 0x80, 0xFF)   /* green          */
#define JT_COL_LABEL   JT_RGBA(0xC0, 0xC0, 0xC0, 0xFF)   /* light grey     */
#define JT_COL_VALUE   JT_RGBA(0xFF, 0xFF, 0xFF, 0xFF)   /* white          */
#define JT_COL_HELD    JT_RGBA(0xFF, 0xE0, 0x40, 0xFF)   /* yellow accent  */
#define JT_COL_ACTIVE  JT_RGBA(0x80, 0xFF, 0x80, 0xFF)   /* green          */
#define JT_COL_DIM     JT_RGBA(0x60, 0x60, 0x60, 0xFF)   /* dark grey      */
#define JT_COL_ERROR   JT_RGBA(0xFF, 0x60, 0x60, 0xFF)   /* red            */
#define JT_COL_CYAN    JT_RGBA(0x40, 0xE0, 0xE0, 0xFF)   /* cyan           */
#define JT_COL_BLACK   JT_RGBA(0x00, 0x00, 0x00, 0xFF)
#define JT_COL_WHITE   JT_COL_VALUE

#endif /* JT_XBOX_COLORS_H */
