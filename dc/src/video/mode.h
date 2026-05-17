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

void jt_video_init(void);
jt_cable_t  jt_video_cable(void);
jt_region_t jt_video_region(void);
const char *jt_cable_name(jt_cable_t c);
const char *jt_region_name(jt_region_t r);
bool        jt_video_is_progressive(void);

#endif
