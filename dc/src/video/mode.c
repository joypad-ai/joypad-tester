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

#include "mode.h"

static jt_cable_t  detected_cable  = JT_CABLE_UNKNOWN;
static jt_region_t detected_region = JT_REGION_UNKNOWN;
static bool        progressive     = false;

void jt_video_init(void)
{
    int cable = vid_check_cable();
    switch (cable) {
        case CT_VGA:       detected_cable = JT_CABLE_VGA;       break;
        case CT_RGB:       detected_cable = JT_CABLE_RGB;       break;
        case CT_COMPOSITE: detected_cable = JT_CABLE_COMPOSITE; break;
        default:           detected_cable = JT_CABLE_UNKNOWN;   break;
    }

    /* Read BIOS region for the 480i refresh selection. */
    int region = flashrom_get_region();
    switch (region) {
        case FLASHROM_REGION_JAPAN:  detected_region = JT_REGION_JAPAN;  break;
        case FLASHROM_REGION_US:     detected_region = JT_REGION_USA;    break;
        case FLASHROM_REGION_EUROPE: detected_region = JT_REGION_EUROPE; break;
        default:                     detected_region = JT_REGION_UNKNOWN; break;
    }

    int mode;
    if (detected_cable == JT_CABLE_VGA) {
        mode = DM_640x480_VGA;
        progressive = true;
    } else if (detected_region == JT_REGION_EUROPE) {
        mode = DM_640x480_PAL_IL;
        progressive = false;
    } else {
        mode = DM_640x480_NTSC_IL;
        progressive = false;
    }
    /* DM_MULTIBUFFER allocates several framebuffers across VRAM so the
     * render loop can draw to a hidden buffer and vid_flip() it at
     * vblank -- tear/flicker-free double buffering. Everything draws a
     * full frame each tick; there's no single-buffer beam race. */
    vid_set_mode(mode | DM_MULTIBUFFER, PM_RGB565);
}

jt_cable_t  jt_video_cable(void)  { return detected_cable;  }
jt_region_t jt_video_region(void) { return detected_region; }
bool        jt_video_is_progressive(void) { return progressive; }

const char *jt_cable_name(jt_cable_t c)
{
    switch (c) {
        case JT_CABLE_VGA:       return "VGA 640x480 60Hz";
        case JT_CABLE_RGB:       return "RGB (480i)";
        case JT_CABLE_COMPOSITE: return "Composite (480i)";
        default:                 return "Unknown";
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
