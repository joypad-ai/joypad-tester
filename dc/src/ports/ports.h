/*
 * ports.h — per-port maple-bus state model.
 *
 * Dreamcast has 4 maple ports (A/B/C/D), each with up to 2 expansion
 * slots. We track a snapshot of every port + slot every frame so both
 * the controller-tester view and the VMU-editor view can read from
 * the same source of truth.
 */
#ifndef JT_PORTS_H
#define JT_PORTS_H

#include <stdint.h>
#include <stdbool.h>

#define JT_NUM_PORTS 4
#define JT_NUM_SLOTS 2  /* per port */

typedef enum {
    JT_STYLE_EMPTY = 0,
    JT_STYLE_PAD,
    JT_STYLE_MOUSE,
    JT_STYLE_KEYBOARD,
    JT_STYLE_LIGHTGUN,
    JT_STYLE_OTHER     /* fallback when func mask doesn't match a known style */
} jt_port_style_t;

typedef enum {
    JT_SLOT_EMPTY = 0,
    JT_SLOT_VMU,
    JT_SLOT_PURUPURU,
    JT_SLOT_MICROPHONE,
    JT_SLOT_OTHER
} jt_slot_kind_t;

/* Pad state. Fields that don't apply to a given device stay zero,
 * same convention as the gcn/ tester. */
typedef struct {
    int16_t  stick_x, stick_y;   /* -128..127 */
    int16_t  stick2_x, stick2_y; /* secondary stick if present */
    uint8_t  trig_l, trig_r;     /* 0..255 */
    uint32_t buttons;            /* DC button bitfield (CONT_A, CONT_B, etc.) */
} jt_pad_state_t;

typedef struct {
    int32_t  dx, dy, dz;         /* per-frame deltas */
    int32_t  abs_x, abs_y;       /* running accumulator */
    uint32_t buttons;            /* MOUSE_*BUTTON bits */
} jt_mouse_state_t;

typedef struct {
    /* Up to 6 simultaneous keys (DC keyboard reports a fixed array). */
    uint8_t scancodes[6];
    uint8_t modifiers;
} jt_kbd_state_t;

typedef struct {
    jt_slot_kind_t kind;
    /* VMU specifics. block_free / block_total are -1 when unknown
     * (not yet probed this second). */
    int16_t block_free;
    int16_t block_total;
    bool    has_lcd;
    bool    has_clock;
    /* Set true while user is holding the actuation key on this slot
     * (rumble/lcd-draw/clock-read). */
    bool    rumble_active;
    bool    lcd_test_active;
    bool    clock_test_active;
} jt_slot_state_t;

typedef struct {
    bool             present;
    jt_port_style_t  style;
    uint32_t         func_mask;   /* raw maple function mask, for diagnostics */
    jt_pad_state_t   pad;
    jt_mouse_state_t mouse;
    jt_kbd_state_t   kbd;
    jt_slot_state_t  slots[JT_NUM_SLOTS];
} jt_port_state_t;

/* Global ports array, populated by ports_poll(). Modes read from
 * this; the input layer writes to it. */
extern jt_port_state_t jt_ports[JT_NUM_PORTS];

/* Re-enumerate maple devices and snapshot their state. Call once
 * per frame from main.c. */
void jt_ports_init(void);
void jt_ports_poll(void);

/* Convenience labels (NUL-terminated, statically allocated). */
const char *jt_port_style_name(jt_port_style_t s);
const char *jt_slot_kind_name(jt_slot_kind_t k);

#endif /* JT_PORTS_H */
