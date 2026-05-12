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

// After multiboot, read the GBA payload's input report.
// Fills `out[2]` with [keys_lo, keys_hi]:
//   out[0] bits 0..7 = A, B, Select, Start, Right, Left, Up, Down
//   out[1] bits 0..1 = R, L
// 0 bit = pressed (GBA REG_KEYINPUT convention).
// Returns true if a fresh non-zero-state read succeeded.
bool GBA_PollInput(int chan, u8 out[2]);

#endif
