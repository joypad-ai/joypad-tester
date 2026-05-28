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
#include "../vms/apply.h"   /* jt_vmu_show_stored_icon */

/* Per-port LCD mirror state (definitions used by the render path
 * below; declared up here so tester_enter can reset them). */
static char lcd_buf[48 * 32 / 8];
static char lcd_last[JT_NUM_PORTS][48 * 32 / 8];
static bool lcd_last_valid[JT_NUM_PORTS];
static int  lcd_cooldown[JT_NUM_PORTS];

/* Per-port/slot rumble pacing. Real Purupuru packs need each effect
 * packet to be left alone long enough for the motor to actually spin
 * up; sending one every frame (60Hz) kept resetting the envelope and
 * the motor never moved. Refire only every RUMBLE_REPEAT_FRAMES. */
#define RUMBLE_REPEAT_FRAMES 30   /* ~0.5s @ 60fps -- matches Basic Thud */
static int rumble_cooldown[JT_NUM_PORTS][JT_NUM_SLOTS];

static void tester_enter(void)
{
    /* Force a fresh LCD push on (re-)entry -- another mode may have
     * drawn to the VMU screens in the meantime. */
    for (int p = 0; p < JT_NUM_PORTS; p++) lcd_last_valid[p] = false;
}

static void tester_leave(void)
{
    /* Revert each slot-1 VMU's LCD back to its stored ICONDATA icon
     * so the next mode sees the "natural" display rather than the
     * frozen last-frame of button state. */
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        if (jt_ports[p].slots[0].kind != JT_SLOT_VMU) continue;
        if (!jt_ports[p].slots[0].has_lcd) continue;
        jt_vmu_show_stored_icon(p, 0);
        lcd_last_valid[p] = false;
    }
}

/* ---- live controller-state mirror on a slot-1 VMU LCD (48x32) ----
 *
 * We render the pad's button/d-pad/trigger/stick state into a standard
 * XBM bitmap (LSB-first, top-left origin) and push it to the VMU in
 * slot 1 via vmu_draw_lcd_xbm (which converts XBM -> the VMU's native
 * orientation, so left/right read correctly). Updates are gated on a
 * change + a small cooldown so we don't flood the maple bus.
 * (State arrays are declared above tester_enter.) */
static void lcd_set(char *b, int x, int y)
{
    if (x < 0 || x >= 48 || y < 0 || y >= 32) return;
    b[y * 6 + (x / 8)] |= (char)(1 << (x % 8));
}

static void lcd_box(char *b, int x0, int y0, int w, int h)
{
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++) lcd_set(b, x, y);
}

static void lcd_frame(char *b, int x0, int y0, int w, int h)
{
    for (int x = x0; x < x0 + w; x++) { lcd_set(b, x, y0); lcd_set(b, x, y0 + h - 1); }
    for (int y = y0; y < y0 + h; y++) { lcd_set(b, x0, y); lcd_set(b, x0 + w - 1, y); }
}

/* Draw the live pad state into a 48x32 XBM buffer. Outlines are always
 * shown so the layout is readable at rest; shapes fill when pressed. */
static void render_buttons_to_lcd(char *b, const jt_pad_state_t *pad)
{
    memset(b, 0, 48 * 32 / 8);
    uint32_t btn = pad->buttons;

    /* Trigger bars across the top (L left-fill, R right-fill). */
    lcd_frame(b, 1, 0, 22, 3);
    lcd_frame(b, 25, 0, 22, 3);
    int lw = pad->trig_l * 20 / 255;
    int rw = pad->trig_r * 20 / 255;
    if (lw > 0) lcd_box(b, 2, 1, lw, 1);
    if (rw > 0) lcd_box(b, 46 - rw, 1, rw, 1);

    /* D-pad cross (left), centered ~ (9,18). */
    int dx = 9, dy = 18;
    lcd_frame(b, dx - 2, dy - 8, 5, 6);
    lcd_frame(b, dx - 2, dy + 4, 5, 6);
    lcd_frame(b, dx - 8, dy - 2, 6, 5);
    lcd_frame(b, dx + 4, dy - 2, 6, 5);
    if (btn & CONT_DPAD_UP)    lcd_box(b, dx - 1, dy - 7, 3, 4);
    if (btn & CONT_DPAD_DOWN)  lcd_box(b, dx - 1, dy + 5, 3, 4);
    if (btn & CONT_DPAD_LEFT)  lcd_box(b, dx - 7, dy - 1, 4, 3);
    if (btn & CONT_DPAD_RIGHT) lcd_box(b, dx + 5, dy - 1, 4, 3);

    /* Face buttons diamond (right), centered ~ (38,18). */
    int fx = 38, fy = 18;
    lcd_frame(b, fx - 2, fy - 8, 5, 6);    /* Y top */
    lcd_frame(b, fx - 2, fy + 4, 5, 6);    /* A bottom */
    lcd_frame(b, fx - 8, fy - 2, 6, 5);    /* X left */
    lcd_frame(b, fx + 4, fy - 2, 6, 5);    /* B right */
    if (btn & CONT_Y) lcd_box(b, fx - 1, fy - 7, 3, 4);
    if (btn & CONT_A) lcd_box(b, fx - 1, fy + 5, 3, 4);
    if (btn & CONT_X) lcd_box(b, fx - 7, fy - 1, 4, 3);
    if (btn & CONT_B) lcd_box(b, fx + 5, fy - 1, 4, 3);

    /* Analog stick: dot inside a small box, lower-center. */
    lcd_frame(b, 19, 12, 11, 11);
    int px = 24 + pad->stick_x * 3 / 128;
    int py = 17 + pad->stick_y * 3 / 128;
    lcd_box(b, px, py, 2, 2);

    /* Start: small box, bottom-center. */
    lcd_frame(b, 20, 27, 8, 4);
    if (btn & CONT_START) lcd_box(b, 21, 28, 6, 2);
}

/* Convert our LSB-first upright 48x32 buffer to the VMU LCD's native
 * format: MSB-first per byte, H+V flipped (matches vmu_xbm_to_bitmap's
 * output, which is what vmu_draw_lcd expects). The xbm helper we used
 * before assumed a char-per-pixel ASCII string and silently produced
 * garbage from our packed bytes -- on emu Flycast ignored it, on real
 * hardware the LCD stayed blank. */
static void lcd_buf_to_native(uint8_t *out, const char *in)
{
    memset(out, 0, 48 * 32 / 8);
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 48; x++) {
            if (!((in[y * 6 + (x / 8)] >> (x % 8)) & 1)) continue;
            int dy = 31 - y, dx = 47 - x;
            out[dy * 6 + (dx / 8)] |= 0x80 >> (dx % 8);
        }
    }
}

/* Render + push the live pad state to the slot-1 VMU's LCD if present.
 * Gated on change + cooldown to bound maple traffic (~20Hz worst case). */
static void drive_slot1_lcd(int port_idx, const jt_pad_state_t *pad)
{
    if (lcd_cooldown[port_idx] > 0) lcd_cooldown[port_idx]--;

    render_buttons_to_lcd(lcd_buf, pad);
    bool changed = !lcd_last_valid[port_idx] ||
                   memcmp(lcd_buf, lcd_last[port_idx], sizeof(lcd_buf)) != 0;
    if (!changed || lcd_cooldown[port_idx] > 0) return;

    maple_device_t *dev = maple_enum_dev(port_idx, 1);  /* slot 1 = unit 1 */
    if (!dev || !dev->valid) return;
    if (!(dev->info.functions & MAPLE_FUNC_LCD)) return;

    uint8_t native[48 * 32 / 8];
    lcd_buf_to_native(native, lcd_buf);
    vmu_draw_lcd(dev, native);
    memcpy(lcd_last[port_idx], lcd_buf, sizeof(lcd_buf));
    lcd_last_valid[port_idx] = true;
    lcd_cooldown[port_idx] = 3;   /* ~20Hz cap */
}

static void actuate_rumble(int port_idx, int slot_idx)
{
    /* Subdevice index = slot_idx + 1 (unit 0 is the primary device). */
    maple_device_t *dev = maple_enum_dev(port_idx, slot_idx + 1);
    if (!dev || !dev->valid) return;
    if (!(dev->info.functions & MAPLE_FUNC_PURUPURU)) return;
    /* "Basic Thud" envelope from KOS's rumble example: short ~0.5s jolt
     * the motor actually has time to spin up. Old values (freq=0x20,
     * inc=0x20) plus 60Hz refire kept restarting the envelope and the
     * real pack never moved -- worked in emu only because Flycast
     * triggers an instant pulse per packet regardless of envelope. */
    purupuru_effect_t effect = { .raw = 0 };
    effect.motor = 1;       /* select motor 1 (required nonzero) */
    effect.fpow  = 7;       /* forward intensity (0-7), max */
    effect.freq  = 26;      /* vibration frequency (4..59) */
    effect.inc   = 1;       /* inclination period -- short jolt */
    purupuru_rumble(dev, &effect);
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
                /* Refire on the cadence the envelope expects; mark the
                 * slot as active every frame so the UI label stays lit
                 * even between actual packets. */
                if (rumble_cooldown[p][s] == 0) {
                    actuate_rumble(p, s);
                    rumble_cooldown[p][s] = RUMBLE_REPEAT_FRAMES;
                } else {
                    rumble_cooldown[p][s]--;
                }
                slot->rumble_active = true;
            } else if (slot->kind == JT_SLOT_PURUPURU) {
                rumble_cooldown[p][s] = 0;   /* ready to fire immediately on next press */
            }
            /* Slot 1 (index 0) VMU continuously mirrors this controller's
             * live button state onto its LCD -- no hold needed. */
            if (s == 0 && slot->kind == JT_SLOT_VMU && slot->has_lcd) {
                drive_slot1_lcd(p, &port->pad);
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

    /* Type column. Map the maple-reported product_name to a short
     * label that fits the 8-char budget so every known DC controller
     * subtype is visually distinct -- the standard pad shouldn't read
     * the same as a twin stick or arcade stick. Unknown product names
     * fall through and get truncated. Non-pad devices use the style
     * name (Mouse / Keyboard / LightGun / etc.). */
    const char *pn = port->product_name;
    const char *type_label = jt_port_style_name(port->style);
    if (port->style == JT_STYLE_PAD && pn[0]) {
        if      (strstr(pn, "Dreamcast Controller")) type_label = "DC Pad";
        else if (strstr(pn, "Twin Stick"))           type_label = "TwinStk";
        else if (strstr(pn, "Arcade Stick"))         type_label = "ArcStk";
        else if (strstr(pn, "Ascii Stick") ||
                 strstr(pn, "ASCII Stick"))          type_label = "AsciiSt";
        else if (strstr(pn, "Mission Stick"))        type_label = "MissStk";
        else if (strstr(pn, "Fishing"))              type_label = "FishCtl";
        else if (strstr(pn, "Racing") ||
                 strstr(pn, "Wheel"))                type_label = "Wheel";
        else if (strstr(pn, "Pop'n") ||
                 strstr(pn, "Popn"))                 type_label = "Pop'n";
        else if (strstr(pn, "Densha"))               type_label = "Densha";
        else if (strstr(pn, "Maracas"))              type_label = "Maracas";
        else if (strstr(pn, "DreamConn"))            type_label = "DConn";
        else if (strstr(pn, "Beat Mania") ||
                 strstr(pn, "Beatmania"))            type_label = "BeatMan";
        else if (strstr(pn, "Samba"))                type_label = "Samba";
        else if (strstr(pn, "Guitar"))               type_label = "Guitar";
        else                                         type_label = pn;
    }
    jt_text(8, y, JT_COL_WHITE, JT_COL_BLACK,
            "Port %c  Type:%-8.8s  S1:%-8.8s  S2:%-8.8s",
            port_label, type_label, slot1, slot2);

    switch (port->style) {
        case JT_STYLE_PAD: {
            jt_pad_state_t *pad = &port->pad;
            jt_text(8, y + 24, JT_COL_CYAN, JT_COL_BLACK,
                    "Stick: %+04d,%+04d  Trig: L%03u R%03u",
                    pad->stick_x, pad->stick_y, pad->trig_l, pad->trig_r);
            /* All 7 face buttons (A/B/C/D/X/Y/Z) + Start + 4-way D-pad.
             * Labels compacted to fit a 53-char single-row budget --
             * 'Du/Dd/Dl/Dr' for the D-pad to keep 'D' free for the
             * separate D button some sticks/fishing pads expose. */
            jt_text(8, y + 48, JT_COL_CYAN, JT_COL_BLACK,
                    "A:%d B:%d C:%d D:%d X:%d Y:%d Z:%d St:%d Du:%d Dd:%d Dl:%d Dr:%d",
                    !!(pad->buttons & CONT_A),
                    !!(pad->buttons & CONT_B),
                    !!(pad->buttons & CONT_C),
                    !!(pad->buttons & CONT_D),
                    !!(pad->buttons & CONT_X),
                    !!(pad->buttons & CONT_Y),
                    !!(pad->buttons & CONT_Z),
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
    jt_text_centered(24, JT_COL_YELLOW, JT_COL_BLACK,
                     "Joypad Tester - Dreamcast v" JT_VERSION_STR);

    /* 4 port rows pulled into the CRT-safe band: start at y=52 (below
     * the header) on a 92px stride so the last row's optional RTC line
     * still clears the footer at y=428. Header (3 lines) + RTC = 96px,
     * but RTC is rare, so 92 stride reads fine. */
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        draw_port_row(p, 52 + p * 92);
    }

    jt_text_centered(428, JT_COL_GREEN, JT_COL_BLACK,
                     "Hold Start+Down for options menu");
}

const jt_mode_t jt_mode_tester = {
    .name   = "Controller Tester",
    .enter  = tester_enter,
    .leave  = tester_leave,
    .update = tester_update,
    .draw   = tester_draw,
};
