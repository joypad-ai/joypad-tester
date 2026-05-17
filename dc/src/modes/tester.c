/*
 * tester.c — controller tester mode.
 *
 * The canonical Joypad Tester view: all 4 maple ports rendered live,
 * simultaneously, with both expansion slots probed. Matches the
 * gcn/pce layout convention so the testers feel like a set.
 *
 * Per-port hold-actions (mirrors gcn/ tester):
 *   Hold A on a port with a Purupuru pack -> rumble it.
 *   Hold B on a port with a VMU -> push logo to its LCD.
 *   Hold Y on a port with a VMU -> read its clock subdevice.
 *
 * v0.1 implements the display + Purupuru rumble path. LCD-draw and
 * clock-read are wired stubs (slot flags update, but the maple
 * round-trip is deferred to v0.2 where the VMU editor needs them
 * authoritatively).
 */
#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/maple/purupuru.h>
#include <stdio.h>
#include <string.h>

#include "tester.h"
#include "../ports/ports.h"
#include "../ui/bfont_util.h"

static void tester_enter(void) { /* nothing yet */ }
static void tester_leave(void) { /* nothing yet */ }

static void actuate_rumble(int port_idx, int slot_idx)
{
    /* Subdevice index = slot_idx + 1 (unit 0 is the primary device). */
    maple_device_t *dev = maple_enum_dev(port_idx, slot_idx + 1);
    if (!dev || !dev->valid) return;
    if (!(dev->info.functions & MAPLE_FUNC_PURUPURU)) return;
    /* purupuru_rumble_raw takes a 32-bit pattern: duration/intensity
     * in the standard Sega encoding. 0x10F419F0 is a moderate
     * short pulse — same value documented in KOS purupuru example. */
    purupuru_rumble_raw(dev, 0x10F419F0);
}

static void tester_update(float dt)
{
    (void)dt;
    /* Per-port hold checks. Only act on slots that report the
     * matching capability so we don't poke incompatible devices. */
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        jt_port_state_t *port = &jt_ports[p];
        if (!port->present || port->style != JT_STYLE_PAD) continue;
        uint32_t btns = port->pad.buttons;

        for (int s = 0; s < JT_NUM_SLOTS; s++) {
            jt_slot_state_t *slot = &port->slots[s];
            slot->rumble_active = false;
            slot->lcd_test_active = false;
            slot->clock_test_active = false;

            if (slot->kind == JT_SLOT_PURUPURU && (btns & CONT_A)) {
                actuate_rumble(p, s);
                slot->rumble_active = true;
            }
            if (slot->kind == JT_SLOT_VMU && slot->has_lcd && (btns & CONT_B)) {
                /* LCD write stub: slot flag flips, v0.2 wires the
                 * actual vmu_draw_lcd_xbm() with the Joypad logo. */
                slot->lcd_test_active = true;
            }
            if (slot->kind == JT_SLOT_VMU && slot->has_clock && (btns & CONT_Y)) {
                slot->clock_test_active = true;
            }
        }
    }
}

static void draw_port_row(int p, int y)
{
    jt_port_state_t *port = &jt_ports[p];
    const char port_label = 'A' + p;

    char slot1[32], slot2[32];
    snprintf(slot1, sizeof(slot1), "%s%s",
             jt_slot_kind_name(port->slots[0].kind),
             port->slots[0].rumble_active ? "*" : "");
    snprintf(slot2, sizeof(slot2), "%s%s",
             jt_slot_kind_name(port->slots[1].kind),
             port->slots[1].rumble_active ? "*" : "");

    /* Format budget: 640px / 12px-per-char = 53 chars max per row.
     * Compact labels (Sty/S1/S2) + 8-char fields = exactly 50 chars.
     * `%-8.8s` truncates anything over 8 chars (e.g. "Purupuru*" = 9). */
    jt_text(8, y, JT_COL_WHITE, JT_COL_BLACK,
            "Port %c  Sty:%-8.8s  S1:%-8.8s  S2:%-8.8s",
            port_label, jt_port_style_name(port->style), slot1, slot2);

    switch (port->style) {
        case JT_STYLE_PAD: {
            jt_pad_state_t *pad = &port->pad;
            jt_text(8, y + 24, JT_COL_CYAN, JT_COL_BLACK,
                    "Stick: %+04d,%+04d  Trig: L%03u R%03u",
                    pad->stick_x, pad->stick_y, pad->trig_l, pad->trig_r);
            jt_text(8, y + 48, JT_COL_CYAN, JT_COL_BLACK,
                    "A:%d B:%d X:%d Y:%d Start:%d  D-U:%d D-D:%d D-L:%d D-R:%d",
                    !!(pad->buttons & CONT_A),
                    !!(pad->buttons & CONT_B),
                    !!(pad->buttons & CONT_X),
                    !!(pad->buttons & CONT_Y),
                    !!(pad->buttons & CONT_START),
                    !!(pad->buttons & CONT_DPAD_UP),
                    !!(pad->buttons & CONT_DPAD_DOWN),
                    !!(pad->buttons & CONT_DPAD_LEFT),
                    !!(pad->buttons & CONT_DPAD_RIGHT));
            break;
        }
        case JT_STYLE_MOUSE: {
            jt_mouse_state_t *m = &port->mouse;
            jt_text(8, y + 24, JT_COL_CYAN, JT_COL_BLACK,
                    "Pos: %+05ld,%+05ld  Wheel: %+02ld  L:%d R:%d M:%d",
                    (long)m->abs_x, (long)m->abs_y, (long)m->dz,
                    !!(m->buttons & MOUSE_LEFTBUTTON),
                    !!(m->buttons & MOUSE_RIGHTBUTTON),
                    !!(m->buttons & MOUSE_SIDEBUTTON));
            break;
        }
        case JT_STYLE_KEYBOARD: {
            char codes[64] = "";
            int n = 0;
            for (int i = 0; i < 6; i++) {
                if (port->kbd.scancodes[i]) {
                    n += snprintf(codes + n, sizeof(codes) - n,
                                  "%02X ", port->kbd.scancodes[i]);
                }
            }
            jt_text(8, y + 24, JT_COL_CYAN, JT_COL_BLACK,
                    "Scancodes: %-30s Mods: %02X",
                    codes, port->kbd.modifiers);
            break;
        }
        default: break;
    }
}

static void tester_draw(void)
{
    jt_text_centered(8, JT_COL_YELLOW, JT_COL_BLACK,
                     "Joypad Tester - Dreamcast v" JT_VERSION_STR);

    /* 4 port rows, ~112 px tall each: header (24) + 2 detail lines
     * (24+24) + 8 px gap, lands the last row above the help footer. */
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        draw_port_row(p, 40 + p * 100);
    }

    jt_text_centered(456, JT_COL_GREEN, JT_COL_BLACK,
                     "Hold Start+Down for options menu");
}

const jt_mode_t jt_mode_tester = {
    .name   = "Controller Tester",
    .enter  = tester_enter,
    .leave  = tester_leave,
    .update = tester_update,
    .draw   = tester_draw,
};
