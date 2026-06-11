/*
 * about.c -- the standard About page (PORTING.md §3.6 layout).
 *
 * Top-to-bottom: white logo sprite centered, title in TITLE color,
 * version (from -DJT_VERSION_STR), platform credit in CYAN, detected
 * hardware state in LABEL grey, GitHub URL in LABEL grey near the
 * bottom, footer hint in FOOTER green.
 *
 * Opens the options menu on Start alone -- PORTING.md §3.5.
 */
#include "about.h"

#include <hal/video.h>
#include <xboxkrnl/xboxkrnl.h>

#include "../ports/ports.h"
#include "../ui/bfont.h"
#include "../ui/colors.h"
#include "../ui/gen_logo.h"
#include "../ui/options_menu.h"

/* AV pack / region constants are surfaced by XVideoGetEncoderSettings():
 *   low byte  (VIDEO_ADAPTER_MASK)  -- AV_PACK_* cable type
 *   byte 1    (VIDEO_STANDARD_MASK) -- NTSC-M / NTSC-J / PAL region
 *   upper bits                     -- VIDEO_MODE_480P / 720P / 1080I,
 *                                     VIDEO_60Hz / 50Hz, widescreen flags
 * The VIDEO_REGION_* values are only defined inside nxdk's hal/video.c
 * so we hard-code the byte-1 codes locally. */
#define JT_VIDEO_REGION_NTSCM 0x00000100
#define JT_VIDEO_REGION_NTSCJ 0x00000200
#define JT_VIDEO_REGION_PAL   0x00000300

static const char *av_pack_label(uint32_t enc)
{
    switch (enc & VIDEO_ADAPTER_MASK) {
        case AV_PACK_NONE:     return "None";
        case AV_PACK_STANDARD: return "Composite";
        case AV_PACK_RFU:      return "RFU";
        case AV_PACK_SCART:    return "SCART RGB";
        case AV_PACK_HDTV:     return "Component";
        case AV_PACK_VGA:      return "VGA";
        case AV_PACK_SVIDEO:   return "S-Video";
        default:               return "?";
    }
}

static const char *region_label(uint32_t enc)
{
    switch (enc & VIDEO_STANDARD_MASK) {
        case JT_VIDEO_REGION_NTSCM: return "NTSC-M";
        case JT_VIDEO_REGION_NTSCJ: return "NTSC-J";
        case JT_VIDEO_REGION_PAL:   return "PAL";
        default:                    return "?";
    }
}

static const char *scan_label(uint32_t enc)
{
    if (enc & VIDEO_MODE_1080I) return "1080i";
    if (enc & VIDEO_MODE_720P)  return "720p";
    if (enc & VIDEO_MODE_480P)  return "480p";
    return "480i";
}

static uint32_t prev_btns_aggregate;

static void about_enter(void)
{
    prev_btns_aggregate = 0;
}
static void about_leave(void) {}

static uint32_t aggregate_buttons(void)
{
    uint32_t b = 0;
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        if (jt_ports[p].present && jt_ports[p].style == JT_STYLE_PAD)
            b |= jt_ports[p].pad.buttons;
    }
    return b;
}

static void about_update(float dt)
{
    (void)dt;
    uint32_t btns  = aggregate_buttons();
    uint32_t edges = btns & ~prev_btns_aggregate;
    prev_btns_aggregate = btns;
    if (edges & JT_BTN_START) jt_options_menu_open();
}

static void about_draw(void)
{
    jt_clear(JT_COL_BLACK);

    int w = jt_video_width();

    /* §3.6 step 1: logo sprite, white, centered at the top. Same
     * mask the screensaver uses, but rendered white here -- a static
     * brand mark, never cycled. */
    /* CRT safe-area: ~48 px inset all edges (~10% of 480). */
    int logo_x = (w - LOGO_W) / 2;
    int logo_y = 48;
    jt_blit_mask(logo_mask, LOGO_W, LOGO_H, LOGO_BYTES_PER_ROW,
                 logo_x, logo_y, JT_COL_WHITE);

    int y = logo_y + LOGO_H + 24;
    jt_text_centered(y, JT_COL_TITLE, JT_COL_BLACK, "Joypad Tester - Xbox");
    y += 24;
    jt_text_centered(y, JT_COL_VALUE, JT_COL_BLACK, "Version " JT_VERSION_STR);
    y += 24;
    jt_text_centered(y, JT_COL_CYAN, JT_COL_BLACK, "Built on nxdk + SDL2");
    y += 24;

    /* Detected hardware state -- "Render" is the framebuffer we asked
     * XVideoSetMode for (always 640x480 today). "Output" decodes what
     * the AV encoder is actually driving over the cable, read from
     * XVideoGetEncoderSettings() -- cable type, region, scan mode,
     * refresh. Useful because a SCART/composite TV will downscale our
     * 480i scanout to 240p, which makes "Video: 640x480" misleading
     * if you only have one line. */
    int pads = 0, mus = 0;
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        if (jt_ports[p].present) pads++;
        for (int s = 0; s < JT_NUM_SLOTS; s++) {
            if (jt_ports[p].slots[s].kind == JT_SLOT_MU) mus++;
        }
    }
    DWORD enc = XVideoGetEncoderSettings();
    int hz = (enc & VIDEO_60Hz) ? 60 : (enc & VIDEO_50Hz) ? 50 : 0;
    jt_text_centered(y, JT_COL_LABEL, JT_COL_BLACK,
                     "Render: %dx%d  Pads: %d  MUs: %d",
                     jt_video_width(), jt_video_height(), pads, mus);
    y += 24;
    jt_text_centered(y, JT_COL_LABEL, JT_COL_BLACK,
                     "Output: %s  %s  %s  %dHz",
                     av_pack_label(enc), region_label(enc),
                     scan_label(enc), hz);
    y += 24;

    /* GitHub URL one row above the footer, both pinned inside the
     * ~48 px CRT-safe bottom inset. */
    jt_text_centered(jt_video_height() - 72, JT_COL_LABEL, JT_COL_BLACK,
                     "github.com/joypad-ai/joypad-tester");
    jt_text_centered(jt_video_height() - 48, JT_COL_FOOTER, JT_COL_BLACK,
                     "Start: options menu");
}

const jt_mode_t jt_mode_about = {
    .name   = "About",
    .enter  = about_enter,
    .leave  = about_leave,
    .update = about_update,
    .draw   = about_draw,
};
