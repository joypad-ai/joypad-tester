/*
 * cursor.c — unified cursor driver.
 *
 * Reads jt_ports[] every frame and integrates motion from any pad's
 * analog stick + any mouse's deltas. The "active source" flips to
 * whichever input produced non-zero motion this frame, which
 * controls cursor rendering (e.g. show a crosshair for mouse,
 * a square for pad) in mode-specific code.
 *
 * Pad motion uses sub-pixel accumulation so slow tilts still produce
 * 1-pixel cursor moves. Mouse motion is already in mouse-units (one
 * count ≈ one pixel) and gets applied 1:1.
 */
#include <dc/maple/controller.h>
#include <dc/maple/mouse.h>

#include "cursor.h"
#include "../ports/ports.h"

jt_cursor_t jt_cursor;

#define SCREEN_W 640
#define SCREEN_H 480
#define PAD_DEADZONE 12      /* out of 128 */
#define PAD_SPEED 240.0f     /* pixels/sec at full tilt */

void jt_cursor_init(int initial_x, int initial_y)
{
    jt_cursor.x = initial_x;
    jt_cursor.y = initial_y;
    jt_cursor.sub_x = 0;
    jt_cursor.sub_y = 0;
    jt_cursor.source = JT_CURSOR_SRC_PAD;
    jt_cursor.button_a = false;
    jt_cursor.button_b = false;
}

static int clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void jt_cursor_update(float dt)
{
    int moved_pad   = 0;
    int moved_mouse = 0;
    bool a_held = false;
    bool b_held = false;

    for (int p = 0; p < JT_NUM_PORTS; p++) {
        jt_port_state_t *port = &jt_ports[p];
        if (!port->present) continue;

        if (port->style == JT_STYLE_PAD) {
            int sx = port->pad.stick_x;
            int sy = port->pad.stick_y;
            if (sx >  PAD_DEADZONE || sx < -PAD_DEADZONE ||
                sy >  PAD_DEADZONE || sy < -PAD_DEADZONE) {
                float fx = (sx / 128.0f) * PAD_SPEED * dt;
                float fy = (sy / 128.0f) * PAD_SPEED * dt;
                jt_cursor.sub_x += fx;
                jt_cursor.sub_y += fy;
                moved_pad = 1;
            }
            if (port->pad.buttons & CONT_A) a_held = true;
            if (port->pad.buttons & CONT_B) b_held = true;
        }

        if (port->style == JT_STYLE_MOUSE) {
            if (port->mouse.dx || port->mouse.dy) {
                jt_cursor.x += port->mouse.dx;
                jt_cursor.y += port->mouse.dy;
                moved_mouse = 1;
            }
            if (port->mouse.buttons & MOUSE_LEFTBUTTON)  a_held = true;
            if (port->mouse.buttons & MOUSE_RIGHTBUTTON) b_held = true;
        }
    }

    /* Apply sub-pixel pad accumulation. */
    int dx = (int)jt_cursor.sub_x;
    int dy = (int)jt_cursor.sub_y;
    jt_cursor.sub_x -= dx;
    jt_cursor.sub_y -= dy;
    jt_cursor.x += dx;
    jt_cursor.y += dy;

    jt_cursor.x = clamp(jt_cursor.x, 0, SCREEN_W - 1);
    jt_cursor.y = clamp(jt_cursor.y, 0, SCREEN_H - 1);

    /* Last-mover wins for active-source. Mouse takes precedence if
     * both moved on the same frame (pointer feels more direct). */
    if (moved_mouse)    jt_cursor.source = JT_CURSOR_SRC_MOUSE;
    else if (moved_pad) jt_cursor.source = JT_CURSOR_SRC_PAD;

    jt_cursor.button_a = a_held;
    jt_cursor.button_b = b_held;
}
