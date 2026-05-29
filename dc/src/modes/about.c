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
#include <dc/maple.h>
#include <dc/maple/controller.h>

#include "about.h"
#include "../ui/bfont_util.h"
#include "../ui/gen_logo.h"
#include "../video/mode.h"
#include "../ports/ports.h"
#include "../vms/vms.h"                     /* jt_vms_decode_icondata */
#include "../vms/apply.h"                   /* jt_vmu_show_mono_bits */
#include "../library/gen_library_icon.h"   /* joypad_logo_icondata_full */

static uint32_t last_btns = 0;
static uint32_t last_vmu_sig = 0;

static uint32_t aggregate_pad_buttons(void)
{
    uint32_t b = 0;
    for (int p = 0; p < JT_NUM_PORTS; p++)
        if (jt_ports[p].present && jt_ports[p].style == JT_STYLE_PAD)
            b |= jt_ports[p].pad.buttons;
    return b;
}

/* Bitmask of which VMU slots currently have an LCD, for hotplug detect. */
static uint32_t vmu_lcd_sig(void)
{
    uint32_t s = 0;
    for (int p = 0; p < JT_NUM_PORTS; p++)
        for (int sl = 0; sl < JT_NUM_SLOTS; sl++)
            if (jt_ports[p].slots[sl].kind == JT_SLOT_VMU &&
                jt_ports[p].slots[sl].has_lcd)
                s |= 1u << (p * JT_NUM_SLOTS + sl);
    return s;
}

/* Show the Joypad logo on every connected VMU's LCD while the About page
 * is up -- a little branding flourish. Decodes the bundled ICONDATA blob
 * once and pushes its mono to each LCD. */
static void push_joypad_icon_to_vmus(void)
{
    jt_icon_t icon;
    if (!jt_vms_decode_icondata(joypad_logo_icondata_full,
                                sizeof(joypad_logo_icondata_full), &icon))
        return;
    for (int p = 0; p < JT_NUM_PORTS; p++)
        for (int sl = 0; sl < JT_NUM_SLOTS; sl++)
            if (jt_ports[p].slots[sl].kind == JT_SLOT_VMU &&
                jt_ports[p].slots[sl].has_lcd)
                jt_vmu_show_mono_bits(p, sl, icon.mono_bits);
}

static void about_enter(void)
{
    last_btns = aggregate_pad_buttons();
    last_vmu_sig = vmu_lcd_sig();
    push_joypad_icon_to_vmus();
}
static void about_leave(void) {}

static void about_update(float dt)
{
    (void)dt;
    uint32_t btns = aggregate_pad_buttons();
    uint32_t edges = btns & ~last_btns;
    /* X toggles 240p <-> 480i on a TV. VGA is always progressive, so the
     * toggle is a no-op there and the hint is hidden. */
    if ((edges & CONT_X) && jt_video_cable() != JT_CABLE_VGA)
        jt_video_toggle_tv_output();
    last_btns = btns;

    /* Re-push the logo when a VMU is inserted/removed while viewing About. */
    uint32_t sig = vmu_lcd_sig();
    if (sig != last_vmu_sig) {
        last_vmu_sig = sig;
        push_joypad_icon_to_vmus();
    }
}

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
                     "Cable:  %s", jt_cable_name(jt_video_cable()));
    y += 24;
    jt_text_centered(y, JT_COL_GREY, JT_COL_BLACK,
                     "Region: %s", jt_region_name(jt_video_region()));
    y += 24;
    jt_text_centered(y, JT_COL_GREY, JT_COL_BLACK,
                     "Output: %s", jt_video_output_name());

    /* TV cables can toggle 240p/480i; VGA is fixed progressive. */
    if (jt_video_cable() != JT_CABLE_VGA) {
        jt_text_centered(380, JT_COL_YELLOW, JT_COL_BLACK,
                         "X: toggle 240p / 480i");
    }
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
