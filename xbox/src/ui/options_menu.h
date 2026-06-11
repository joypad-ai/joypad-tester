/*
 * options_menu.h -- modal mode-switcher overlay.
 *
 * PORTING.md §3.5: black box with a 2-px yellow border, centered
 * against the actual surface width; D-pad navigation, A confirm,
 * B cancel; tester first, About last. In the tester the menu opens
 * on Start+Down (Start alone stays a testable button); elsewhere it
 * opens on Start alone.
 *
 * Open-cooldown: jt_options_menu_input_locked() returns true for a
 * few frames after open so the bouncing controller press that
 * triggered the open doesn't leak through as a confirm.
 *
 * The just_closed flag is one-shot per close; the dispatcher reads
 * it to skip the underlying mode's update for one frame so the A
 * press that selected a new mode doesn't leak into the newly-active
 * mode's input handler.
 */
#ifndef JT_XBOX_OPTIONS_MENU_H
#define JT_XBOX_OPTIONS_MENU_H

#include <stdbool.h>

void jt_options_menu_init(void);

/* Caller signals "user pressed the open combo" from inside a mode's
 * update() -- e.g. tester checks Start+Down, about checks Start. */
void jt_options_menu_open(void);

bool jt_options_menu_visible(void);
bool jt_options_menu_just_closed(void);
bool jt_options_menu_input_locked(void);

void jt_options_menu_update(float dt);
void jt_options_menu_draw(void);

#endif /* JT_XBOX_OPTIONS_MENU_H */
