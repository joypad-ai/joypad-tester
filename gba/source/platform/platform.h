// platform.h — minimum shim for eyes_anim.c when ported to the GBA.
// The OLED-side code includes "platform/platform.h" and calls
// platform_time_ms(); we wire that to a VBlank-derived counter from main.c.
#ifndef EYES_GBA_PLATFORM_H
#define EYES_GBA_PLATFORM_H

#include <stdint.h>

uint32_t platform_time_ms(void);

#endif
