/*
 * bfont.c -- text + rect drawing on the Xbox CPU framebuffer.
 *
 * XVideoGetFB returns a 32-bit BGRA framebuffer (mode set by
 * XVideoSetMode(640, 480, 32, ...) in main.c). debugPrint in nxdk's
 * hal/debug.c walks the same buffer; this file reimplements just the
 * positioned-glyph + filled-rect primitives we need without the
 * scrolling line-buffer pb_print / debugPrint wrap us into.
 *
 * Font: unscii_16 (public domain, vendored as font_unscii_16.h next
 * to this file -- same file nxdk's debug.c uses). 8x16 glyphs, Latin-1
 * supplement.
 */
#include "bfont.h"

#include <hal/video.h>
#include <xboxkrnl/xboxkrnl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const unsigned char systemFont[] = {
#include "font_unscii_16.h"
};

/* Double-buffer state. fb_a is whatever the BIOS / XVideoSetMode left
 * pointing at the display when we booted (its address is whatever
 * XVideoGetFB returns the first time). fb_b is a contiguous buffer we
 * allocate up-front and toggle into via XVideoSetFB on each present.
 *
 * `fb` (the active back-buffer the draw helpers write to) flips
 * between fb_a and fb_b: whichever one is NOT currently being
 * scanned out by the video engine is safe to write. */
static uint32_t       *fb;          /* CURRENT back buffer (write here) */
static uint32_t       *fb_a;        /* original / front at boot */
static uint32_t       *fb_b;        /* second framebuffer we allocate */
static int             vid_w, vid_h;
static int             vid_pitch;   /* in pixels (== vid_w for 32bpp) */

void jt_video_refresh(void)
{
    VIDEO_MODE vm = XVideoGetMode();
    vid_w     = vm.width;
    vid_h     = vm.height;
    vid_pitch = vm.width;   /* 32bpp -- pitch in pixels matches width */
}

bool jt_video_init(void)
{
    jt_video_refresh();
    fb_a = (uint32_t *)XVideoGetFB();
    /* Allocate the second framebuffer the same way nxdk's hal/video.c
     * allocates the first: contiguous physical memory below the 2 GB
     * line, 4 KB aligned, write-combined for fast CPU streaming.
     *
     * Note on addressing: MmAllocateContiguousMemoryEx with
     * PAGE_WRITECOMBINE returns a virtual address with bit 31 set
     * (the WC mapping). XVideoSetFB takes a "physical-ish" pointer
     * (bit 31 clear) -- it asserts the bit is 0 and then masks it
     * off when writing the hardware register. So we keep two views
     * of each buffer:
     *   - the WC virtual we write through (high bit set, no cache
     *     pollution, fast bursts; needs sfence before the GPU reads)
     *   - the masked address we hand to XVideoSetFB (high bit clear)
     * fb_a comes back from XVideoGetFB already as the WC mapping
     * nxdk allocated, so it's symmetric. */
    SIZE_T size = (SIZE_T)vid_w * vid_h * 4;
    fb_b = (uint32_t *)MmAllocateContiguousMemoryEx(
        size, 0x00000000, 0x7FFFFFFF, 0x1000,
        PAGE_READWRITE | PAGE_WRITECOMBINE);
    if (!fb_b) {
        /* Allocation failed -- fall back to single-buffer mode with
         * fb = fb_a. jt_video_present becomes a vsync-only no-op. */
        fb = fb_a;
        return false;
    }
    memset(fb_b, 0, size);
    /* Start drawing into fb_b; fb_a stays on screen until first present. */
    fb = fb_b;
    return true;
}

void jt_video_present(void)
{
    if (!fb_b) {
        /* Single-buffer fallback. */
        asm __volatile__("sfence");
        XVideoWaitForVBlank();
        return;
    }
    /* PCRTC_START (what XVideoSetFB writes) is hardware double-
     * buffered: the register update doesn't take effect until the
     * next vblank pulse. So we have to write the new buffer pointer
     * FIRST, then wait for vblank -- waiting first would leave the
     * scanout still pointing at the buffer we're about to overwrite,
     * which is exactly the tearing we're trying to fix.
     *
     * sfence drains the WC write buffer so the GPU sees a clean
     * frame the moment the flip lands. */
    asm __volatile__("sfence");
    uint32_t *new_front = fb;
    unsigned int phys = (unsigned int)new_front & 0x7FFFFFFF;
    XVideoSetFB((unsigned char *)phys);
    XVideoWaitForVBlank();
    fb = (new_front == fb_a) ? fb_b : fb_a;
}

int jt_video_width(void)  { return vid_w; }
int jt_video_height(void) { return vid_h; }

void jt_clear(uint32_t color)
{
    if (!fb) return;
    int n = vid_w * vid_h;
    for (int i = 0; i < n; i++) fb[i] = color;
}

void jt_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    if (!fb) return;
    if (x < 0)        { w += x; x = 0; }
    if (y < 0)        { h += y; y = 0; }
    if (x + w > vid_w) w = vid_w - x;
    if (y + h > vid_h) h = vid_h - y;
    if (w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++) {
        uint32_t *dst = fb + (y + row) * vid_pitch + x;
        for (int col = 0; col < w; col++) dst[col] = color;
    }
}

/* Draw one 8x16 unscii glyph at (px, py). transparent_bg = bg == 0
 * (caller convention) -- in that case we don't touch background
 * pixels. The font is non-vmirror (FONT_VMIRROR=0 in the header) so
 * the high bit of each byte is the leftmost pixel. */
static void draw_glyph(unsigned char c, int px, int py,
                       uint32_t fg, uint32_t bg, int transparent_bg)
{
    if (!fb) return;
    const unsigned char *glyph = systemFont + (c * JT_FONT_H);
    for (int row = 0; row < JT_FONT_H; row++) {
        int sy = py + row;
        if (sy < 0 || sy >= vid_h) continue;
        unsigned char bits = glyph[row];
        uint32_t *dst_row = fb + sy * vid_pitch;
        unsigned char mask = 0x80;
        for (int col = 0; col < JT_FONT_W; col++) {
            int sx = px + col;
            if (sx >= 0 && sx < vid_w) {
                if (bits & mask) {
                    dst_row[sx] = fg;
                } else if (!transparent_bg) {
                    dst_row[sx] = bg;
                }
            }
            mask >>= 1;
        }
    }
}

static int draw_str(int px, int py, uint32_t fg, uint32_t bg,
                    const char *s)
{
    int transparent_bg = (bg == 0);
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        draw_glyph(c, px, py, fg, bg, transparent_bg);
        px += JT_FONT_W;
    }
    return px;
}

int jt_text(int x, int y, uint32_t fg, uint32_t bg, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return draw_str(x, y, fg, bg, buf);
}

int jt_text_centered(int y, uint32_t fg, uint32_t bg, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    int len = (int)strlen(buf);
    int x = (vid_w - len * JT_FONT_W) / 2;
    if (x < 0) x = 0;
    return draw_str(x, y, fg, bg, buf);
}

void jt_blit_mask(const uint8_t *mask, int mask_w, int mask_h,
                  int bytes_per_row, int px, int py, uint32_t color)
{
    if (!fb || !mask) return;
    for (int row = 0; row < mask_h; row++) {
        int sy = py + row;
        if (sy < 0 || sy >= vid_h) continue;
        const uint8_t *src = mask + row * bytes_per_row;
        uint32_t *dst_row = fb + sy * vid_pitch;
        for (int col = 0; col < mask_w; col++) {
            int sx = px + col;
            if (sx < 0 || sx >= vid_w) continue;
            if (src[col >> 3] & (0x80 >> (col & 7))) dst_row[sx] = color;
        }
    }
}
