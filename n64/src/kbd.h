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
 * We surface the raw scancodes + status and light the keyboard LEDs.
 * A full JBSC->ASCII mapping is deferred: the X/Y matrix->key table
 * isn't published in machine-readable form anywhere -- even meeq's
 * reference ROM only dumps raw hex and special-cases the two lock
 * keys below. Filling the table needs the (Japan-only) RND-001 in
 * hand to capture each key's code; this build's raw readout is
 * exactly the tool for doing that.
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

/* Known scancodes (the only two anyone has documented). */
#define KBD_SCANCODE_CAPS_LOCK 0x0F05
#define KBD_SCANCODE_NUM_LOCK  0x0A05

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

#endif
