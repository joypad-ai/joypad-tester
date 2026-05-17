/*
 * cursor.h — virtual cursor driven by analog stick OR DC mouse.
 *
 * Single cursor model: whichever input source moved last "owns" it
 * this frame. Pad and mouse can coexist on different ports; the
 * cursor reads from all of them and integrates motion. Modes that
 * want pointer-style interaction (the VMU editor's canvas drawing,
 * the options menu hover state) call jt_cursor_update() each frame
 * and read jt_cursor.x / .y.
 */
#ifndef JT_CURSOR_H
#define JT_CURSOR_H

#include <stdbool.h>

typedef enum {
    JT_CURSOR_SRC_PAD = 0,
    JT_CURSOR_SRC_MOUSE
} jt_cursor_source_t;

typedef struct {
    int x, y;                  /* screen-space pixel coords */
    float sub_x, sub_y;        /* sub-pixel accumulator (pad input) */
    jt_cursor_source_t source; /* set by whichever device moved last */
    bool button_a;             /* edge-detected paint/click */
    bool button_b;             /* edge-detected erase/cancel */
} jt_cursor_t;

extern jt_cursor_t jt_cursor;

void jt_cursor_init(int initial_x, int initial_y);
/* dt: frame delta in seconds. Reads from jt_ports[] and updates
 * the cursor. Clamps to (0..639, 0..479). */
void jt_cursor_update(float dt);

#endif /* JT_CURSOR_H */
