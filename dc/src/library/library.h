/*
 * library.h — VMUICONS.VMS packed icon-collection save format.
 *
 * One library save per VMU. Stores many icon entries in a single
 * file (much cheaper than one ICONDATA_VMS per icon, which burns 2
 * blocks each).
 *
 * On-disc layout (inside the VMS wrapper):
 *
 *   0x000..0x27F  Standard VMS file header (640 bytes).
 *                 description = "VMU Icon Library",
 *                 boot ROM desc = "Joypad Tester icon library save",
 *                 application id = "JOYPAD-TESTER",
 *                 icon count = 1, anim speed = 0, eyecatch = 0,
 *                 plus the standard 16-color palette at 0x60 + a
 *                 generic "library" icon bitmap at 0x80.
 *
 *   0x280..       Library payload:
 *     0x000..0x01F  library header (32 bytes)
 *                     [0..3]  magic "VMIL"
 *                     [4]     format version (1 in v0.2)
 *                     [5]     reserved
 *                     [6..7]  entry_size (sizeof on-disc entry, LE)
 *                     [8..9]  entry_count (LE)
 *                     [10..11] capacity (max entries supported, LE)
 *                     [12..31] reserved (zeros)
 *     0x020..       entries[N], each entry_size bytes (see below)
 *
 *   Per-entry layout (~692 bytes each in v0.2):
 *     [0..15]   name (ASCII, NUL-padded)
 *     [16..23]  unix timestamp of last edit (uint64 LE)
 *     [24..25]  flags (LE):
 *                  bit 0: has color icon
 *                  bit 1: has mono icon
 *                  bit 2: real-mode flag set
 *                  bit 3: backup-on-replace entry (auto-stashed)
 *                  bit 4: favorite
 *     [26..27]  reserved
 *     [28..127] description (100 bytes, ASCII, NUL-padded)
 *     [128..159] palette (32 bytes, 16 ARGB1555 entries)
 *     [160..671] color bitmap (512 bytes, 32x32 @ 4bpp)
 *     [672..799] mono bitmap (128 bytes, 32x32 @ 1bpp)
 *
 *   = 800 bytes per entry; with a 32-byte library header that fits
 *   in 16 user blocks (8KB) when capacity = ~10 entries. Capacity
 *   default is 16.
 */
#ifndef JT_LIBRARY_H
#define JT_LIBRARY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "../vms/vms.h"

#define JT_LIBRARY_FILENAME    "VMUICONS.VMS"
#define JT_LIBRARY_MAGIC       "VMIL"
#define JT_LIBRARY_VERSION     1
#define JT_LIBRARY_ENTRY_SIZE  800
#define JT_LIBRARY_CAPACITY    16
#define JT_LIBRARY_NAME_LEN    16
#define JT_LIBRARY_DESC_LEN    100

/* Per-entry flags. */
#define JT_LIB_FLAG_COLOR    (1u << 0)
#define JT_LIB_FLAG_MONO     (1u << 1)
#define JT_LIB_FLAG_REALMODE (1u << 2)
#define JT_LIB_FLAG_BACKUP   (1u << 3)
#define JT_LIB_FLAG_FAVORITE (1u << 4)

typedef struct {
    char     name[JT_LIBRARY_NAME_LEN + 1];   /* +1 for NUL */
    char     description[JT_LIBRARY_DESC_LEN + 1];
    uint64_t timestamp;
    uint16_t flags;
    jt_icon_t icon;     /* full icon for editor round-trip */
} jt_library_entry_t;

/* In-memory representation of a library save. Loaded with
 * jt_library_load and saved back with jt_library_save. */
typedef struct {
    uint16_t entry_count;
    uint16_t capacity;
    jt_library_entry_t entries[JT_LIBRARY_CAPACITY];
} jt_library_t;

/* Initialize an empty library (no entries). */
void jt_library_init(jt_library_t *lib);

/* Load library from a VMU. Returns 0 on success, -1 if no library
 * exists on that VMU, -2 on parse error, -3 on I/O error. */
struct maple_device;
int jt_library_load(struct maple_device *dev, jt_library_t *lib);

/* Write library back to a VMU. Returns 0 on success, -1 on I/O error,
 * -2 on encode error. */
int jt_library_save(struct maple_device *dev, const jt_library_t *lib);

/* Append an entry to the library. Returns 0 on success, -1 if full. */
int jt_library_append(jt_library_t *lib, const jt_library_entry_t *entry);

/* Convenience: pull the existing ICONDATA_VMS bytes from `dev` and
 * stash them as a backup entry in that VMU's library. Used by the
 * apply path when backup-on-replace is enabled. Returns 0 on success. */
int jt_library_backup_icondata(struct maple_device *dev,
                               const void *icondata_bytes, int size);

#endif /* JT_LIBRARY_H */
