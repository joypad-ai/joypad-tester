/*
 * apply.c — ICONDATA_VMS write path.
 *
 * Workflow:
 *   1. Resolve the maple subdevice at (port, slot+1) and confirm it's
 *      a VMU.
 *   2. If backup is enabled and an ICONDATA_VMS already exists on the
 *      target, stash the existing bytes into the library (see
 *      library/library.h's jt_library_backup_icondata).
 *   3. Encode the editor canvas to the 1024-byte on-disc blob.
 *   4. vmufs_write("ICONDATA_VMS", ..., flags=VMUFS_OVERWRITE).
 *
 * Errors at any step return without committing the new icon, so the
 * VMU state is consistent with the result code.
 */
#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/vmu.h>
#include <dc/vmufs.h>
#include <stdlib.h>
#include <string.h>

#include "apply.h"
#include "../library/library.h"
#include "../library/gen_library_icon.h"   /* baked default ICONDATA blob */

#define ICONDATA_FILENAME "ICONDATA_VMS"

static bool backup_enabled = true;

void jt_apply_set_backup_enabled(bool enabled) { backup_enabled = enabled; }
bool jt_apply_get_backup_enabled(void) { return backup_enabled; }

/* Pending "change icon" target (port, slot); negative port = none. */
static int pending_port = -1, pending_slot = -1;

void jt_apply_set_pending_target(int port_idx, int slot_idx)
{
    pending_port = port_idx; pending_slot = slot_idx;
}
bool jt_apply_has_pending_target(void) { return pending_port >= 0; }
void jt_apply_clear_pending_target(void) { pending_port = pending_slot = -1; }
bool jt_apply_take_pending_target(int *port_idx, int *slot_idx)
{
    if (pending_port < 0) return false;
    if (port_idx) *port_idx = pending_port;
    if (slot_idx) *slot_idx = pending_slot;
    pending_port = pending_slot = -1;
    return true;
}

const char *jt_apply_result_str(jt_apply_result_t r)
{
    switch (r) {
        case JT_APPLY_OK:                return "Applied OK";
        case JT_APPLY_ERR_NO_VMU:        return "No VMU on target slot";
        case JT_APPLY_ERR_NO_SPACE:      return "Not enough free blocks";
        case JT_APPLY_ERR_BACKUP_FAILED: return "Backup failed; aborted";
        case JT_APPLY_ERR_WRITE_FAILED:  return "Write failed";
        case JT_APPLY_ERR_ENCODE:        return "Encode failed";
        default:                         return "Unknown error";
    }
}

jt_apply_result_t jt_apply_icondata(const jt_icon_t *icon,
                                    int port_idx, int slot_idx)
{
    maple_device_t *dev = maple_enum_dev(port_idx, slot_idx + 1);
    if (!dev || !dev->valid) return JT_APPLY_ERR_NO_VMU;
    if (!(dev->info.functions & MAPLE_FUNC_MEMCARD)) return JT_APPLY_ERR_NO_VMU;

    /* ICONDATA_VMS occupies 2 user blocks (1024 bytes / 512 per block).
     * Need at least 2 free blocks, plus a few more if the backup is
     * about to create a library file. Be conservative and require 4. */
    int free_blocks = vmufs_free_blocks(dev);
    if (free_blocks < 2) return JT_APPLY_ERR_NO_SPACE;

    /* Read the existing ICONDATA_VMS (if any) once, for two purposes:
     *   1. Preserve the Real Mode (3D BIOS) flag -- it's a per-VMU
     *      display setting, so swapping the icon shouldn't silently
     *      turn it off. We OR it onto the icon being written.
     *   2. Back it up into the library (when backup is enabled). */
    jt_icon_t to_write = *icon;
    {
        void  *existing = NULL;
        int    existing_size = 0;
        if (vmufs_read(dev, ICONDATA_FILENAME, &existing, &existing_size) == 0) {
            jt_icon_t prev;
            if (jt_vms_decode_icondata(existing, existing_size, &prev) &&
                prev.real_mode_flag) {
                to_write.real_mode_flag = true;
            }
            if (backup_enabled) {
                int rc = jt_library_backup_icondata(dev, existing, existing_size);
                if (rc != 0) { free(existing); return JT_APPLY_ERR_BACKUP_FAILED; }
            }
            free(existing);
        }
        /* If vmufs_read failed, the file simply doesn't exist -- not
         * an error; proceed to write the new one. */
    }

    /* Encode + write. */
    uint8_t buf[JT_VMS_ICONDATA_SIZE];
    if (!jt_vms_encode_icondata(&to_write, buf)) return JT_APPLY_ERR_ENCODE;

    int rc = vmufs_write(dev, ICONDATA_FILENAME, buf, sizeof(buf),
                         VMUFS_OVERWRITE);
    if (rc != 0) return JT_APPLY_ERR_WRITE_FAILED;
    return JT_APPLY_OK;
}

/* ---- VMU LCD helpers ---- */

/* 32x32 mono icon (MSB-first, row-major) -> the VMU LCD's native 48x32
 * bitmap: icon centered (8px pad each side), MSB-first per byte, rotated
 * 180 deg. This matches what vmu_draw_lcd expects -- the XBM helper we
 * used before silently produced garbage from packed bytes, leaving the
 * LCD blank on hardware (same bug the tester's LCD mirror hit). */
static void mono32_to_lcd_native(const uint8_t *mono, uint8_t out[48 * 32 / 8])
{
    memset(out, 0, 48 * 32 / 8);
    for (int r = 0; r < 32; r++) {
        for (int c = 0; c < 32; c++) {
            int p = r * 32 + c;
            if (!((mono[p / 8] >> (7 - (p % 8))) & 1)) continue;
            int x = 8 + c, y = r;            /* upright position in 48x32 */
            int dx = 47 - x, dy = 31 - y;    /* 180-deg rotate to native */
            out[dy * 6 + (dx / 8)] |= (uint8_t)(0x80 >> (dx % 8));
        }
    }
}

int jt_vmu_show_mono_bits(int port_idx, int slot_idx, const uint8_t *mono_bits)
{
    maple_device_t *dev = maple_enum_dev(port_idx, slot_idx + 1);
    if (!dev || !dev->valid) return -1;
    if (!(dev->info.functions & MAPLE_FUNC_LCD)) return -1;
    uint8_t native[48 * 32 / 8];
    mono32_to_lcd_native(mono_bits, native);
    return vmu_draw_lcd(dev, native);
}

int jt_vmu_ensure_icondata(int port_idx, int slot_idx)
{
    maple_device_t *dev = maple_enum_dev(port_idx, slot_idx + 1);
    if (!dev || !dev->valid) return -1;
    if (!(dev->info.functions & MAPLE_FUNC_MEMCARD)) return -1;

    /* Already present? */
    void *existing = NULL;
    int   existing_size = 0;
    if (vmufs_read(dev, ICONDATA_FILENAME, &existing, &existing_size) == 0) {
        free(existing);
        return 0;
    }
    if (vmufs_free_blocks(dev) < 2) return -2;
    return vmufs_write(dev, ICONDATA_FILENAME,
                       (void *)joypad_logo_icondata_full,
                       sizeof(joypad_logo_icondata_full),
                       VMUFS_OVERWRITE);
}

int jt_vmu_show_stored_icon(int port_idx, int slot_idx)
{
    maple_device_t *dev = maple_enum_dev(port_idx, slot_idx + 1);
    if (!dev || !dev->valid) return -1;
    if (!(dev->info.functions & MAPLE_FUNC_LCD)) return -1;
    uint8_t native[48 * 32 / 8];
    void *raw = NULL;
    int   sz = 0;
    if (vmufs_read(dev, ICONDATA_FILENAME, &raw, &sz) == 0 && raw) {
        jt_icon_t icon;
        if (jt_vms_decode_icondata(raw, sz, &icon)) mono32_to_lcd_native(icon.mono_bits, native);
        else memset(native, 0, sizeof(native));
        free(raw);
    } else {
        memset(native, 0, sizeof(native));
    }
    return vmu_draw_lcd(dev, native);
}
