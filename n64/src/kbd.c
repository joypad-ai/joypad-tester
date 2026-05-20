/*
 * kbd.c — see kbd.h.
 */

#include "kbd.h"

#include <libdragon.h>
#include <string.h>

/* Status byte: bit 4 is documented as "set when 4 keys were pressed
 * at the same time" (overflow). Other bits are referenced in the docs
 * but not precisely mapped, so we expose the raw byte and only decode
 * the overflow bit. */
#define KBD_STATUS_OVERFLOW 0x10

/* Persistent LED state per port. Power LED on by default so a plugged-
 * in keyboard visibly lights up the moment it's detected (matches
 * meeq's KeyboardTest-N64). */
static uint8_t led_status[4] = {
    KBD_LED_POWER, KBD_LED_POWER, KBD_LED_POWER, KBD_LED_POWER,
};

void kbd_poll(int port, kbd_state_t *out)
{
    memset(out, 0, sizeof(*out));
    if (port < 0 || port > 3) return;

    /* TX: 0x13 + the running LED-param byte. RX: 7 bytes. */
    uint8_t cmd[2] = { 0x13, led_status[port] };
    uint8_t r[7]   = { 0 };
    joybus_exec_cmd(port, sizeof(cmd), sizeof(r), cmd, r);
    out->responded = true;

    /* Bytes 0..5 are three 16-bit X/Y scancodes; byte 6 is status. */
    for (int i = 0; i < KBD_MAX_KEYS; i++) {
        uint16_t sc = ((uint16_t)r[i * 2] << 8) | r[i * 2 + 1];
        if (sc != 0) out->keys[out->nkeys++] = sc;
    }
    out->status   = r[6];
    out->overflow = (r[6] & KBD_STATUS_OVERFLOW) != 0;

    /* Light Caps/Num Lock LEDs while their keys are held (reference
     * scancodes from meeq's KeyboardTest-N64). The Power LED stays on. */
    bool caps = false, num = false;
    for (int i = 0; i < out->nkeys; i++) {
        if (out->keys[i] == KBD_SCANCODE_CAPS_LOCK) caps = true;
        if (out->keys[i] == KBD_SCANCODE_NUM_LOCK)  num  = true;
    }
    out->caps_lock = caps;
    out->num_lock  = num;

    if (caps) led_status[port] |=  KBD_LED_CAPS_LOCK;
    else      led_status[port] &= ~KBD_LED_CAPS_LOCK;
    if (num)  led_status[port] |=  KBD_LED_NUM_LOCK;
    else      led_status[port] &= ~KBD_LED_NUM_LOCK;
    out->leds = led_status[port];
}
