/*
 * about.c — version + build info + detected video state.
 *
 * Page layout (640x480):
 *   y=16..124   joypad-logo silhouette sprite, cyan, centered
 *   y=144       "Joypad Tester - Dreamcast" title
 *   y=184..     version / build / video / region / mode
 *   y=...       short description lines
 *   y=408       github URL
 *   y=456       footer hint
 */
#include <kos.h>
#include <dc/video.h>

#include "about.h"
#include "../ui/bfont_util.h"
#include "../ui/gen_logo.h"
#include "../video/mode.h"

static void about_enter(void) {}
static void about_leave(void) {}
static void about_update(float dt) { (void)dt; }

/* Paint the logo silhouette mask (silhouette pixels in `color`,
 * mask-unset pixels in black) at top-left (sx, sy). Same blit
 * approach the screensaver uses -- shared mask, two callers. */
static void draw_logo_sprite(int sx, int sy, uint16_t color)
{
    for (int row = 0; row < LOGO_H; row++) {
        int dst_y = sy + row;
        if (dst_y < 0 || dst_y >= 480) continue;
        const unsigned char *row_bits = logo_mask + row * LOGO_BYTES_PER_ROW;
        uint16_t *dst_row = vram_s + dst_y * 640;
        for (int col = 0; col < LOGO_W; col++) {
            int dst_x = sx + col;
            if (dst_x < 0 || dst_x >= 640) continue;
            unsigned char byte = row_bits[col >> 3];
            unsigned char bit  = byte & (0x80 >> (col & 7));
            dst_row[dst_x] = bit ? color : 0;
        }
    }
}

static void about_draw(void)
{
    /* Centered logo at top, rendered in white per the UI/UX guide
     * for static / non-screensaver placements. */
    int logo_x = (640 - LOGO_W) / 2;
    int logo_y = 28;   /* nudged down out of CRT top overscan */
    draw_logo_sprite(logo_x, logo_y, JT_COL_WHITE);

    /* Title under the logo. */
    int y = logo_y + LOGO_H + 12;     /* ~136 */
    jt_text_centered(y, JT_COL_YELLOW, JT_COL_BLACK,
                     "Joypad Tester - Dreamcast");

    y += 40;
    jt_text_centered(y, JT_COL_WHITE, JT_COL_BLACK,
                     "Version " JT_VERSION_STR);
    y += 32;
    jt_text_centered(y, JT_COL_CYAN, JT_COL_BLACK,
                     "Built on KallistiOS");
    y += 32;
    jt_text_centered(y, JT_COL_GREY, JT_COL_BLACK,
                     "Video:  %s", jt_cable_name(jt_video_cable()));
    y += 24;
    jt_text_centered(y, JT_COL_GREY, JT_COL_BLACK,
                     "Region: %s", jt_region_name(jt_video_region()));
    y += 24;
    jt_text_centered(y, JT_COL_GREY, JT_COL_BLACK,
                     "Mode:   %s",
                     jt_video_is_progressive() ? "Progressive 60Hz" : "Interlaced 480i");

    jt_text_centered(404, JT_COL_GREY, JT_COL_BLACK,
                     "github.com/joypad-ai/joypad-tester");
    jt_text_centered(428, JT_COL_GREEN, JT_COL_BLACK,
                     "Start: options menu");
}

const jt_mode_t jt_mode_about = {
    .name   = "About",
    .enter  = about_enter,
    .leave  = about_leave,
    .update = about_update,
    .draw   = about_draw,
};
