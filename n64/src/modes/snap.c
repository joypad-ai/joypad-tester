/*
 * snap.c — Snap Station test mode.
 *
 * Port of meeq's SnapStationTest.c (public domain), adapted to our
 * mode dispatcher. The original is hard-coded to port 4; we scan
 * all four ports each frame and pick whichever one reports a Snap
 * Station accessory.
 *
 * Useful as a protocol-validation harness for builders of homebrew
 * Snap Station replicas (Pokemon Snap kiosk reproductions etc.).
 * Drives the documented Joybus state machine: Idle / Pre-Save /
 * Post-Save / Reset / Pre-Roll / Capture / Post-Roll / Busy.
 *
 * Renders via ui/text helpers (matches the rest of the modes for
 * consistent title colour, centring, and footer placement).
 */

#include "snap.h"
#include "../ui/text.h"

#include <libdragon.h>
#include <stdio.h>
#include <string.h>

/* === Constants from meeq's joybus_n64_accessory.h ===              */
#define ACCESSORY_DATA_SIZE                 32
#define ACCESSORY_ADDR_PROBE                0x8000
#define ACCESSORY_ADDR_SNAP_STATE           0xC000
#define ACCESSORY_PROBE_RUMBLE_PAK          0x80
#define ACCESSORY_PROBE_BIO_SENSOR          0x81
#define ACCESSORY_PROBE_TRANSFER_PAK_ON     0x84
#define ACCESSORY_PROBE_SNAP_STATION        0x85
#define ACCESSORY_PROBE_TRANSFER_PAK_OFF    0xFE
#define SNAP_STATION_STATE_IDLE             0x00
#define SNAP_STATION_STATE_PRE_SAVE         0xCC
#define SNAP_STATION_STATE_POST_SAVE        0x33
#define SNAP_STATION_STATE_RESET_CONSOLE    0x5A
#define SNAP_STATION_STATE_PRE_ROLL         0x01
#define SNAP_STATION_STATE_CAPTURE_PHOTO    0x02
#define SNAP_STATION_STATE_POST_ROLL        0x04
#define SNAP_STATION_STATE_BUSY             0x08

typedef struct {
    int     active_port;            /* -1 if none */
    uint8_t last_probe;
    uint8_t last_state;
    bool    last_io_error;
    char    last_msg[64];           /* most recent operation result */
} snap_state_t;

static snap_state_t st;

static const char *state_name(uint8_t s)
{
    switch (s) {
        case SNAP_STATION_STATE_IDLE:          return "Idle";
        case SNAP_STATION_STATE_PRE_SAVE:      return "Pre-Save";
        case SNAP_STATION_STATE_POST_SAVE:     return "Post-Save";
        case SNAP_STATION_STATE_RESET_CONSOLE: return "Reset Console";
        case SNAP_STATION_STATE_PRE_ROLL:      return "Pre-Roll";
        case SNAP_STATION_STATE_CAPTURE_PHOTO: return "Capture Photo";
        case SNAP_STATION_STATE_POST_ROLL:     return "Post-Roll";
        case SNAP_STATION_STATE_BUSY:          return "Busy";
        default:                               return "Unknown";
    }
}

static const char *probe_name(uint8_t p)
{
    switch (p) {
        case 0:                                return "Inactive";
        case ACCESSORY_PROBE_RUMBLE_PAK:       return "Rumble Pak";
        case ACCESSORY_PROBE_BIO_SENSOR:       return "Bio Sensor";
        case ACCESSORY_PROBE_TRANSFER_PAK_ON:  return "Active Transfer Pak";
        case ACCESSORY_PROBE_TRANSFER_PAK_OFF: return "Inactive Transfer Pak";
        case ACCESSORY_PROBE_SNAP_STATION:     return "Active Snap Station";
        default:                               return "Unknown";
    }
}

static void scan_for_snap(void)
{
    st.active_port = -1;
    JOYPAD_PORT_FOREACH (port) {
        if (joypad_get_accessory_type(port) == JOYPAD_ACCESSORY_TYPE_SNAP_STATION) {
            st.active_port = (int)port;
            break;
        }
    }
}

static void op_read_probe(void)
{
    if (st.active_port < 0) return;
    uint8_t data[ACCESSORY_DATA_SIZE];
    int rc = joybus_accessory_read(st.active_port, ACCESSORY_ADDR_PROBE, data);
    if (rc != 0) {
        st.last_io_error = true;
        snprintf(st.last_msg, sizeof(st.last_msg),
                 "Probe read: I/O error (%d)", rc);
        return;
    }
    st.last_io_error = false;
    st.last_probe = data[ACCESSORY_DATA_SIZE - 1];
    snprintf(st.last_msg, sizeof(st.last_msg),
             "Probe: 0x%02x (%s)", st.last_probe, probe_name(st.last_probe));
}

static void op_read_state(void)
{
    if (st.active_port < 0) return;
    uint8_t data[ACCESSORY_DATA_SIZE];
    int rc = joybus_accessory_read(st.active_port, ACCESSORY_ADDR_SNAP_STATE, data);
    if (rc != 0) {
        st.last_io_error = true;
        snprintf(st.last_msg, sizeof(st.last_msg),
                 "State read: I/O error (%d)", rc);
        return;
    }
    st.last_io_error = false;
    st.last_state = data[ACCESSORY_DATA_SIZE - 1];
    snprintf(st.last_msg, sizeof(st.last_msg),
             "State: 0x%02x (%s)", st.last_state, state_name(st.last_state));
}

static void op_write_command(uint8_t cmd)
{
    if (st.active_port < 0) return;
    uint8_t data[ACCESSORY_DATA_SIZE] = {0};
    data[ACCESSORY_DATA_SIZE - 1] = cmd;
    int rc = joybus_accessory_write(st.active_port, ACCESSORY_ADDR_SNAP_STATE, data);
    if (rc != 0) {
        st.last_io_error = true;
        snprintf(st.last_msg, sizeof(st.last_msg),
                 "Cmd 0x%02x write: I/O error (%d)", cmd, rc);
        return;
    }
    op_read_state();
}

static void snap_enter(void)
{
    memset(&st, 0, sizeof(st));
    st.active_port = -1;
    st.last_msg[0] = '\0';
}

static void snap_update(void)
{
    int prev_active = st.active_port;
    scan_for_snap();

    if (st.active_port >= 0 && prev_active != st.active_port) {
        op_read_probe();
    }

    if (st.active_port < 0) return;

    JOYPAD_PORT_FOREACH (p) {
        joypad_buttons_t pressed = joypad_get_buttons_pressed(p);
        if      (pressed.r)       op_read_state();
        else if (pressed.a)       op_write_command(SNAP_STATION_STATE_PRE_SAVE);
        else if (pressed.b)       op_write_command(SNAP_STATION_STATE_POST_SAVE);
        else if (pressed.c_down)  op_write_command(SNAP_STATION_STATE_RESET_CONSOLE);
        else if (pressed.c_left)  op_write_command(SNAP_STATION_STATE_PRE_ROLL);
        else if (pressed.c_up)    op_write_command(SNAP_STATION_STATE_CAPTURE_PHOTO);
        else if (pressed.c_right) op_write_command(SNAP_STATION_STATE_POST_ROLL);
    }
}

static void snap_draw(void)
{
    surface_t *surf = display_get();
    graphics_fill_screen(surf, graphics_make_color(0, 0, 0, 0xff));

    int screen_w = surf->width  ? surf->width  : 320;
    int screen_h = surf->height ? surf->height : 240;
    int y = 12;

    txt_draw_centered(surf, y, JT_COL_TITLE, "Snap Station Test", screen_w);
    y += 24;

    if (st.active_port < 0) {
        txt_draw_centered(surf, y, JT_COL_DIM,
                          "No Snap Station detected on any port.",
                          screen_w);
        y += TXT_GLYPH_H + 6;
        txt_draw_centered(surf, y, JT_COL_DIM,
                          "Connect one (homebrew or original).",
                          screen_w);
        y += TXT_GLYPH_H + 6;
    } else {
        char line[64];
        snprintf(line, sizeof(line), "Snap Station: Port %d", st.active_port + 1);
        txt_draw_centered(surf, y, JT_COL_VALUE, line, screen_w);
        y += TXT_GLYPH_H + 4;
        snprintf(line, sizeof(line), "Probe: 0x%02x (%s)",
                 st.last_probe, probe_name(st.last_probe));
        txt_draw_centered(surf, y, JT_COL_LABEL, line, screen_w);
        y += TXT_GLYPH_H + 2;
        snprintf(line, sizeof(line), "State: 0x%02x (%s)",
                 st.last_state, state_name(st.last_state));
        txt_draw_centered(surf, y, JT_COL_LABEL, line, screen_w);
        y += TXT_GLYPH_H + 8;
    }

    /* Command list, left-aligned around the centre. */
    const int CMD_W = 36 * TXT_GLYPH_W;
    int cmd_x = (screen_w - CMD_W) / 2;
    if (cmd_x < 16) cmd_x = 16;
    txt_draw(surf, cmd_x, y, JT_COL_LABEL, "Command List:");
    y += TXT_GLYPH_H + 4;
    txt_drawf(surf, cmd_x, y, JT_COL_VALUE, "R       = Read State");                            y += TXT_GLYPH_H + 2;
    txt_drawf(surf, cmd_x, y, JT_COL_VALUE, "A       = Pre-Save      (0x%02x)", SNAP_STATION_STATE_PRE_SAVE);       y += TXT_GLYPH_H + 2;
    txt_drawf(surf, cmd_x, y, JT_COL_VALUE, "B       = Post-Save     (0x%02x)", SNAP_STATION_STATE_POST_SAVE);      y += TXT_GLYPH_H + 2;
    txt_drawf(surf, cmd_x, y, JT_COL_VALUE, "C-Down  = Reset Console (0x%02x)", SNAP_STATION_STATE_RESET_CONSOLE);  y += TXT_GLYPH_H + 2;
    txt_drawf(surf, cmd_x, y, JT_COL_VALUE, "C-Left  = Pre-Roll      (0x%02x)", SNAP_STATION_STATE_PRE_ROLL);       y += TXT_GLYPH_H + 2;
    txt_drawf(surf, cmd_x, y, JT_COL_VALUE, "C-Up    = Capture Photo (0x%02x)", SNAP_STATION_STATE_CAPTURE_PHOTO);  y += TXT_GLYPH_H + 2;
    txt_drawf(surf, cmd_x, y, JT_COL_VALUE, "C-Right = Post-Roll     (0x%02x)", SNAP_STATION_STATE_POST_ROLL);      y += TXT_GLYPH_H + 4;

    if (st.last_msg[0]) {
        txt_drawf(surf, cmd_x, y, JT_COL_LABEL, ">> %s", st.last_msg);
    }

    /* Footer hint: shared helper places it inside overscan. */
    (void)screen_h;
    txt_draw_footer(surf, "Start: options menu");

    display_show(surf);
}

const jt_mode_t jt_mode_snap = {
    .name   = "Snap Station Test",
    .enter  = snap_enter,
    .leave  = NULL,
    .update = snap_update,
    .draw   = snap_draw,
};
