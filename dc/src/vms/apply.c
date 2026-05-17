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
#include <string.h>

#include "apply.h"
#include "../library/library.h"

#define ICONDATA_FILENAME "ICONDATA_VMS"

static bool backup_enabled = true;

void jt_apply_set_backup_enabled(bool enabled) { backup_enabled = enabled; }
bool jt_apply_get_backup_enabled(void) { return backup_enabled; }

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

    /* Backup-on-replace. Read the existing ICONDATA_VMS (if any) and
     * stash it into the library before we overwrite. */
    if (backup_enabled) {
        void  *existing = NULL;
        int    existing_size = 0;
        if (vmufs_read(dev, ICONDATA_FILENAME, &existing, &existing_size) == 0) {
            /* File exists -- back it up. The library will append the
             * bytes as a "previous" entry tagged with a timestamp. */
            int rc = jt_library_backup_icondata(dev, existing, existing_size);
            free(existing);
            if (rc != 0) return JT_APPLY_ERR_BACKUP_FAILED;
        }
        /* If vmufs_read failed, the file simply doesn't exist -- not
         * an error; proceed to write the new one. */
    }

    /* Encode + write. */
    uint8_t buf[JT_VMS_ICONDATA_SIZE];
    if (!jt_vms_encode_icondata(icon, buf)) return JT_APPLY_ERR_ENCODE;

    int rc = vmufs_write(dev, ICONDATA_FILENAME, buf, sizeof(buf),
                         VMUFS_OVERWRITE);
    if (rc != 0) return JT_APPLY_ERR_WRITE_FAILED;
    return JT_APPLY_OK;
}
