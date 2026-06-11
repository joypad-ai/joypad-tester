/*
 * screensaver.h -- bouncing-logo idle screensaver.
 *
 * PORTING.md §3.4: 30s idle threshold, joypad-logo silhouette
 * sprite bounces at constant velocity, 7-color cycle advances on
 * each wall hit. Any pad/stick input wakes it.
 *
 * The dispatcher polls tick(dt) every frame and (when active)
 * draws the saver instead of the active mode. consume_wake() lets
 * the dispatcher skip the underlying mode's update for one frame on
 * wake so the input that woke the saver doesn't immediately get
 * consumed by the mode (e.g. opening the options menu).
 */
#ifndef JT_XBOX_SCREENSAVER_H
#define JT_XBOX_SCREENSAVER_H

#include <stdbool.h>

void jt_screensaver_init(void);
bool jt_screensaver_active(void);

/* Idle counter + position step. Reads jt_ports state to decide
 * "input vs no input". Call once per frame before mode update. */
void jt_screensaver_tick(float dt);

void jt_screensaver_draw(void);

/* Single-frame "we just woke" pulse. Cleared on read. */
bool jt_screensaver_consume_wake(void);

#endif /* JT_XBOX_SCREENSAVER_H */
