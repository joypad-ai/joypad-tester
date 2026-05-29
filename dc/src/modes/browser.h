/*
 * browser.h — per-VMU save browser mode.
 *
 * Lists every save on every connected VMU, decodes a small thumbnail
 * per save by extracting frame 0 of its embedded icon, and offers
 * action verbs: Extract -> Editor, Extract -> Library, Apply ->
 * ICONDATA_VMS (to the same or a different VMU).
 */
#ifndef JT_MODE_BROWSER_H
#define JT_MODE_BROWSER_H

#include "../app.h"
#include "../vms/vms.h"

extern const jt_mode_t jt_mode_browser;

/* Public accessor so the editor can push a loaded icon into its
 * canvas via the shared editor-canvas handle. Mode plumbing only.
 * The plain push has no library source (the editor treats it as new). */
void jt_browser_push_to_editor(const jt_icon_t *icon);

/* Push an icon AND record where it came from in the library, so the
 * editor's "Save" can overwrite that entry in place (vs "Save As"). */
void jt_browser_push_to_editor_src(const jt_icon_t *icon,
                                   int port, int slot, int lib_index,
                                   const char *name);

/* Editor reads the pending library source (set by the _src push, cleared
 * by the plain push). Returns false if the loaded icon has no source. */
bool jt_browser_consume_source(int *port, int *slot, int *lib_index,
                               char *name_out, int name_cap);

/* Push the "change icon" base (the target VMU's current icon) into the
 * editor. Called by the Icon Library when it's empty, so the editor
 * opens pre-seeded with the VMU's current icon to edit from. */
void jt_browser_push_change_base(void);

#endif
