/*
 * ports.c — maple-bus enumeration + per-port state snapshot.
 *
 * Don't cache maple_device_t pointers across frames. The bus
 * re-enumerates on hot-plug, and a stale pointer is undefined
 * behaviour. Re-fetch each tick via maple_enum_type() / _dev() —
 * cheap (a few hundred cycles).
 */
#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/maple/mouse.h>
#include <dc/maple/keyboard.h>
#include <dc/maple/vmu.h>
#include <dc/maple/purupuru.h>
#include <dc/vmufs.h>
#include <string.h>
#include <time.h>

#include "ports.h"

jt_port_state_t jt_ports[JT_NUM_PORTS];

/* VMU block counts come from a maple round-trip that costs ~16ms per
 * read. Refresh only every Nth poll so the rest of the frame loop
 * stays cheap. At 60fps + N=30 this is ~2 reads/sec, plenty for
 * the live tester display. */
#define VMU_REFRESH_INTERVAL 30
static uint32_t poll_counter = 0;

static jt_port_style_t style_from_func(uint32_t func)
{
    if (func & MAPLE_FUNC_CONTROLLER) return JT_STYLE_PAD;
    if (func & MAPLE_FUNC_MOUSE)      return JT_STYLE_MOUSE;
    if (func & MAPLE_FUNC_KEYBOARD)   return JT_STYLE_KEYBOARD;
    if (func & MAPLE_FUNC_LIGHTGUN)   return JT_STYLE_LIGHTGUN;
    return JT_STYLE_OTHER;
}

static jt_slot_kind_t slot_kind_from_func(uint32_t func)
{
    /* Memcard is the dominant slot device; check first. A real VMU
     * also advertises LCD + CLOCK, but we surface those as flags on
     * the VMU slot state rather than as separate kinds. */
    if (func & MAPLE_FUNC_MEMCARD)    return JT_SLOT_VMU;
    if (func & MAPLE_FUNC_PURUPURU)   return JT_SLOT_PURUPURU;
    if (func & MAPLE_FUNC_MICROPHONE) return JT_SLOT_MICROPHONE;
    if (func)                          return JT_SLOT_OTHER;
    return JT_SLOT_EMPTY;
}

void jt_ports_init(void)
{
    memset(jt_ports, 0, sizeof(jt_ports));
}

static void poll_pad(jt_port_state_t *port, maple_device_t *dev)
{
    cont_state_t *st = (cont_state_t *)maple_dev_status(dev);
    if (!st) return;
    port->pad.stick_x  = st->joyx;
    port->pad.stick_y  = st->joyy;
    port->pad.stick2_x = st->joy2x;
    port->pad.stick2_y = st->joy2y;
    port->pad.trig_l   = st->ltrig;
    port->pad.trig_r   = st->rtrig;
    port->pad.buttons  = st->buttons;
}

static void poll_mouse(jt_port_state_t *port, maple_device_t *dev)
{
    mouse_state_t *st = (mouse_state_t *)maple_dev_status(dev);
    if (!st) return;
    port->mouse.dx = st->dx;
    port->mouse.dy = st->dy;
    port->mouse.dz = st->dz;
    port->mouse.abs_x += st->dx;
    port->mouse.abs_y += st->dy;
    port->mouse.buttons = st->buttons;
}

static void poll_kbd(jt_port_state_t *port, maple_device_t *dev)
{
    kbd_state_t *st = (kbd_state_t *)maple_dev_status(dev);
    if (!st) return;
    memcpy(port->kbd.scancodes, st->matrix, sizeof(port->kbd.scancodes));
    port->kbd.modifiers = st->shift_keys;
}

void jt_ports_poll(void)
{
    poll_counter++;
    bool refresh_vmu_blocks = (poll_counter % VMU_REFRESH_INTERVAL) == 0;

    for (int p = 0; p < JT_NUM_PORTS; p++) {
        jt_port_state_t *port = &jt_ports[p];

        /* Primary device on this port (controller / mouse / kbd /
         * gun) lives at unit 0. */
        maple_device_t *dev = maple_enum_dev(p, 0);
        if (!dev || !dev->valid) {
            port->present = false;
            port->style = JT_STYLE_EMPTY;
            port->func_mask = 0;
            memset(&port->pad, 0, sizeof(port->pad));
            memset(&port->mouse, 0, sizeof(port->mouse));
            memset(&port->kbd, 0, sizeof(port->kbd));
        } else {
            port->present = true;
            port->func_mask = dev->info.functions;
            port->style = style_from_func(dev->info.functions);
            switch (port->style) {
                case JT_STYLE_PAD:      poll_pad(port, dev); break;
                case JT_STYLE_MOUSE:    poll_mouse(port, dev); break;
                case JT_STYLE_KEYBOARD: poll_kbd(port, dev); break;
                default: break;
            }
        }

        /* Expansion slots. Maple subdevices live at unit 1+. */
        for (int s = 0; s < JT_NUM_SLOTS; s++) {
            jt_slot_state_t *slot = &port->slots[s];
            maple_device_t *sub = maple_enum_dev(p, s + 1);
            if (!sub || !sub->valid) {
                slot->kind = JT_SLOT_EMPTY;
                slot->block_free = slot->block_total = -1;
                slot->has_lcd = slot->has_clock = false;
                continue;
            }
            slot->kind = slot_kind_from_func(sub->info.functions);
            slot->has_lcd   = (sub->info.functions & MAPLE_FUNC_LCD)   != 0;
            slot->has_clock = (sub->info.functions & MAPLE_FUNC_CLOCK) != 0;

            /* VMU block counts. vmufs_root_read + vmufs_free_blocks
             * cost a maple round-trip each. Refresh on first detect
             * (block_total == -1) and on the throttled cadence; the
             * result rarely changes mid-frame anyway. */
            if (slot->kind == JT_SLOT_VMU &&
                (refresh_vmu_blocks || slot->block_total < 0)) {
                vmu_root_t root;
                if (vmufs_root_read(sub, &root) == 0) {
                    slot->block_total = root.blk_cnt;
                } else {
                    slot->block_total = -1;
                }
                int fb = vmufs_free_blocks(sub);
                slot->block_free = (fb >= 0) ? (int16_t)fb : -1;
            } else if (slot->kind != JT_SLOT_VMU) {
                slot->block_free  = -1;
                slot->block_total = -1;
            }
        }
    }
}

const char *jt_port_style_name(jt_port_style_t s)
{
    switch (s) {
        case JT_STYLE_EMPTY:    return "Empty";
        case JT_STYLE_PAD:      return "Pad";
        case JT_STYLE_MOUSE:    return "Mouse";
        case JT_STYLE_KEYBOARD: return "Keyboard";
        case JT_STYLE_LIGHTGUN: return "LightGun";
        default:                return "Other";
    }
}

const char *jt_slot_kind_name(jt_slot_kind_t k)
{
    switch (k) {
        case JT_SLOT_EMPTY:      return "---";
        case JT_SLOT_VMU:        return "VMU";
        case JT_SLOT_PURUPURU:   return "Purupuru";
        case JT_SLOT_MICROPHONE: return "Mic";
        default:                 return "Other";
    }
}
