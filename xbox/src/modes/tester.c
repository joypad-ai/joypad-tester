/*
 * tester.c -- the joypad-tester landing screen for the OG Xbox.
 *
 * Layout follows PORTING.md §3.3 (per-port readout, fields stay at
 * zero, never blanked, decode known device subtypes to a friendly
 * label). Five rows per port:
 *
 *   header     port letter + type + Slot1 + Slot2 + Rumble
 *   sticks     L stick + R stick + LT + RT
 *   buttons    digital A/B/X/Y/Bk/Wh/Back/Start + L3 + R3
 *   pressure   0..255 pressure for the 6 analog face buttons
 *   dpad       4 dpad directions
 *
 * Centered title at the top, footer hint at the bottom. HELD color
 * on held buttons, ACTIVE on connected port headers, DIM on unheld
 * labels.
 *
 * Holding A continuously rumbles the pad that's holding it -- start
 * on the rising edge, stop on the falling edge (matches dc/'s
 * Purupuru behaviour after the cont=1 fix).
 *
 * Options menu opens on Start+D-pad-Down (so plain Start stays a
 * testable button here -- PORTING.md §3.5).
 */
#include "tester.h"

#include <stdio.h>
#include <string.h>
#include <hal/xbox.h>

#include "../ports/ports.h"
#include "../ports/accessories.h"
#include "../ui/bfont.h"
#include "../ui/colors.h"
#include "../ui/options_menu.h"

/* CRT-safe layout: standard NTSC/PAL safe-area is ~6% inset all
 * around (~38 px in 640x480). 32 px is a safe round number every
 * consumer CRT respects. Text is rendered at x=SAFE_X to keep the
 * leftmost glyph clear of the overscan; text_centered already pulls
 * from jt_video_width() so it's centered against the full surface --
 * the centered title naturally lands inside the safe area too. */
/* CRT overscan can hide up to ~10% of each edge on a poorly-adjusted
 * TV. 32 px got us partway in but real hardware reported the footer
 * still clipped; 48 px (~10% of 480 / ~7.5% of 640) is the standard
 * TV-safe inset homebrew docs converge on. */
#define SAFE_X      48
#define SAFE_TOP    48
#define SAFE_BOTTOM 48
#define ROW_H       16

#define TITLE_Y      (SAFE_TOP)             /* y=32  */
#define PORT_TOP     (TITLE_Y + ROW_H + 12) /* y=64  */
#define PORT_PITCH   (ROW_H * 5)            /* 4 rows + 1-row gap = 80 px */

/* Quick-reboot to the dashboard combo: Back + Black + LT + RT held
 * for ~1 second on any pad, mirroring the standard OG-Xbox in-game
 * combo. nxdk's HalReturnToFirmware(HalQuickRebootRoutine) is the
 * canonical "return to dashboard" call. */
static int reboot_combo_hold_frames;
#define REBOOT_COMBO_FRAMES 60

/* Per-port last-sent rumble levels so the header can read them
 * without recomputing, and so we can skip SDL_GameControllerRumble
 * calls when nothing changed. A button pressure drives the left
 * (heavy) motor; B drives the right (light) one -- doubles as a
 * pressure-sensitivity demo. */
static uint16_t rumble_left[JT_NUM_PORTS];
static uint16_t rumble_right[JT_NUM_PORTS];
/* Daisy pad rumble levels, in parallel with the primary pad's. */
static uint16_t rumble_left_daisy[JT_NUM_PORTS];
static uint16_t rumble_right_daisy[JT_NUM_PORTS];
static uint32_t prev_btns_aggregate;

/* On-screen mouse cursor + cumulative wheel total. Position integrates
 * dx/dy drained from accessories.c's per-mouse motion accumulator
 * each frame; we use floats so sub-pixel motion at low gain doesn't
 * round to zero. wheel_cumul shows that scroll bytes are arriving
 * even when each report's wheel byte returns to 0 between ticks. */
static struct {
    bool    active;
    float   x, y;
    int32_t wheel_cumul;
} jt_cursor;

/* 8x12 classic arrow silhouette, MSB-first per byte (jt_blit_mask
 * format). Rendered in white over a 10x14 black backing rect so it
 * stays visible on any underlying color. */
static const uint8_t cursor_mask[12] = {
    0x80, /* X.......  */
    0xC0, /* XX......  */
    0xE0, /* XXX.....  */
    0xF0, /* XXXX....  */
    0xF8, /* XXXXX...  */
    0xFC, /* XXXXXX..  */
    0xFE, /* XXXXXXX.  */
    0xF8, /* XXXXX...  */
    0xB0, /* X.XX....  */
    0x30, /* ..XX....  */
    0x18, /* ...XX...  */
    0x18, /* ...XX...  */
};

/* Label by XID bType/bSubType -- nxdk pins SDL_JoystickInstanceID to
 * xid_dev->uid, so we can match cheaply. This is more reliable than
 * VID/PID + SDL_GameControllerName (which renders "Original" because
 * nxdk's SDL formats names as "Original Xbox Controller #N", which
 * truncates ugly in our 8-char "Type:" column). */
static const char *xbox_pad_label_for(int32_t instance_id)
{
    return jt_accessory_xid_type_label(instance_id);
}

/* USB-class-driven detection via accessories.c -- per (port, pad,
 * slot) cell. pad_idx 0 = primary pad, 1 = daisy-chained pad.
 * MSC -> "MU 8MB"; UAC -> "Headset"; empty -> "-------". */
static void format_slot(char *dst, size_t cap,
                        int port_idx, int pad_idx, int slot_idx)
{
    const char *acc = jt_accessory_for_slot(port_idx, pad_idx, slot_idx);
    if (acc && acc[0]) {
        snprintf(dst, cap, "%-7.7s", acc);
        return;
    }
    snprintf(dst, cap, "-------");
}

/* Per-device sub-renderers. Each returns the number of pixels it
 * consumed (so the port renderer can stack multiple devices on the
 * same Xbox chassis port -- e.g. a hub with a keyboard + mouse).
 *
 * Heights:
 *   pad        4 * ROW_H  (header + sticks + buttons + d-pad)
 *   remote     2 * ROW_H  (header + last-button row)
 *   keyboard   3 * ROW_H  (header + mods + keys)
 *   mouse      3 * ROW_H  (header + state + cursor/wheel-total)
 *   steel      3 * ROW_H  (header + button hex + gear)
 *   empty      4 * ROW_H  (header + zero rows, for layout stability) */

static int draw_pad_block(int p, int y_top);
static int draw_daisy_pad_block(int p, int y_top);
static int draw_remote_block(int p, int y_top);
static int draw_keyboard_block(int p, int y_top);
static int draw_mouse_block(int p, int y_top);
static int draw_steel_block(int p, int y_top);
static int draw_empty_block(int p, int y_top);

/* Render every device sitting on port `p` starting at `y_top` and
 * return the total vertical pixels consumed. tester_draw stacks the
 * port heights plus a per-port gap so a USB-hub port with two
 * devices doesn't crowd into the next port row. */
static int draw_port(int p, int y_top)
{
    const jt_port_state_t *port = &jt_ports[p];
    int y = y_top;

    /* Pad block first (if any), then ALL the non-pad accessory
     * blocks on the same Xbox port -- a USB hub can plug a pad +
     * keyboard + mouse into one chassis port and we want to see
     * every device, not just the first one. udev_root_port walks
     * use the same Xbox-port permutation table nxdk's SDL XID
     * driver does, so XID + HID devices on the same hub resolve to
     * the same port_idx and stack here. */
    if (port->present && port->style == JT_STYLE_PAD) {
        y += draw_pad_block(p, y);
        if (port->has_daisy_pad) y += draw_daisy_pad_block(p, y);
    }
    if (jt_accessory_dvd_remote_at_port(p))      y += draw_remote_block(p, y);
    if (jt_accessory_steel_battalion_at_port(p)) y += draw_steel_block(p, y);
    if (jt_accessory_keyboard_present(p))        y += draw_keyboard_block(p, y);
    if (jt_accessory_mouse_present(p))           y += draw_mouse_block(p, y);
    /* The chassis-direct MU / Headset / Hub blocks have been
     * disabled for v1.0.0: the topology heuristic that decides
     * "is this device plugged straight into the chassis vs sitting
     * inside a controller" mis-fires on real hardware (the OG Xbox
     * controller itself is a compound USB hub with class 0x09 +
     * HID interfaces, and the heuristic ends up labelling the pad
     * as "Hub" or worse). Those blocks land in v1.1.0 once the
     * hub-chain identification is reliable on real boards. */

    /* No device on this port at all -- render the empty layout so
     * the row count is predictable across all 4 chassis slots. */
    if (y == y_top) y += draw_empty_block(p, y);
    return y - y_top;
}

static int draw_pad_block(int p, int y_top)
{
    const jt_port_state_t *port = &jt_ports[p];

    const char *type_label = xbox_pad_label_for(port->pad.instance_id);
    char s1[16], s2[16];
    format_slot(s1, sizeof(s1), p, 0, 0);
    format_slot(s2, sizeof(s2), p, 0, 1);
    const char *rumble =
        (rumble_left[p] || rumble_right[p]) ? "active" : "idle  ";

    jt_text(SAFE_X, y_top, JT_COL_ACTIVE, JT_COL_BLACK,
            "Port %d  Type:%-8.8s  Slot1:%-7.7s  Slot2:%-7.7s  Rumble:%s",
            p + 1, type_label, s1, s2, rumble);

    const jt_pad_state_t *pad = &port->pad;
    uint32_t b = pad->buttons;
    int y_sticks = y_top + ROW_H;
    int y_press  = y_top + ROW_H * 2;
    int y_dpad   = y_top + ROW_H * 3;

    jt_text(SAFE_X, y_sticks, JT_COL_LABEL, JT_COL_BLACK,
            "Stick: %+04d,%+04d  R-Stick: %+04d,%+04d  LT:%03u RT:%03u",
            pad->stick_lx / 256, pad->stick_ly / 256,
            pad->stick_rx / 256, pad->stick_ry / 256,
            pad->trig_l, pad->trig_r);

    int bx = SAFE_X;
    int by = y_press;
    bx = jt_text(bx, by, JT_COL_LABEL, JT_COL_BLACK, "Btns:  ");
#define ANA_BTN(label, value) do { \
        uint8_t v = (value); \
        uint32_t col = v ? JT_COL_HELD : JT_COL_DIM; \
        bx = jt_text(bx, by, col, JT_COL_BLACK, "%s:%03u ", (label), v); \
    } while (0)
    ANA_BTN("A",  pad->abtn_a);
    ANA_BTN("B",  pad->abtn_b);
    ANA_BTN("X",  pad->abtn_x);
    ANA_BTN("Y",  pad->abtn_y);
    ANA_BTN("Wh", pad->abtn_white);
    ANA_BTN("Bk", pad->abtn_black);
#undef ANA_BTN

    int dx_pos = SAFE_X;
    int dy_pos = y_dpad;
#define DIGITAL(label, mask) do { \
        uint32_t col = (b & (mask)) ? JT_COL_HELD : JT_COL_DIM; \
        dx_pos = jt_text(dx_pos, dy_pos, col, JT_COL_BLACK, \
                         "%s:%d ", (label), !!(b & (mask))); \
    } while (0)
    DIGITAL("D-U", JT_BTN_DPAD_UP);
    DIGITAL("D-D", JT_BTN_DPAD_DOWN);
    DIGITAL("D-L", JT_BTN_DPAD_LEFT);
    DIGITAL("D-R", JT_BTN_DPAD_RIGHT);
    dx_pos = jt_text(dx_pos, dy_pos, JT_COL_DIM, JT_COL_BLACK, "  ");
    DIGITAL("Back",  JT_BTN_BACK);
    DIGITAL("Start", JT_BTN_START);
    DIGITAL("LSB",   JT_BTN_LSTICK);
    DIGITAL("RSB",   JT_BTN_RSTICK);
#undef DIGITAL

    return ROW_H * 4;
}

/* Daisy-chained second pad on the same Xbox chassis port (a pad
 * plugged into another pad's expansion slot). Same field set as the
 * primary pad block, including its own Slot1/Slot2 columns -- the
 * daisy pad has its own expansion slots, and devices plugged into
 * those (e.g. a mic in the daisy's slot 1) get attributed to this
 * row, not the primary's. */
static int draw_daisy_pad_block(int p, int y_top)
{
    const jt_port_state_t *port = &jt_ports[p];
    const jt_pad_state_t *pad = &port->daisy_pad;
    uint32_t b = pad->buttons;

    const char *pn = xbox_pad_label_for(port->daisy_pad.instance_id);
    char s1[16], s2[16];
    format_slot(s1, sizeof(s1), p, 1, 0);
    format_slot(s2, sizeof(s2), p, 1, 1);

    jt_text(SAFE_X, y_top, JT_COL_ACTIVE, JT_COL_BLACK,
            "Port %d  Type:%-8.8s  Slot1:%-7.7s  Slot2:%-7.7s",
            p + 1, pn, s1, s2);

    jt_text(SAFE_X, y_top + ROW_H, JT_COL_LABEL, JT_COL_BLACK,
            "Stick: %+04d,%+04d  R-Stick: %+04d,%+04d  LT:%03u RT:%03u",
            pad->stick_lx / 256, pad->stick_ly / 256,
            pad->stick_rx / 256, pad->stick_ry / 256,
            pad->trig_l, pad->trig_r);

    int bx = SAFE_X;
    int by = y_top + ROW_H * 2;
    bx = jt_text(bx, by, JT_COL_LABEL, JT_COL_BLACK, "Btns:  ");
#define ANA_BTN(label, value) do { \
        uint8_t v = (value); \
        uint32_t col = v ? JT_COL_HELD : JT_COL_DIM; \
        bx = jt_text(bx, by, col, JT_COL_BLACK, "%s:%03u ", (label), v); \
    } while (0)
    ANA_BTN("A",  pad->abtn_a);
    ANA_BTN("B",  pad->abtn_b);
    ANA_BTN("X",  pad->abtn_x);
    ANA_BTN("Y",  pad->abtn_y);
    ANA_BTN("Wh", pad->abtn_white);
    ANA_BTN("Bk", pad->abtn_black);
#undef ANA_BTN

    int dx_pos = SAFE_X;
    int dy_pos = y_top + ROW_H * 3;
#define DIGITAL(label, mask) do { \
        uint32_t col = (b & (mask)) ? JT_COL_HELD : JT_COL_DIM; \
        dx_pos = jt_text(dx_pos, dy_pos, col, JT_COL_BLACK, \
                         "%s:%d ", (label), !!(b & (mask))); \
    } while (0)
    DIGITAL("D-U", JT_BTN_DPAD_UP);
    DIGITAL("D-D", JT_BTN_DPAD_DOWN);
    DIGITAL("D-L", JT_BTN_DPAD_LEFT);
    DIGITAL("D-R", JT_BTN_DPAD_RIGHT);
    dx_pos = jt_text(dx_pos, dy_pos, JT_COL_DIM, JT_COL_BLACK, "  ");
    DIGITAL("Back",  JT_BTN_BACK);
    DIGITAL("Start", JT_BTN_START);
    DIGITAL("LSB",   JT_BTN_LSTICK);
    DIGITAL("RSB",   JT_BTN_RSTICK);
#undef DIGITAL

    return ROW_H * 4;
}

static int draw_remote_block(int p, int y_top)
{
    uint16_t code = 0, since = 0;
    bool got = jt_accessory_dvd_remote_state(p, &code, &since);
    const char *name = got ? jt_accessory_dvd_button_name(code) : "";
    jt_text(SAFE_X, y_top, JT_COL_ACTIVE, JT_COL_BLACK,
            "Port %d  Type:Remote   (DVD Movie Playback Kit IR receiver)",
            p + 1);
    jt_text(SAFE_X, y_top + ROW_H, JT_COL_LABEL, JT_COL_BLACK,
            "Last:  Code=0x%04X  Button=%-10.10s  Since=%5ums",
            got ? code : 0,
            (name && name[0]) ? name : "(idle/unmapped)",
            got ? since : 0);
    return ROW_H * 2;
}

static int draw_keyboard_block(int p, int y_top)
{
    uint8_t mods = 0;
    uint8_t keys[6] = {0};
    (void)jt_accessory_keyboard_at_port(p, &mods, keys);
    jt_text(SAFE_X, y_top, JT_COL_ACTIVE, JT_COL_BLACK,
            "Port %d  Type:Keyboard  (USB HID boot protocol)",
            p + 1);
    jt_text(SAFE_X, y_top + ROW_H, JT_COL_LABEL, JT_COL_BLACK,
            "Mods:  Ctrl:%d Shift:%d Alt:%d GUI:%d   (raw 0x%02X)",
            ((mods >> 0) | (mods >> 4)) & 1,
            ((mods >> 1) | (mods >> 5)) & 1,
            ((mods >> 2) | (mods >> 6)) & 1,
            ((mods >> 3) | (mods >> 7)) & 1,
            mods);

    /* USB HID boot protocol reports up to 6 simultaneous keys.
     * Pressing more than that triggers an "ErrorRollOver" state --
     * the keyboard returns 0x01 in every key slot (USB HID Usage ID
     * 0x01 = ErrorRollOver) to signal it can't tell us what's held.
     * Display that explicitly instead of as a confusing string of
     * "01 01 01 01 01 01". */
    bool rollover = (keys[0] == 0x01 && keys[1] == 0x01 &&
                     keys[2] == 0x01 && keys[3] == 0x01 &&
                     keys[4] == 0x01 && keys[5] == 0x01);
    if (rollover) {
        jt_text(SAFE_X, y_top + ROW_H * 2, JT_COL_ERROR, JT_COL_BLACK,
                "Keys:  -- ROLLOVER -- (>6 keys pressed; HID boot proto max=6)");
    } else {
        jt_text(SAFE_X, y_top + ROW_H * 2, JT_COL_LABEL, JT_COL_BLACK,
                "Keys:  %02X %02X %02X %02X %02X %02X",
                keys[0], keys[1], keys[2], keys[3], keys[4], keys[5]);
    }
    return ROW_H * 3;
}

static int draw_mouse_block(int p, int y_top)
{
    uint8_t btns = 0;
    int8_t dx = 0, dy = 0, wheel = 0;
    (void)jt_accessory_mouse_at_port(p, &btns, &dx, &dy, &wheel);
    jt_text(SAFE_X, y_top, JT_COL_ACTIVE, JT_COL_BLACK,
            "Port %d  Type:Mouse     (USB HID boot protocol)",
            p + 1);
    jt_text(SAFE_X, y_top + ROW_H, JT_COL_LABEL, JT_COL_BLACK,
            "Btns:  L:%d R:%d M:%d   dx:%+04d dy:%+04d  Wheel:%+04d",
            !!(btns & 0x01), !!(btns & 0x02), !!(btns & 0x04),
            (int)dx, (int)dy, (int)wheel);
    /* Cursor position + cumulative wheel: cursor confirms motion is
     * being integrated even when the per-report dx/dy snapshot reads
     * back as 0 between reports; wheel_cumul ticks every scroll event
     * for mice whose boot-protocol report includes byte 3. */
    jt_text(SAFE_X, y_top + ROW_H * 2, JT_COL_LABEL, JT_COL_BLACK,
            "Cursor: %3d,%3d  WheelTotal:%+05d",
            (int)jt_cursor.x, (int)jt_cursor.y,
            (int)jt_cursor.wheel_cumul);
    return ROW_H * 3;
}

static int draw_steel_block(int p, int y_top)
{
    uint32_t btn_a = 0, btn_b = 0;
    int8_t gear = 0;
    (void)jt_accessory_steel_battalion_state(p, &btn_a, &btn_b, &gear);
    jt_text(SAFE_X, y_top, JT_COL_ACTIVE, JT_COL_BLACK,
            "Port %d  Type:Steel Battalion Controller",
            p + 1);
    jt_text(SAFE_X, y_top + ROW_H, JT_COL_LABEL, JT_COL_BLACK,
            "Btns A: 0x%08lX   Btns B: 0x%08lX",
            (unsigned long)btn_a, (unsigned long)btn_b);
    jt_text(SAFE_X, y_top + ROW_H * 2, JT_COL_LABEL, JT_COL_BLACK,
            "Gear:  %+d  (range -2..5: R = -1, N = 0, 1..5 fwd)",
            (int)gear);
    return ROW_H * 3;
}

static int draw_empty_block(int p, int y_top)
{
    jt_text(SAFE_X, y_top, JT_COL_LABEL, JT_COL_BLACK,
            "Port %d  Type:--        Slot1:------- Slot2:------- Rumble:idle  ",
            p + 1);
    jt_text(SAFE_X, y_top + ROW_H, JT_COL_DIM, JT_COL_BLACK,
            "Stick: +000,+000  R-Stick: +000,+000  LT:000 RT:000");
    jt_text(SAFE_X, y_top + ROW_H * 2, JT_COL_DIM, JT_COL_BLACK,
            "Btns:  A:000 B:000 X:000 Y:000 Wh:000 Bk:000");
    jt_text(SAFE_X, y_top + ROW_H * 3, JT_COL_DIM, JT_COL_BLACK,
            "D-U:0 D-D:0 D-L:0 D-R:0   Back:0 Start:0 LSB:0 RSB:0");
    return ROW_H * 4;
}

static void tester_enter(void)
{
    for (int i = 0; i < JT_NUM_PORTS; i++) {
        rumble_left[i] = 0;
        rumble_right[i] = 0;
    }
    prev_btns_aggregate = 0;
}

static void tester_leave(void)
{
    /* Stop any in-flight rumble before handing control to the next
     * mode. */
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        if (rumble_left[p] || rumble_right[p]) {
            jt_port_rumble(p, 0, 0, 0);
            rumble_left[p] = rumble_right[p] = 0;
        }
    }
}

static uint32_t aggregate_buttons(void)
{
    uint32_t b = 0;
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        if (jt_ports[p].present && jt_ports[p].style == JT_STYLE_PAD)
            b |= jt_ports[p].pad.buttons;
    }
    return b;
}

static void tester_update(float dt)
{
    (void)dt;

    /* Pressure-driven rumble. The Duke / S pad has two motors -- a
     * heavy/low-frequency one on the left grip and a light/high-
     * frequency one on the right -- which SDL exposes through
     * SDL_GameControllerRumble(strong, weak, duration). Map A's
     * pressure to the left motor and B's to the right so the tester
     * doubles as a pressure-sensitivity demo: feather A for a soft
     * rumble on the left, press hard for max, do the same with B
     * for the right.
     *
     * Skip sending an SDL call when nothing changed -- per-frame
     * SDL_GameControllerRumble calls are fine on hardware but the
     * pressure values can hold steady for many frames, so why bother. */
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        if (!jt_ports[p].present || jt_ports[p].style != JT_STYLE_PAD) {
            if (rumble_left[p] || rumble_right[p]) {
                jt_port_rumble(p, 0, 0, 0);
                rumble_left[p] = rumble_right[p] = 0;
            }
            continue;
        }
        /* 0..255 pressure -> 0..65535 motor speed. 257 == 65535/255. */
        uint16_t left  = (uint16_t)(jt_ports[p].pad.abtn_a * 257);
        uint16_t right = (uint16_t)(jt_ports[p].pad.abtn_b * 257);
        /* Always send: SDL's rumble duration is a "stop after N ms"
         * timer, so we have to re-call within that window or the
         * motor cuts out mid-hold. 200 ms is plenty for our 60 Hz
         * update cadence. Cheap call on hardware; no need to skip. */
        jt_port_rumble(p, left, right, 200);
        rumble_left[p] = left;
        rumble_right[p] = right;

        /* Daisy pad on the same chassis port gets its own A->left,
         * B->right pressure rumble path. Stop motors if no daisy is
         * attached this frame. */
        if (jt_ports[p].has_daisy_pad) {
            uint16_t dleft  = (uint16_t)(jt_ports[p].daisy_pad.abtn_a * 257);
            uint16_t dright = (uint16_t)(jt_ports[p].daisy_pad.abtn_b * 257);
            jt_port_rumble_daisy(p, dleft, dright, 200);
            rumble_left_daisy[p] = dleft;
            rumble_right_daisy[p] = dright;
        } else if (rumble_left_daisy[p] || rumble_right_daisy[p]) {
            jt_port_rumble_daisy(p, 0, 0, 0);
            rumble_left_daisy[p] = rumble_right_daisy[p] = 0;
        }
    }

    /* Dashboard-reboot combo: Back + Black + LT + RT held for ~1
     * second on any pad. Matches the standard OG Xbox in-game
     * "return to dashboard" gesture (e.g. Halo 2 quit). Trigger
     * threshold matches the screensaver's >20 deadzone. */
    bool combo_held = false;
    for (int q = 0; q < JT_NUM_PORTS; q++) {
        if (!jt_ports[q].present || jt_ports[q].style != JT_STYLE_PAD)
            continue;
        uint32_t bq = jt_ports[q].pad.buttons;
        if ((bq & JT_BTN_BACK)  &&
            (bq & JT_BTN_BLACK) &&
            jt_ports[q].pad.trig_l > 200 &&
            jt_ports[q].pad.trig_r > 200) {
            combo_held = true; break;
        }
    }
    if (combo_held) {
        if (++reboot_combo_hold_frames >= REBOOT_COMBO_FRAMES) {
            /* nxdk's XLaunchXBE(NULL) is the canonical "return to
             * dashboard" call: it allocates the persistent
             * LaunchDataPage if needed, marks it as LDT_LAUNCH_DASHBOARD,
             * runs MmPersistContiguousMemory so the BIOS keeps the
             * type across reset, and finally triggers
             * HalReturnToFirmware(HalRebootRoutine). My previous
             * hand-rolled equivalent skipped the persist step, which
             * is why the reboot cold-booted instead of launching the
             * dashboard. */
            XLaunchXBE(NULL);
            /* unreached */
        }
    } else {
        reboot_combo_hold_frames = 0;
    }

    /* Mouse cursor + wheel: drain the first connected mouse's motion
     * accumulator and integrate into the on-screen cursor. Walking
     * ports here (rather than holding a mouse handle) keeps the
     * cursor alive across hot-plug / port-change without extra
     * lifecycle code. */
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        if (!jt_accessory_mouse_present(p)) continue;
        int32_t dx = 0, dy = 0, dwheel = 0;
        if (!jt_accessory_mouse_consume_motion(p, &dx, &dy, &dwheel)) continue;
        if (!jt_cursor.active) {
            jt_cursor.x = jt_video_width()  * 0.5f;
            jt_cursor.y = jt_video_height() * 0.5f;
            jt_cursor.active = true;
        }
        jt_cursor.x += (float)dx;
        jt_cursor.y += (float)dy;
        jt_cursor.wheel_cumul += dwheel;
        if (jt_cursor.x < 0) jt_cursor.x = 0;
        if (jt_cursor.y < 0) jt_cursor.y = 0;
        if (jt_cursor.x > jt_video_width()  - 8)  jt_cursor.x = jt_video_width()  - 8;
        if (jt_cursor.y > jt_video_height() - 12) jt_cursor.y = jt_video_height() - 12;
        break;   /* only one cursor across all mice */
    }

    /* Open the options menu on Start+D-pad-Down (so plain Start stays
     * a testable button here). Edge-based on the aggregate so a held
     * combo only fires once. */
    uint32_t btns  = aggregate_buttons();
    uint32_t edges = btns & ~prev_btns_aggregate;
    prev_btns_aggregate = btns;
    const uint32_t combo = JT_BTN_START | JT_BTN_DPAD_DOWN;
    if ((edges & combo) && (btns & combo) == combo) {
        jt_options_menu_open();
    }
}

static void tester_draw(void)
{
    /* No per-frame full clear: opaque text bg lets each row
     * self-overpaint, and main.c does a one-shot clear on mode/
     * saver/menu transitions to wipe stale content. */
    jt_text_centered(TITLE_Y, JT_COL_TITLE, JT_COL_BLACK,
                     "Joypad Tester - Xbox v" JT_VERSION_STR);

    /* Variable-height port stacking: each port's height tracks its
     * actual rendered content (4 rows for a pad, 2-3 for a single
     * accessory, more if a hub stacks several), with a fixed
     * PORT_GAP between blocks so a tall port + a single-device next
     * port don't visually merge. */
    int y_cursor = PORT_TOP;
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        y_cursor += draw_port(p, y_cursor);
        y_cursor += ROW_H;   /* one-row gap */
    }

    /* Footer pinned inside the CRT safe-bottom margin. */
    jt_text_centered(jt_video_height() - SAFE_BOTTOM,
                     JT_COL_FOOTER, JT_COL_BLACK,
                     "Hold Start+Down for options menu");

    /* Mouse cursor LAST so it draws over every row + the footer. */
    if (jt_cursor.active) {
        int cx = (int)jt_cursor.x;
        int cy = (int)jt_cursor.y;
        jt_fill_rect(cx - 1, cy - 1, 10, 14, JT_COL_BLACK);
        jt_blit_mask(cursor_mask, 8, 12, 1, cx, cy, JT_COL_WHITE);
    }
}

const jt_mode_t jt_mode_tester = {
    .name   = "Controller Tester",
    .enter  = tester_enter,
    .leave  = tester_leave,
    .update = tester_update,
    .draw   = tester_draw,
};
