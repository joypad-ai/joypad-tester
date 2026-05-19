/*
 * bio.c — see header.
 */

#include "bio.h"

#include <string.h>

/* Bio pulse byte lives at 0xC000 in the accessory address space --
 * same slot as the Rumble motor (since the Bio Sensor steals that
 * register). Constant inlined here because libdragon trunk doesn't
 * expose it yet. */
#define BIO_ADDR_PULSE                   0xC000

#define BIO_PERIODS_MIN                  8
#define BIO_PERIODS_MAX                  16
#define BIO_PERIOD_INTERVAL_TICKS        (TICKS_PER_SECOND / 2)   /* 500 ms */
#define BIO_PERIODS_PER_MINUTE           120                       /* 60 * 2 */

typedef enum {
    BIO_STOPPED = 0,
    BIO_RESTING,
    BIO_PULSING,
} bio_state_t;

typedef struct {
    bio_state_t state;
    int64_t     period_start_ticks;
    unsigned    period_beats;
    unsigned    period_cursor;
    unsigned    period_counter;
    unsigned    beats_per_period[BIO_PERIODS_MAX];
} bio_reader_t;

static bio_reader_t readers[JOYBUS_PORT_COUNT];

void bio_reset(joypad_port_t port)
{
    memset(&readers[port], 0, sizeof(readers[port]));
}

void bio_tick(joypad_port_t port)
{
    bio_reader_t *r = &readers[port];
    uint8_t data[32];

    int rc = joybus_accessory_read(port, BIO_ADDR_PULSE, data);
    if (rc == JOYBUS_ACCESSORY_IO_STATUS_NO_PAK
        || rc == JOYBUS_ACCESSORY_IO_STATUS_NO_DEVICE) {
        bio_reset(port);
        return;
    }
    if (rc != JOYBUS_ACCESSORY_IO_STATUS_OK) {
        /* Transient CRC mismatch -- skip this sample but keep state. */
        return;
    }

    /* First successful read after a reset: prime the period clock so
     * the first window doesn't immediately roll. */
    if (r->state == BIO_STOPPED) {
        r->state = BIO_RESTING;
        r->period_start_ticks = timer_ticks();
    }

    int64_t now = timer_ticks();
    if (r->period_start_ticks + BIO_PERIOD_INTERVAL_TICKS < now) {
        r->beats_per_period[r->period_cursor++] = r->period_beats;
        if (r->period_cursor >= BIO_PERIODS_MAX) r->period_cursor = 0;
        r->period_beats = 0;
        r->period_counter++;
        r->period_start_ticks = now;
    }

    /* The sensor reports 0x00 during the "pulse on" phase and 0x03
     * during the "pulse off" phase. A beat = the falling edge from
     * pulse-on back to pulse-off. */
    uint8_t  sensor = data[0];
    bio_state_t next = BIO_STOPPED;
    if (sensor == 0x00) next = BIO_PULSING;
    if (sensor == 0x03) next = BIO_RESTING;

    if (r->state == BIO_PULSING && next == BIO_RESTING) {
        r->period_beats++;
    }
    r->state = next;
}

bool bio_is_pulsing(joypad_port_t port)
{
    return readers[port].state == BIO_PULSING;
}

int bio_get_bpm(joypad_port_t port)
{
    bio_reader_t *r = &readers[port];
    unsigned n = r->period_counter;
    if (n < BIO_PERIODS_MIN) return 0;          /* not enough samples yet */
    if (n > BIO_PERIODS_MAX) n = BIO_PERIODS_MAX;
    unsigned sum = 0;
    for (unsigned i = 0; i < n; i++) sum += r->beats_per_period[i];
    return (int)((sum * BIO_PERIODS_PER_MINUTE) / n);
}
