// display.h — OLED-compatible display API for GBA.
//
// The eyes_anim.c port from src/core/services/display/ assumes a 128×64
// monochrome surface with `display_pixel(x, y, bool on)` semantics. This
// header keeps that contract; the implementation in display.c maps each
// "OLED pixel" to a 2×2 block in the GBA's 240×160 RGB555 framebuffer
// so the rendered eyes fill most of the screen.
//
// Vertical centering: 128*2 = 256 (clipped to 240, losing 8 px each side
// of the right column), 64*2 = 128 (centered with 16 px top + 16 px
// bottom margin in the 160-row frame).
#ifndef EYES_DISPLAY_H
#define EYES_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

// Render at native GBA resolution (240×128 logical, vertically centered
// in the 160-row physical screen with 16 px top + 16 px bottom margin).
// eyes_anim has been scaled 2x to match.
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  128

void display_init(void);
void display_clear(void);
void display_pixel(int x, int y, bool on);

// Pupil draw — like display_pixel but uses a distinct palette color
// so the pupil is visible against a black background. Routed to by
// eyes_anim's pupil-cut path (a circular fill at the eye center).
void display_pupil_pixel(int x, int y);

// Fast horizontal span fill — equivalent to a row of display_pixel(x..x+w-1, y, on)
// calls but implemented with a single memset, ~10× faster than per-pixel.
// Used by eyes_anim's optimized fill_ellipse path.
void display_hspan(int x, int y, int w, bool on);
void display_hspan_pupil(int x, int y, int w);

// Upscale + AA the shadow buffer into the current Mode-4 back page.
// Call once per frame from main, after eyes_anim_render() finishes,
// before signalling VBlank to flip pages.
void display_present(void);

// Mode 4 page flip — call from VBlank only so the swap lands during
// scanout-off and the previous frame stays clean.
void display_flip_page(void);

// Set the foreground (eye) color. The edge midtone is auto-computed
// as a half-bright version of the same hue. Background stays black.
// Use the RGB5 macro from gba_video.h or pack r/g/b in [0..31] manually.
void display_set_fg_color(uint16_t color);

// Set the pupil color. Distinct from fg/bg so pupils stay visible
// against a dark background.
void display_set_pupil_color(uint16_t color);

#endif
