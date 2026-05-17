/*
 * library.c — VMUICONS.VMS pack/unpack + I/O.
 *
 * VMS wrapper convention: we always emit a fully-formed VMS header
 * (standard 640 bytes) so the BIOS file manager shows the library
 * with a normal name + icon, then append our private payload after
 * that. Reads use the same offset.
 */
#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/vmu.h>
#include <dc/vmufs.h>
#include <string.h>
#include <time.h>

#include "library.h"

#define VMS_HEADER_SIZE  640    /* 0x280 -- ends right where payload starts */
#define PAYLOAD_HEADER_SIZE 32

/* A small, recognizable 32x32 4bpp icon for the library save itself.
 * Pattern: stack of 3 small squares (a "library shelf" metaphor).
 * Pre-built at runtime when we encode the header. */
static void build_library_icon(uint8_t *out_bitmap, uint8_t *out_palette)
{
    /* Palette: black + white + cyan accent + filler. */
    memset(out_palette, 0, 32);
    /* index 0 = black */
    out_palette[0] = 0x00; out_palette[1] = 0x00;
    /* index 1 = white */
    out_palette[2] = 0xFF; out_palette[3] = 0xFF;
    /* index 2 = cyan accent */
    out_palette[4] = (uint8_t)((0xF << 4) | 0xF); /* g=15, b=15 */
    out_palette[5] = (uint8_t)((0xF << 4) | 0x4); /* a=15, r=4 */

    /* Bitmap: outer frame (idx 0) + three centered "shelves". 32x32,
     * 4bpp packed -- 2 px/byte, high nibble = even col. */
    memset(out_bitmap, 0x11, 512);  /* fill white (idx 1, both nibbles) */
    /* Border 1px in idx 0 black: rows 0/31 all 0, col 0/31 all 0. */
    for (int x = 0; x < 32; x += 2) out_bitmap[x / 2] = 0x00;
    for (int x = 0; x < 32; x += 2) out_bitmap[(31 * 32 + x) / 2] = 0x00;
    for (int y = 0; y < 32; y++) {
        out_bitmap[(y * 32) / 2]      &= 0x0F;        /* col 0  -> high nibble black */
        out_bitmap[(y * 32 + 30) / 2] &= 0xF0;        /* col 31 -> low nibble black */
    }
    /* Three "shelves" -- horizontal bars at y=10, 16, 22, x=8..23. */
    int rows[3] = { 10, 16, 22 };
    for (int r = 0; r < 3; r++) {
        int y = rows[r];
        for (int x = 8; x < 24; x += 2) {
            out_bitmap[(y * 32 + x) / 2] = (uint8_t)((2 << 4) | 2); /* cyan */
        }
    }
}

void jt_library_init(jt_library_t *lib)
{
    memset(lib, 0, sizeof(*lib));
    lib->capacity = JT_LIBRARY_CAPACITY;
}

/* Build the VMS-format wrapper header. Output buffer must be at least
 * VMS_HEADER_SIZE bytes. */
static void write_vms_header(uint8_t *out)
{
    memset(out, 0, VMS_HEADER_SIZE);
    memcpy(out + 0x00, "VMU Icon Lib    ", 16);
    memcpy(out + 0x10, "Joypad Tester icon library save ", 32);
    memcpy(out + 0x30, "JOYPAD-TESTER  ", 16);
    /* icon count = 1, anim speed = 0, eyecatch = 0 */
    out[0x40] = 1; out[0x41] = 0;
    out[0x42] = 0; out[0x43] = 0;
    out[0x44] = 0; out[0x45] = 0;
    /* CRC computed after data is in place; placeholder zeros here. */
    /* data_size: bytes after the header itself (set at write time). */
    /* 0x60: palette + 0x80: bitmap */
    build_library_icon(out + 0x80, out + 0x60);
}

int jt_library_append(jt_library_t *lib, const jt_library_entry_t *entry)
{
    if (lib->entry_count >= lib->capacity) return -1;
    lib->entries[lib->entry_count++] = *entry;
    return 0;
}

/* Serialize an entry into entry_size bytes. */
static void serialize_entry(const jt_library_entry_t *e, uint8_t *out)
{
    memset(out, 0, JT_LIBRARY_ENTRY_SIZE);
    /* [0..15] name */
    memcpy(out, e->name, JT_LIBRARY_NAME_LEN);
    /* [16..23] timestamp LE */
    for (int i = 0; i < 8; i++) {
        out[16 + i] = (uint8_t)((e->timestamp >> (i * 8)) & 0xFF);
    }
    /* [24..25] flags LE */
    out[24] = (uint8_t)(e->flags & 0xFF);
    out[25] = (uint8_t)((e->flags >> 8) & 0xFF);
    /* [28..127] description */
    memcpy(out + 28, e->description, JT_LIBRARY_DESC_LEN);
    /* [128..159] palette */
    for (int i = 0; i < JT_PALETTE_ENTRIES; i++) {
        out[128 + i * 2 + 0] = (uint8_t)(e->icon.palette[i] & 0xFF);
        out[128 + i * 2 + 1] = (uint8_t)((e->icon.palette[i] >> 8) & 0xFF);
    }
    /* [160..671] color bitmap (4bpp packed). */
    for (int i = 0; i < JT_CANVAS_W * JT_CANVAS_H; i += 2) {
        uint8_t hi = e->icon.color_indices[i]     & 0x0F;
        uint8_t lo = e->icon.color_indices[i + 1] & 0x0F;
        out[160 + i / 2] = (uint8_t)((hi << 4) | lo);
    }
    /* [672..799] mono bitmap. */
    memcpy(out + 672, e->icon.mono_bits, 128);
}

static void deserialize_entry(const uint8_t *in, jt_library_entry_t *e)
{
    memset(e, 0, sizeof(*e));
    memcpy(e->name, in, JT_LIBRARY_NAME_LEN);
    e->name[JT_LIBRARY_NAME_LEN] = '\0';
    uint64_t ts = 0;
    for (int i = 0; i < 8; i++) {
        ts |= ((uint64_t)in[16 + i]) << (i * 8);
    }
    e->timestamp = ts;
    e->flags = (uint16_t)(in[24] | (in[25] << 8));
    memcpy(e->description, in + 28, JT_LIBRARY_DESC_LEN);
    e->description[JT_LIBRARY_DESC_LEN] = '\0';
    for (int i = 0; i < JT_PALETTE_ENTRIES; i++) {
        e->icon.palette[i] = (uint16_t)(in[128 + i * 2] |
                                        (in[128 + i * 2 + 1] << 8));
    }
    for (int i = 0; i < JT_CANVAS_W * JT_CANVAS_H; i += 2) {
        uint8_t byte = in[160 + i / 2];
        e->icon.color_indices[i]     = (byte >> 4) & 0x0F;
        e->icon.color_indices[i + 1] = byte & 0x0F;
    }
    memcpy(e->icon.mono_bits, in + 672, 128);
    e->icon.has_color_icon = (e->flags & JT_LIB_FLAG_COLOR) != 0;
    e->icon.real_mode_flag = (e->flags & JT_LIB_FLAG_REALMODE) != 0;
}

int jt_library_save(struct maple_device *dev, const jt_library_t *lib)
{
    /* total size = VMS header + payload header + entry_count * entry_size.
     * Round up to whole blocks (512 bytes each) so vmufs_write is happy. */
    size_t payload_size = PAYLOAD_HEADER_SIZE +
                          (size_t)lib->entry_count * JT_LIBRARY_ENTRY_SIZE;
    size_t total = VMS_HEADER_SIZE + payload_size;
    size_t total_rounded = (total + 511) & ~((size_t)511);

    uint8_t *buf = (uint8_t *)malloc(total_rounded);
    if (!buf) return -2;
    memset(buf, 0, total_rounded);
    write_vms_header(buf);

    /* Payload header at offset VMS_HEADER_SIZE. */
    uint8_t *p = buf + VMS_HEADER_SIZE;
    memcpy(p, JT_LIBRARY_MAGIC, 4);
    p[4] = JT_LIBRARY_VERSION;
    p[5] = 0;
    p[6] = (uint8_t)(JT_LIBRARY_ENTRY_SIZE & 0xFF);
    p[7] = (uint8_t)((JT_LIBRARY_ENTRY_SIZE >> 8) & 0xFF);
    p[8] = (uint8_t)(lib->entry_count & 0xFF);
    p[9] = (uint8_t)((lib->entry_count >> 8) & 0xFF);
    p[10] = (uint8_t)(lib->capacity & 0xFF);
    p[11] = (uint8_t)((lib->capacity >> 8) & 0xFF);
    /* p[12..31] reserved. */
    for (int i = 0; i < lib->entry_count; i++) {
        serialize_entry(&lib->entries[i],
                        buf + VMS_HEADER_SIZE + PAYLOAD_HEADER_SIZE +
                              i * JT_LIBRARY_ENTRY_SIZE);
    }

    /* Stamp the VMS header data-size field with the payload size,
     * then CRC the whole file. */
    uint32_t data_size = (uint32_t)payload_size;
    buf[0x48] = (uint8_t)(data_size & 0xFF);
    buf[0x49] = (uint8_t)((data_size >> 8) & 0xFF);
    buf[0x4A] = (uint8_t)((data_size >> 16) & 0xFF);
    buf[0x4B] = (uint8_t)((data_size >> 24) & 0xFF);
    /* CRC is computed with bytes 0x46..0x47 zeroed (they already are). */
    uint16_t crc = jt_vms_crc(buf, total_rounded);
    buf[0x46] = (uint8_t)(crc & 0xFF);
    buf[0x47] = (uint8_t)((crc >> 8) & 0xFF);

    int rc = vmufs_write(dev, JT_LIBRARY_FILENAME, buf,
                         (int)total_rounded, VMUFS_OVERWRITE);
    free(buf);
    return (rc == 0) ? 0 : -1;
}

int jt_library_load(struct maple_device *dev, jt_library_t *lib)
{
    jt_library_init(lib);

    void *raw = NULL;
    int   size = 0;
    if (vmufs_read(dev, JT_LIBRARY_FILENAME, &raw, &size) != 0) return -1;
    if ((size_t)size < VMS_HEADER_SIZE + PAYLOAD_HEADER_SIZE) {
        free(raw);
        return -2;
    }
    const uint8_t *buf = (const uint8_t *)raw;

    const uint8_t *p = buf + VMS_HEADER_SIZE;
    if (memcmp(p, JT_LIBRARY_MAGIC, 4) != 0) {
        free(raw);
        return -2;
    }
    uint8_t  version    = p[4];
    uint16_t esz        = (uint16_t)(p[6] | (p[7] << 8));
    uint16_t count      = (uint16_t)(p[8] | (p[9] << 8));
    uint16_t capacity   = (uint16_t)(p[10] | (p[11] << 8));
    (void)version;

    if (esz != JT_LIBRARY_ENTRY_SIZE) {
        /* Future-proofing: alien entry size = older or newer format.
         * v0.2 only knows JT_LIBRARY_ENTRY_SIZE; bail rather than guess. */
        free(raw);
        return -2;
    }
    if (count > JT_LIBRARY_CAPACITY) count = JT_LIBRARY_CAPACITY;

    lib->entry_count = count;
    lib->capacity = (capacity > 0) ? capacity : JT_LIBRARY_CAPACITY;
    if (lib->capacity > JT_LIBRARY_CAPACITY) lib->capacity = JT_LIBRARY_CAPACITY;

    for (int i = 0; i < count; i++) {
        size_t off = VMS_HEADER_SIZE + PAYLOAD_HEADER_SIZE +
                     (size_t)i * JT_LIBRARY_ENTRY_SIZE;
        if (off + JT_LIBRARY_ENTRY_SIZE > (size_t)size) break;
        deserialize_entry(buf + off, &lib->entries[i]);
    }

    free(raw);
    return 0;
}

int jt_library_backup_icondata(struct maple_device *dev,
                               const void *icondata_bytes, int size)
{
    if (size < 0xA0) return -1;
    /* Decode the existing ICONDATA_VMS so we can store it as a library
     * entry rather than as raw bytes -- keeps the entry format uniform
     * and makes the backup browseable like any other library icon. */
    jt_icon_t icon;
    if (!jt_vms_decode_icondata((const uint8_t *)icondata_bytes,
                                (size_t)size, &icon)) {
        return -1;
    }

    jt_library_t lib;
    int load_rc = jt_library_load(dev, &lib);
    if (load_rc == -1) {
        /* No library yet -- start a fresh one. */
        jt_library_init(&lib);
    } else if (load_rc < 0) {
        return load_rc;
    }

    jt_library_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "BACKUP-%lu",
             (unsigned long)(time(NULL) & 0xFFFF));
    snprintf(entry.description, sizeof(entry.description),
             "Auto-stashed before Apply overwrite.");
    entry.timestamp = (uint64_t)time(NULL);
    entry.flags = JT_LIB_FLAG_COLOR | JT_LIB_FLAG_MONO | JT_LIB_FLAG_BACKUP;
    if (icon.real_mode_flag) entry.flags |= JT_LIB_FLAG_REALMODE;
    entry.icon = icon;

    if (jt_library_append(&lib, &entry) != 0) {
        /* Library full -- oldest non-favorite gets bumped. Simplest:
         * drop entry 0 (FIFO) and retry. */
        for (int i = 0; i < lib.entry_count - 1; i++) {
            lib.entries[i] = lib.entries[i + 1];
        }
        lib.entry_count--;
        if (jt_library_append(&lib, &entry) != 0) return -1;
    }

    return jt_library_save(dev, &lib);
}
