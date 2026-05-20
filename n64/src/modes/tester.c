/*
 * tester.c — default landing screen.
 *
 * Renders directly to the display surface with per-segment colour
 * (header/style/value/held/dim) using ui/text.{c,h}'s txt_draw
 * helpers. Layout mirrors gcn/ppc/main.c::print_port and the dc
 * tester:
 *
 *   Port N Style: <style>  Pak: <pak>  Rumble: <state>
 *   Stick: +000,+000 C-Stick: +000,+000 L-Trig:000 R-Trig:000
 *   A:0 B:0 X:0 Y:0 L:0 R:0 Z:0 Start:0
 *   D-U:0 D-D:0 D-L:0 D-R:0 C-U:0 C-D:0 C-L:0 C-R:0
 *
 * At 8x8 font in 320x240 with a 16-px overscan-safe left margin we
 * have ~36 cols visible; the longer GC lines clip on the right edge
 * just like libdragon's console_* does. Future v0.2 polish item: a
 * narrower (5x8 or 6x8) custom sprite font to make these lines fit
 * comfortably without clipping.
 *
 * Special cases:
 *   - GBA-on-Joybus rows swap "Pak: ... Rumble: ..." for "Boot: ..."
 *     and decode REG_KEYINPUT bits sent by our GBA payload (see
 *     gba/source/main_tester.c).
 *   - Mouse rows replace the stick + button rows with abs/delta.
 *   - Bio Sensor accessory inserts a placeholder pulse row.
 *   - Transfer Pak appends a single GB-cart-title line.
 *
 * A on any pad fires the GBA multiboot upload (on any port reporting
 * GBA in JOYBUS mode) on the leading edge, and rumbles **that port's**
 * Rumble Pak for as long as A is held (per-port -- holding A on port
 * 1 doesn't shake port 2's pak). This subsumes a dedicated rumble
 * test mode.
 */

#include "tester.h"
#include "../app.h"
#include "../bio.h"
#include "../gba.h"
#include "../kbd.h"
#include "../mouse.h"
#include "../tpak_info.h"
#include "../ui/text.h"

#include <libdragon.h>

#define ROW_H        TXT_GLYPH_H
#define COL_W        TXT_GLYPH_W
#define MARGIN_X     16      /* overscan-safe; matches the user's "left edge bleeds" feedback */
#define MARGIN_Y     12
/* Widest port row at 8x8 font is the analog row
 * ("Stick: +000,+000 C-Stick: +000,+000 L-Trig:000 R-Trig:000") at
 * ~52 chars. Used to centre the table against the actual surface
 * width rather than hard-coding 320. */
#define TABLE_W (52 * COL_W)

/* Palette aliases against the repo-shared constants in ui/text.h so
 * every mode renders the title row + bottom footer the same colour. */
#define COL_TITLE   JT_COL_TITLE
#define COL_HEADER  0xa0ffa0ff       /* lighter green for port headers */
#define COL_LABEL   JT_COL_LABEL
#define COL_VALUE   JT_COL_VALUE
#define COL_HELD    JT_COL_HELD
#define COL_DIM     JT_COL_DIM
#define COL_ACTIVE  JT_COL_ACTIVE
#define COL_ERROR   JT_COL_ERROR
#define COL_FOOTER  JT_COL_FOOTER

typedef enum {
    GBA_STATE_IDLE = 0,
    GBA_STATE_BOOTING,
    GBA_STATE_BOOTED,
    GBA_STATE_FAILED,
    GBA_STATE_RETRY,    /* cooldown after a -2 ready-timeout */
} gba_state_t;

static gba_state_t gba_state[JOYBUS_PORT_COUNT];
static int         gba_last_err[JOYBUS_PORT_COUNT];
static uint8_t     gba_last_input[JOYBUS_PORT_COUNT][2];
static joypad_accessory_type_t prev_acc[JOYBUS_PORT_COUNT];

/* Sticky GBA-cable detection. libdragon's joypad poll regularly drops
 * JOYBUS_IDENTIFIER_GBA_LINK_CABLE for ~1s windows even when the cable
 * is connected (the GBA in JOYBUS mode walks through transient BIOS
 * states the subsystem doesn't classify as GBA). 3s of sticky cache
 * comfortably bridges those gaps so the on-screen row + auto-boot
 * trigger don't flap. */
#define GBA_SEEN_TTL_FRAMES   180
#define GBA_RETRY_HOLD_FRAMES 120     /* ~2s @ 60Hz, matches gcn variant */
#define GBA_BOOT_GUARD_FRAMES 30      /* let UI draw before first boot */
#define GBA_INPUT_HOLD_FRAMES 60      /* sticky 1s of "input was active" */
static int  gba_seen[JOYBUS_PORT_COUNT];
static int  gba_retry_in[JOYBUS_PORT_COUNT];
static int  gba_input_warm[JOYBUS_PORT_COUNT];
static uint32_t boot_guard_frames = 0;

/* Latest RandNet keyboard poll per port (command 0x13). */
static kbd_state_t kbd_state[JOYBUS_PORT_COUNT];

static bool is_gba_port(int port) { return gba_seen[port] > 0; }

bool jt_tester_gba_input_active(void)
{
    /* gba_input_warm[] is bumped any time we observe non-idle
     * REG_KEYINPUT and decays from there. Reading the counter
     * (instead of the raw last poll) keeps GBA activity "alive"
     * across the brief BOOTED -> IDLE -> BOOTING reboots that can
     * happen when sticky-detect flickers off, so the screensaver
     * timer doesn't sneak past on those frames. */
    JOYPAD_PORT_FOREACH (port) {
        if (gba_input_warm[port] > 0) return true;
    }
    return false;
}

static const char *style_label(joypad_style_t s, joybus_identifier_t id)
{
    /* The two identifier-only device types (no joypad_style_t entry):
     * GBA-in-JOYBUS-mode + the Hey You, Pikachu! Voice Recognition
     * Unit. Special-case them off the raw identifier. */
    if (id == JOYBUS_IDENTIFIER_GBA_LINK_CABLE)        return "GBA   ";
    if (id == JOYBUS_IDENTIFIER_N64_VOICE_RECOGNITION) return "VRU   ";
    if (id == JOYBUS_IDENTIFIER_N64_RANDNET_KEYBOARD)  return "Keybd ";
    switch (s) {
        case JOYPAD_STYLE_N64:   return "N64   ";
        case JOYPAD_STYLE_GCN:   return "GCN   ";
        case JOYPAD_STYLE_MOUSE: return "Mouse ";
        case JOYPAD_STYLE_NONE:
        default:                 return "----- ";
    }
}

static const char *pak_label(joypad_accessory_type_t a)
{
    switch (a) {
        case JOYPAD_ACCESSORY_TYPE_CONTROLLER_PAK: return "Memory      ";
        case JOYPAD_ACCESSORY_TYPE_RUMBLE_PAK:     return "Rumble Pak  ";
        case JOYPAD_ACCESSORY_TYPE_TRANSFER_PAK:   return "Transfer Pak";
        case JOYPAD_ACCESSORY_TYPE_BIO_SENSOR:     return "Bio Sensor  ";
        case JOYPAD_ACCESSORY_TYPE_SNAP_STATION:   return "Snap Station";
        case JOYPAD_ACCESSORY_TYPE_NONE:
        default:                                   return "None        ";
    }
}

/* Returns the y position of the row below the rendered block. */
static int draw_port(surface_t *surf, int table_x, int y, joypad_port_t port)
{
    joybus_identifier_t     id    = joypad_get_identifier(port);
    joypad_style_t          style = joypad_get_style(port);
    joypad_accessory_type_t acc   = joypad_get_accessory_type(port);
    joypad_inputs_t         in    = joypad_get_inputs(port);
    bool rumble_sup = joypad_get_rumble_supported(port);
    bool rumble_act = joypad_get_rumble_active(port);
    bool gba_here   = is_gba_port(port);
    bool connected  = (style != JOYPAD_STYLE_NONE)
                      || gba_here
                      || (id == JOYBUS_IDENTIFIER_N64_VOICE_RECOGNITION)
                      || (id == JOYBUS_IDENTIFIER_N64_RANDNET_KEYBOARD);

    /* === Header row: Port N + Style + Pak + Rumble (or Boot). === */
    int x = table_x;
    x = txt_drawf(surf, x, y, connected ? COL_HEADER : COL_DIM,
                  "Port %d ", port + 1);
    x = txt_draw (surf, x, y, COL_LABEL, "Type: ");
    x = txt_draw (surf, x, y, COL_VALUE, style_label(style, id));
    x += COL_W;

    if (gba_here) {
        uint32_t boot_col;
        const char *boot;
        switch (gba_state[port]) {
            case GBA_STATE_BOOTED:  boot = "Booted   ";  boot_col = COL_ACTIVE; break;
            case GBA_STATE_BOOTING: boot = "Boot...  ";  boot_col = COL_HELD;   break;
            case GBA_STATE_FAILED:  boot = "BootFail ";  boot_col = COL_ERROR;  break;
            case GBA_STATE_RETRY:   boot = "Retrying ";  boot_col = COL_HELD;   break;
            case GBA_STATE_IDLE:
            default:                boot = "Detecting"; boot_col = COL_LABEL;  break;
        }
        x = txt_draw (surf, x, y, COL_LABEL, "Boot: ");
        x = txt_draw (surf, x, y, boot_col, boot);
        if (gba_state[port] == GBA_STATE_FAILED) {
            extern const uint32_t gba_payload_len;
            extern const uint8_t  gba_payload[];
            x = txt_drawf(surf, x + COL_W, y, COL_ERROR, "err%+d", gba_last_err[port]);
            /* Runtime diag: payload size + game-code byte. Surfaces
             * whether the embedded ROM linked in correctly so a -10/
             * -11/-12 from gba_boot_embedded can be diagnosed without
             * a debugger. */
            x = txt_drawf(surf, x + COL_W, y, COL_DIM, "len:%lu hdr:%02x",
                          (unsigned long)gba_payload_len, gba_payload[0xac]);
        }
    } else if (id == JOYBUS_IDENTIFIER_N64_VOICE_RECOGNITION) {
        x = txt_draw(surf, x, y, COL_LABEL, "Voice Recognition Unit");
    } else if (id == JOYBUS_IDENTIFIER_N64_RANDNET_KEYBOARD) {
        x = txt_draw(surf, x, y, COL_LABEL, "RandNet Keyboard");
    } else {
        x = txt_draw(surf, x, y, COL_LABEL, "Pak: ");
        x = txt_draw(surf, x, y,
            acc == JOYPAD_ACCESSORY_TYPE_NONE ? COL_DIM : COL_ACTIVE,
            pak_label(acc));
        x += COL_W;
        x = txt_draw(surf, x, y, COL_LABEL, "Rmbl: ");
        const char *r = !rumble_sup ? "Off" : rumble_act ? "ON " : "-- ";
        txt_draw(surf, x, y, rumble_act ? COL_HELD : COL_DIM, r);
    }
    y += ROW_H;

    if (!connected) {
        /* Single-row blank break before the next port. */
        return y + ROW_H;
    }

    /* === Mouse: abs + delta + L/R rows. === */
    if (style == JOYPAD_STYLE_MOUSE) {
        int ax, ay;
        mouse_get(port, &ax, &ay);
        x = table_x;
        x = txt_draw (surf, x, y, COL_LABEL, "Abs: ");
        x = txt_drawf(surf, x, y, COL_VALUE, "%+05d,%+05d   ", ax, ay);
        x = txt_draw (surf, x, y, COL_LABEL, "d: ");
        txt_drawf(surf, x, y, COL_VALUE, "%+03d,%+03d", in.stick_x, in.stick_y);
        y += ROW_H;
        x = table_x;
        x = txt_draw(surf, x, y, in.btn.a ? COL_HELD : COL_DIM, "L:1 ");
        x = txt_draw(surf, x, y, in.btn.b ? COL_HELD : COL_DIM, "R:1 ");
        return y + ROW_H * 2;
    }

    /* === VRU: detection only; audio capture path deferred. === */
    if (id == JOYBUS_IDENTIFIER_N64_VOICE_RECOGNITION) {
        txt_draw(surf, table_x, y, COL_LABEL,
                 "Mic audio capture coming in v0.2+");
        return y + ROW_H * 2;
    }

    /* === RandNet keyboard: raw scancodes + status. === The JBSC code
     * is the key's X/Y matrix coordinate (X<<8 | Y); a full
     * scancode->ASCII map is deferred (table unpublished). */
    if (id == JOYBUS_IDENTIFIER_N64_RANDNET_KEYBOARD) {
        const kbd_state_t *k = &kbd_state[port];
        x = table_x;
        x = txt_draw (surf, x, y, COL_LABEL, "Keys: ");
        x = txt_drawf(surf, x, y, COL_VALUE, "%d  ", k->nkeys);
        for (int i = 0; i < KBD_MAX_KEYS; i++) {
            if (i < k->nkeys)
                x = txt_drawf(surf, x, y, COL_HELD, "%04X ", k->keys[i]);
            else
                x = txt_draw (surf, x, y, COL_DIM, "---- ");
        }
        y += ROW_H;
        x = table_x;
        x = txt_draw (surf, x, y, COL_LABEL, "Status: ");
        x = txt_drawf(surf, x, y, COL_VALUE, "%02X  ", k->status);
        x = txt_draw (surf, x, y, COL_LABEL, "Caps:");
        x = txt_draw (surf, x, y, k->caps_lock ? COL_HELD : COL_DIM,
                      k->caps_lock ? "ON " : "-- ");
        x = txt_draw (surf, x, y, COL_LABEL, "Num:");
        x = txt_draw (surf, x, y, k->num_lock ? COL_HELD : COL_DIM,
                      k->num_lock ? "ON " : "-- ");
        if (k->overflow)
            txt_draw(surf, x, y, COL_ERROR, "(4+ keys)");
        return y + ROW_H + ROW_H / 2;
    }

    /* === Bio Sensor: live BPM + pulse indicator. === The Bio Sensor
     * is just a pak accessory -- the underlying N64 controller still
     * reads buttons normally, so render the BPM line as an *extra*
     * row here and fall through to the standard pad layout below. */
    if (acc == JOYPAD_ACCESSORY_TYPE_BIO_SENSOR) {
        bool pulsing = bio_is_pulsing(port);
        int  bpm     = bio_get_bpm(port);
        x = table_x;
        x = txt_draw (surf, x, y, COL_LABEL, "BPM: ");
        if (bpm > 0) {
            x = txt_drawf(surf, x, y, COL_VALUE, "%-4d ", bpm);
        } else {
            x = txt_draw (surf, x, y, COL_DIM,   "---  ");
        }
        x = txt_draw (surf, x, y, COL_LABEL, "Pulse: ");
        txt_draw     (surf, x, y, pulsing ? COL_HELD : COL_DIM,
                      pulsing ? "PULSING" : "Resting");
        y += ROW_H;
    }

    /* === GBA: decode the polled REG_KEYINPUT bytes. === */
    if (gba_here) {
        if (gba_state[port] == GBA_STATE_BOOTED) {
            uint8_t lo = gba_last_input[port][0];
            uint8_t hi = gba_last_input[port][1];
            const struct { const char *lbl; bool held; } btns1[] = {
                { "A:",  !(lo & 0x01) }, { "B:",  !(lo & 0x02) },
                { "Sel:", !(lo & 0x04) }, { "Start:", !(lo & 0x08) },
                { "L:",  !(hi & 0x02) }, { "R:",  !(hi & 0x01) },
            };
            x = table_x;
            for (size_t i = 0; i < sizeof(btns1)/sizeof(btns1[0]); i++) {
                x = txt_draw(surf, x, y, COL_LABEL, btns1[i].lbl);
                x = txt_draw(surf, x, y, btns1[i].held ? COL_HELD : COL_DIM, "1");
                x += COL_W / 2;
            }
            y += ROW_H;
            const struct { const char *lbl; bool held; } dpad[] = {
                { "D-U:", !(lo & 0x40) }, { "D-D:", !(lo & 0x80) },
                { "D-L:", !(lo & 0x20) }, { "D-R:", !(lo & 0x10) },
            };
            x = table_x;
            for (size_t i = 0; i < sizeof(dpad)/sizeof(dpad[0]); i++) {
                x = txt_draw(surf, x, y, COL_LABEL, dpad[i].lbl);
                x = txt_draw(surf, x, y, dpad[i].held ? COL_HELD : COL_DIM, "1");
                x += COL_W / 2;
            }
            return y + ROW_H + ROW_H / 2;
        } else {
            txt_draw(surf, table_x, y, COL_DIM, "(no input -- multiboot first)");
            return y + ROW_H * 2;
        }
    }

    /* === Standard pad row 2: Stick / C-Stick / L-Trig / R-Trig. ===
     * N64 pads have no analog right-stick or analog triggers -- omit
     * C-Stick on N64; analog L/R stay visible (always 000 on N64) so
     * the GCN-shaped row alignment isn't disrupted across mixed ports.
     * Actually keeping L-Trig/R-Trig on both reads as 000 for N64 and
     * non-zero for GCN, which is the useful signal. */
    bool is_n64 = (style == JOYPAD_STYLE_N64);
    x = table_x;
    x = txt_draw (surf, x, y, COL_LABEL, "Stick: ");
    x = txt_drawf(surf, x, y, COL_VALUE, "%+04d,%+04d ", in.stick_x, in.stick_y);
    if (!is_n64) {
        x = txt_draw (surf, x, y, COL_LABEL, "C-Stick: ");
        x = txt_drawf(surf, x, y, COL_VALUE, "%+04d,%+04d ", in.cstick_x, in.cstick_y);
    }
    x = txt_draw (surf, x, y, COL_LABEL, "L-Trig:");
    x = txt_drawf(surf, x, y, COL_VALUE, "%03u ", in.analog_l);
    x = txt_draw (surf, x, y, COL_LABEL, "R-Trig:");
    txt_drawf(surf, x, y, COL_VALUE, "%03u", in.analog_r);
    y += ROW_H;

    /* === Row 3: Face / shoulder buttons. === N64 pads have no X/Y. */
    {
        const struct { const char *lbl; int v; bool n64; } btns[] = {
            {"A:", in.btn.a, true}, {"B:", in.btn.b, true},
            {"X:", in.btn.x, false}, {"Y:", in.btn.y, false},
            {"L:", in.btn.l, true}, {"R:", in.btn.r, true},
            {"Z:", in.btn.z, true}, {"Start:", in.btn.start, true},
        };
        x = table_x;
        for (size_t i = 0; i < sizeof(btns)/sizeof(btns[0]); i++) {
            if (is_n64 && !btns[i].n64) continue;
            x = txt_draw (surf, x, y, COL_LABEL, btns[i].lbl);
            x = txt_draw (surf, x, y, btns[i].v ? COL_HELD : COL_DIM,
                          btns[i].v ? "1" : "0");
            x += COL_W / 2;
        }
        y += ROW_H;
    }

    /* === Row 4: D-pad (+ C-pad on N64). === GCN has no digital
     * C-buttons (it uses the analog C-Stick instead), so omit them
     * on GCN. */
    {
        const struct { const char *lbl; int v; bool gcn; } dpad[] = {
            {"D-U:", in.btn.d_up,    true}, {"D-D:", in.btn.d_down,  true},
            {"D-L:", in.btn.d_left,  true}, {"D-R:", in.btn.d_right, true},
            {"C-U:", in.btn.c_up,    false},{"C-D:", in.btn.c_down,  false},
            {"C-L:", in.btn.c_left,  false},{"C-R:", in.btn.c_right, false},
        };
        bool is_gcn = (style == JOYPAD_STYLE_GCN);
        x = table_x;
        for (size_t i = 0; i < sizeof(dpad)/sizeof(dpad[0]); i++) {
            if (is_gcn && !dpad[i].gcn) continue;
            x = txt_draw (surf, x, y, COL_LABEL, dpad[i].lbl);
            x = txt_draw (surf, x, y, dpad[i].v ? COL_HELD : COL_DIM,
                          dpad[i].v ? "1" : "0");
            x += COL_W / 2;
        }
        y += ROW_H;
    }

    /* === Optional Transfer Pak GB cart row. === */
    if (acc == JOYPAD_ACCESSORY_TYPE_TRANSFER_PAK) {
        const tpak_info_t *t = tpak_info_read(port);
        if (t->title[0]) {
            x = table_x;
            x = txt_draw (surf, x, y, COL_LABEL, "GB cart: ");
            x = txt_drawf(surf, x, y, t->valid ? COL_VALUE : COL_HELD,
                          "%s ", t->title);
            x = txt_drawf(surf, x, y, COL_LABEL, "t:%02x r:%02x m:%02x",
                          t->cart_type, t->rom_size, t->ram_size);
            y += ROW_H;
        } else if (t->attempted) {
            txt_draw(surf, table_x, y, COL_DIM,
                     "GB cart: no cart / header unreadable");
            y += ROW_H;
        }
    }

    return y + ROW_H / 2;  /* small gap before the next port */
}

static void tester_update(void)
{
    JOYPAD_PORT_FOREACH (port) {
        joypad_style_t          style = joypad_get_style(port);
        joypad_accessory_type_t acc   = joypad_get_accessory_type(port);

        if (joypad_get_identifier(port) == JOYBUS_IDENTIFIER_GBA_LINK_CABLE)
            gba_seen[port] = GBA_SEEN_TTL_FRAMES;
        else if (gba_seen[port] > 0)
            gba_seen[port]--;

        /* Cable went away (sticky cache expired) -- reset the GBA
         * state machine so a re-plug starts clean instead of being
         * stuck on a stale BOOTED / FAILED / RETRY. */
        if (gba_seen[port] == 0 && gba_state[port] != GBA_STATE_IDLE) {
            gba_state[port] = GBA_STATE_IDLE;
            gba_last_err[port] = 0;
            gba_retry_in[port] = 0;
        }
        if (gba_retry_in[port] > 0) gba_retry_in[port]--;

        if (style == JOYPAD_STYLE_MOUSE) mouse_tick(port);
        if (acc   == JOYPAD_ACCESSORY_TYPE_BIO_SENSOR) bio_tick(port);
        if (joypad_get_identifier(port) == JOYBUS_IDENTIFIER_N64_RANDNET_KEYBOARD)
            kbd_poll(port, &kbd_state[port]);

        if (prev_acc[port] == JOYPAD_ACCESSORY_TYPE_TRANSFER_PAK
            && acc != JOYPAD_ACCESSORY_TYPE_TRANSFER_PAK) {
            tpak_info_reset(port);
        }
        if (prev_acc[port] == JOYPAD_ACCESSORY_TYPE_BIO_SENSOR
            && acc != JOYPAD_ACCESSORY_TYPE_BIO_SENSOR) {
            bio_reset(port);
        }
        prev_acc[port] = acc;
    }

    /* Auto-multiboot: fire one upload as soon as a GBA cable is
     * detected, matching the gcn tester's behaviour (no A-press
     * needed). RETRY backs off for ~2s on a -2 timeout so cold-boot
     * GBAs that haven't entered multiboot-wait yet get a second
     * chance instead of being parked in FAILED. The boot guard
     * skips the first ~30 frames so the UI gets to draw before
     * gba_boot_embedded blocks the main loop for several seconds. */
    if (boot_guard_frames < GBA_BOOT_GUARD_FRAMES) boot_guard_frames++;

    if (boot_guard_frames >= GBA_BOOT_GUARD_FRAMES) {
        JOYPAD_PORT_FOREACH (port) {
            if (gba_state[port] == GBA_STATE_RETRY && gba_retry_in[port] == 0)
                gba_state[port] = GBA_STATE_IDLE;

            if (gba_state[port] == GBA_STATE_IDLE && is_gba_port(port)) {
                gba_state[port] = GBA_STATE_BOOTING;
                int err = gba_boot_embedded(port);
                if (err == 0) {
                    gba_state[port] = GBA_STATE_BOOTED;
                } else if (err == -2) {
                    /* GBA's BIOS isn't in multiboot-wait yet (cold-
                     * boot animation). Back off and try again. */
                    gba_state[port] = GBA_STATE_RETRY;
                    gba_retry_in[port] = GBA_RETRY_HOLD_FRAMES;
                    gba_last_err[port] = err;
                } else {
                    gba_state[port] = GBA_STATE_FAILED;
                    gba_last_err[port] = err;
                }
            }
        }
    }

    JOYPAD_PORT_FOREACH (port) {
        /* Rumble is per-port: each pad rumbles while *its* own A is
         * held. joypad_get_rumble_supported is true for both N64
         * pads carrying a Rumble Pak AND GameCube pads (which have
         * built-in rumble, no accessory needed) -- we don't have to
         * check accessory_type separately. */
        if (joypad_get_rumble_supported(port)) {
            bool want = joypad_get_buttons_held(port).a;
            if (want != joypad_get_rumble_active(port)) {
                joypad_set_rumble_active(port, want);
            }
        }
        if (gba_state[port] == GBA_STATE_BOOTED) {
            gba_poll_input(port, gba_last_input[port]);
            /* Non-idle REG_KEYINPUT (lo!=0xFF or hi bottom-bits!=0x03)
             * means a GBA button is held -- bump the warm counter. We
             * ignore the all-zero "poll returned no data" case since
             * gba_poll_input writes 0/0 on a failed read, not from a
             * real button. */
            uint8_t lo = gba_last_input[port][0];
            uint8_t hi = gba_last_input[port][1];
            bool any_data = (lo != 0 || hi != 0);
            bool any_btn  = any_data && (lo != 0xFF || (hi & 0x03) != 0x03);
            if (any_btn)
                gba_input_warm[port] = GBA_INPUT_HOLD_FRAMES;
        }
        if (gba_input_warm[port] > 0) gba_input_warm[port]--;
    }
}

static void tester_leave(void)
{
    /* Belt-and-braces: silence every rumble pak when leaving the
     * tester, so a long A-hold doesn't outlive the mode switch. */
    JOYPAD_PORT_FOREACH (port) {
        if (joypad_get_rumble_supported(port)
            && joypad_get_rumble_active(port)) {
            joypad_set_rumble_active(port, false);
        }
    }
}

static void tester_draw(void)
{
    surface_t *surf = display_get();
    graphics_fill_screen(surf, graphics_make_color(0, 0, 0, 0xff));

    /* Use the surface's actual width for centering -- hardcoding 320
     * makes title + footer drift left in display modes where libdragon
     * returns a wider surface. Same fix the options menu uses. */
    int screen_w = surf->width  ? surf->width  : 320;
    int screen_h = surf->height ? surf->height : 240;

    /* Centre the port table by computing where its widest row would
     * start, then shift left by ~2 glyphs because the actual visible
     * weight of each row leans right (longer labels at the right end,
     * shorter values at the left) and the pure-width centre lands a
     * touch right of optical centre. Clamp to MARGIN_X so the table
     * never bleeds into the left overscan zone on narrow surfaces. */
    int table_x = ((screen_w - TABLE_W) / 2) - (COL_W * 2);
    if (table_x < MARGIN_X) table_x = MARGIN_X;

    int y = MARGIN_Y;
    txt_draw_centered(surf, y, COL_TITLE,
                      "Joypad Tester - N64 v" JT_VERSION_STR, screen_w);
    y += ROW_H * 2;

    JOYPAD_PORT_FOREACH (port) {
        y = draw_port(surf, table_x, y, port);
    }

    /* Footer hint: same wording + placement convention the other
     * subdirs (dc / gba / pce / gcn) use at the bottom of the screen. */
    txt_draw_centered(surf, screen_h - MARGIN_Y - ROW_H, COL_FOOTER,
                      "Hold Start+Down for options menu", screen_w);

    display_show(surf);
}

const jt_mode_t jt_mode_tester = {
    .name   = "Controller Tester",
    .enter  = NULL,
    .leave  = tester_leave,
    .update = tester_update,
    .draw   = tester_draw,
};
