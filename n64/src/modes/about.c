/*
 * about.c — version + credits screen, dc/-style.
 *
 * Layout (320x240):
 *   y=16..80     logo silhouette, white, centered
 *   y=92         title (yellow, centered)
 *   y=108        version (white, centered)
 *   y=128        "Built on LibDragon" (cyan, centered)
 *   y=160..      grey credit lines
 *   y=204        GitHub URL (grey, centered)
 *   y=224        "Start: options menu" footer (green, centered)
 */

#include "about.h"
#include "../app.h"
#include "../gen_logo.h"
#include "../ui/text.h"

#include <libdragon.h>

/* Re-export the shared palette under the dc-style names this file
 * already uses, so layout reads the same as the dc tester's about
 * page. */
#define COL_TITLE   JT_COL_TITLE
#define COL_VALUE   JT_COL_VALUE
#define COL_CYAN    JT_COL_CYAN
#define COL_GREY    0x909090ff       /* slightly brighter than JT_COL_DIM for readability on credits */
#define COL_GREEN   JT_COL_FOOTER
#define LOGO_SCALE  1            /* 76 native + 2x horizontal in draw -> 152 wide */

/* Draw the joypad logo silhouette mask in the given fg colour at
 * (sx, sy). 2x horizontal scaling compensates for N64's anamorphic
 * 320x240 framebuffer (same trick the screensaver uses). */
static void draw_logo(surface_t *surf, int sx, int sy, uint32_t fg)
{
    uint8_t r = (fg >> 24) & 0xff;
    uint8_t g = (fg >> 16) & 0xff;
    uint8_t b = (fg >>  8) & 0xff;
    uint32_t native = graphics_make_color(r, g, b, 0xff);

    for (int row = 0; row < LOGO_H; row++) {
        const uint8_t *row_bytes = &gen_logo[row * LOGO_W];
        for (int col = 0; col < LOGO_W; col++) {
            if (row_bytes[col]) {
                int x = sx + col * 2;
                graphics_draw_pixel(surf, x,     sy + row, native);
                graphics_draw_pixel(surf, x + 1, sy + row, native);
            }
        }
    }
}

static void about_draw(void)
{
    surface_t *surf = display_get();
    graphics_fill_screen(surf, graphics_make_color(0, 0, 0, 0xff));

    /* Use the surface's actual width for centering -- matches the
     * options-menu + tester fix; hardcoding 320 drifts when libdragon
     * hands back a wider surface. */
    int screen_w = surf->width  ? surf->width  : 320;
    int screen_h = surf->height ? surf->height : 240;

    /* Centred logo. 76x64 source, drawn 2x wide -> 152x64 on screen. */
    int logo_w = LOGO_W * 2;
    int logo_x = (screen_w - logo_w) / 2;
    int logo_y = 16;
    draw_logo(surf, logo_x, logo_y, COL_VALUE);

    int y = logo_y + LOGO_H + 12;     /* ~92 */
    txt_draw_centered(surf, y, COL_TITLE, "Joypad Tester - Nintendo 64", screen_w);
    y += 16;
    txt_draw_centered(surf, y, COL_VALUE, "Version " JT_VERSION_STR,      screen_w);
    y += 20;
    txt_draw_centered(surf, y, COL_CYAN,  "Built on LibDragon",           screen_w);
    y += 28;
    txt_draw_centered(surf, y, COL_GREY,  "Tests N64 / GCN pads, Mouse, VRU, KB,", screen_w);
    y += 12;
    txt_draw_centered(surf, y, COL_GREY,  "Rumble / Memory / Transfer Pak,",      screen_w);
    y += 12;
    txt_draw_centered(surf, y, COL_GREY,  "Bio Sensor, GB Camera, Snap Station,", screen_w);
    y += 12;
    txt_draw_centered(surf, y, COL_GREY,  "and GBA via the GC/GBA link cable.",   screen_w);

    txt_draw_centered(surf, screen_h - 40, COL_GREY,
                      "github.com/joypad-ai/joypad-tester", screen_w);
    txt_draw_footer(surf, "Start: options menu");

    display_show(surf);
}

const jt_mode_t jt_mode_about = {
    .name   = "About",
    .enter  = NULL,
    .leave  = NULL,
    .update = NULL,
    .draw   = about_draw,
};
