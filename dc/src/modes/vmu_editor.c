/*
 * vmu_editor.c — stub for v0.1; full editor lands in v0.2.
 *
 * v0.2 scope (see dc/CHANGELOG.md placeholder + README):
 *   - 32x32 color (16-palette) + mono canvases, drawable with
 *     unified cursor (analog stick or DC mouse).
 *   - 16-bit (ARGB1555) color picker.
 *   - VMU file browser per detected port: lists every save, decodes
 *     icons (palette at offset 0x60, bitmap at 0x80), shows
 *     animation for multi-frame icons.
 *   - Three write targets:
 *       ICONDATA_VMS  (apply, with backup-on-replace to library)
 *       VMUICONS.VMS  (our library save -- packed many icons in one
 *                      file; format-versioned "VMIL" header).
 *       Editor canvas (transient, read by Apply / Save actions).
 *   - DC keyboard support for naming saves, with on-screen keyboard
 *     fallback.
 *   - VGA-aware: 12x canvas zoom on progressive, 10x on interlaced.
 *
 * v0.1 just shows a placeholder so the options-menu mode switch
 * round-trip is testable.
 */
#include "vmu_editor.h"
#include "../ui/bfont_util.h"
#include "../ports/ports.h"

static void vmu_editor_enter(void) {}
static void vmu_editor_leave(void) {}
static void vmu_editor_update(float dt) { (void)dt; }

static void vmu_editor_draw(void)
{
    jt_text_centered(8, JT_COL_YELLOW, JT_COL_BLACK,
                     "VMU Icon Editor");
    jt_text_centered(220, JT_COL_WHITE, JT_COL_BLACK,
                     "Coming in v0.2");
    jt_text_centered(252, JT_COL_GREY, JT_COL_BLACK,
                     "Draw / edit / extract / apply VMU icons");

    /* Show detected VMUs as a tease of the v0.2 file browser. */
    int row = 0;
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        for (int s = 0; s < JT_NUM_SLOTS; s++) {
            if (jt_ports[p].slots[s].kind == JT_SLOT_VMU) {
                jt_text(140, 320 + row * 24, JT_COL_CYAN, JT_COL_BLACK,
                        "VMU detected: Port %c, Slot %d", 'A' + p, s + 1);
                row++;
            }
        }
    }
    if (row == 0) {
        jt_text_centered(320, JT_COL_GREY, JT_COL_BLACK,
                         "(no VMUs detected -- plug one in to preview the editor)");
    }

    jt_text_centered(456, JT_COL_GREEN, JT_COL_BLACK,
                     "Hold Start+Down for options menu");
}

const jt_mode_t jt_mode_vmu_editor = {
    .name   = "VMU Icon Editor",
    .enter  = vmu_editor_enter,
    .leave  = vmu_editor_leave,
    .update = vmu_editor_update,
    .draw   = vmu_editor_draw,
};
