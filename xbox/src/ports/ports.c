/*
 * ports.c -- maintain jt_ports[] from SDL_GameController state.
 *
 * SDL2's gamecontroller layer ships in nxdk and already handles the
 * XID enumeration + button mapping for the standard Duke / S pads,
 * Logitech wheels, etc. We keep a stable port_idx -> SDL controller
 * mapping by indexing on SDL's player-index (which SDL pins to the
 * XID port the device came in on), so port A == player 0, etc.
 *
 * Expansion-slot (MU / Voice) detection lives in a follow-up; for
 * v0.1.0 the slot rows just render "EMPTY".
 */
#include "ports.h"
#include "accessories.h"

#include <SDL.h>
#include <string.h>

jt_port_state_t jt_ports[JT_NUM_PORTS];

/* SDL_GameController* per port. Two slots per chassis port -- the
 * primary device + a daisy-chained device (PSO-style accessory
 * plugged through a controller's expansion slot). nxdk's SDL XID
 * driver gives both pads the same player_index, so the simplest
 * way to keep both alive is a second slot keyed on the same Xbox
 * port; the tester renders them as separate stacked blocks. */
static SDL_GameController *pad_for_port[JT_NUM_PORTS];
static SDL_GameController *daisy_pad_for_port[JT_NUM_PORTS];

static int added_events_total;
static int removed_events_total;

int jt_ports_added_events(void)   { return added_events_total; }
int jt_ports_removed_events(void) { return removed_events_total; }

const char *jt_ports_slot_state(void)
{
    static char buf[JT_NUM_PORTS + 1];
    for (int i = 0; i < JT_NUM_PORTS; i++)
        buf[i] = pad_for_port[i] ? '1' : '0';
    buf[JT_NUM_PORTS] = '\0';
    return buf;
}

void jt_ports_init(void)
{
    memset(jt_ports, 0, sizeof(jt_ports));
    memset(pad_for_port, 0, sizeof(pad_for_port));
    memset(daisy_pad_for_port, 0, sizeof(daisy_pad_for_port));
    SDL_GameControllerEventState(SDL_ENABLE);
    jt_accessories_init();
}

static int port_for_pad(SDL_GameController *pad)
{
    if (!pad) return -1;
    for (int i = 0; i < JT_NUM_PORTS; i++) {
        if (pad_for_port[i] == pad || daisy_pad_for_port[i] == pad) return i;
    }
    return -1;
}

/* SDL_GameControllerGetPlayerIndex on nxdk returns the actual Xbox
 * port number (1..4), not an SDL-internal index. The xbox-specific
 * implementation in nxdk's SDL2 (SDL_xboxjoystick.c xid_get_device_port)
 * already walks the USB hub topology and applies the OG Xbox's
 * port-permutation table -- the kernel exposes USB ports as
 * {3 -> port1, 4 -> port2, 1 -> port3, 2 -> port4}, which is why a
 * naive root-hub port_num lookup kept returning 1 for everything.
 *
 * Return the 0-based array index (player_index - 1), clamped to the
 * valid range. -1 if the controller isn't on a numbered port (e.g.
 * a USB hub-attached pad on a debug rig). */
static int port_idx_for_pad(SDL_GameController *pad)
{
    if (!pad) return -1;
    int player = SDL_GameControllerGetPlayerIndex(pad);
    if (player < 1 || player > JT_NUM_PORTS) return -1;
    return player - 1;
}

/* SDL trigger axis is 0..32767. The dc/ trigger field is 0..255 and
 * the tester display + screensaver are already calibrated to that
 * range, so downscale here rather than at every call site. */
static uint8_t trig_from_sdl(int16_t v)
{
    if (v < 0) return 0;
    return (uint8_t)(((unsigned)v * 255u) / 32767u);
}

/* Fill a jt_pad_state_t from the given SDL_GameController. Shared
 * between the primary pad slot and the daisy-chained second pad. */
static void read_pad_state(SDL_GameController *pad, jt_pad_state_t *p)
{
    uint32_t b = 0;
#define B(sdl, mask) do { if (SDL_GameControllerGetButton(pad, (sdl))) b |= (mask); } while (0)
    B(SDL_CONTROLLER_BUTTON_A,             JT_BTN_A);
    B(SDL_CONTROLLER_BUTTON_B,             JT_BTN_B);
    B(SDL_CONTROLLER_BUTTON_X,             JT_BTN_X);
    B(SDL_CONTROLLER_BUTTON_Y,             JT_BTN_Y);
    /* nxdk's SDL gamecontroller mapping puts Black on RIGHTSHOULDER
     * and White on LEFTSHOULDER (Duke shoulder buttons aren't real
     * "bumpers" but the mapping is the established convention). */
    B(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, JT_BTN_BLACK);
    B(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  JT_BTN_WHITE);
    B(SDL_CONTROLLER_BUTTON_BACK,          JT_BTN_BACK);
    B(SDL_CONTROLLER_BUTTON_START,         JT_BTN_START);
    B(SDL_CONTROLLER_BUTTON_LEFTSTICK,     JT_BTN_LSTICK);
    B(SDL_CONTROLLER_BUTTON_RIGHTSTICK,    JT_BTN_RSTICK);
    B(SDL_CONTROLLER_BUTTON_DPAD_UP,       JT_BTN_DPAD_UP);
    B(SDL_CONTROLLER_BUTTON_DPAD_DOWN,     JT_BTN_DPAD_DOWN);
    B(SDL_CONTROLLER_BUTTON_DPAD_LEFT,     JT_BTN_DPAD_LEFT);
    B(SDL_CONTROLLER_BUTTON_DPAD_RIGHT,    JT_BTN_DPAD_RIGHT);
#undef B
    p->buttons  = b;
    p->stick_lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    p->stick_ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    p->stick_rx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX);
    p->stick_ry = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY);
    p->trig_l   = trig_from_sdl(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
    p->trig_r   = trig_from_sdl(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));

    /* Analog button pressure. SDL_GameController only exposes 6 axes
     * on nxdk (two sticks + two triggers); nxdk's XID joystick driver
     * discards the analog-button bytes (A/B/X/Y/Black/White) when
     * filling its XINPUT_GAMEPAD shim. The raw 20-byte XID report
     * still lands in xid_dev->utr_list[0]->buff each interrupt-in
     * read, so we go pull pressure straight from there -- matched to
     * this pad via the SDL_Joystick instance ID, which nxdk pins to
     * the XID device's uid. */
    SDL_Joystick *joy = SDL_GameControllerGetJoystick(pad);
    p->instance_id = joy ? SDL_JoystickInstanceID(joy) : -1;
    if (joy) {
        jt_accessory_xid_pressure(p->instance_id,
                                  &p->abtn_a, &p->abtn_b,
                                  &p->abtn_x, &p->abtn_y,
                                  &p->abtn_black, &p->abtn_white);
    }
}

/* Populate the port's primary pad slot. */
static void poll_pad(int port_idx, SDL_GameController *pad)
{
    jt_port_state_t *port = &jt_ports[port_idx];
    port->present = true;
    port->style = JT_STYLE_PAD;
    const char *n = SDL_GameControllerName(pad);
    if (n) {
        strncpy(port->product_name, n, sizeof(port->product_name) - 1);
        port->product_name[sizeof(port->product_name) - 1] = '\0';
    } else {
        port->product_name[0] = '\0';
    }
    port->vendor_id  = SDL_GameControllerGetVendor(pad);
    port->product_id = SDL_GameControllerGetProduct(pad);
    read_pad_state(pad, &port->pad);
}

/* Populate the daisy-chained second pad slot for the port. */
static void poll_daisy_pad(int port_idx, SDL_GameController *pad)
{
    jt_port_state_t *port = &jt_ports[port_idx];
    port->has_daisy_pad = true;
    const char *n = SDL_GameControllerName(pad);
    if (n) {
        strncpy(port->daisy_product_name, n,
                sizeof(port->daisy_product_name) - 1);
        port->daisy_product_name[sizeof(port->daisy_product_name) - 1] = '\0';
    } else {
        port->daisy_product_name[0] = '\0';
    }
    port->daisy_vendor_id  = SDL_GameControllerGetVendor(pad);
    port->daisy_product_id = SDL_GameControllerGetProduct(pad);
    read_pad_state(pad, &port->daisy_pad);
}

void jt_ports_poll(void)
{
    /* Drain the SDL event queue: hot-plug events come through here,
     * and SDL_GameControllerGetButton/Axis read cached values that
     * are only refreshed when the event pump runs. */
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_CONTROLLERDEVICEADDED) {
            added_events_total++;
            SDL_GameController *pad = SDL_GameControllerOpen(e.cdevice.which);
            int idx = port_idx_for_pad(pad);
            if (pad && idx >= 0) {
                if (pad_for_port[idx] == NULL) {
                    pad_for_port[idx] = pad;
                } else if (daisy_pad_for_port[idx] == NULL) {
                    daisy_pad_for_port[idx] = pad;
                } else {
                    /* Both slots taken -- no place to put a third
                     * pad on the same chassis port. */
                    SDL_GameControllerClose(pad);
                }
            } else if (pad) {
                SDL_GameControllerClose(pad);
            }
        }
        else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
            removed_events_total++;
            SDL_GameController *pad =
                SDL_GameControllerFromInstanceID(e.cdevice.which);
            for (int i = 0; i < JT_NUM_PORTS; i++) {
                if (pad_for_port[i] == pad) {
                    /* Promote the daisy to primary so the layout
                     * stays stable when the original is unplugged. */
                    pad_for_port[i] = daisy_pad_for_port[i];
                    daisy_pad_for_port[i] = NULL;
                } else if (daisy_pad_for_port[i] == pad) {
                    daisy_pad_for_port[i] = NULL;
                }
            }
            if (pad) SDL_GameControllerClose(pad);
        }
    }

    for (int p = 0; p < JT_NUM_PORTS; p++) {
        jt_port_state_t *port = &jt_ports[p];
        /* Clear the daisy slot every frame and re-populate it below
         * if a second pad is currently attached. */
        port->has_daisy_pad = false;
        port->daisy_product_name[0] = '\0';
        port->daisy_vendor_id = port->daisy_product_id = 0;
        memset(&port->daisy_pad, 0, sizeof(port->daisy_pad));

        SDL_GameController *pad = pad_for_port[p];
        if (!pad || !SDL_GameControllerGetAttached(pad)) {
            port->present = false;
            port->style = JT_STYLE_EMPTY;
            port->product_name[0] = '\0';
            port->vendor_id = port->product_id = 0;
            memset(&port->pad, 0, sizeof(port->pad));
            for (int s = 0; s < JT_NUM_SLOTS; s++) {
                port->slots[s].kind = JT_SLOT_EMPTY;
                port->slots[s].block_free = port->slots[s].block_total = -1;
            }
            continue;
        }
        poll_pad(p, pad);

        SDL_GameController *daisy = daisy_pad_for_port[p];
        if (daisy && SDL_GameControllerGetAttached(daisy)) {
            poll_daisy_pad(p, daisy);
        }
        /* Slot accessory population happens in jt_accessories_tick
         * (driven from main.c); ports.c only needs to clear the slot
         * state here so the tester's slot field can read accessories.c
         * directly via jt_accessory_for_slot. */
        for (int s = 0; s < JT_NUM_SLOTS; s++) {
            port->slots[s].kind = JT_SLOT_EMPTY;
            port->slots[s].block_free = port->slots[s].block_total = -1;
        }
    }

    /* For non-pad ports the tester now enumerates devices itself
     * (a USB hub can put a keyboard AND a mouse behind one Xbox
     * port; each renders its own row). ports.c just flags the port
     * as present if anything is plugged in -- style stays EMPTY
     * because there's no single "primary" device to label. */
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        if (jt_ports[p].present) continue;
        if (jt_accessory_dvd_remote_at_port(p)        ||
            jt_accessory_steel_battalion_at_port(p)   ||
            jt_accessory_keyboard_present(p)          ||
            jt_accessory_mouse_present(p)             ||
            jt_accessory_mu_direct_at_port(p, NULL)   ||
            jt_accessory_headset_direct_at_port(p)    ||
            jt_accessory_hub_direct_at_port(p)) {
            jt_ports[p].present = true;
            jt_ports[p].style   = JT_STYLE_OTHER;
            jt_ports[p].product_name[0] = '\0';
        }
    }
}

void jt_port_rumble(int port_idx, uint16_t strong, uint16_t weak,
                    uint32_t duration_ms)
{
    if (port_idx < 0 || port_idx >= JT_NUM_PORTS) return;
    SDL_GameController *pad = pad_for_port[port_idx];
    if (!pad) return;
    SDL_GameControllerRumble(pad, strong, weak, duration_ms);
}

void jt_port_rumble_daisy(int port_idx, uint16_t strong, uint16_t weak,
                          uint32_t duration_ms)
{
    if (port_idx < 0 || port_idx >= JT_NUM_PORTS) return;
    SDL_GameController *pad = daisy_pad_for_port[port_idx];
    if (!pad) return;
    SDL_GameControllerRumble(pad, strong, weak, duration_ms);
}

const char *jt_port_style_name(jt_port_style_t s)
{
    switch (s) {
        case JT_STYLE_EMPTY:    return "Empty";
        case JT_STYLE_PAD:      return "Pad";
        case JT_STYLE_REMOTE:   return "Remote";
        case JT_STYLE_KEYBOARD: return "Keyboard";
        case JT_STYLE_MOUSE:    return "Mouse";
        case JT_STYLE_STEEL:    return "SteelBtl";
        case JT_STYLE_OTHER:    return "Other";
    }
    return "?";
}

const char *jt_slot_kind_name(jt_slot_kind_t k)
{
    switch (k) {
        case JT_SLOT_EMPTY: return "Empty";
        case JT_SLOT_MU:    return "MU";
        case JT_SLOT_VOICE: return "Voice";
        case JT_SLOT_OTHER: return "Other";
    }
    return "?";
}
