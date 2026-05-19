/*
 * gba.c — GBA-via-Joybus detect + multiboot upload for N64.
 *
 * Ported from gcn/ppc/gba.c (libogc SI_Transfer flavour) onto
 * LibDragon's blocking joybus_exec_cmd. Same Kawasedo handshake +
 * stream cipher, same embedded payload, same 0x14 post-boot polling.
 * Only the transport primitives differ.
 *
 * Reference: AxioDL/jbus Endpoint.cpp + GBATEK SIO JOY BUS Mode.
 *
 * Copyright (c) 2026 Robert Dale Smith
 * MIT License -- see ../LICENSE.md
 */

#include "gba.h"

#include <libdragon.h>
#include <string.h>

extern const uint8_t  gba_payload[];
extern const uint32_t gba_payload_len;

/* JOYSTAT bits (returned by writes / probes). */
#define JSTAT_PSF1       0x20
#define JSTAT_PSF0       0x10
#define JSTAT_SEND       0x08
#define JSTAT_RECV       0x02
#define JSTAT_VALID_MASK 0xC5

/* GBA device ID returned by the Joybus 0x00 probe. Little-endian
 * 0x00 0x04 on the wire, swizzled to 0x0400 in host order. */
#define GBA_TYPE_ID 0x0400

/* Kawasedo cipher constants -- the named magic numbers the GBA BIOS
 * mixes through the encrypted-body XOR chain. */
#define MAGIC_SEDO 0x6f646573u
#define MAGIC_KAWA 0x6177614bu
#define MAGIC_BY   0x20796220u
#define CRC_POLY   0xa1c1u
#define CRC_SEED   0x15a0u

#define GBA_DELAY_US           70
#define GBA_READY_TIMEOUT_MS   4000
#define GBA_ECHO_ATTEMPTS      200
#define GBA_ECHO_INTERVAL_MS   10

/* LibDragon's joybus_exec_cmd is a synchronous blocking call that
 * packs the cmd into a PIF block, sends it, and copies the reply
 * back. Wrapping it for parity with the libogc-flavour helper in
 * gcn/ppc/gba.c so the rest of the file reads identically. */
static bool jb_xfer(int port, const uint8_t *out, size_t out_len,
                    uint8_t *in, size_t in_len)
{
    joybus_exec_cmd(port, out_len, in_len, out, in);
    return true;
}

static bool jb_read4(int port, uint8_t r5[5])
{
    uint8_t cmd = 0x14;
    return jb_xfer(port, &cmd, 1, r5, 5);
}

static bool jb_write4(int port, uint32_t word, uint8_t *jstat)
{
    uint8_t cmd[5] = { 0x15,
        (uint8_t)word, (uint8_t)(word >> 8),
        (uint8_t)(word >> 16), (uint8_t)(word >> 24) };
    uint8_t resp[1] = {0};
    if (!jb_xfer(port, cmd, 5, resp, 1)) return false;
    if (jstat) *jstat = resp[0];
    return true;
}

static void busy_us(uint32_t us) { wait_ticks(TICKS_FROM_US(us)); }
static void busy_ms(uint32_t ms) { wait_ms(ms); }

static bool gba_handshake(int port, bool reset,
                          uint16_t *out_type, uint8_t *out_jstat)
{
    uint8_t cmd = reset ? 0xFF : 0x00;
    uint8_t r3[3] = {0};
    if (!jb_xfer(port, &cmd, 1, r3, 3)) return false;
    *out_type  = (uint16_t)r3[0] | ((uint16_t)r3[1] << 8);
    *out_jstat = r3[2];
    return true;
}

/* Verbatim port of the joypad-os gc_key calculation -- matches the
 * Doridian-derived multiboot payload we ship. The (rom_len - 0x200)
 * >> 3 pre-shift and the final SEDO/KAWA xor select are critical;
 * other variants of the algorithm floating around online use a
 * different shift and the GBA rejects them. */
static uint32_t calculate_gc_key(uint32_t rom_len)
{
    uint32_t size = (rom_len - 0x200) >> 3;
    uint32_t res1 = (size & 0x3F80) << 1;
    res1 |= (size & 0x4000) << 2;
    res1 |= (size & 0x7F);
    res1 |= 0x380000;
    uint32_t res2 = res1 >> 8;
    res2 += res1 >> 16;
    res2 += res1;
    res2 <<= 24;
    res2 |= res1;
    res2 |= 0x80808080u;
    if ((res2 & 0x200) == 0) res2 ^= MAGIC_KAWA;
    else                     res2 ^= MAGIC_SEDO;
    return res2;
}

static uint32_t gba_crc_step(uint32_t crc, uint32_t value)
{
    for (int i = 0; i < 32; i++) {
        if ((crc ^ value) & 1) crc = (crc >> 1) ^ CRC_POLY;
        else                   crc = (crc >> 1);
        value >>= 1;
    }
    return crc;
}

bool gba_detect(int port)
{
    /* Direct Joybus 0x00 probe (status / identify). Returns 3 bytes:
     * device-type[2] + jstat[1]. Wire pattern for a GBA in JOYBUS
     * mode is 0x00 0x04 (or 0x04 0x00 depending on which side of the
     * transport byte-swap you read it from). Compare on the raw
     * bytes so we're agnostic to libdragon vs libogc unpack order.
     *
     * Falling back to libdragon's cached identifier covers the rare
     * case where our probe came back zeroed (PIF transfer hiccup)
     * but the subsystem's last successful poll saw the cable. */
    uint8_t cmd = 0x00, r3[3] = {0};
    joybus_exec_cmd(port, 1, 3, &cmd, r3);
    if (r3[0] == 0x00 && r3[1] == 0x04) return true;
    if (r3[0] == 0x04 && r3[1] == 0x00) return true;
    return joypad_get_identifier(port) == JOYBUS_IDENTIFIER_GBA_LINK_CABLE;
}

int gba_boot_embedded(int port)
{
    /* Don't re-check gba_detect here -- the tester gates the boot
     * trigger on its own sticky-detect cache (1s TTL), and libdragon's
     * cached identifier flickers off briefly even when a GBA cable is
     * physically connected. If the cable is actually gone, the
     * handshake-status poll below will time out cleanly with -2. */
    /* Split each pre-flight check into its own error code so a failure
     * here is unambiguously diagnosable from the on-screen err number
     * (-10/-11/-12) instead of merging into a generic -1. */
    if (gba_payload_len == 0) return -10;
    const uint8_t *rom = gba_payload;
    uint32_t       len = gba_payload_len;
    if (len >= 0x40000)       return -11;
    if (rom[0xac] == 0)       return -12;

    uint16_t type;
    uint8_t  js = 0;

    /* One reset, then poll status until PSF0 latches. Cmd 0xFF
     * triggers a SystemCall(0x26) hard-reset on the GBA side when
     * the GBA is already running a multibooted payload that polls
     * REG_JOYCNTRL.RST -- so a second reset would restart the cycle
     * and we'd never see PSF0. Send it once, then status-poll. */
    (void)gba_handshake(port, true, &type, &js);

    uint64_t ready_deadline = get_ticks_ms() + GBA_READY_TIMEOUT_MS;
    bool ready = false;
    while (!ready) {
        if (get_ticks_ms() > ready_deadline) return -2;
        if (gba_handshake(port, false, &type, &js)
            && type == GBA_TYPE_ID
            && (js & JSTAT_PSF0))
            ready = true;
        else
            busy_us(GBA_DELAY_US);
    }

    busy_us(GBA_DELAY_US);

    /* Read session-key seed -- the GBA BIOS generates a random
     * 32-bit value and clocks it out across four jb_read4 cycles.
     * We XOR with SEDO to recover the cipher state. */
    uint32_t session_key = 0;
    for (int rt = 0; rt < 20; rt++) {
        uint8_t r5[5];
        if (!jb_read4(port, r5)) return -3;
        if (r5[4] & JSTAT_VALID_MASK) return -3;
        session_key = (uint32_t)r5[0] | ((uint32_t)r5[1] << 8) |
                      ((uint32_t)r5[2] << 16) | ((uint32_t)r5[3] << 24);
        if (session_key != 0) break;
        busy_ms(5);
    }
    if (session_key == 0) return -3;
    busy_ms(2);
    session_key ^= MAGIC_SEDO;

    /* Send our_key directly -- the joypad-os formula already yields
     * the wire-order word, no host->wire byte swap needed. */
    uint32_t our_key = calculate_gc_key(len);
    if (!jb_write4(port, our_key, &js)) return -4;
    if (js & JSTAT_VALID_MASK)          return -4;

    /* Stream the unencrypted header (0..0xBF). */
    for (uint32_t i = 0; i < 0xC0; i += 4) {
        uint32_t word = (uint32_t)rom[i] | ((uint32_t)rom[i + 1] << 8) |
                        ((uint32_t)rom[i + 2] << 16) | ((uint32_t)rom[i + 3] << 24);
        busy_us(GBA_DELAY_US);
        if (!jb_write4(port, word, &js)) return -5;
        if (js & JSTAT_VALID_MASK)       return -5;
    }

    /* Stream the encrypted body + the final CRC word. */
    uint32_t fcrc = CRC_SEED;
    uint32_t i;
    for (i = 0xC0; i < len; i += 4) {
        uint32_t plaintext = (uint32_t)rom[i] | ((uint32_t)rom[i + 1] << 8) |
                             ((uint32_t)rom[i + 2] << 16) | ((uint32_t)rom[i + 3] << 24);
        fcrc = gba_crc_step(fcrc, plaintext);
        session_key = (session_key * MAGIC_KAWA) + 1;
        uint32_t encrypted = plaintext ^ session_key;
        encrypted ^= ((~(i + (0x20u << 20))) + 1u);
        encrypted ^= MAGIC_BY;
        busy_us(GBA_DELAY_US);
        if (!jb_write4(port, encrypted, &js)) return -6;
        if (js & JSTAT_VALID_MASK)            return -6;
    }
    uint32_t final_word = (fcrc & 0xFFFF) | (len << 16);
    session_key = (session_key * MAGIC_KAWA) + 1;
    final_word ^= session_key;
    final_word ^= ((~(i + (0x20u << 20))) + 1u);
    final_word ^= MAGIC_BY;
    busy_us(GBA_DELAY_US);
    if (!jb_write4(port, final_word, &js)) return -6;

    /* Drain CRC reply. */
    busy_us(GBA_DELAY_US);
    uint8_t r5[5];
    jb_read4(port, r5);

    /* Brief breather before polling -- payload needs a moment after
     * BIOS handoff to hit its first JOY_TRANS write. */
    busy_ms(500);

    /* Wait for the payload's game-code echo. */
    uint32_t expected = (uint32_t)rom[0xAC] | ((uint32_t)rom[0xAD] << 8) |
                        ((uint32_t)rom[0xAE] << 16) | ((uint32_t)rom[0xAF] << 24);
    for (int p = 0; p < GBA_ECHO_ATTEMPTS; p++) {
        if (jb_read4(port, r5)) {
            uint32_t v = (uint32_t)r5[0] | ((uint32_t)r5[1] << 8) |
                         ((uint32_t)r5[2] << 16) | ((uint32_t)r5[3] << 24);
            if (v == expected) break;
        }
        busy_ms(GBA_ECHO_INTERVAL_MS);
    }
    /* Always send the handshake-complete write -- unblocks the GBA
     * payload's "wait for RECV" loop even if our echo poll above
     * never observed the game-code value. Matches the joypad-os
     * "proceed anyway" behaviour. */
    busy_us(GBA_DELAY_US);
    jb_write4(port, expected, &js);
    return 0;
}

bool gba_poll_input(int port, uint8_t out[2])
{
    uint8_t r5[5];
    out[0] = 0;
    out[1] = 0;
    for (int attempt = 0; attempt < 8; attempt++) {
        if (!jb_read4(port, r5)) continue;
        if (r5[4] & JSTAT_VALID_MASK) continue;
        if (r5[0] || r5[1] || r5[2] || r5[3]) {
            out[0] = r5[0];
            out[1] = r5[1];
            return true;
        }
    }
    return false;
}
