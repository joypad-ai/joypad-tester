/*
 * tester.h — default landing screen: per-port grid + GBA multiboot
 * trigger + mouse delta + Transfer Pak GB header peek.
 */
#ifndef N64_MODE_TESTER_H
#define N64_MODE_TESTER_H

#include "../app.h"

extern const jt_mode_t jt_mode_tester;

/* True iff any GBA-via-Joybus port has been booted and its most-
 * recent REG_KEYINPUT poll shows a non-idle state. Lets the global
 * screensaver-idle check count GBA presses as "input" (libdragon's
 * joypad subsystem doesn't surface them on its own). */
bool jt_tester_gba_input_active(void);

/* True while a RandNet keyboard had a key held in the last second.
 * Lets the screensaver-idle check count typing as input (the keyboard
 * isn't surfaced as a libdragon controller). */
bool jt_tester_kbd_input_active(void);

#endif
