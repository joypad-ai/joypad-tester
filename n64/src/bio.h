/*
 * bio.h — Bio Sensor pulse decoder + BPM running-average.
 *
 * Port of meeq's bio_sensor.{c,h} (public domain), adapted to use
 * libdragon trunk's synchronous joybus_accessory_read each frame
 * rather than the VI-interrupt async path the original wires up.
 *
 * The Bio Sensor is a controller-pak-slot accessory shipped (JP-only)
 * with Pokemon Stadium 2. It reports a single byte at 0xC000:
 *   0x00 -- finger present, pulse "on" phase
 *   0x03 -- finger present, pulse "off" phase
 *   other -- no contact / not transmitting
 *
 * Beat detection: count transitions from PULSING (0x00) to RESTING
 * (0x03). A rolling window of 500 ms periods (8..16 entries) yields
 * BPM via average * 120 (60 s * 2 periods/s).
 *
 * Usage: call bio_tick(port) once per frame while the port shows a
 * Bio Sensor accessory; read bio_is_pulsing / bio_get_bpm for
 * display. Disconnect is auto-detected (NO_PAK from libdragon's
 * accessory I/O); call bio_reset(port) explicitly only when you
 * want to clear stats without an unplug.
 */
#ifndef N64_BIO_H
#define N64_BIO_H

#include <libdragon.h>
#include <stdbool.h>

void bio_tick(joypad_port_t port);
bool bio_is_pulsing(joypad_port_t port);
int  bio_get_bpm(joypad_port_t port);
void bio_reset(joypad_port_t port);

#endif
