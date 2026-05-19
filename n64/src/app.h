/*
 * app.h — shared types for Joypad Tester N64.
 *
 * The app is a thin dispatcher: main.c polls the joypad subsystem
 * and the idle-screensaver counter every frame, then forwards to
 * whichever jt_mode_t is currently active. The options menu (ui/
 * options_menu.{c,h}) overlays on Start+Down and lets the user
 * pick a different mode. New testers/viewers register a jt_mode_t
 * entry in main.c's mode_table[] and append to jt_mode_id_t below.
 *
 * Mode contract: each mode owns its full render path -- console_*
 * or graphics_draw_* against display_get(), then display_show(). The
 * dispatcher doesn't touch the framebuffer between modes.
 */
#ifndef N64_APP_H
#define N64_APP_H

#include <stdbool.h>

/* The version string is injected by the Makefile (-D flag) from the
 * VERSION file at the n64/ root. The fallback below only kicks in for
 * editor / IDE builds that don't see the Makefile's defines. Same
 * pattern as dc/. */
#ifndef JT_VERSION_STR
#define JT_VERSION_STR "0.0.0-dev"
#endif

/* Modes the options menu can switch between. New modes append here
 * AND register a jt_mode_t entry in main.c's mode_table[]. Order
 * here defines the menu order. */
typedef enum {
    JT_MODE_TESTER = 0,   /* default landing screen: per-port grid */
    JT_MODE_CPAK,         /* Controller Pak note browser */
    JT_MODE_GBC,          /* Game Boy Camera viewer (via Transfer Pak) */
    JT_MODE_SNAP,         /* Snap Station protocol exerciser */
    JT_MODE_ABOUT,        /* version + credits */
    JT_MODE_COUNT
} jt_mode_id_t;

typedef struct jt_mode {
    const char *name;           /* shown in the options menu */
    void (*enter)(void);        /* called once when switched to (may be NULL) */
    void (*leave)(void);        /* called once when switched away (may be NULL) */
    void (*update)(void);       /* per-frame state advance (may be NULL) */
    void (*draw)(void);         /* per-frame render -- mode owns display_show */
} jt_mode_t;

/* Globals owned by main.c, declared here so modes can read them. */
extern jt_mode_id_t jt_current_mode;
void jt_request_mode(jt_mode_id_t next);

/* Returns true iff any pad showed meaningful activity this frame
 * (button held or stick deflected past dead-zone). Used by main's
 * screensaver tick. */
bool jt_any_input_this_frame(void);

#endif /* N64_APP_H */
