# Joypad Tester — Nintendo 64 — Changelog

## v0.1.0 — unreleased

First release. Forks Christopher Bonhage (meeq)'s
[JoypadTest-N64](https://github.com/meeq/JoypadTest-N64) (public domain)
as the baseline N64 + GameCube controller + accessory matrix, and adds
the cross-console GBA-over-Joybus path (probe + Kawasedo-encrypted
multiboot upload) so a single ROM tests every Joybus-speaking device
that can land on an N64 controller port.

### Highlights

- Full meeq device matrix: standard N64 pad, GameCube pad (via
  passive 3-pin adapter), Rumble Pak, Controller Pak, Transfer Pak
  with Game Boy cartridge introspection, Bio Sensor, Snap Station,
  GB Camera.
- GBA Joybus probe: per-port `0xff` identify command, recognising
  the `0x04 0x00` reply as "GBA in JOYBUS mode" so users with a
  GameCube-GBA link cable + N64-port adapter can verify the
  GBA-side handshake without burning a separate test ROM.
- GBA multiboot uploader: ports the Kawasedo handshake + stream
  cipher + 0x14 post-boot poll from
  [`gcn/ppc/gba.c`](../gcn/ppc/gba.c) (libogc SI_Transfer flavor)
  onto LibDragon's `joybus_exec_command`. Embeds
  [`gba/build/tester/tester_mb.gba`](../gba/build/tester) as
  `gba_payload[]` and uploads on the user's request, putting the
  GBA into JOYBUS mode and then polling it as a fifth virtual
  joypad — same artifact the GameCube tester uses.

Full feature breakdown + build / loading instructions in
[`n64/README.md`](https://github.com/joypad-ai/joypad-tester/blob/main/n64/README.md).
