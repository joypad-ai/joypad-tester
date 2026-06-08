/*
 * mu.c -- probe Memory Units, mount each, read free + total bytes.
 *
 * nxdk's mount layer (nxMountDrive) wraps the kernel's
 * IoCreateSymbolicLink + filesystem mount, so we just hand it a
 * drive letter (we pick 'M' + mu_idx, well outside C/E/D) and the
 * kernel-style device path. Failure means the MU isn't inserted
 * (or the device path convention differs and we need to revisit).
 *
 * Mount cost: an empty MU slot resolves fast; a present-but-not-yet-
 * ready MU may take a vblank. We probe at startup and then re-probe
 * the still-unmounted indices once a second so hotplug picks up
 * without flooding the bus.
 */
#include "mu.h"

#include <nxdk/mount.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "ports.h"   /* JT_NUM_PORTS, JT_NUM_SLOTS for the mapping */

static jt_mu_info_t mu_state[JT_MU_COUNT];

static char mu_letter(int idx)
{
    /* 'M', 'N', ..., 'T' for indices 0..7. Outside the standard
     * C / D / E / Z letters nxdk's drive-list sample uses. */
    return (char)('M' + idx);
}

static bool refresh_one(int idx)
{
    if (mu_state[idx].present) {
        /* Already mounted; just refresh the byte counts. */
        char root[4] = { mu_letter(idx), ':', '\\', 0 };
        ULARGE_INTEGER free_b, total_b;
        if (GetDiskFreeSpaceExA(root, NULL, &total_b, &free_b)) {
            mu_state[idx].free_kb  = (uint32_t)(free_b.QuadPart / 1024);
            mu_state[idx].total_kb = (uint32_t)(total_b.QuadPart / 1024);
            return true;
        }
        /* Drive vanished -- forget it; the next probe pass will try
         * to re-mount if the MU comes back. */
        nxUnmountDrive(mu_letter(idx));
        mu_state[idx].present = false;
        mu_state[idx].free_kb = mu_state[idx].total_kb = 0;
        return false;
    }

    char path[64];
    snprintf(path, sizeof(path), "\\Device\\Memunit%d\\Partition1", idx);
    if (!nxMountDrive(mu_letter(idx), path)) return false;

    char root[4] = { mu_letter(idx), ':', '\\', 0 };
    ULARGE_INTEGER free_b, total_b;
    if (!GetDiskFreeSpaceExA(root, NULL, &total_b, &free_b)) {
        /* Mount appeared to succeed but we can't query it; drop it. */
        nxUnmountDrive(mu_letter(idx));
        return false;
    }
    mu_state[idx].present  = true;
    mu_state[idx].free_kb  = (uint32_t)(free_b.QuadPart / 1024);
    mu_state[idx].total_kb = (uint32_t)(total_b.QuadPart / 1024);
    return true;
}

void jt_mu_init(void)
{
    memset(mu_state, 0, sizeof(mu_state));
    for (int i = 0; i < JT_MU_COUNT; i++) refresh_one(i);
}

void jt_mu_refresh(void)
{
    for (int i = 0; i < JT_MU_COUNT; i++) refresh_one(i);
}

const jt_mu_info_t *jt_mu_get(int mu_idx)
{
    if (mu_idx < 0 || mu_idx >= JT_MU_COUNT) return NULL;
    return &mu_state[mu_idx];
}

int jt_mu_index_for(int port_idx, int slot_idx)
{
    if (port_idx < 0 || port_idx >= JT_NUM_PORTS) return -1;
    if (slot_idx < 0 || slot_idx >= JT_NUM_SLOTS) return -1;
    int idx = port_idx * JT_NUM_SLOTS + slot_idx;
    if (idx >= JT_MU_COUNT) return -1;
    return idx;
}
