/*
 * vms.c — ICONDATA_VMS encode/decode + generic save icon extract.
 *
 * Mirror of the byte-level logic in dreamcast-icon-maker's app.js
 * (createVMSData / parseIconData), reimplemented in C against the
 * jt_icon_t canvas representation. See vms.h for the on-disc layout.
 */
#include <string.h>

#include "vms.h"

/* The 16-byte 3D-mode unlock sequence the Dreamcast BIOS scans for
 * at offset 0x2C0 of ICONDATA_VMS. Setting it enables the hidden
 * 3D file-manager "Real Mode" — same value documented in the EVMU
 * project. */
static const uint8_t REAL_MODE_SEQUENCE[16] = {
    0xda, 0x69, 0xd0, 0xda, 0xc7, 0x4e, 0xf8, 0x36,
    0x18, 0x92, 0x79, 0x68, 0x2d, 0xb5, 0x30, 0x86,
};

uint16_t jt_palette_pack(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    /* The on-disc palette uses 4 bits per channel packed into 2 bytes:
     *   byte 0 = (g_hi<<4) | b_hi
     *   byte 1 = (a_hi<<4) | r_hi
     * We return that as a uint16_t with byte 0 in the low half so
     * the byte order matches the file when written little-endian. */
    uint8_t rh = (r >> 4) & 0x0F;
    uint8_t gh = (g >> 4) & 0x0F;
    uint8_t bh = (b >> 4) & 0x0F;
    uint8_t ah = (a >> 4) & 0x0F;
    return (uint16_t)(((gh << 4) | bh) | (((ah << 4) | rh) << 8));
}

void jt_palette_unpack(uint16_t entry, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    uint8_t b0 = (uint8_t)(entry & 0xFF);
    uint8_t b1 = (uint8_t)((entry >> 8) & 0xFF);
    /* Multiply by 17 to expand 4-bit values to full 8-bit (0x0..0xF -> 0..0xFF). */
    if (g) *g = ((b0 >> 4) & 0x0F) * 17;
    if (b) *b = (b0 & 0x0F)        * 17;
    if (a) *a = ((b1 >> 4) & 0x0F) * 17;
    if (r) *r = (b1 & 0x0F)        * 17;
}

bool jt_vms_encode_icondata(const jt_icon_t *icon, uint8_t *out)
{
    if (!icon || !out) return false;
    memset(out, 0, JT_VMS_ICONDATA_SIZE);

    /* 0x00..0x0F: description (ASCII, NUL-padded). The on-disc spec
     * says Shift-JIS; in v1 we restrict to ASCII which is JIS-safe in
     * the low range, no transcoding needed. */
    memcpy(out, icon->description, 16);

    /* 0x10..0x1F: fixed bytes. 0x10 always 0x20; 0x14 is 0xA0 when a
     * color icon section is present, else 0x00. */
    out[0x10] = 0x20;
    out[0x14] = icon->has_color_icon ? 0xA0 : 0x00;

    /* 0x20..0x9F: mono bitmap (128 bytes, MSB-first per byte). */
    memcpy(out + 0x20, icon->mono_bits, sizeof(icon->mono_bits));

    if (icon->has_color_icon) {
        /* 0xA0..0xBF: palette (16 entries x 2 bytes, little-endian). */
        for (int i = 0; i < JT_PALETTE_ENTRIES; i++) {
            uint16_t e = icon->palette[i];
            out[0xA0 + i * 2 + 0] = (uint8_t)(e & 0xFF);
            out[0xA0 + i * 2 + 1] = (uint8_t)((e >> 8) & 0xFF);
        }
        /* 0xC0..0x2BF: color bitmap. Two pixels per byte; even column
         * (low x) goes in the high nibble. 32 cols * 32 rows / 2 px = 512. */
        for (int i = 0; i < JT_CANVAS_W * JT_CANVAS_H; i += 2) {
            uint8_t hi = icon->color_indices[i]     & 0x0F;
            uint8_t lo = icon->color_indices[i + 1] & 0x0F;
            out[0xC0 + i / 2] = (uint8_t)((hi << 4) | lo);
        }
    }

    /* 0x2C0..0x2CF: optional Real Mode sequence. */
    if (icon->real_mode_flag) {
        memcpy(out + 0x2C0, REAL_MODE_SEQUENCE, sizeof(REAL_MODE_SEQUENCE));
    }
    /* 0x2D0..0x3FF: zero padding (already memset to 0). */

    return true;
}

bool jt_vms_decode_icondata(const uint8_t *in, size_t size, jt_icon_t *icon)
{
    if (!in || !icon || size < 0xA0) return false;
    memset(icon, 0, sizeof(*icon));

    memcpy(icon->description, in, 16);
    icon->has_color_icon = (in[0x14] == 0xA0);
    icon->real_mode_flag = (size >= 0x2D0) &&
                           (memcmp(in + 0x2C0, REAL_MODE_SEQUENCE,
                                   sizeof(REAL_MODE_SEQUENCE)) == 0);

    memcpy(icon->mono_bits, in + 0x20, sizeof(icon->mono_bits));

    if (icon->has_color_icon && size >= 0x2C0) {
        for (int i = 0; i < JT_PALETTE_ENTRIES; i++) {
            uint8_t b0 = in[0xA0 + i * 2 + 0];
            uint8_t b1 = in[0xA0 + i * 2 + 1];
            icon->palette[i] = (uint16_t)(b0 | (b1 << 8));
        }
        for (int i = 0; i < JT_CANVAS_W * JT_CANVAS_H; i += 2) {
            uint8_t byte = in[0xC0 + i / 2];
            icon->color_indices[i]     = (byte >> 4) & 0x0F;
            icon->color_indices[i + 1] = byte & 0x0F;
        }
    }
    return true;
}

unsigned jt_vms_save_icon_count(const uint8_t *in, size_t size)
{
    if (!in || size < 0x42) return 0;
    uint16_t n = (uint16_t)(in[0x40] | (in[0x41] << 8));
    return (n >= 1 && n <= 3) ? n : 0;
}

bool jt_vms_extract_save_icon(const uint8_t *in, size_t size,
                              unsigned frame, jt_icon_t *icon)
{
    if (!in || !icon) return false;
    unsigned count = jt_vms_save_icon_count(in, size);
    if (count == 0 || frame >= count) return false;
    /* Need palette (0x60..0x7F) + the requested frame (0x80 + frame*512). */
    size_t frame_off = 0x80 + (size_t)frame * 512;
    if (size < frame_off + 512) return false;

    memset(icon, 0, sizeof(*icon));
    icon->has_color_icon = true;
    memcpy(icon->description, in, 16);
    /* No real-mode sequence on a generic save. */
    icon->real_mode_flag = false;

    /* Palette at 0x60. Same packing as ICONDATA_VMS. */
    for (int i = 0; i < JT_PALETTE_ENTRIES; i++) {
        uint8_t b0 = in[0x60 + i * 2 + 0];
        uint8_t b1 = in[0x60 + i * 2 + 1];
        icon->palette[i] = (uint16_t)(b0 | (b1 << 8));
    }
    /* Bitmap at frame_off. */
    for (int i = 0; i < JT_CANVAS_W * JT_CANVAS_H; i += 2) {
        uint8_t byte = in[frame_off + i / 2];
        icon->color_indices[i]     = (byte >> 4) & 0x0F;
        icon->color_indices[i + 1] = byte & 0x0F;
    }

    /* Generic saves carry no mono icon, so derive one from the color by
     * luminance, matching the web maker's color->mono conversion exactly
     * (dreamcast-icon-maker app.js processMonoImage: Rec.601 luma, and
     * brightness <= 64 -> "on"/black ink). Dark pixels become the ink;
     * alpha=0 is treated as off. */
    memset(icon->mono_bits, 0, sizeof(icon->mono_bits));
    for (int p = 0; p < JT_CANVAS_W * JT_CANVAS_H; p++) {
        uint8_t r, g, b, a;
        jt_palette_unpack(icon->palette[icon->color_indices[p]], &r, &g, &b, &a);
        /* Rec. 601 luma weights -- same as the web maker. */
        unsigned luma = (unsigned)(r * 299 + g * 587 + b * 114) / 1000u;
        if (a > 0 && luma <= 64) {
            icon->mono_bits[p / 8] |= (uint8_t)(1u << (7 - (p % 8)));
        }
    }

    return true;
}

uint16_t jt_vms_crc(const uint8_t *data, size_t size)
{
    /* Sega's VMS save-file CRC: standard polynomial 0x1021 applied
     * after XOR-ing each byte into the high half. See KOS vmu_pkg
     * docs for the reference implementation. */
    uint32_t n = 0;
    for (size_t i = 0; i < size; i++) {
        n ^= ((uint32_t)data[i]) << 8;
        for (int b = 0; b < 8; b++) {
            if (n & 0x8000) n = (n << 1) ^ 4129;
            else            n <<= 1;
        }
    }
    return (uint16_t)(n & 0xFFFF);
}
