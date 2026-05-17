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

#endif
