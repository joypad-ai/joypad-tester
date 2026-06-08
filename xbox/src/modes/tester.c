/*
 * tester.c -- per-port controller display.
 *
 * pbkit's pb_print is line-buffered text into a fixed-pitch overlay,
 * so we just emit four port blocks one after another. Matches the
 * dc/ tester's spirit (every port simultaneously, dense per-row info)
 * within pb_print's tighter character grid.
 *
 * Holding A on a connected pad rumbles that pad for as long as A is
 * held -- one shot per rising edge, with a stop call on the falling
 * edge -- following the dc/ "continuous rumble" semantics.
 */
#include "tester.h"

#include <pbkit/pbkit.h>
#include <stdbool.h>

#include "../ports/ports.h"

static bool rumble_on[JT_NUM_PORTS];

/* '#' for held buttons, '-' for not. Compact enough that all the
 * interesting digital state fits in two short lines per port. */
static char hot(uint32_t mask, uint32_t bit) { return (mask & bit) ? '#' : '-'; }

static const char *style_label(jt_port_style_t s)
{
    switch (s) {
        case JT_STYLE_EMPTY: return "----";
        case JT_STYLE_PAD:   return "Pad ";
        case JT_STYLE_OTHER: return "Othr";
    }
    return "?   ";
}

static void draw_port(int p)
{
    jt_port_state_t *port = &jt_ports[p];
    char port_label = 'A' + p;

    if (!port->present) {
        pb_print("Port %c  [%s]\n\n", port_label, style_label(port->style));
        return;
    }

    /* Title row -- port letter, style, product name (truncated), and
     * the vendor/product IDs so subtype identification is unambiguous
     * even when the product_name is generic. */
    pb_print("Port %c  [%s] %-32.32s  v=%04x p=%04x\n",
             port_label, style_label(port->style),
             port->product_name[0] ? port->product_name : "(no name)",
             port->vendor_id, port->product_id);

    /* Digital buttons. Match the on-pad layout where possible: face
     * buttons together, then shoulders, then Back/Start, then sticks
     * clickables, then the dpad. */
    uint32_t b = port->pad.buttons;
    pb_print("  A%c B%c X%c Y%c   W%c K%c   Bk%c St%c   L3%c R3%c   "
             "D:%c%c%c%c\n",
             hot(b, JT_BTN_A),     hot(b, JT_BTN_B),
             hot(b, JT_BTN_X),     hot(b, JT_BTN_Y),
             hot(b, JT_BTN_WHITE), hot(b, JT_BTN_BLACK),
             hot(b, JT_BTN_BACK),  hot(b, JT_BTN_START),
             hot(b, JT_BTN_LSTICK), hot(b, JT_BTN_RSTICK),
             hot(b, JT_BTN_DPAD_UP), hot(b, JT_BTN_DPAD_DOWN),
             hot(b, JT_BTN_DPAD_LEFT), hot(b, JT_BTN_DPAD_RIGHT));

    /* Sticks + triggers. Stick values are signed 16-bit (SDL convention);
     * triggers are downscaled to 0..255 by ports.c for display parity
     * with dc/. */
    pb_print("  LS %+6d,%+6d  RS %+6d,%+6d   LT %3u  RT %3u\n",
             port->pad.stick_lx, port->pad.stick_ly,
             port->pad.stick_rx, port->pad.stick_ry,
             port->pad.trig_l, port->pad.trig_r);

    /* Expansion slots -- v0.1.0 placeholder until MU detection. */
    pb_print("  Slot1 [%s]  Slot2 [%s]\n\n",
             jt_slot_kind_name(port->slots[0].kind),
             jt_slot_kind_name(port->slots[1].kind));
}

void jt_tester_draw(void)
{
    pb_print("Joypad Tester  Xbox  v0.1.0\n\n");
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        draw_port(p);
    }

    /* Continuous rumble while A is held: send the start command on
     * the rising edge, a stop on the falling edge. SDL handles the
     * actual envelope; we don't refire each frame. */
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        if (!jt_ports[p].present) {
            if (rumble_on[p]) {
                jt_port_rumble(p, 0, 0, 0);
                rumble_on[p] = false;
            }
            continue;
        }
        bool want = (jt_ports[p].pad.buttons & JT_BTN_A) != 0;
        if (want && !rumble_on[p]) {
            /* Max strong, no weak, ~10 second budget that we'll cut
             * short ourselves on release. */
            jt_port_rumble(p, 0xFFFF, 0, 10000);
            rumble_on[p] = true;
        } else if (!want && rumble_on[p]) {
            jt_port_rumble(p, 0, 0, 0);
            rumble_on[p] = false;
        }
    }
}
