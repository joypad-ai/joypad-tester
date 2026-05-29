/*
 * mode.c — KOS video bring-up.
 *
 * Detect cable at boot. VGA gets the 60Hz progressive path; everything
 * else falls back to a 480i mode chosen per BIOS region. Region read
 * from flashrom so a region-modded console still picks the right
 * refresh.
 */
#include <kos.h>
#include <dc/video.h>
#include <dc/flashrom.h>
#include <string.h>

#include "mode.h"

static jt_cable_t     detected_cable  = JT_CABLE_UNKNOWN;
static jt_region_t    detected_region = JT_REGION_UNKNOWN;
static jt_video_out_t out_mode        = JT_OUT_480I;

/* Offscreen 640x480 render target for the 240p downscale path. The UI is
 * always drawn at 640x480 (into here when in 240p, straight to the
 * framebuffer otherwise); jt_video_end_frame box-averages this down to
 * the real 320x240 framebuffer. ~600KB of BSS in 16MB main RAM. */
static uint16_t offscreen[640 * 480];
/* The real back buffer, stashed while drawing is redirected offscreen. */
static uint16_t *saved_fb = NULL;
static int       fb_out_w = 640;   /* real framebuffer width (stride) */

/* Pick the KOS display mode for the current output + region. */
static int select_mode(void)
{
    if (out_mode == JT_OUT_VGA)  return DM_640x480_VGA;
    if (out_mode == JT_OUT_240P)
        return (detected_region == JT_REGION_EUROPE) ? DM_320x240_PAL
                                                      : DM_320x240_NTSC;
    return (detected_region == JT_REGION_EUROPE) ? DM_640x480_PAL_IL
                                                 : DM_640x480_NTSC_IL;
}

static void apply_mode(void)
{
    /* DM_MULTIBUFFER allocates several framebuffers across VRAM so the
     * render loop can draw to a hidden buffer and vid_flip() it at
     * vblank -- tear/flicker-free double buffering. */
    vid_set_mode(select_mode() | DM_MULTIBUFFER, PM_RGB565);
    /* Use the width KOS actually configured as the downscale stride,
     * rather than assuming 320, so the box-average lands on scanlines. */
    fb_out_w = vid_mode ? vid_mode->width : ((out_mode == JT_OUT_240P) ? 320 : 640);
}

void jt_video_init(void)
{
    int cable = vid_check_cable();
    switch (cable) {
        case CT_VGA:       detected_cable = JT_CABLE_VGA;       break;
        case CT_RGB:       detected_cable = JT_CABLE_RGB;       break;
        case CT_COMPOSITE: detected_cable = JT_CABLE_COMPOSITE; break;
        default:           detected_cable = JT_CABLE_UNKNOWN;   break;
    }

    /* Read BIOS region for the refresh selection (50Hz PAL vs 60Hz). */
    int region = flashrom_get_region();
    switch (region) {
        case FLASHROM_REGION_JAPAN:  detected_region = JT_REGION_JAPAN;  break;
        case FLASHROM_REGION_US:     detected_region = JT_REGION_USA;    break;
        case FLASHROM_REGION_EUROPE: detected_region = JT_REGION_EUROPE; break;
        default:                     detected_region = JT_REGION_UNKNOWN; break;
    }

    /* Default to the best non-flickering mode the display supports: VGA
     * gets 480p (progressive), everything else (RGB/composite) gets 480i.
     * 240p is opt-in via the About-screen toggle -- it's flicker-free on a
     * CRT/PVM but interlaced TVs handle 480i fine, so 480i is the safer
     * default. (Note: the 320x240 240p mode doesn't render under Flycast,
     * only on real hardware.) */
    out_mode = (detected_cable == JT_CABLE_VGA) ? JT_OUT_VGA : JT_OUT_480I;
    apply_mode();
}

jt_cable_t     jt_video_cable(void)  { return detected_cable;  }
jt_region_t    jt_video_region(void) { return detected_region; }
jt_video_out_t jt_video_output(void) { return out_mode; }
bool jt_video_is_progressive(void)   { return out_mode != JT_OUT_480I; }

jt_video_out_t jt_video_toggle_tv_output(void)
{
    if (out_mode == JT_OUT_VGA) return JT_OUT_VGA;   /* VGA: nothing to toggle */
    out_mode = (out_mode == JT_OUT_240P) ? JT_OUT_480I : JT_OUT_240P;
    apply_mode();
    return out_mode;
}

/* Average two RGB565 pixels without unpacking: mask each channel's low
 * bit before the shift so the halving can't carry across channels. */
static inline uint16_t avg565(uint16_t a, uint16_t b)
{
    return (uint16_t)((a & b) + (((a ^ b) & 0xF7DE) >> 1));
}

void jt_video_begin_frame(void)
{
    if (out_mode == JT_OUT_240P) {
        saved_fb = vram_s;                 /* real 320x240 back buffer */
        vram_s   = (uint16 *)offscreen;    /* redirect the 640x480 draw */
    }
}

void jt_video_end_frame(void)
{
    if (out_mode == JT_OUT_240P) {
        /* 2x2 box-average 640x480 offscreen -> 320x240 framebuffer.
         * Solid regions (panels, zoomed canvas pixels) average to
         * themselves and stay crisp; only thin/high-contrast edges
         * soften, which is exactly what removes interlace flicker. */
        for (int oy = 0; oy < 240; oy++) {
            const uint16_t *r0 = offscreen + (oy * 2) * 640;
            const uint16_t *r1 = r0 + 640;
            uint16_t       *o  = saved_fb + oy * fb_out_w;
            for (int ox = 0; ox < 320; ox++) {
                int sx = ox * 2;
                o[ox] = avg565(avg565(r0[sx], r0[sx + 1]),
                               avg565(r1[sx], r1[sx + 1]));
            }
        }
        vram_s = saved_fb;
    }
    vid_flip(-1);
    vid_waitvbl();
}

const char *jt_cable_name(jt_cable_t c)
{
    switch (c) {
        case JT_CABLE_VGA:       return "VGA";
        case JT_CABLE_RGB:       return "RGB";
        case JT_CABLE_COMPOSITE: return "Composite";
        default:                 return "Unknown";
    }
}

const char *jt_video_output_name(void)
{
    bool pal = (detected_region == JT_REGION_EUROPE);
    switch (out_mode) {
        case JT_OUT_VGA:  return "VGA 640x480p 60Hz";
        case JT_OUT_240P: return pal ? "240p 320x240 50Hz" : "240p 320x240 60Hz";
        default:          return pal ? "480i 640x480 50Hz" : "480i 640x480 60Hz";
    }
}

const char *jt_region_name(jt_region_t r)
{
    switch (r) {
        case JT_REGION_JAPAN:  return "Japan";
        case JT_REGION_USA:    return "USA";
        case JT_REGION_EUROPE: return "Europe";
        default:               return "Unknown";
    }
}
