# Joypad Tester — GBA — Changelog

## v0.1.0 — 2026-05-11

Initial release. Two multiboot ROM variants built from one source tree.

### Eyes (`joypad_mb.gba`)
- Doridian-style joybus controller + animated cartoon eyes overlay
  rendered to a Mode-4 framebuffer.
- Per-emotion FG/pupil palette, dpad-as-self-centering-analog gaze,
  ACTIVE → wander → sleep cycle.
- Intended for embedding into [joypad-os](https://github.com/joypad-ai/joypad-os)
  as the GBA-as-controller payload (RP2040 host).

### Tester (`tester_mb.gba`)
- Doridian-style joybus controller + on-GBA text console with live
  button state in a 2-column layout.
- Mode-4 page-flipped idle screensaver: 64×54 logo bounces with 7-colour
  cycle (red/green/yellow/blue/magenta/cyan/white) on every wall hit,
  matching the joypad-tester GameCube variant.
- Intended for embedding into joypad-tester's GameCube/Wii host (so
  testers see visual feedback on the GBA itself) and for standalone use
  via flash cart as a pure button tester.

### Shared
- Doridian joybus handshake taken verbatim from Joybus-PIO; modifying it
  breaks the cable's level-shifter MCU.
- Robust joybus reset handling: `REG_JOYCNT.RST` polled inside the
  VBlank busy-wait so a host `cmd 0xFF` triggers `SystemCall(0x26)`
  within microseconds — enables clean hot-swap and host-reboot
  re-multiboot.
- `bin2c.py` emits `joypad_payload[]` / `joypad_payload_len` from
  either variant, so the host's externs don't change when switching
  ROMs.
