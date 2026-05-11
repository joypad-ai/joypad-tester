#include "n64.h"

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <string.h>

#define SI_CMD_RESET 0xFF
#define SI_CMD_POLL 0x01
#define SI_CMD_READ 0x02
#define SI_CMD_WRITE 0x03
// GameCube keyboard polling. Like the standard GCN cmd 0x40, this is a
// 3-byte command [cmd, mode, rumble] — sending only the cmd byte returns
// stale data. Reference: joypad-os src/lib/joybus-pio (GamecubeConsole.c).
#define SI_CMD_KBD_POLL 0x54
#define N64_TYPE_CONTROLLER 0x05000000
#define N64_TYPE_MOUSE      0x02000000
#define N64_TYPE_MIC        0x00010000
#define N64_TYPE_GBA        0x00040000

#define PAK_ADDR_LABEL  0x0000
#define PAK_ADDR_PROBE  0x8000
#define PAK_ADDR_RUMBLE 0xC000
#define PAK_ADDR_BIO    0xC000  // same register, different accessory's view

#define BIO_PERIOD_MS         500
#define BIO_WINDOW_MIN        8     // samples needed before reporting BPM
#define BIO_WINDOW_MAX        16
#define BIO_PERIODS_PER_MIN   120   // 60s × 2 periods/sec

#define PAK_PROBE_RUMBLE        0x80
#define PAK_PROBE_BIO_SENSOR    0x81
#define PAK_PROBE_TRANSFER_ON   0x84
#define PAK_PROBE_SNAP_STATION  0x85
#define PAK_PROBE_TRANSFER_OFF  0xFE

static n64_kind_t chan_kind[SI_MAX_CHAN];
static int fail_count[SI_MAX_CHAN];
static n64_pak_t pak_cache[SI_MAX_CHAN];
static u64 pak_probe_time[SI_MAX_CHAN];
static bool rumble_state[SI_MAX_CHAN];

// Bio Sensor heart-rate tracking, per channel.
static struct {
  bool pulsing;
  u64 period_start;
  u32 beats_in_period;
  u32 period_count;        // periods accumulated since detection
  u32 history[BIO_WINDOW_MAX];
  u32 history_idx;         // ring index, 0..MAX-1
} bio[SI_MAX_CHAN];

// Per-channel SI buffers — separate per channel because SI_Transfer may queue
// the buffer pointer for later (interrupt-driven) consumption.
static u8 cmd_poll_buf[SI_MAX_CHAN] ATTRIBUTE_ALIGN(32);
static u8 cmd_reset_buf[SI_MAX_CHAN] ATTRIBUTE_ALIGN(32);
static u8 cmd_kbd_buf[SI_MAX_CHAN][3] ATTRIBUTE_ALIGN(32);
static u8 reset_resp[SI_MAX_CHAN][3] ATTRIBUTE_ALIGN(32);
static u8 kbd_resp[SI_MAX_CHAN][8] ATTRIBUTE_ALIGN(32);
static u8 cmd_pak[SI_MAX_CHAN][35] ATTRIBUTE_ALIGN(32);
static u8 poll_resp[SI_MAX_CHAN][4] ATTRIBUTE_ALIGN(32);
static u8 read_resp[SI_MAX_CHAN][33] ATTRIBUTE_ALIGN(32);
static u8 write_resp[SI_MAX_CHAN][1] ATTRIBUTE_ALIGN(32);
static volatile u32 xfer_done_mask;

static void si_xfer_cb(s32 chan, u32 err) {
  xfer_done_mask |= (1u << chan);
}

static bool wait_xfer(s32 chan, u32 timeout_ms) {
  u32 mask = (1u << chan);
  u64 deadline = gettime() + millisecs_to_ticks(timeout_ms);
  while (!(xfer_done_mask & mask)) {
    if (gettime() > deadline) return false;
  }
  return true;
}

// 5-bit address checksum (libdragon-equivalent).
static u16 addr_checksum(u16 addr) {
  static const u16 xor_table[16] = {
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x15, 0x1F, 0x0B,
      0x16, 0x19, 0x07, 0x0E,
      0x1C, 0x0D, 0x1A, 0x01,
  };
  u16 checksum = 0;
  addr &= ~0x1F;
  for (int i = 15; i >= 5; i--)
    if ((addr >> i) & 0x1) checksum ^= xor_table[i];
  checksum &= 0x1F;
  return addr | checksum;
}

// 8-bit data checksum over the 32-byte payload (libdragon-equivalent).
// Reserved for future CRC verification on accessory writes.
__attribute__((unused))
static u8 data_checksum(const u8 *data) {
  u8 ret = 0;
  for (int i = 0; i <= 32; i++) {
    for (int j = 7; j >= 0; j--) {
      int tmp = 0;
      if (ret & 0x80) tmp = 0x85;
      ret <<= 1;
      if (i < 32 && data[i] & (0x01 << j)) ret |= 0x1;
      ret ^= tmp;
    }
  }
  return ret;
}

static bool si_poll(s32 chan) {
  cmd_poll_buf[chan] = SI_CMD_POLL;
  u32 mask = (1u << chan);
  xfer_done_mask &= ~mask;
  if (!SI_Transfer(chan, &cmd_poll_buf[chan], 1, poll_resp[chan], 4,
                   si_xfer_cb, 65))
    return false;
  return wait_xfer(chan, 50);
}

// Read 32 bytes from the controller-pak's expansion slot. Returns the SI
// transfer success only — caller interprets the data and CRC byte (read_resp
// last byte) since libdragon's accessory protocol overloads the CRC: matching
// = pak present, XOR'd with 0xFF = no pak, anything else = mismatch.
static bool pak_read(s32 chan, u16 addr, u8 *data, u8 *crc_byte) {
  u16 ac = addr_checksum(addr);
  cmd_pak[chan][0] = SI_CMD_READ;
  cmd_pak[chan][1] = (ac >> 8) & 0xFF;
  cmd_pak[chan][2] = ac & 0xFF;
  u32 mask = (1u << chan);
  xfer_done_mask &= ~mask;
  if (!SI_Transfer(chan, cmd_pak[chan], 3, read_resp[chan], 33, si_xfer_cb, 65))
    return false;
  if (!wait_xfer(chan, 50)) return false;
  memcpy(data, read_resp[chan], 32);
  if (crc_byte) *crc_byte = read_resp[chan][32];
  return true;
}

static bool pak_write(s32 chan, u16 addr, const u8 *data, u8 *crc_byte) {
  u16 ac = addr_checksum(addr);
  cmd_pak[chan][0] = SI_CMD_WRITE;
  cmd_pak[chan][1] = (ac >> 8) & 0xFF;
  cmd_pak[chan][2] = ac & 0xFF;
  memcpy(&cmd_pak[chan][3], data, 32);
  u32 mask = (1u << chan);
  xfer_done_mask &= ~mask;
  if (!SI_Transfer(chan, cmd_pak[chan], 35, write_resp[chan], 1, si_xfer_cb,
                   65))
    return false;
  if (!wait_xfer(chan, 50)) return false;
  if (crc_byte) *crc_byte = write_resp[chan][0];
  return true;
}

// Send a controller reset (cmd 0xFF). N64 controllers don't reliably re-scan
// the expansion-slot pak-detect line on hot-swap — a reset forces them to
// re-evaluate, which is what makes hot-replug detection work at all. Side
// effect: stick-origin calibration is cleared until the next idle frame.
static void controller_reset(s32 chan) {
  cmd_reset_buf[chan] = SI_CMD_RESET;
  u32 mask = (1u << chan);
  xfer_done_mask &= ~mask;
  if (SI_Transfer(chan, &cmd_reset_buf[chan], 1, reset_resp[chan], 3,
                  si_xfer_cb, 65))
    wait_xfer(chan, 50);
}

// Multi-step accessory detection mirroring libdragon's joypad_accessory.c.
// We try every step independently rather than bailing on an early CRC that
// looks like "no pak" — a freshly hot-plugged accessory often reports the
// "no pak" CRC for one or two transfers before the controller refreshes its
// expansion-slot state, even though the real pak is present and answers later
// commands. Each probe step is its own positive identification.
static n64_pak_t probe_pak(s32 chan) {
  u8 data[32];
  u8 readback[32];

  // Step 0: If we're currently in "no pak detected" mode, send a controller
  // reset to force re-evaluation of the expansion-slot pak-detect line.
  // Skipped while a pak is already detected so we don't keep clobbering the
  // stick origin during normal use.
  if (pak_cache[chan] == N64_PAK_NONE) controller_reset(chan);

  // Step 1: Reset accessory state (turn off transfer pak in case it was on).
  // Don't gate on the CRC — a flaky CRC here doesn't mean there's no pak.
  memset(data, PAK_PROBE_TRANSFER_OFF, sizeof(data));
  pak_write(chan, PAK_ADDR_PROBE, data, NULL);

  // Step 2: Memory Pak label probe.
  for (size_t i = 0; i < sizeof(data); i++) data[i] = i;
  if (pak_write(chan, PAK_ADDR_LABEL, data, NULL) &&
      pak_read(chan, PAK_ADDR_LABEL, readback, NULL) &&
      memcmp(data, readback, sizeof(data)) == 0) {
    return N64_PAK_MEMORY;
  }

  // Step 3: Rumble / Bio Sensor probe.
  memset(data, PAK_PROBE_RUMBLE, sizeof(data));
  if (pak_write(chan, PAK_ADDR_PROBE, data, NULL) &&
      pak_read(chan, PAK_ADDR_PROBE, readback, NULL)) {
    if (readback[0] == PAK_PROBE_RUMBLE) return N64_PAK_RUMBLE;
    if (readback[0] == PAK_PROBE_BIO_SENSOR) return N64_PAK_BIO_SENSOR;
  }

  // Step 4: Transfer Pak probe. Power it off again afterward so the GB cart
  // doesn't keep drawing power.
  memset(data, PAK_PROBE_TRANSFER_ON, sizeof(data));
  if (pak_write(chan, PAK_ADDR_PROBE, data, NULL) &&
      pak_read(chan, PAK_ADDR_PROBE, readback, NULL) &&
      readback[0] == PAK_PROBE_TRANSFER_ON) {
    memset(data, PAK_PROBE_TRANSFER_OFF, sizeof(data));
    pak_write(chan, PAK_ADDR_PROBE, data, NULL);
    return N64_PAK_TRANSFER;
  }

  // Step 5: Snap Station probe (Pokémon Snap photo printer dock).
  memset(data, PAK_PROBE_SNAP_STATION, sizeof(data));
  if (pak_write(chan, PAK_ADDR_PROBE, data, NULL) &&
      pak_read(chan, PAK_ADDR_PROBE, readback, NULL) &&
      readback[0] == PAK_PROBE_SNAP_STATION) {
    return N64_PAK_SNAP_STATION;
  }

  return N64_PAK_NONE;
}

static void bio_reset(int chan) {
  memset((void *)&bio[chan], 0, sizeof(bio[chan]));
}

static void bio_update(int chan, u8 sensor_byte) {
  bool now_pulsing = (sensor_byte == 0x00);
  bool was_pulsing = bio[chan].pulsing;
  bio[chan].pulsing = now_pulsing;

  // Beat is detected on the falling edge (pulsing → resting).
  if (was_pulsing && !now_pulsing) bio[chan].beats_in_period++;

  u64 now = gettime();
  if (bio[chan].period_start == 0) bio[chan].period_start = now;
  if (diff_msec(bio[chan].period_start, now) >= BIO_PERIOD_MS) {
    bio[chan].history[bio[chan].history_idx % BIO_WINDOW_MAX] =
        bio[chan].beats_in_period;
    bio[chan].history_idx++;
    bio[chan].beats_in_period = 0;
    bio[chan].period_count++;
    bio[chan].period_start = now;
  }
}

int N64_GetBioBPM(int chan) {
  u32 n = bio[chan].period_count;
  if (n < BIO_WINDOW_MIN) return 0;
  if (n > BIO_WINDOW_MAX) n = BIO_WINDOW_MAX;
  u32 sum = 0;
  for (u32 i = 0; i < n; i++) sum += bio[chan].history[i];
  return (int)((sum * BIO_PERIODS_PER_MIN) / n);
}

bool N64_GetBioPulsing(int chan) { return bio[chan].pulsing; }

void N64_SetRumble(int chan, bool on) {
  if (chan_kind[chan] != N64_KIND_CONTROLLER ||
      pak_cache[chan] != N64_PAK_RUMBLE)
    return;
  if (rumble_state[chan] == on) return;
  u8 buf[32];
  memset(buf, on ? 0x01 : 0x00, sizeof(buf));
  if (pak_write(chan, PAK_ADDR_RUMBLE, buf, NULL)) rumble_state[chan] = on;
}

static n64_kind_t kind_for_type(u32 raw) {
  u32 hi = raw & ~0xffff;
  if (hi == N64_TYPE_CONTROLLER) return N64_KIND_CONTROLLER;
  if (hi == N64_TYPE_MOUSE)      return N64_KIND_MOUSE;
  if (hi == N64_TYPE_MIC)        return N64_KIND_MIC;
  if (hi == N64_TYPE_GBA)        return N64_KIND_GBA;
  return N64_KIND_NONE;
}

void N64_Scan(N64State *state) {
  for (s32 c = 0; c < SI_MAX_CHAN; c++) {
    n64_kind_t fresh = kind_for_type(SI_GetType(c));
    if (fresh != N64_KIND_NONE) chan_kind[c] = fresh;
    // GBA detection is non-sticky: the multiboot path needs the cache to
    // clear immediately on failure/disconnect so a re-attempt on the
    // same port works without rebooting the host. (Controllers / mice
    // need stickiness because libogc's SI_GetType bounces between cached
    // type and NORESP; GBA doesn't have that problem.)
    if (chan_kind[c] == N64_KIND_GBA && fresh != N64_KIND_GBA) {
      chan_kind[c] = N64_KIND_NONE;
    }
    n64_kind_t kind = chan_kind[c];

    if (kind == N64_KIND_NONE) {
      state[c].present = false;
      state[c].kind = N64_KIND_NONE;
      state[c].pak = N64_PAK_NONE;
      state[c].rumble_active = false;
      pak_cache[c] = N64_PAK_NONE;
      pak_probe_time[c] = 0;
      rumble_state[c] = false;
      continue;
    }

    // Only controllers + mice answer cmd 0x01 with a 4-byte input report.
    // Mics and other peripherals use different protocols; for them we
    // surface "present = true" + the kind label, with all input fields
    // zero (TODO: per-kind polling).
    bool poll_ok = false;
    if (kind == N64_KIND_CONTROLLER || kind == N64_KIND_MOUSE) {
      poll_ok = si_poll(c);
    }
    if (poll_ok) {
      state[c].present = true;
      state[c].kind = kind;
      state[c].buttons = (poll_resp[c][0] << 8) | poll_resp[c][1];
      state[c].stick_x = (s8)poll_resp[c][2];
      state[c].stick_y = (s8)poll_resp[c][3];
      fail_count[c] = 0;
    } else if (kind == N64_KIND_CONTROLLER || kind == N64_KIND_MOUSE) {
      // Polling failed for a kind that should poll. Bail after a few
      // consecutive failures.
      if (++fail_count[c] >= 60) {
        chan_kind[c] = N64_KIND_NONE;
        state[c].present = false;
        state[c].kind = N64_KIND_NONE;
        fail_count[c] = 0;
        continue;
      }
    } else {
      // Kinds we don't poll yet (e.g. mic): mark present, no inputs.
      state[c].present = true;
      state[c].kind = kind;
      state[c].buttons = 0;
      state[c].stick_x = 0;
      state[c].stick_y = 0;
    }

    // Pak slot is only on N64 controllers (mouse / mic / keyboard / GBA
    // have no expansion bay).
    if (kind != N64_KIND_CONTROLLER) {
      state[c].pak = N64_PAK_NONE;
      state[c].rumble_active = false;
      continue;
    }

    // Re-probe accessory periodically. Run faster (every ~250ms) so a hot
    // unplug/replug of the pak is reflected quickly in the UI.
    u64 now = gettime();
    if (pak_probe_time[c] == 0 ||
        diff_msec(pak_probe_time[c], now) > 250) {
      n64_pak_t prev = pak_cache[c];
      pak_cache[c] = probe_pak(c);
      pak_probe_time[c] = now;
      if (pak_cache[c] != N64_PAK_RUMBLE) rumble_state[c] = false;
      // Reset bio-sensor state on transitions in/out of bio sensor mode.
      if (prev != pak_cache[c] && (prev == N64_PAK_BIO_SENSOR ||
                                   pak_cache[c] == N64_PAK_BIO_SENSOR)) {
        bio_reset(c);
      }
    }

    // If bio sensor is active, sample the pulse register every frame.
    if (pak_cache[c] == N64_PAK_BIO_SENSOR) {
      u8 buf[32];
      if (pak_read(c, PAK_ADDR_BIO, buf, NULL)) bio_update(c, buf[0]);
    }

    state[c].pak = pak_cache[c];
    state[c].rumble_active = rumble_state[c];
  }
}

// Poll a GameCube ASCII keyboard. SI cmd 0x54 returns 8 bytes; per
// reverse-engineered docs (mostly from PSO), the 3 simultaneous keycodes
// are at bytes 4..6 and the last byte is a checksum. The exact byte
// layout varies across references — the only way to verify is to test
// on hardware and watch which bytes change as keys are pressed. The
// scancodes themselves are GC-specific (not USB HID) and need a lookup
// table to render; for now we surface the raw bytes.
bool GCKeyboard_Poll(int chan, u8 raw[8]) {
  // 3-byte command: [0x54 cmd, mode, rumble]. Mode 3 is the default
  // GC reading mode; rumble flag is always 0 for keyboards.
  cmd_kbd_buf[chan][0] = SI_CMD_KBD_POLL;
  cmd_kbd_buf[chan][1] = 0x03;
  cmd_kbd_buf[chan][2] = 0x00;
  u32 mask = (1u << chan);
  xfer_done_mask &= ~mask;
  memset(kbd_resp[chan], 0, 8);
  if (!SI_Transfer(chan, cmd_kbd_buf[chan], 3, kbd_resp[chan], 8, si_xfer_cb,
                   65))
    return false;
  if (!wait_xfer(chan, 50)) return false;
  memcpy(raw, kbd_resp[chan], 8);
  return true;
}
