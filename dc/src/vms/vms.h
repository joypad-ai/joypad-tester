/*
 * vms.h — ICONDATA_VMS encode/decode + generic save icon extract.
 *
 * Two VMS file shapes are relevant:
 *
 *   ICONDATA_VMS (the "magic" filename the BIOS recognizes):
 *     0x00..0x0F  description (16 bytes, Shift-JIS, ASCII-safe in v1)
 *     0x10..0x1F  fixed/header bytes; byte 0x10 = 0x20, byte 0x14
 *                 = 0xA0 when a color icon is embedded (else 0x00).
 *     0x20..0x9F  mono bitmap (128 bytes, 32x32 @ 1bpp, MSB-first)
 *     0xA0..0xBF  color palette (32 bytes, 16 x ARGB1555 packed g/b in
 *                 byte 0 of each entry, a/r in byte 1; 4 bits/channel)
 *     0xC0..0x2BF color bitmap (512 bytes, 32x32 @ 4bpp; 2 px/byte,
 *                 high nibble = even col)
 *     0x2C0..0x2CF optional 3D-mode unlock sequence
 *     0x2D0..0x3FF zero padding -- total 1024 bytes (= 2 VMU blocks)
 *
 *   Generic game-save VMS (every other save the BIOS shows):
 *     0x00..0x0F  description (16 bytes Shift-JIS)
 *     0x10..0x2F  boot ROM description (32 bytes)
 *     0x30..0x3F  application identifier (16 bytes ASCII)
 *     0x40..0x41  icon count (uint16 LE, 1..3 -> animation frames)
 *     0x42..0x43  animation speed (uint16 LE)
 *     0x44..0x45  eyecatch type (uint16 LE, 0..3)
 *     0x46..0x47  CRC (uint16 LE)
 *     0x48..0x4B  data size (uint32 LE)
 *     0x4C..0x5F  reserved
 *     0x60..0x7F  icon palette (16 x ARGB1555)
 *     0x80..      bitmap frames (32x32 @ 4bpp, 512 bytes each)
 *     ...         optional eyecatch then game data
 */
#ifndef JT_VMS_H
#define JT_VMS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define JT_VMS_ICONDATA_SIZE  1024   /* exactly 2 VMU blocks */
#define JT_PALETTE_ENTRIES    16
#define JT_CANVAS_W           32
#define JT_CANVAS_H           32

/* Internal canvas representation. The editor mode owns one of these;
 * vms.c knows how to translate to/from the on-disc 1024-byte blob. */
typedef struct {
    uint16_t palette[JT_PALETTE_ENTRIES];        /* ARGB1555 per slot */
    uint8_t  color_indices[JT_CANVAS_W * JT_CANVAS_H]; /* 0..15 */
    uint8_t  mono_bits[JT_CANVAS_W * JT_CANVAS_H / 8]; /* 1bpp, MSB-first */
    bool     has_color_icon;
    bool     real_mode_flag;   /* writes the 3D-mode sequence to 0x2C0 */
    char     description[16];  /* ASCII, NUL-padded */
} jt_icon_t;

/* Encode/decode ICONDATA_VMS. Returns true on success.
 * encode: writes exactly JT_VMS_ICONDATA_SIZE bytes to `out`.
 * decode: reads up to `size` bytes from `in` (must be >= 0x2C0 to be
 *         valid). Tolerant of mono-only icons (no color section). */
bool jt_vms_encode_icondata(const jt_icon_t *icon, uint8_t *out);
bool jt_vms_decode_icondata(const uint8_t *in, size_t size, jt_icon_t *icon);

/* Extract icon from a generic save's VMS header at `in` (size >= 0x80
 * + frame_count*512). Reads the palette at 0x60 and the `frame` th
 * bitmap starting at 0x80. Output goes into `icon`'s palette + color
 * fields; mono_bits is generated from luminance threshold so the
 * editor can preview both representations. */
bool jt_vms_extract_save_icon(const uint8_t *in, size_t size,
                              unsigned frame, jt_icon_t *icon);

/* Read the save header's animated icon count (1..3) from offset 0x40.
 * Returns 0 if the input is too small or the count is invalid. */
unsigned jt_vms_save_icon_count(const uint8_t *in, size_t size);

/* VMS CRC algorithm (16-bit, used by game saves). Not needed for
 * ICONDATA_VMS but exposed for completeness when the library save
 * format wants to checksum its payload. */
uint16_t jt_vms_crc(const uint8_t *data, size_t size);

/* Helpers for converting between the ARGB1555-packed byte pair the
 * on-disc palette uses and the uint16_t our canvas keeps in memory.
 * Useful when building palette editors or eyecatch decoders later. */
uint16_t jt_palette_pack(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void     jt_palette_unpack(uint16_t entry,
                           uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);

#endif /* JT_VMS_H */
