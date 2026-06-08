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
#include "mu.h"

#include <SDL.h>
#include <string.h>

jt_port_state_t jt_ports[JT_NUM_PORTS];

/* SDL_GameController* per port, keyed by player index. NULL slots
 * are "no pad in this port". Refreshed from CONTROLLERDEVICEADDED /
 * REMOVED events by ports_poll. */
static SDL_GameController *pad_for_port[JT_NUM_PORTS];

void jt_ports_init(void)
{
    memset(jt_ports, 0, sizeof(jt_ports));
    memset(pad_for_port, 0, sizeof(pad_for_port));
    SDL_GameControllerEventState(SDL_ENABLE);
    jt_mu_init();
}

/* Frame counter so we re-probe MUs at a sane cadence rather than
 * every frame -- nxMountDrive on an empty slot is cheap but probing
 * 8 of them per vblank is still wasted maple-equivalent traffic. */
static uint32_t poll_counter = 0;
#define MU_REFRESH_STRIDE 60   /* ~1 second at 60 Hz */

static int port_for_pad(SDL_GameController *pad)
{
    if (!pad) return -1;
    int idx = SDL_GameControllerGetPlayerIndex(pad);
    if (idx < 0 || idx >= JT_NUM_PORTS) return -1;
    return idx;
}

/* SDL trigger axis is 0..32767. The dc/ trigger field is 0..255 and
 * the tester display + screensaver are already calibrated to that
 * range, so downscale here rather than at every call site. */
static uint8_t trig_from_sdl(int16_t v)
{
    if (v < 0) return 0;
    return (uint8_t)(((unsigned)v * 255u) / 32767u);
}

static void poll_pad(int port_idx, SDL_GameController *pad)
{
    jt_port_state_t *port = &jt_ports[port_idx];
    port->present = true;
    port->style = JT_STYLE_PAD;
    /* SDL exposes vendor / product but not the wire product_name on
     * Xbox -- the device's USB descriptor string isn't always meaningful
     * (most pads report a generic "Microsoft X-Box Controller"). Use
     * SDL_GameControllerName which falls back to the SDL mapping name
     * when the descriptor is empty. */
    const char *n = SDL_GameControllerName(pad);
    if (n) {
        strncpy(port->product_name, n, sizeof(port->product_name) - 1);
        port->product_name[sizeof(port->product_name) - 1] = '\0';
    } else {
        port->product_name[0] = '\0';
    }
    port->vendor_id  = SDL_GameControllerGetVendor(pad);
    port->product_id = SDL_GameControllerGetProduct(pad);

    jt_pad_state_t *p = &port->pad;
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

    /* Analog button pressure isn't exposed by the gamecontroller
     * layer; SDL_Joystick has it on extra axes but nxdk's mapping
     * currently only surfaces triggers. Leave the abtn_* fields at
     * 0 -- the tester display falls back to the digital bits. */
}

void jt_ports_poll(void)
{
    /* Drain the SDL event queue: hot-plug events come through here,
     * and SDL_GameControllerGetButton/Axis read cached values that
     * are only refreshed when the event pump runs. */
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_CONTROLLERDEVICEADDED) {
            SDL_GameController *pad = SDL_GameControllerOpen(e.cdevice.which);
            int idx = port_for_pad(pad);
            if (idx >= 0) pad_for_port[idx] = pad;
        }
        else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
            SDL_GameController *pad =
                SDL_GameControllerFromInstanceID(e.cdevice.which);
            int idx = port_for_pad(pad);
            if (idx >= 0) pad_for_port[idx] = NULL;
            if (pad) SDL_GameControllerClose(pad);
        }
    }

    for (int p = 0; p < JT_NUM_PORTS; p++) {
        jt_port_state_t *port = &jt_ports[p];
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

        /* Memory Units. jt_mu_index_for handles the convention
         * mapping (port * NUM_SLOTS + slot); jt_mu_get returns NULL
         * for an out-of-range index and a present=false entry for
         * an empty slot. Voice / other slot kinds aren't differentiated
         * yet -- present means "we got a FATX mount", anything else
         * stays EMPTY. */
        for (int s = 0; s < JT_NUM_SLOTS; s++) {
            int mu = jt_mu_index_for(p, s);
            const jt_mu_info_t *info = (mu >= 0) ? jt_mu_get(mu) : NULL;
            if (info && info->present) {
                port->slots[s].kind = JT_SLOT_MU;
                /* Surface KB as our "block" count -- MUs are small
                 * enough that KB fits in 4 digits and the tester
                 * label has room. The block_total field stays as
                 * total KB so the display can compute used%. */
                port->slots[s].block_free  = (int16_t)(info->free_kb / 1);
                port->slots[s].block_total = (int16_t)(info->total_kb / 1);
                if (info->free_kb  > 32767) port->slots[s].block_free  = 32767;
                if (info->total_kb > 32767) port->slots[s].block_total = 32767;
            } else {
                port->slots[s].kind = JT_SLOT_EMPTY;
                port->slots[s].block_free = port->slots[s].block_total = -1;
            }
        }
    }

    if ((++poll_counter % MU_REFRESH_STRIDE) == 0) jt_mu_refresh();
}

void jt_port_rumble(int port_idx, uint16_t strong, uint16_t weak,
                    uint32_t duration_ms)
{
    if (port_idx < 0 || port_idx >= JT_NUM_PORTS) return;
    SDL_GameController *pad = pad_for_port[port_idx];
    if (!pad) return;
    SDL_GameControllerRumble(pad, strong, weak, duration_ms);
}

const char *jt_port_style_name(jt_port_style_t s)
{
    switch (s) {
        case JT_STYLE_EMPTY: return "Empty";
        case JT_STYLE_PAD:   return "Pad";
        case JT_STYLE_OTHER: return "Other";
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
