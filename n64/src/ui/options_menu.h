/*
 * options_menu.h — modal mode picker.
 *
 * Triggered by Start+D-pad Down on any pad. Captures input while
 * visible (D-pad navigates, A confirms, Start+Down toggles closed).
 * Renders by taking over the console; the active mode's draw() is
 * skipped for the frame.
 */
#ifndef N64_UI_OPTIONS_MENU_H
#define N64_UI_OPTIONS_MENU_H

#include <stdbool.h>

void jt_options_menu_init(void);
bool jt_options_menu_visible(void);
void jt_options_menu_update(void);
void jt_options_menu_draw(void);

#endif
