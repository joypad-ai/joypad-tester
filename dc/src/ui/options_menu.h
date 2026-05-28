/*
 * options_menu.h — Start+Down modal mode picker.
 */
#ifndef JT_OPTIONS_MENU_H
#define JT_OPTIONS_MENU_H

#include <stdbool.h>

void jt_options_menu_init(void);
void jt_options_menu_update(float dt);
void jt_options_menu_draw(void);
bool jt_options_menu_visible(void);

/* True for the single frame on which the menu closed (after confirm or
 * cancel). Lets main.c skip mode->update on that frame so the
 * confirming Start press doesn't leak through into the newly-active
 * mode's input handler. */
bool jt_options_menu_just_closed(void);

/* True from the frame the menu closes until the confirm/cancel buttons
 * (A / B / Start) are released. main.c suppresses mode input while this
 * holds so the A press that selected a menu item doesn't carry over and
 * register as a click in the newly-active mode. */
bool jt_options_menu_input_locked(void);

#endif
