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
 * canvas via the shared editor-canvas handle. Mode plumbing only. */
void jt_browser_push_to_editor(const jt_icon_t *icon);

#endif
