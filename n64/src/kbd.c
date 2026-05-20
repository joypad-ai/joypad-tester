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

/* Full key matrix, transcribed from the n64brew wiki "Keyboard" page
 * (Key Matrix Map). jbsc = X<<8 | Y; `ch` is the base (unshifted)
 * ASCII char, 0 for non-character keys. Shift upper-cases letters at
 * lookup time -- JIS symbol shift-variants aren't in the source
 * table, so we don't fabricate them. */
typedef struct { uint16_t jbsc; const char *name; char ch; } kbd_key_t;

static const kbd_key_t KBD_KEYS[] = {
    { 0x0606, "0", '0' }, { 0x0C05, "1", '1' }, { 0x0505, "2", '2' },
    { 0x0605, "3", '3' }, { 0x0705, "4", '4' }, { 0x0805, "5", '5' },
    { 0x0905, "6", '6' }, { 0x0906, "7", '7' }, { 0x0806, "8", '8' },
    { 0x0706, "9", '9' },
    { 0x0D07, "A", 'a' }, { 0x0708, "B", 'b' }, { 0x0508, "C", 'c' },
    { 0x0507, "D", 'd' }, { 0x0601, "E", 'e' }, { 0x0607, "F", 'f' },
    { 0x0707, "G", 'g' }, { 0x0807, "H", 'h' }, { 0x0804, "I", 'i' },
    { 0x0907, "J", 'j' }, { 0x0903, "K", 'k' }, { 0x0803, "L", 'l' },
    { 0x0908, "M", 'm' }, { 0x0808, "N", 'n' }, { 0x0704, "O", 'o' },
    { 0x0604, "P", 'p' }, { 0x0C01, "Q", 'q' }, { 0x0701, "R", 'r' },
    { 0x0C07, "S", 's' }, { 0x0801, "T", 't' }, { 0x0904, "U", 'u' },
    { 0x0608, "V", 'v' }, { 0x0501, "W", 'w' }, { 0x0C08, "X", 'x' },
    { 0x0901, "Y", 'y' }, { 0x0D08, "Z", 'z' },
    { 0x0602, "Space", ' ' }, { 0x0506, "-", '-' }, { 0x0703, "+", '+' },
    { 0x0603, "*", '*' }, { 0x1105, "|", '|' }, { 0x1004, "\\", '\\' },
    { 0x0C04, "[", '[' }, { 0x0406, "]", ']' }, { 0x0702, "?", '?' },
    { 0x0802, ">", '>' }, { 0x0902, "<", '<' }, { 0x0C06, "`", '`' },
    { 0x0504, "'", '\'' },
    { 0x0D04, "Enter", 0 }, { 0x0D06, "Bksp", 0 }, { 0x0D01, "Tab", 0 },
    { 0x0A08, "Esc", 0 },
    { 0x0E01, "Shift", 0 }, { 0x0E06, "Shift", 0 }, { 0x1107, "Ctrl", 0 },
    { 0x1008, "Alt", 0 }, { 0x0F07, "Meta", 0 }, { 0x0B05, "Menu", 0 },
    { 0x0F05, "Caps", 0 }, { 0x0A05, "Num", 0 },
    { 0x0204, "Up", 0 }, { 0x0305, "Down", 0 },
    { 0x0205, "Left", 0 }, { 0x0405, "Right", 0 },
    { 0x1010, "Home", 0 }, { 0x0206, "End", 0 },
    { 0x0208, "PgUp", 0 }, { 0x0207, "PgDn", 0 },
    { 0x0B01, "F1", 0 }, { 0x0A01, "F2", 0 }, { 0x0B08, "F3", 0 },
    { 0x0A07, "F4", 0 }, { 0x0B07, "F5", 0 }, { 0x0A02, "F6", 0 },
    { 0x0B02, "F7", 0 }, { 0x0A03, "F8", 0 }, { 0x0B03, "F9", 0 },
    { 0x0A04, "F10", 0 }, { 0x0203, "F11", 0 }, { 0x0B06, "F12", 0 },
    { 0x0E02, "Henkan", 0 }, { 0x1002, "Muhenkan", 0 },
    { 0x1006, "Kana", 0 }, { 0x0D05, "Zen/Han", 0 },
};
#define KBD_KEYS_N (sizeof(KBD_KEYS) / sizeof(KBD_KEYS[0]))

const char *kbd_key_name(uint16_t jbsc)
{
    for (size_t i = 0; i < KBD_KEYS_N; i++)
        if (KBD_KEYS[i].jbsc == jbsc) return KBD_KEYS[i].name;
    return "?";
}

char kbd_key_char(uint16_t jbsc, bool shift)
{
    for (size_t i = 0; i < KBD_KEYS_N; i++) {
        if (KBD_KEYS[i].jbsc != jbsc) continue;
        char c = KBD_KEYS[i].ch;
        if (shift && c >= 'a' && c <= 'z') c -= 32;   /* upper-case */
        return c;
    }
    return 0;
}
