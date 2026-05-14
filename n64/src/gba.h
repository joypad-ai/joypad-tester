/*
 * gba.h — GBA-over-Joybus detect + multiboot upload + input polling
 * for the N64 build. Same Kawasedo handshake algorithm as
 * gcn/ppc/gba.h, retargeted onto LibDragon's joybus_exec_cmd
 * instead of libogc's SI_Transfer.
 *
 * Copyright (c) 2026 Robert Dale Smith
 * MIT License -- see ../LICENSE.md
 */
#ifndef N64_GBA_H
#define N64_GBA_H

#include <stdbool.h>
#include <stdint.h>

/* Returns true when the device on `port` (0..3) responds to a Joybus
 * 0x00 identify with the GBA-in-JOYBUS-mode ID (0x0400). */
bool gba_detect(int port);

/* Send the embedded GBA payload (linked in via gba_payload.c) over
 * Joybus multiboot. Returns 0 on success or a negative error code:
 *   -1 no GBA detected on this port
 *   -2 GBA never reached PSF0-ready after reset
 *   -3 session-key seed never arrived
 *   -4 our_key write rejected
 *   -5 header chunk rejected
 *   -6 encrypted body chunk / final CRC rejected
 * Takes ~1-2s on success (most spent streaming payload bytes). */
int gba_boot_embedded(int port);

/* After multiboot, the payload sits in JOYBUS mode polling for
 * input. This is a single 4-byte read (cmd 0x14) returning the
 * payload's view of its own joypad state -- two bytes that mean
 * whatever the payload defines, with the high byte usually carrying
 * the GBA's KEYINPUT register and the low byte a frame counter or
 * status. Returns true if the read landed; out[0..1] hold the bytes. */
bool gba_poll_input(int port, uint8_t out[2]);

#endif
