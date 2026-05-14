/*
 * Joypad Tester - Nintendo 64
 *
 * Forked from Christopher Bonhage (meeq)'s JoypadTest-N64 example
 * (public domain) and ported to LibDragon trunk's current joypad
 * subsystem API. Renders the live state of every Joybus device on
 * the four N64 controller ports: standard N64 pad, GameCube pad via
 * passive 3-pin adapter, and -- planned for the next iteration --
 * a GBA in JOYBUS mode connected via a GameCube/GBA link cable +
 * N64-port adapter.
 *
 * Copyright (c) 2026 Robert Dale Smith
 * MIT License -- see ../LICENSE.md
 */

#include <string.h>
#include <libdragon.h>

#include "gba.h"

/* Track per-port GBA-boot status so we can display "Booting...",
 * "Booted OK", or "Boot failed (code -N)" alongside the port row. */
typedef enum {
    GBA_STATE_IDLE = 0,
    GBA_STATE_BOOTING,
    GBA_STATE_BOOTED,
    GBA_STATE_FAILED,
} gba_state_t;

static gba_state_t gba_state[JOYBUS_PORT_COUNT];
static int         gba_last_err[JOYBUS_PORT_COUNT];
static uint8_t     gba_last_input[JOYBUS_PORT_COUNT][2];

static const char *style_name(joypad_style_t s)
{
    switch (s) {
        case JOYPAD_STYLE_NONE:  return "----";
        case JOYPAD_STYLE_N64:   return "N64 ";
        case JOYPAD_STYLE_GCN:   return "GCN ";
        case JOYPAD_STYLE_MOUSE: return "Mse ";
        default:                 return "??? ";
    }
}

/* The Joybus 0xff "identify" command returns a 16-bit device ID.
 * LibDragon's joypad subsystem maps the standard pads to its
 * joypad_style_t enum, but GBA-in-JOYBUS-mode + 64GB-link + RandNet
 * etc. live below that abstraction. Peeking at the raw identifier
 * lets us recognise the GBA cable without LibDragon's subsystem
 * needing to grow a new style. */
static const char *identifier_name(joybus_identifier_t id)
{
    switch (id) {
        case JOYBUS_IDENTIFIER_NONE:                  return "no device";
        case JOYBUS_IDENTIFIER_UNKNOWN:               return "unknown";
        case JOYBUS_IDENTIFIER_N64_CONTROLLER:        return "N64 pad";
        case JOYBUS_IDENTIFIER_N64_MOUSE:             return "N64 mouse";
        case JOYBUS_IDENTIFIER_N64_VOICE_RECOGNITION: return "N64 VRU";
        case JOYBUS_IDENTIFIER_N64_RANDNET_KEYBOARD:  return "RandNet KB";
        case JOYBUS_IDENTIFIER_GBA_LINK_CABLE:        return "GBA (Joybus)";
        case JOYBUS_IDENTIFIER_64GB_LINK_CABLE:       return "64GB Cable";
        default:
            /* GameCube controllers share the upper byte 0x09 with a
             * bitfield in the low byte for rumble / wireless / origin
             * status, so we lump them under one label. */
            if ((id & 0xff00) == 0x0900) return "GCN pad";
            return "other";
    }
}

static const char *accessory_name(joypad_accessory_type_t a)
{
    switch (a) {
        case JOYPAD_ACCESSORY_TYPE_NONE:           return "-";
        case JOYPAD_ACCESSORY_TYPE_CONTROLLER_PAK: return "MemPak";
        case JOYPAD_ACCESSORY_TYPE_RUMBLE_PAK:     return "Rumble";
        case JOYPAD_ACCESSORY_TYPE_TRANSFER_PAK:   return "Xfer";
        case JOYPAD_ACCESSORY_TYPE_BIO_SENSOR:     return "Bio";
        case JOYPAD_ACCESSORY_TYPE_SNAP_STATION:   return "Snap";
        default:                                   return "?";
    }
}

static void print_port(joypad_port_t port)
{
    joybus_identifier_t     id    = joypad_get_identifier(port);
    joypad_style_t          style = joypad_get_style(port);
    joypad_accessory_type_t acc   = joypad_get_accessory_type(port);
    joypad_inputs_t         in    = joypad_get_inputs(port);

    /* Port header: identifier + style + accessory. The identifier
     * is the raw 16-bit Joybus reply; style is what LibDragon's
     * subsystem decoded it to. They agree on standard pads but the
     * identifier carries more for non-pad devices (GBA cable etc.). */
    printf("P%d id:%04x %s %-12s acc:%-6s",
        port + 1, id, style_name(style), identifier_name(id),
        accessory_name(acc));

    /* GBA-on-Joybus rows get a per-port boot-state suffix so the user
     * can see Idle / Booting / Booted / Failed without scrolling. */
    if (id == JOYBUS_IDENTIFIER_GBA_LINK_CABLE) {
        switch (gba_state[port]) {
            case GBA_STATE_IDLE:
                printf(" [press A on any pad to multiboot]\n");
                return;
            case GBA_STATE_BOOTING:
                printf(" [booting...]\n");
                return;
            case GBA_STATE_BOOTED:
                printf(" [booted; poll %02x %02x]\n",
                    gba_last_input[port][0], gba_last_input[port][1]);
                return;
            case GBA_STATE_FAILED:
                printf(" [boot failed err=%d]\n", gba_last_err[port]);
                return;
        }
    }

    /* For non-pad devices the joypad subsystem won't have meaningful
     * inputs to render, so stop after the header. */
    if (style == JOYPAD_STYLE_NONE) {
        printf("\n");
        return;
    }

    /* Sticks + triggers (analog). GameCube pads use stick + cstick +
     * analog L/R; N64 pads have only stick (cstick + analog triggers
     * are emulated to 0/127 by the LibDragon joypad subsystem so the
     * same struct works for both). */
    printf(" stk %+4d,%+4d  cstk %+4d,%+4d  L%3d R%3d\n",
        in.stick_x,  in.stick_y,
        in.cstick_x, in.cstick_y,
        in.analog_l, in.analog_r);

    /* Buttons. The 'btn' sub-struct unifies the N64 + GameCube button
     * sets; fields not present on the physical pad read as 0. */
    printf("   A:%d B:%d X:%d Y:%d  L:%d R:%d Z:%d Strt:%d  D:%d%d%d%d C:%d%d%d%d\n",
        in.btn.a, in.btn.b, in.btn.x, in.btn.y,
        in.btn.l, in.btn.r, in.btn.z, in.btn.start,
        in.btn.d_up,  in.btn.d_down,  in.btn.d_left,  in.btn.d_right,
        in.btn.c_up,  in.btn.c_down,  in.btn.c_left,  in.btn.c_right);
}

int main(void)
{
    timer_init();
    display_init(RESOLUTION_320x240, DEPTH_32_BPP, 2, GAMMA_NONE,
                 FILTERS_RESAMPLE);
    joypad_init();

    console_init();
    console_set_render_mode(RENDER_MANUAL);
    console_set_debug(false);

    while (1) {
        joypad_poll();
        console_clear();

        printf("== Joypad Tester - N64 ==\n");
        printf("\n");

        /* If any pad pressed A this frame and any port shows a
         * not-yet-booted GBA, multiboot it. We do this BEFORE rendering
         * so the state transition shows up on the same frame. */
        bool any_a_pressed = false;
        JOYPAD_PORT_FOREACH (port) {
            joypad_buttons_t pressed = joypad_get_buttons_pressed(port);
            if (pressed.a) { any_a_pressed = true; break; }
        }
        if (any_a_pressed) {
            JOYPAD_PORT_FOREACH (port) {
                if (gba_state[port] == GBA_STATE_IDLE
                    && joypad_get_identifier(port)
                       == JOYBUS_IDENTIFIER_GBA_LINK_CABLE) {
                    gba_state[port] = GBA_STATE_BOOTING;
                    int err = gba_boot_embedded(port);
                    if (err == 0) {
                        gba_state[port] = GBA_STATE_BOOTED;
                    } else {
                        gba_state[port] = GBA_STATE_FAILED;
                        gba_last_err[port] = err;
                    }
                }
            }
        }

        /* Once a GBA is booted, poll its 0x14 status so the on-screen
         * suffix shows live bytes coming back from the payload. */
        JOYPAD_PORT_FOREACH (port) {
            if (gba_state[port] == GBA_STATE_BOOTED) {
                gba_poll_input(port, gba_last_input[port]);
            }
        }

        JOYPAD_PORT_FOREACH (port) {
            print_port(port);
        }

        console_render();
    }
}
