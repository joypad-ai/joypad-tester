/*
 * apply.h — write ICONDATA_VMS to a target VMU, with backup-on-replace.
 *
 * Apply is independent of the editor UI: it takes a fully-formed
 * jt_icon_t plus a port/slot pair and returns a status code so the
 * caller can render success/failure messaging.
 *
 * Backup-on-replace policy: when the target VMU already has an
 * ICONDATA_VMS, the existing file is copied into the per-VMU library
 * save (creating it if absent, see library/library.h) BEFORE the new
 * one is written. That way the player can recover from a misclick.
 * Toggleable via apply_set_backup_enabled().
 */
#ifndef JT_APPLY_H
#define JT_APPLY_H

#include <stdbool.h>

#include "vms.h"

typedef enum {
    JT_APPLY_OK = 0,
    JT_APPLY_ERR_NO_VMU,        /* port/slot has no VMU */
    JT_APPLY_ERR_NO_SPACE,      /* not enough free blocks */
    JT_APPLY_ERR_BACKUP_FAILED, /* library backup failed; original not touched */
    JT_APPLY_ERR_WRITE_FAILED,  /* vmufs_write returned non-zero */
    JT_APPLY_ERR_ENCODE,        /* icon encode failed (shouldn't happen) */
} jt_apply_result_t;

/* Write `icon` as ICONDATA_VMS to the VMU at (port_idx, slot_idx).
 * Returns one of the result codes above. Slot index is 0 or 1
 * (matches jt_slot_state_t layout). */
jt_apply_result_t jt_apply_icondata(const jt_icon_t *icon,
                                    int port_idx, int slot_idx);

/* Backup-on-replace policy toggle. Default ON. */
void jt_apply_set_backup_enabled(bool enabled);
bool jt_apply_get_backup_enabled(void);

/* String form for displaying a result code to the user. */
const char *jt_apply_result_str(jt_apply_result_t r);

/* ---- VMU LCD helpers --------------------------------------------------
 * Push a 48x32 image to a VMU's physical LCD. The 32x32 mono icon is
 * centered (8px padding left/right). Used by the editor to mirror the
 * live mono canvas, and on mode-leave to restore the VMU to its
 * "natural" stored ICONDATA icon. */

/* Render a 32x32 mono bitmap (canvas mono_bits layout, MSB-first within
 * byte, row-major) onto the VMU LCD. Returns 0 on success. */
int jt_vmu_show_mono_bits(int port_idx, int slot_idx, const uint8_t *mono_bits);

/* Read the VMU's ICONDATA_VMS, extract its mono icon, and push it to
 * the LCD. If no ICONDATA exists or decode fails the LCD is cleared.
 * Returns 0 on success, negative on error / missing device. */
int jt_vmu_show_stored_icon(int port_idx, int slot_idx);

/* If the VMU at (port, slot) has no ICONDATA_VMS, write the baked
 * Joypad-logo default so things that live INSIDE ICONDATA (Real Mode /
 * 3D BIOS, the LCD mono icon) have a file to attach to. No-op if one
 * already exists. Returns 0 on success, negative on error / no device. */
int jt_vmu_ensure_icondata(int port_idx, int slot_idx);

/* "Change icon" handoff: the file manager sets a pending target VMU
 * before sending the user to the Icon Library to choose an icon; the
 * Library applies the chosen icon to that VMU and clears the target.
 * take_ returns true and clears if a target was set. */
void jt_apply_set_pending_target(int port_idx, int slot_idx);
bool jt_apply_take_pending_target(int *port_idx, int *slot_idx);
bool jt_apply_has_pending_target(void);
void jt_apply_clear_pending_target(void);

#endif /* JT_APPLY_H */
