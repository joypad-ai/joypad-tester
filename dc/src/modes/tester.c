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
#include <dc/maple/vmu.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "tester.h"
#include "../ports/ports.h"
#include "../ui/bfont_util.h"

static void tester_enter(void) { /* nothing yet */ }
static void tester_leave(void) { /* nothing yet */ }

/* 48x32 1bpp test pattern pushed to the VMU LCD when the user holds B.
 * Generated once at first invocation: filled outer border + a centered
 * "JT" wordmark. Each row is 6 bytes (LSB-first per XBM convention). */
static char vmu_lcd_pattern[48 * 32 / 8];
static int  vmu_lcd_pattern_ready = 0;

static void set_pixel(int x, int y)
{
    if (x < 0 || x >= 48 || y < 0 || y >= 32) return;
    vmu_lcd_pattern[y * 6 + (x / 8)] |= (1 << (x % 8));
}

static void build_vmu_lcd_pattern(void)
{
    memset(vmu_lcd_pattern, 0, sizeof(vmu_lcd_pattern));
    /* Solid 1px outer frame. */
    for (int x = 0; x < 48; x++) { set_pixel(x, 0); set_pixel(x, 31); }
    for (int y = 0; y < 32; y++) { set_pixel(0, y); set_pixel(47, y); }
    /* "J": a 7x14 letter at x=10..16, y=9..22. */
    for (int x = 10; x <= 16; x++) set_pixel(x, 9);    /* top bar */
    for (int y = 9;  y <= 19; y++) set_pixel(15, y);   /* stem */
    for (int x = 10; x <= 15; x++) set_pixel(x, 22);   /* bottom curl */
    set_pixel(10, 21);
    set_pixel(10, 20);
    /* "T": 7x14 letter at x=24..30, y=9..22. */
    for (int x = 24; x <= 30; x++) set_pixel(x, 9);    /* top bar */
    for (int y = 9;  y <= 22; y++) set_pixel(27, y);   /* stem */
    vmu_lcd_pattern_ready = 1;
}

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

static void actuate_lcd(int port_idx, int slot_idx)
{
    maple_device_t *dev = maple_enum_dev(port_idx, slot_idx + 1);
    if (!dev || !dev->valid) return;
    if (!(dev->info.functions & MAPLE_FUNC_LCD)) return;
    if (!vmu_lcd_pattern_ready) build_vmu_lcd_pattern();
    /* vmu_draw_lcd takes a raw bitmap (192 bytes for 48x32 @ 1bpp). */
    vmu_draw_lcd(dev, vmu_lcd_pattern);
}

static void read_rtc(int port_idx, int slot_idx, jt_slot_state_t *slot)
{
    maple_device_t *dev = maple_enum_dev(port_idx, slot_idx + 1);
    if (!dev || !dev->valid) return;
    if (!(dev->info.functions & MAPLE_FUNC_CLOCK)) return;
    time_t unix_time = 0;
    if (vmu_get_datetime(dev, &unix_time) != 0) return;
    struct tm *t = localtime(&unix_time);
    if (!t) return;
    slot->rtc_year = t->tm_year + 1900;
    slot->rtc_mon  = t->tm_mon  + 1;
    slot->rtc_day  = t->tm_mday;
    slot->rtc_hour = t->tm_hour;
    slot->rtc_min  = t->tm_min;
    slot->rtc_sec  = t->tm_sec;
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
                actuate_lcd(p, s);
                slot->lcd_test_active = true;
            }
            if (slot->kind == JT_SLOT_VMU && slot->has_clock && (btns & CONT_Y)) {
                read_rtc(p, s, slot);
                slot->clock_test_active = true;
            }
        }
    }
}

static void format_slot_label(char *buf, size_t cap, jt_slot_state_t *slot)
{
    /* 8-char budget. Pack the most informative summary per kind:
     *   VMU + known free blocks -> "VMU NNN " (free count)
     *   VMU, blocks unknown     -> "VMU"
     *   Purupuru (rumble live)  -> "Puru*"
     *   anything else           -> kind name (truncated by %-8.8s) */
    if (slot->kind == JT_SLOT_VMU && slot->block_free >= 0) {
        snprintf(buf, cap, "VMU %d", slot->block_free);
    } else if (slot->kind == JT_SLOT_PURUPURU) {
        snprintf(buf, cap, "Puru%s", slot->rumble_active ? "*" : "");
    } else {
        snprintf(buf, cap, "%s", jt_slot_kind_name(slot->kind));
    }
}

static void draw_port_row(int p, int y)
{
    jt_port_state_t *port = &jt_ports[p];
    const char port_label = 'A' + p;

    char slot1[16], slot2[16];
    format_slot_label(slot1, sizeof(slot1), &port->slots[0]);
    format_slot_label(slot2, sizeof(slot2), &port->slots[1]);

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

    /* Optional 3rd detail line: surface RTC reading from any VMU slot
     * on this port whose clock test just fired. Mouse / keyboard styles
     * already use y+24; this lives at y+72 so it never collides. */
    for (int s = 0; s < JT_NUM_SLOTS; s++) {
        jt_slot_state_t *slot = &port->slots[s];
        if (slot->kind == JT_SLOT_VMU && slot->has_clock && slot->clock_test_active) {
            jt_text(8, y + 72, JT_COL_GREEN, JT_COL_BLACK,
                    "S%d RTC: %04d-%02d-%02d %02d:%02d:%02d",
                    s + 1,
                    slot->rtc_year, slot->rtc_mon,  slot->rtc_day,
                    slot->rtc_hour, slot->rtc_min,  slot->rtc_sec);
            break;
        }
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
