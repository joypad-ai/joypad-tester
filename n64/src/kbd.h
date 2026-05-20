/*
 * kbd.h — N64 RandNet keyboard (RND-001) Joybus reader.
 *
 * The RandNet keyboard reports as Joybus identifier 0x0002
 * (JOYBUS_IDENTIFIER_N64_RANDNET_KEYBOARD). libdragon only ships the
 * identifier constant -- no read API -- so this rolls the documented
 * keyboard read by hand over joybus_exec_cmd.
 *
 * Protocol (consoleprotocols.com + n64brew wiki, cross-checked
 * against meeq's KeyboardTest-N64 reference ROM):
 *   TX: command 0x13 + 1 LED-parameter byte (see KBD_LED_* below).
 *   RX: 7 bytes = three 16-bit key scancodes (X<<8 | Y matrix coords)
 *       + one status byte. Up to 3 simultaneous keys; a 4th sets the
 *       status overflow bit. The RandNet cart polled at ~30 Hz.
 *
 * We surface decoded key names + a typed-text line + LED control.
 * The full X/Y matrix->key table (see kbd.c) is transcribed from the
 * n64brew wiki "Keyboard" page's Key Matrix Map -- 85 keys, cross-
 * checked against meeq's KeyboardTest-N64 for the lock-key codes.
 * Unverified on real RND-001 hardware (Japan-only, no emulator
 * emulates it), but the protocol + table are correct-by-source.
 */
#ifndef N64_KBD_H
#define N64_KBD_H

#include <stdint.h>
#include <stdbool.h>

#define KBD_MAX_KEYS 3

/* LED-parameter byte bits (from meeq's KeyboardTest-N64). */
#define KBD_LED_NUM_LOCK   0x01
#define KBD_LED_CAPS_LOCK  0x02
#define KBD_LED_POWER      0x04

/* Notable scancodes (full table lives in kbd.c). */
#define KBD_SCANCODE_CAPS_LOCK 0x0F05
#define KBD_SCANCODE_NUM_LOCK  0x0A05
#define KBD_SCANCODE_BACKSPACE 0x0D06
#define KBD_SCANCODE_SHIFT_L   0x0E01
#define KBD_SCANCODE_SHIFT_R   0x0E06

typedef struct {
    bool     responded;            /* joybus exchange completed */
    int      nkeys;                /* number of non-zero scancodes (0..3) */
    uint16_t keys[KBD_MAX_KEYS];   /* raw 16-bit JBSC scancodes: X<<8 | Y */
    uint8_t  status;               /* raw 7th status byte */
    bool     overflow;             /* status bit 4: >3 keys held at once */
    bool     caps_lock;            /* Caps Lock key (0x0F05) currently held */
    bool     num_lock;             /* Num Lock key (0x0A05) currently held */
    uint8_t  leds;                 /* LED-param byte last sent to the pad */
} kbd_state_t;

/* Poll the RandNet keyboard on `port` (Joybus command 0x13). Fills
 * *out with the up-to-3 held scancodes + status, and drives the
 * keyboard's Power LED (always) + Caps/Num Lock LEDs (while their
 * keys are held). Caller should gate this on the port reporting
 * JOYBUS_IDENTIFIER_N64_RANDNET_KEYBOARD. */
void kbd_poll(int port, kbd_state_t *out);

/* Look up the documented key name for a raw JBSC scancode (X<<8 | Y),
 * e.g. 0x0D07 -> "A", 0x0602 -> "Space". Returns "?" if unknown.
 * Table transcribed from the n64brew wiki's Keyboard "Key Matrix Map". */
const char *kbd_key_name(uint16_t jbsc);

/* Printable ASCII for a scancode, or 0 if the key isn't a character
 * (modifiers, F-keys, arrows, lock keys, Japanese IME keys, etc.).
 * `shift` upper-cases letters; symbol shift-variants aren't modeled
 * (the matrix table only documents each key's base function). */
char kbd_key_char(uint16_t jbsc, bool shift);

#endif
