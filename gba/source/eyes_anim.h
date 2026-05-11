// eyes_anim.h - Animated Cartoon Eyes
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Robert Dale Smith
//
// Standalone two-eye animation for OLED display. Pseudo-3D cylinder
// rotation for gaze, plus a small set of emotion states (happy, sad,
// angry, surprised, wink, etc) expressed entirely through eye shape.
//
// Independent of joy_anim — apps can toggle between them as separate
// "screens".

#ifndef EYES_ANIM_H
#define EYES_ANIM_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    EYES_EVENT_BOOT,
    EYES_EVENT_CONNECT,
    EYES_EVENT_DISCONNECT,
    EYES_EVENT_BUTTON_PRESS,    // Random reaction from the "fun" pool
    EYES_EVENT_RUMBLE,
    EYES_EVENT_IDLE_TIMEOUT,
} eyes_event_t;

typedef enum {
    EYES_STATE_BOOT,
    EYES_STATE_IDLE,
    EYES_STATE_SLEEP,
    EYES_STATE_HAPPY,
    EYES_STATE_SAD,
    EYES_STATE_ACTIVE,
    EYES_STATE_SURPRISED,
    EYES_STATE_ANGRY,
    EYES_STATE_SUSPICIOUS,
    EYES_STATE_WINK_L,           // Left eye closed
    EYES_STATE_WINK_R,           // Right eye closed
    EYES_STATE_EXCITED,
    EYES_STATE_CONFUSED,
    EYES_STATE_COUNT,
} eyes_state_t;

void eyes_anim_init(void);

// Advance animation. Returns true if frame changed and needs redraw.
bool eyes_anim_tick(uint32_t now_ms);

// Render eyes into the display framebuffer. Calls display_clear() first.
void eyes_anim_render(void);

// Set gaze target. x, y are normalized 0.0 .. 1.0 (0.5 = center).
void eyes_anim_set_look(float x, float y);

// Drive the eyelids externally — typically wired to L2/R2 analog triggers.
// l, r are normalized 0.0 (open) .. 1.0 (fully closed). The keyframe
// height is multiplied by (1 - eyelid), so blinks/state still work; this
// just acts as a ceiling on openness.
void eyes_anim_set_eyelids(float l, float r);

// Trigger a state transition.
void eyes_anim_event(eyes_event_t event);

// Force-set an emotion (use directly when an app wants a specific reaction
// rather than the random pick BUTTON_PRESS does).
void eyes_anim_set_state(eyes_state_t state);

// While true, pin the current emotion at its "peak" frame instead of
// running through the release. Set to false (e.g. on button release) and
// the emotion plays its release frames out and returns to ACTIVE.
// No-op for non-emotion states (BOOT/IDLE/SLEEP/ACTIVE).
void eyes_anim_set_held(bool held);

eyes_state_t eyes_anim_get_state(void);

#endif // EYES_ANIM_H
