/*
 * mu.h -- Memory Unit detection + free-space readout.
 *
 * Xbox kernel exposes up to 8 Memory Units at well-known paths
 * (\Device\Memunit0\Partition1 ... \Device\Memunit7\Partition1).
 * We probe each, mount any that resolve under a per-MU drive letter,
 * and call GetDiskFreeSpaceExA to surface free + total bytes.
 *
 * Mapping to port/slot: convention is sequential -- port A top is MU
 * index 0, port A bottom is 1, port B top is 2, ..., port D bottom
 * is 7. nxdk doesn't ship an enumeration API, so this mapping is
 * empirical and likely needs to be confirmed on hardware.
 */
#ifndef JT_XBOX_MU_H
#define JT_XBOX_MU_H

#include <stdbool.h>
#include <stdint.h>

#define JT_MU_COUNT 8

typedef struct {
    bool     present;        /* Mount succeeded; device responded. */
    uint32_t free_kb;        /* Free bytes / 1024. */
    uint32_t total_kb;       /* Total bytes / 1024. */
} jt_mu_info_t;

/* Initial mount probe -- safe to call once at boot. Idempotent. */
void jt_mu_init(void);

/* Re-probe any unmounted MUs (hotplug pickup). Call from the polling
 * tick, but throttled -- nxMountDrive may take a vblank or two when
 * the underlying device hasn't responded yet. */
void jt_mu_refresh(void);

/* Snapshot of an MU's current state. mu_idx in [0, JT_MU_COUNT). */
const jt_mu_info_t *jt_mu_get(int mu_idx);

/* Convert (port, slot) to MU index using the sequential mapping
 * documented at the top of this header. Returns -1 if out of range. */
int jt_mu_index_for(int port_idx, int slot_idx);

#endif /* JT_XBOX_MU_H */
