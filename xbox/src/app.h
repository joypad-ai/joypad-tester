/*
 * app.h -- mode dispatcher contract, shared across modes.
 *
 * Each mode is a function-pointer struct (jt_mode_t) registered in
 * main.c's mode_table. PORTING.md §4: tester first, About last,
 * accessory/feature modes between. The options menu (ui/options_menu)
 * issues jt_request_mode() with the desired mode index; main.c calls
 * leave / enter between update + draw on the frame the mode flips.
 *
 * Version: injected at build time via -DJT_VERSION_STR="<ver>" from
 * the Makefile (PORTING.md §3.7). Fallback to "unknown" so source
 * still parses if a developer builds the tree by hand without -D.
 */
#ifndef JT_XBOX_APP_H
#define JT_XBOX_APP_H

#include <stdbool.h>

#ifndef JT_VERSION_STR
#define JT_VERSION_STR "unknown"
#endif
#ifndef JT_BUILD_STAMP
#define JT_BUILD_STAMP "?"
#endif

typedef enum {
    JT_MODE_TESTER = 0,
    JT_MODE_ABOUT,
    JT_MODE_COUNT,
} jt_mode_id_t;

typedef struct {
    const char *name;
    void (*enter)(void);
    void (*leave)(void);
    void (*update)(float dt);
    void (*draw)(void);
} jt_mode_t;

/* Currently-active mode id; the dispatcher updates this between
 * apply_mode_switch's leave/enter calls. */
extern jt_mode_id_t jt_current_mode;

/* Request a switch to `id` -- applied between update and draw. */
void jt_request_mode(jt_mode_id_t id);

#endif /* JT_XBOX_APP_H */
