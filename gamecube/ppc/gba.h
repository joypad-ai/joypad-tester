#ifndef GBA_H
#define GBA_H

#include <gccore.h>
#include <stdbool.h>

// Detect a GBA in joybus multiboot mode (probe returns type 0x0400).
bool GBA_Detect(int chan);

// Multiboot the embedded payload to a detected GBA. Returns 0 on
// success or a negative error code indicating which step failed:
//   -1 detect, -2 ready timeout, -3 session key, -4 our_key send,
//   -5 header send, -6 body send, -7 echo timeout.
// Blocks for several seconds during the upload — only call once per
// detection event.
int GBA_BootEmbedded(int chan);

// Diagnostic accessors — last value seen when waiting for the post-boot
// game-code echo. Useful for distinguishing "BIOS rejected upload" (=
// last encrypted word residue) from "payload running but unexpected
// echo" (= 0x30303030 with byte-shift, or arbitrary garbage).
u32 GBA_LastEcho(int chan);
u8  GBA_LastEchoStat(int chan);
// Capture of first 3 echo samples (post-multiboot reads), to spot
// patterns. echoes[i] = value, stats[i] = JOYSTAT byte.
void GBA_SnapEchoSamples(int chan, u32 echoes[3], u8 stats[3]);

// After multiboot, read the GBA payload's input report.
// Fills `out[2]` with [keys_lo, keys_hi]:
//   out[0] bits 0..7 = A, B, Select, Start, Right, Left, Up, Down
//   out[1] bits 0..1 = R, L
// 0 bit = pressed (GBA REG_KEYINPUT convention).
// Returns true if a fresh non-zero-state read succeeded.
bool GBA_PollInput(int chan, u8 out[2]);

#endif
