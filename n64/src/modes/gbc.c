/*
 * gbc.c — Game Boy Camera viewer mode.
 *
 * Port of meeq's GBCameraTest.c (public domain) using libdragon
 * trunk's synchronous tpak_* API (libdragon trunk doesn't expose the
 * joypad-subsystem's transfer-pak async path yet). Each capture is a
 * blocking Joybus exchange of ~3.5 KB pixel data which takes a few
 * hundred ms; the UI is "press A to grab a fresh frame" rather than
 * a true live viewfinder.
 *
 * Pipeline per capture:
 *   1. tpak_init(port) -- power the Transfer Pak, set access mode.
 *   2. Enable GB cart RAM (write 0x0A to 0x0000).
 *   3. Switch RAM bank to camera-regs (0x10 -> 0x4000).
 *   4. Write the camera register block twice -- once with
 *      capture_mode=0 (init), then with capture_mode=1 (trigger).
 *   5. Poll regs[0] bit 0 until capture completes.
 *   6. Switch RAM bank to camera SRAM (0x00 -> 0x4000).
 *   7. Read 128x112 / 4 = 3584 bytes of raw 2bpp planar data from
 *      0xA100.
 *   8. Process planar -> RGBA32, store for display.
 *
 * Register layout + processing kernel are lifted from meeq's file
 * (which itself documents the GB Camera ASIC threshold-matrix and
 * planar-tile decode). Default exposure / gain / matrix-slope/offset
 * values picked so a reasonable indoor capture works without
 * tuning.
 */

#include "gbc.h"
#include "../ui/text.h"

#include <libdragon.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define GB_CART_TYPE_CAMERA       0xFC
#define GB_CART_ADDR_RAM_ENABLE   0x0000
#define GB_CART_ADDR_RAM_BANK     0x4000
#define GB_CART_ADDR_CAMERA_REGS  0xA000
#define GB_CART_ADDR_CAMERA_DATA  0xA100
#define GB_RAM_ENABLE             0x0A
#define GB_RAM_BANK_CAMERA_REGS   0x10
#define GB_RAM_BANK_CAMERA_SRAM   0x00

#define CAM_W                     128
#define CAM_H                     112
#define CAM_RAW_BYTES             (CAM_W * CAM_H * 2 / 8)   /* 2bpp planar */
#define CAM_PIXELS                (CAM_W * CAM_H)

/* Camera defaults from meeq -- exposure 0x1000 is mid-range, gain 5
 * for indoor lighting. Tuning a UI for these is a future polish item. */
#define CAM_FILTER_POSITIVE       0b01
#define CAM_INIT_GAIN             5
#define CAM_INIT_EXPOSURE         0x1000
#define CAM_INIT_VREF_BIAS        0b101            /* 2.5 */
#define CAM_INIT_EDGE_RATIO       0b0010           /* 100% */
#define CAM_INIT_CALIBRATE        0b10             /* positive */
#define CAM_INIT_MATRIX_SLOPE     10
#define CAM_INIT_MATRIX_OFFSET    100

/* Authentic Game Boy 4-shade green palette in RGBA32. */
static const uint32_t GB_PALETTE[4] = {
    0x9bbc0fff,  /* lightest */
    0x8bac0fff,
    0x306230ff,
    0x0f380fff,  /* darkest */
};

typedef struct __attribute__((packed)) {
    uint8_t capture_lo;        /* bit 0 = capture in progress */
    uint8_t filter_gain;       /* filter[2:1] | gain[4:0] -- packed */
    uint8_t edge_vh;           /* edge_vh_mode + edge_v_only */
    uint8_t exposure_hi;
    uint8_t exposure_lo;
    uint8_t vref_invert;       /* vref_bias[2:0] | invert[3] | edge_ratio[7:4] */
    uint8_t vref_offset_cal;   /* vref_offset[5:0] | calibrate[7:6] */
    uint8_t matrix[48];
    uint8_t padding[10];
} cam_regs_t;

static const uint8_t matrix_layout[16] = {
    0x00, 0x1e, 0x18, 0x06,
    0x0f, 0x2d, 0x27, 0x15,
    0x0c, 0x2a, 0x24, 0x12,
    0x03, 0x21, 0x1b, 0x09,
};

/* Build the camera matrix from a slope+offset pair, per the GB Camera
 * ROM's 16-entry interpolation table at $7C20. Lifted verbatim from
 * meeq's gb_camera_set_matrix. */
static void build_matrix(uint8_t slope, uint8_t offset, uint8_t matrix[48])
{
    uint8_t qlevels[4];
    for (int c = 0; c < 4; c++) {
        uint16_t v = offset + (c * slope);
        qlevels[c] = (v < 256) ? (uint8_t)v : 255;
    }
    for (int c = 0; c < 3; c++) {
        uint16_t acc = qlevels[c];
        uint16_t inc = qlevels[c + 1] - acc;
        acc <<= 4;     /* 4.4 fixed point */
        for (int pixel = 0; pixel < 16; pixel++) {
            matrix[c + matrix_layout[pixel]] = (uint8_t)(acc >> 4);
            acc += inc;
        }
    }
}

static void cam_regs_init(cam_regs_t *r, bool trigger)
{
    memset(r, 0, sizeof(*r));
    r->capture_lo  = trigger ? 0x01 : 0x00;
    /* filter_mode in bits 2:1, gain in 4:0 of byte 1 (per GB Camera
     * register packing). */
    r->filter_gain = (CAM_FILTER_POSITIVE << 1) | (CAM_INIT_GAIN & 0x1f);
    r->edge_vh     = 0;            /* no edge enhancement */
    r->exposure_hi = (CAM_INIT_EXPOSURE >> 8) & 0xff;
    r->exposure_lo = (CAM_INIT_EXPOSURE     ) & 0xff;
    r->vref_invert = CAM_INIT_VREF_BIAS | (CAM_INIT_EDGE_RATIO << 4);
    r->vref_offset_cal = (CAM_INIT_CALIBRATE << 6);  /* offset=0 */
    build_matrix(CAM_INIT_MATRIX_SLOPE, CAM_INIT_MATRIX_OFFSET, r->matrix);
}

/* 128x112 planar 2bpp -> 128x112 RGBA32. Each 8x8 tile is 16 bytes
 * (8 rows of 2-byte planar). Tile order: row-major across 16 tiles
 * per row, 14 rows of tiles. */
static void process_raw(const uint8_t *raw, uint32_t *out)
{
    uint32_t i = 0;
    for (int yt = 0; yt < CAM_H; yt++) {
        uint32_t yto = (yt & 7) + ((yt & 0x78) << 4);
        for (int xt = 0; xt < 16; xt++) {
            uint32_t addr = ((xt << 3) + yto) << 1;
            uint8_t lo = raw[addr];
            uint8_t hi = raw[addr + 1];
            for (int x = 0; x < 8; x++) {
                uint8_t shade = (hi & 0x80) ? 2 : 0;
                if (lo & 0x80) shade |= 1;
                out[i++] = GB_PALETTE[shade];
                lo <<= 1;
                hi <<= 1;
            }
        }
    }
}

/* === Per-mode state. === */

typedef enum {
    GBC_NEEDS_TPAK = 0,
    GBC_NEEDS_CAMERA,
    GBC_READY,
    GBC_CAPTURING,
    GBC_HAVE_IMAGE,
} gbc_phase_t;

static gbc_phase_t phase = GBC_NEEDS_TPAK;
static int         active_port = -1;
static uint32_t    image_rgba[CAM_PIXELS];
static bool        image_valid = false;
static char        last_msg[64];

static void set_msg(const char *s)
{
    snprintf(last_msg, sizeof(last_msg), "%s", s);
}

static int find_camera_port(void)
{
    JOYPAD_PORT_FOREACH (p) {
        if (joypad_get_accessory_type(p) == JOYPAD_ACCESSORY_TYPE_TRANSFER_PAK) {
            struct gameboy_cartridge_header hdr;
            if (tpak_init(p) != 0)               continue;
            if (tpak_get_cartridge_header(p, &hdr) != 0) continue;
            if (hdr.cartridge_type != GB_CART_TYPE_CAMERA) continue;
            return (int)p;
        }
    }
    return -1;
}

static bool do_capture(int port)
{
    set_msg("Capturing...");

    cam_regs_t regs_init;
    cam_regs_t regs_trigger;
    cam_regs_init(&regs_init,    false);
    cam_regs_init(&regs_trigger, true);

    uint8_t enable_byte[32] = {0};
    uint8_t bank_regs [32] = {0};
    uint8_t bank_sram [32] = {0};
    memset(enable_byte, GB_RAM_ENABLE,             sizeof(enable_byte));
    memset(bank_regs,   GB_RAM_BANK_CAMERA_REGS,   sizeof(bank_regs));
    memset(bank_sram,   GB_RAM_BANK_CAMERA_SRAM,   sizeof(bank_sram));

    if (tpak_write(port, GB_CART_ADDR_RAM_ENABLE, enable_byte, sizeof(enable_byte)) != 0) {
        set_msg("RAM enable write failed"); return false;
    }
    if (tpak_write(port, GB_CART_ADDR_RAM_BANK,   bank_regs,   sizeof(bank_regs))   != 0) {
        set_msg("Cam regs bank write failed"); return false;
    }
    if (tpak_write(port, GB_CART_ADDR_CAMERA_REGS, (uint8_t *)&regs_init,    sizeof(regs_init)) != 0) {
        set_msg("Regs init write failed"); return false;
    }
    if (tpak_write(port, GB_CART_ADDR_CAMERA_REGS, (uint8_t *)&regs_trigger, sizeof(regs_trigger)) != 0) {
        set_msg("Regs trigger write failed"); return false;
    }

    /* Poll regs[0].bit0 for capture-done. ~10 retries should be
     * generous for indoor lighting. */
    bool done = false;
    for (int retry = 0; retry < 20 && !done; retry++) {
        uint8_t poll[32];
        if (tpak_read(port, GB_CART_ADDR_CAMERA_REGS, poll, sizeof(poll)) != 0) {
            set_msg("Capture poll read failed"); return false;
        }
        if (!(poll[0] & 0x01)) done = true;
        else wait_ms(50);
    }
    if (!done) { set_msg("Capture never finished"); return false; }

    if (tpak_write(port, GB_CART_ADDR_RAM_BANK, bank_sram, sizeof(bank_sram)) != 0) {
        set_msg("SRAM bank switch failed"); return false;
    }
    uint8_t raw[CAM_RAW_BYTES];
    if (tpak_read(port, GB_CART_ADDR_CAMERA_DATA, raw, sizeof(raw)) != 0) {
        set_msg("SRAM read failed"); return false;
    }
    process_raw(raw, image_rgba);
    image_valid = true;
    set_msg("Capture complete");
    return true;
}

static void gbc_enter(void)
{
    phase       = GBC_NEEDS_TPAK;
    active_port = -1;
    image_valid = false;
    last_msg[0] = '\0';
}

static void gbc_update(void)
{
    /* (Re-)detect on every frame so plugging/unplugging works
     * without leaving the mode. Cheap when no TPak is present. */
    int port = find_camera_port();
    if (port < 0) {
        active_port = -1;
        phase = (joypad_get_accessory_type(JOYPAD_PORT_1) == JOYPAD_ACCESSORY_TYPE_TRANSFER_PAK)
                ? GBC_NEEDS_CAMERA : GBC_NEEDS_TPAK;
        return;
    }
    if (port != active_port) {
        active_port = port;
        phase = GBC_READY;
        set_msg("GB Camera ready. Press A to capture.");
    }

    /* A on any pad triggers a capture. */
    JOYPAD_PORT_FOREACH (p) {
        joypad_buttons_t pressed = joypad_get_buttons_pressed(p);
        if (pressed.a) {
            phase = GBC_CAPTURING;
            bool ok = do_capture(active_port);
            phase = ok ? GBC_HAVE_IMAGE : GBC_READY;
            break;
        }
    }
}

static void gbc_draw(void)
{
    surface_t *surf = display_get();
    graphics_fill_screen(surf, graphics_make_color(0, 0, 0, 0xff));

    int screen_w = surf->width  ? surf->width  : 320;

    /* Title: same yellow + centred as every other page. */
    txt_draw_centered(surf, 12, JT_COL_TITLE, "Game Boy Camera Viewer", screen_w);

    int y = 32;

    /* When we have an image, centre it horizontally. The original
     * layout pinned to a hard-coded x=176 (assumes 320-px surface),
     * which drifted off-screen on display modes where libdragon
     * returns a wider framebuffer. */
    if (image_valid) {
        int img_x = (screen_w - CAM_W) / 2;
        for (int row = 0; row < CAM_H; row++) {
            for (int col = 0; col < CAM_W; col++) {
                uint32_t rgba = image_rgba[row * CAM_W + col];
                uint8_t r = (rgba >> 24) & 0xff;
                uint8_t g = (rgba >> 16) & 0xff;
                uint8_t b = (rgba >>  8) & 0xff;
                graphics_draw_pixel(surf, img_x + col, y + row,
                                    graphics_make_color(r, g, b, 0xff));
            }
        }
        y += CAM_H + 8;
    }

    /* Status line + per-port + capture hint -- all centred under the
     * image (or under the title when no image yet). */
    const char *status;
    uint32_t status_col = JT_COL_LABEL;
    switch (phase) {
        case GBC_NEEDS_TPAK:    status = "Plug in a Transfer Pak"; break;
        case GBC_NEEDS_CAMERA:  status = "Insert GB Camera cartridge"; break;
        case GBC_READY:         status = "GB Camera ready";  status_col = JT_COL_ACTIVE; break;
        case GBC_CAPTURING:     status = "Capturing...";     status_col = JT_COL_HELD;   break;
        case GBC_HAVE_IMAGE:    status = "Photo captured";   status_col = JT_COL_ACTIVE; break;
        default:                status = "";                 break;
    }
    txt_draw_centered(surf, y, status_col, status, screen_w);
    y += TXT_GLYPH_H + 4;

    if (active_port >= 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Port: %d", active_port + 1);
        txt_draw_centered(surf, y, JT_COL_LABEL, buf, screen_w);
        y += TXT_GLYPH_H + 4;
    }

    txt_draw_centered(surf, y, JT_COL_LABEL, "A: capture", screen_w);
    y += TXT_GLYPH_H + 4;

    if (last_msg[0]) {
        char buf[80];
        snprintf(buf, sizeof(buf), ">> %s", last_msg);
        txt_draw_centered(surf, y, JT_COL_DIM, buf, screen_w);
    }

    /* Footer hint: shared helper places it inside overscan. */
    txt_draw_footer(surf, "Start: options menu");
    display_show(surf);
}

const jt_mode_t jt_mode_gbc = {
    .name   = "Game Boy Camera Viewer",
    .enter  = gbc_enter,
    .leave  = NULL,
    .update = gbc_update,
    .draw   = gbc_draw,
};
