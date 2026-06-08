/*
 * ports.h -- OG Xbox controller / expansion-slot state.
 *
 * Mirrors dc/src/ports/ports.h: jt_ports[N] is the polled snapshot of
 * every port + expansion slot, so the tester / library code reads
 * from cached state instead of touching SDL or XID directly.
 *
 * Xbox has 4 controller ports and (per pad) 2 expansion slots
 * (top + bottom) that take Memory Units, voice headsets, etc. MU
 * detection lands in a follow-up; this v0.1.0 file only models the
 * pad state.
 */
#ifndef JT_XBOX_PORTS_H
#define JT_XBOX_PORTS_H

#include <stdbool.h>
#include <stdint.h>

#define JT_NUM_PORTS 4
#define JT_NUM_SLOTS 2  /* top + bottom expansion slot per pad */

/* Port "style". Xbox doesn't have the DC's mouse / keyboard /
 * lightgun mix on the controller ports the way the DC does -- almost
 * everything is a pad variant -- but we keep the enum shape so other
 * input classes (steering wheel, IR receiver, etc.) can be added
 * without a churn elsewhere. */
typedef enum {
    JT_STYLE_EMPTY = 0,
    JT_STYLE_PAD,
    JT_STYLE_OTHER,
} jt_port_style_t;

/* Slot "kind". Same shape as dc/'s. v0.1.0 only emits EMPTY and the
 * placeholder OTHER -- MU / Voice will land with the MU detect work. */
typedef enum {
    JT_SLOT_EMPTY = 0,
    JT_SLOT_MU,
    JT_SLOT_VOICE,
    JT_SLOT_OTHER,
} jt_slot_kind_t;

typedef struct {
    /* Digital (held) buttons -- packed bitmask, see JT_BTN_* below. */
    uint32_t buttons;
    /* Analog buttons (A/B/X/Y/Black/White) report 0..255 pressure on
     * an Xbox pad; we surface the four "main" face buttons here for
     * future use, even though most testers only care about the bit. */
    uint8_t  abtn_a, abtn_b, abtn_x, abtn_y;
    uint8_t  abtn_black, abtn_white;
    /* Sticks -- signed 16-bit per SDL gamecontroller convention. */
    int16_t  stick_lx, stick_ly;
    int16_t  stick_rx, stick_ry;
    /* Triggers -- 0..255, SDL exposes them as int16 0..32767 and we
     * downscale on read for parity with the dc/ trigger range. */
    uint8_t  trig_l, trig_r;
} jt_pad_state_t;

typedef struct {
    jt_slot_kind_t kind;
    /* Free / total blocks for MUs; -1 == unknown / not applicable. */
    int16_t        block_free;
    int16_t        block_total;
} jt_slot_state_t;

typedef struct {
    bool             present;       /* a device is plugged into this port */
    jt_port_style_t  style;
    char             product_name[64];
    uint16_t         vendor_id;
    uint16_t         product_id;
    jt_pad_state_t   pad;
    jt_slot_state_t  slots[JT_NUM_SLOTS];
} jt_port_state_t;

extern jt_port_state_t jt_ports[JT_NUM_PORTS];

/* Initialize SDL_GameController + clear state. Call once at boot. */
void jt_ports_init(void);

/* Refresh jt_ports[] from SDL's cached state. Call once per frame
 * after pumping SDL_PollEvent. */
void jt_ports_poll(void);

/* Drive rumble (0..65535 strong/weak) for `duration_ms`. No-op if
 * the port has no pad. Edge-based call -- pass 0,0 to stop. */
void jt_port_rumble(int port_idx, uint16_t strong, uint16_t weak, uint32_t duration_ms);

const char *jt_port_style_name(jt_port_style_t s);
const char *jt_slot_kind_name(jt_slot_kind_t k);

/* Packed digital-button bits. Layout chosen to match the SDL
 * gamecontroller button indices where natural, but the consumer
 * just tests by name -- the actual bit values are an implementation
 * detail. */
#define JT_BTN_A            (1u <<  0)
#define JT_BTN_B            (1u <<  1)
#define JT_BTN_X            (1u <<  2)
#define JT_BTN_Y            (1u <<  3)
#define JT_BTN_BLACK        (1u <<  4)   /* SDL: RIGHTSHOULDER on xbox map */
#define JT_BTN_WHITE        (1u <<  5)   /* SDL: LEFTSHOULDER */
#define JT_BTN_BACK         (1u <<  6)
#define JT_BTN_START        (1u <<  7)
#define JT_BTN_LSTICK       (1u <<  8)
#define JT_BTN_RSTICK       (1u <<  9)
#define JT_BTN_DPAD_UP      (1u << 10)
#define JT_BTN_DPAD_DOWN    (1u << 11)
#define JT_BTN_DPAD_LEFT    (1u << 12)
#define JT_BTN_DPAD_RIGHT   (1u << 13)

#endif /* JT_XBOX_PORTS_H */
