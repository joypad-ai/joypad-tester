/*
 * mode.h — video init + cable/region detection.
 */
#ifndef JT_VIDEO_MODE_H
#define JT_VIDEO_MODE_H

#include <stdbool.h>

typedef enum {
    JT_CABLE_VGA = 0,
    JT_CABLE_RGB,
    JT_CABLE_COMPOSITE,
    JT_CABLE_UNKNOWN
} jt_cable_t;

typedef enum {
    JT_REGION_JAPAN = 0,
    JT_REGION_USA,
    JT_REGION_EUROPE,
    JT_REGION_UNKNOWN
} jt_region_t;

/* How the framebuffer is presented. VGA is always progressive 480p.
 * On a TV the user chooses between 240p (default) and 480i: 240p renders
 * the 640x480 UI into an offscreen buffer and box-averages it down to a
 * real 320x240 progressive framebuffer (flicker-free on a PVM/CRT),
 * while 480i draws straight to a 640x480 interlaced framebuffer. */
typedef enum {
    JT_OUT_VGA = 0,   /* 640x480 VGA progressive 60Hz (no downscale) */
    JT_OUT_480I,      /* 640x480 interlaced (no downscale) */
    JT_OUT_240P,      /* 320x240 progressive; UI rendered 640x480 -> downscaled */
} jt_video_out_t;

void jt_video_init(void);
jt_cable_t  jt_video_cable(void);
jt_region_t jt_video_region(void);
const char *jt_cable_name(jt_cable_t c);
const char *jt_region_name(jt_region_t r);
bool        jt_video_is_progressive(void);

/* Current output mode + a human-readable name for the About screen. */
jt_video_out_t jt_video_output(void);
const char    *jt_video_output_name(void);

/* Toggle the TV output between 240p and 480i at runtime. No-op (returns
 * JT_OUT_VGA) when a VGA cable is connected. Returns the new output. */
jt_video_out_t jt_video_toggle_tv_output(void);

/* Frame framing. Every presented frame goes through this pair so the
 * 240p downscale wraps all drawing: begin redirects drawing to the
 * offscreen buffer (240p only), end averages it into the real 320x240
 * framebuffer (240p only) and flips at vblank. In 480i/VGA they just
 * flip. Both the main loop and jt_show_busy use these. */
void jt_video_begin_frame(void);
void jt_video_end_frame(void);

#endif
