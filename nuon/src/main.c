/*
 * Joypad Tester - NUON
 *
 * Reads the BIOS _Controller[] array each frame and renders the live
 * state of all four NUON joypad ports plus the IR remote on screen.
 *
 * Built against the public BIOS joystick API documented in
 * nuon/joystick.h (the ButtonX() macros, _DeviceDetect(), the
 * ControllerData struct fields). No VM Labs sample source is copied
 * -- the rendering loop, layout, and string composition are original.
 *
 * Copyright (c) 2026 Robert Dale Smith
 * MIT License -- see ../LICENSE.md
 */

#include <nuon/dma.h>
#include <nuon/mutil.h>
#include <nuon/msprintf.h>
#include <nuon/bios.h>
#include <nuon/mml2d.h>
#include <nuon/joystick.h>

#define SCREENW   (720)
#define SCREENH   (480)
#define NUM_PORTS (4)

/* Y-Cr-Cb-Alpha colorspace (NUON's native framebuffer format). The
 * kXxx constants come from mml2d.h; we define a few extras for the
 * connected / dim / held / label states so they read at a glance. */
#define BG_COLOR    (0x10808000)  /* kBlack */
#define ACTIVE_CLR  (0x91233700)  /* bright green */
#define DIM_CLR     (0x47808000)  /* dark grey */
#define HELD_CLR    (0xd2921100)  /* yellow -- button is held this frame */
#define LBL_CLR     (0xb5808000)  /* light grey -- field labels */

mmlGC            gl_gc;
mmlSysResources  gl_sysRes;

/* Double-buffered framebuffer. The video hardware scans out
 * continuously from whichever buffer mmlSimpleVideoSetup last pointed
 * at; a single-buffer redraw loop tears visibly. We draw into
 * gl_screenbuffers[gl_drawbuffer], swap on vblank, and the next
 * iteration's clear+redraw targets the (now-not-displayed) other
 * buffer. */
mmlDisplayPixmap gl_screenbuffers[2];
int              gl_drawbuffer    = 1;
int              gl_displaybuffer = 0;

/* Fill the active draw buffer via 8x8 DMA tiles -- the SDK's
 * mmlInitDisplayPixmaps reserves backing memory but doesn't clear it,
 * and _DMABiLinear with DMA_DIRECT_BIT is the documented fast-path
 * for plotting a constant color. */
static void clear_screen(void)
{
    mmlDisplayPixmap *p = &gl_screenbuffers[gl_drawbuffer];
    long x, y;
    for (x = 0; x < p->wide; x += 8) {
        for (y = 0; y < p->high; y += 8) {
            _DMABiLinear(p->dmaFlags | DMA_DIRECT_BIT,
                         p->memP,
                         (8 << 16) | x,
                         (8 << 16) | y,
                         (void *)BG_COLOR);
        }
    }
}

static void draw_text(int x, int y, long color, const char *s)
{
    mmlDisplayPixmap *p = &gl_screenbuffers[gl_drawbuffer];
    DebugWS(p->dmaFlags, p->memP, x, y, color, s);
}

static void swap_screenbuffers(void)
{
    int tmp = gl_displaybuffer;
    gl_displaybuffer = gl_drawbuffer;
    gl_drawbuffer = tmp;
    mmlSimpleVideoSetup(&gl_screenbuffers[gl_displaybuffer],
                        &gl_sysRes, eTwoTapVideoFilter);
}

/* Pick HELD_CLR if `pressed` is non-zero so the held buttons jump
 * out visually against the dim grey labels for unpressed ones. */
static long held(int pressed)
{
    return pressed ? HELD_CLR : DIM_CLR;
}

static void render_port(int idx, int row_y)
{
    char buf[SPRINTF_MAX];
    /* The ButtonA() / ButtonB() / ... macros expand to "a.buttons &
     * MASK" with no parens around `a`, so we need a struct lvalue
     * (not a pointer-deref expression). Copy the volatile BIOS slot
     * into a non-volatile local once per frame; that snapshot is what
     * we render. */
    ControllerData c = _Controller[idx];
    int connected = c.status ? 1 : 0;

    /* Headers stay color-coded by connection state. We render the
     * button row regardless of status so users can still see the
     * `buttons` field even on slots the BIOS reports as unplugged
     * (useful for diagnosing non-standard controllers whose detect
     * bit doesn't set). */
    msprintf(buf, "P%d %s", idx + 1, connected ? "ON" : "--");
    draw_text(16, row_y, connected ? ACTIVE_CLR : DIM_CLR, buf);

    /* Face buttons: A B, then shoulders L R, then Start, then Z
     * (the NUON-button / Select equivalent). */
    msprintf(buf, "A");  draw_text( 80, row_y, held(ButtonA(c)),     buf);
    msprintf(buf, "B");  draw_text( 96, row_y, held(ButtonB(c)),     buf);
    msprintf(buf, "L");  draw_text(112, row_y, held(ButtonL(c)),     buf);
    msprintf(buf, "R");  draw_text(128, row_y, held(ButtonR(c)),     buf);
    msprintf(buf, "St"); draw_text(148, row_y, held(ButtonStart(c)), buf);
    msprintf(buf, "Z");  draw_text(172, row_y, held(ButtonZ(c)),     buf);

    /* D-pad. */
    msprintf(buf, "U"); draw_text(200, row_y, held(ButtonUp(c)),    buf);
    msprintf(buf, "D"); draw_text(216, row_y, held(ButtonDown(c)),  buf);
    msprintf(buf, "L"); draw_text(232, row_y, held(ButtonLeft(c)),  buf);
    msprintf(buf, "R"); draw_text(248, row_y, held(ButtonRight(c)), buf);

    /* C-buttons (directional cluster around the right thumb). */
    msprintf(buf, "cU"); draw_text(276, row_y, held(ButtonCUp(c)),    buf);
    msprintf(buf, "cD"); draw_text(296, row_y, held(ButtonCDown(c)),  buf);
    msprintf(buf, "cL"); draw_text(316, row_y, held(ButtonCLeft(c)),  buf);
    msprintf(buf, "cR"); draw_text(336, row_y, held(ButtonCRight(c)), buf);

    /* Analog axes. msprintf in this SDK doesn't understand %+4d /
     * width modifiers, so we use plain %d and let labels handle the
     * sign visibility. */
    msprintf(buf, "X %d Y %d   ", (int)c.d1.xAxis, (int)c.d2.yAxis);
    draw_text(372, row_y, LBL_CLR, buf);

    /* Raw 22-bit `properties` word -- identifies the controller
     * family (standard pad vs. wheel / lightgun / fishing rod /
     * mouse / etc.). */
    msprintf(buf, "P:%08x", (int)c.properties);
    draw_text(484, row_y, LBL_CLR, buf);

    /* Raw scalar-2 / scalar-3 tail. For non-standard peripherals
     * these bytes carry secondary axes, throttle/brake, mouse X/Y
     * deltas, spinner values, etc. */
    msprintf(buf, "%02x %02x %02x %02x",
             (int)c.d3.xAxis2  & 0xff,
             (int)c.d4.yAxis2  & 0xff,
             (int)c.d5.spinner1 & 0xff,
             (int)c.d6.spinner2 & 0xff);
    draw_text(580, row_y, LBL_CLR, buf);
}

/* The IR remote populates _Controller[0].remote_buttons only -- a
 * 32-bit field whose bits map to physical buttons on whichever model
 * of DVD remote came with the player. The mapping isn't uniform
 * across Samsung / RCA / Toshiba units, so we dump the raw bits and
 * let the user identify their remote by pressing each button. */
static void render_remote(int row_y)
{
    char buf[SPRINTF_MAX];
    unsigned long bits = _Controller[0].remote_buttons;
    int i, pos;

    draw_text(16, row_y, ACTIVE_CLR, "IR");

    /* Most-significant bit first, grouped in bytes for readability. */
    pos = 80;
    for (i = 31; i >= 0; i--) {
        char digit[2];
        digit[0] = (bits & (1UL << i)) ? '1' : '0';
        digit[1] = '\0';
        draw_text(pos, row_y, (bits & (1UL << i)) ? HELD_CLR : DIM_CLR, digit);
        pos += 9;
        if (i % 8 == 0 && i != 0) {
            draw_text(pos, row_y, DIM_CLR, "-");
            pos += 9;
        }
    }

    msprintf(buf, "%08x", (int)bits);
    draw_text(pos + 16, row_y, LBL_CLR, buf);
}

int main(void)
{
    int i;
    int row_y;

    mmlPowerUpGraphics(&gl_sysRes);
    mmlInitGC(&gl_gc, &gl_sysRes);
    mmlInitDisplayPixmaps(&gl_screenbuffers[0], &gl_sysRes,
                          SCREENW, SCREENH, e888Alpha, 1, 0);
    mmlInitDisplayPixmaps(&gl_screenbuffers[1], &gl_sysRes,
                          SCREENW, SCREENH, e888Alpha, 1, 0);

    /* Pre-clear both buffers. mmlInitDisplayPixmaps reserves SDRAM
     * but doesn't initialize it; without an explicit clear the first
     * frame would show whichever buffer the video hardware starts
     * reading full of garbage. */
    {
        int saved = gl_drawbuffer;
        gl_drawbuffer = 0; clear_screen();
        gl_drawbuffer = 1; clear_screen();
        gl_drawbuffer = saved;
    }

    mmlSimpleVideoSetup(&gl_screenbuffers[gl_displaybuffer],
                        &gl_sysRes, eTwoTapVideoFilter);

    while (1) {
        clear_screen();

        draw_text(248, 24, kYellow, "Joypad Tester - NUON");

        /* Probe slots 1..3 every frame -- BIOS auto-polls slot 0 on
         * hardware, but slots 1+ need an explicit detect call to pick
         * up hot-plug. */
        for (i = 1; i < NUM_PORTS; i++) {
            _DeviceDetect(i);
        }

        for (i = 0; i < NUM_PORTS; i++) {
            row_y = 96 + (i * 40);
            render_port(i, row_y);
        }

        row_y = 96 + (NUM_PORTS * 40) + 32;
        render_remote(row_y);

        swap_screenbuffers();
        _VidSync(0);
    }

    return 0;
}
