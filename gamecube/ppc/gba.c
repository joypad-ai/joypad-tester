// gba.c — GBA-via-link-cable multiboot + post-boot input polling.
//
// Ported from joypad-os/src/native/host/gc/gba_multiboot.c (RP2040 PIO
// joybus implementation) to libogc SI_Transfer. Same Kawasedo handshake
// + stream cipher, same payload, same post-boot 0x14 polling — only the
// transport primitives differ.
//
// Reference: AxioDL/jbus Endpoint.cpp + GBATEK SIO JOY BUS Mode.

#include "gba.h"

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <string.h>

extern const u8 gba_payload[];
extern const u32 gba_payload_len;

// JOYSTAT bits.
#define JSTAT_PSF1 0x20
#define JSTAT_PSF0 0x10
#define JSTAT_SEND 0x08
#define JSTAT_RECV 0x02
#define JSTAT_VALID_MASK 0xC5

// GBA device ID returned by the joybus probe (little-endian 0x00 0x04).
#define GBA_TYPE_ID 0x0400

// Kawasedo cipher constants.
#define MAGIC_SEDO 0x6f646573u
#define MAGIC_KAWA 0x6177614bu
#define MAGIC_BY   0x20796220u
#define CRC_POLY   0xa1c1u
#define CRC_SEED   0x15a0u

#define GBA_DELAY_US 70
#define GBA_READY_TIMEOUT_MS 1500
#define GBA_ECHO_ATTEMPTS 200  // 200 × 10ms = 2s, matches joypad-os's poll
#define GBA_ECHO_INTERVAL_MS 10

static u8 cmd_buf[8] ATTRIBUTE_ALIGN(32);
static u8 resp_buf[8] ATTRIBUTE_ALIGN(32);
static volatile u32 xfer_done_mask;

// Diagnostic: last echo value read while waiting for payload handshake.
static u32 last_echo[4];
static u8  last_echo_stat[4];
static u32 sample_echo[4][3];
static u8  sample_stat[4][3];

u32 GBA_LastEcho(int chan) { return last_echo[chan & 3]; }
u8  GBA_LastEchoStat(int chan) { return last_echo_stat[chan & 3]; }
void GBA_SnapEchoSamples(int chan, u32 echoes[3], u8 stats[3]) {
  for (int i = 0; i < 3; i++) {
    echoes[i] = sample_echo[chan & 3][i];
    stats[i]  = sample_stat[chan & 3][i];
  }
}

static void si_cb(s32 chan, u32 err) {
  (void)err;
  xfer_done_mask |= (1u << chan);
}

// One round-trip joybus transfer: send `out_len` bytes, receive `in_len`
// bytes. Returns true on success. ~5ms timeout matches the original.
static bool jb_xfer(int chan, const u8 *out, u32 out_len, u8 *in, u32 in_len) {
  if (out_len > sizeof(cmd_buf) || in_len > sizeof(resp_buf)) return false;
  memcpy(cmd_buf, out, out_len);
  memset(resp_buf, 0, in_len);
  u32 mask = (1u << chan);
  xfer_done_mask &= ~mask;
  if (!SI_Transfer(chan, cmd_buf, out_len, resp_buf, in_len, si_cb, 65))
    return false;
  // 50ms is generous — typical SI transfer is <1ms, but if PAD/N64
  // polling has work queued ahead of us we can wait several ms. 5ms was
  // tight enough to false-fail.
  u64 deadline = gettime() + millisecs_to_ticks(50);
  while (!(xfer_done_mask & mask)) {
    if (gettime() > deadline) return false;
  }
  memcpy(in, resp_buf, in_len);
  return true;
}

static bool jb_read4(int chan, u8 r5[5]) {
  u8 cmd = 0x14;
  return jb_xfer(chan, &cmd, 1, r5, 5);
}

static bool jb_write4(int chan, u32 word, u8 *jstat) {
  u8 cmd[5] = {0x15, (u8)word, (u8)(word >> 8), (u8)(word >> 16),
               (u8)(word >> 24)};
  u8 resp[1];
  if (!jb_xfer(chan, cmd, 5, resp, 1)) return false;
  if (jstat) *jstat = resp[0];
  return true;
}

static void busy_us(u32 us) {
  u64 deadline = gettime() + microsecs_to_ticks(us);
  while (gettime() < deadline) {}
}

static void busy_ms(u32 ms) { busy_us(ms * 1000); }

static bool gba_handshake(int chan, bool reset, u16 *out_type, u8 *out_jstat) {
  u8 cmd = reset ? 0xFF : 0x00;
  u8 r3[3];
  if (!jb_xfer(chan, &cmd, 1, r3, 3)) return false;
  *out_type = (u16)r3[0] | ((u16)r3[1] << 8);
  *out_jstat = r3[2];
  return true;
}

// Verbatim port of joypad-os's calculate_gc_key — the version that
// matches the Doridian-derived multiboot payload we ship. Uses the
// (rom_len - 0x200) >> 3 pre-shift; result is the value to send on the
// wire directly (no bswap).
static u32 calculate_gc_key(u32 rom_len) {
  u32 size = (rom_len - 0x200) >> 3;
  u32 res1 = (size & 0x3F80) << 1;
  res1 |= (size & 0x4000) << 2;
  res1 |= (size & 0x7F);
  res1 |= 0x380000;
  u32 res2 = res1 >> 8;
  res2 += res1 >> 16;
  res2 += res1;
  res2 <<= 24;
  res2 |= res1;
  res2 |= 0x80808080u;
  if ((res2 & 0x200) == 0) res2 ^= MAGIC_KAWA;
  else                     res2 ^= MAGIC_SEDO;
  return res2;
}

static u32 gba_crc_step(u32 crc, u32 value) {
  for (int i = 0; i < 32; i++) {
    if ((crc ^ value) & 1)
      crc = (crc >> 1) ^ CRC_POLY;
    else
      crc = (crc >> 1);
    value >>= 1;
  }
  return crc;
}

bool GBA_Detect(int chan) {
  u8 cmd = 0x00;
  u8 r[3];
  if (!jb_xfer(chan, &cmd, 1, r, 3)) return false;
  u16 id = (u16)r[0] | ((u16)r[1] << 8);
  return id == GBA_TYPE_ID;
}

int GBA_BootEmbedded(int chan) {
  if (gba_payload_len == 0 || !GBA_Detect(chan)) return -1;
  const u8 *rom = gba_payload;
  u32 len = gba_payload_len;
  if (len == 0 || len >= 0x40000) return -1;
  if (rom[0xac] == 0) return -1;

  u16 type;
  u8 js = 0;

  // Loop reset+status until the GBA reports ready (PSF0 set in jstat).
  // Matches FIX94/gba-link-cable-rom-sender's wait pattern; the BIOS may
  // not be ready on the first try.
  u64 ready_deadline = gettime() + millisecs_to_ticks(GBA_READY_TIMEOUT_MS);
  do {
    if (!gba_handshake(chan, true, &type, &js)) return -2;
    if (type != GBA_TYPE_ID) return -2;
    if (!gba_handshake(chan, false, &type, &js)) return -2;
    if (type != GBA_TYPE_ID) return -2;
    if (gettime() > ready_deadline) return -2;
    busy_us(GBA_DELAY_US);
  } while (!(js & JSTAT_PSF0));

  busy_us(GBA_DELAY_US);

  // Read session-key seed.
  u32 session_key = 0;
  for (int rt = 0; rt < 20; rt++) {
    u8 r5[5];
    if (!jb_read4(chan, r5)) return -3;
    if (r5[4] & JSTAT_VALID_MASK) return -3;
    session_key = (u32)r5[0] | ((u32)r5[1] << 8) | ((u32)r5[2] << 16) |
                  ((u32)r5[3] << 24);
    if (session_key != 0) break;
    busy_ms(5);
  }
  if (session_key == 0) return -3;
  busy_ms(2);
  session_key ^= MAGIC_SEDO;

  // Send our_key directly (joypad-os's formula already produces wire
  // bytes — no bswap).
  u32 our_key = calculate_gc_key(len);
  if (!jb_write4(chan, our_key, &js)) return -4;
  if (js & JSTAT_VALID_MASK) return -4;

  // Stream unencrypted header.
  for (u32 i = 0; i < 0xC0; i += 4) {
    u32 word = (u32)rom[i] | ((u32)rom[i + 1] << 8) |
               ((u32)rom[i + 2] << 16) | ((u32)rom[i + 3] << 24);
    busy_us(GBA_DELAY_US);
    if (!jb_write4(chan, word, &js)) return -5;
    if (js & JSTAT_VALID_MASK) return -5;
  }

  // Stream encrypted body + final CRC.
  u32 fcrc = CRC_SEED;
  u32 i;
  for (i = 0xC0; i < len; i += 4) {
    u32 plaintext = (u32)rom[i] | ((u32)rom[i + 1] << 8) |
                    ((u32)rom[i + 2] << 16) | ((u32)rom[i + 3] << 24);
    fcrc = gba_crc_step(fcrc, plaintext);
    session_key = (session_key * MAGIC_KAWA) + 1;
    u32 encrypted = plaintext ^ session_key;
    encrypted ^= ((~(i + (0x20u << 20))) + 1u);
    encrypted ^= MAGIC_BY;
    busy_us(GBA_DELAY_US);
    if (!jb_write4(chan, encrypted, &js)) return -6;
    if (js & JSTAT_VALID_MASK) return -6;
  }
  u32 final_word = (fcrc & 0xFFFF) | (len << 16);
  session_key = (session_key * MAGIC_KAWA) + 1;
  final_word ^= session_key;
  final_word ^= ((~(i + (0x20u << 20))) + 1u);
  final_word ^= MAGIC_BY;
  busy_us(GBA_DELAY_US);
  if (!jb_write4(chan, final_word, &js)) return -6;

  // Drain CRC reply.
  busy_us(GBA_DELAY_US);
  u8 r5[5];
  jb_read4(chan, r5);

  // Brief breather before polling — payload still needs a moment after
  // BIOS handoff to hit its first JOY_TRANS write.
  busy_ms(500);

  // Wait for payload's game-code echo.
  u32 expected = (u32)rom[0xAC] | ((u32)rom[0xAD] << 8) |
                 ((u32)rom[0xAE] << 16) | ((u32)rom[0xAF] << 24);
  last_echo[chan & 3] = 0;
  last_echo_stat[chan & 3] = 0;
  for (int s = 0; s < 3; s++) {
    sample_echo[chan & 3][s] = 0;
    sample_stat[chan & 3][s] = 0;
  }
  int sample_idx = 0;
  bool got_echo = false;
  for (int p = 0; p < GBA_ECHO_ATTEMPTS; p++) {
    if (jb_read4(chan, r5)) {
      u32 v = (u32)r5[0] | ((u32)r5[1] << 8) | ((u32)r5[2] << 16) |
              ((u32)r5[3] << 24);
      if (sample_idx < 3) {
        sample_echo[chan & 3][sample_idx] = v;
        sample_stat[chan & 3][sample_idx] = r5[4];
        sample_idx++;
      }
      if (v != 0) { last_echo[chan & 3] = v; last_echo_stat[chan & 3] = r5[4]; }
      if (v == expected) { got_echo = true; break; }
    }
    busy_ms(GBA_ECHO_INTERVAL_MS);
  }
  // Always send the handshake-complete write — this unblocks the GBA
  // payload's "wait for RECV" loop even if the echo poll above never
  // observed the game-code value. Matches joypad-os's "proceed anyway"
  // behavior. Treat the upload as successful either way; if the payload
  // isn't actually running, subsequent input polling will surface that.
  (void)got_echo;
  busy_us(GBA_DELAY_US);
  jb_write4(chan, expected, &js);
  return 0;
}

bool GBA_PollInput(int chan, u8 out[2]) {
  u8 r5[5];
  out[0] = 0;
  out[1] = 0;
  for (int attempt = 0; attempt < 8; attempt++) {
    if (!jb_read4(chan, r5)) continue;
    if (r5[4] & JSTAT_VALID_MASK) continue;
    if (r5[0] || r5[1] || r5[2] || r5[3]) {
      out[0] = r5[0];
      out[1] = r5[1];
      return true;
    }
  }
  return false;
}
