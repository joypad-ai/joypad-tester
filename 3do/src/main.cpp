/*
 * Joypad Tester - 3DO
 *
 * Reads the 3DO Portfolio event utility for connected input devices
 * and renders one row per *detected* device, ordered by their
 * position in the daisy chain (player 1 = first connected, etc).
 *
 * Currently classifies pads + mice (the two device types the event
 * utility exposes through dedicated polls). Stick / Light Gun /
 * Sillypad / Keyboard reach the device list via EB_DescribePods +
 * the low-level event-broker message protocol; their renderers are
 * queued for a follow-up iteration (see .dev/docs/3do_roadmap.md).
 *
 * Built against trapexit/3do-devkit; helpers (BasicDisplay,
 * abort_err) come from the devkit's src/ tree and link in via the
 * normal build.
 *
 * Copyright (c) 2026 Robert Dale Smith
 * Licensed under MIT - see ../LICENSE.md.
 */

#include "abort.h"
#include "display.hpp"

#include "celutils.h"
#include "controlpad.h"
#include "event.h"
#include "filefunctions.h"
#include "graphics.h"
#include "item.h"
#include "mem.h"
#include "operror.h"
#include "types.h"

#define MAX_PADS       8
// Mouse counts in the wild:
//   retail 3DO  -> 1 mouse (single mouse port on the console)
//   arcade 3DO  -> 2 mice (M2 dual-cab cabinets)
//   protocol    -> 4 max (no commercial hardware ships this many)
// We poll up to 2 -- the realistic ceiling -- so empty slots beyond
// don't add probe overhead each frame.
#define MAX_MICE       2
#define MAX_DEVICES    8          // daisy-chain hardware limit
#define IDLE_FRAMES    (60 * 30)
#define SCREEN_W       320
#define SCREEN_H       240
#define CYCLE_COUNT    7
#define LOGO_W         64
#define LOGO_H         64

typedef enum {
  DEV_PAD,
  DEV_MOUSE,
} dev_type_t;

// One connected device. `order_index` is the player number (1-based,
// daisy-chain order). `device_id` is the hardware-reported ID -- not
// the event-utility class index. v0.3 leaves it as a TODO until the
// EB_DescribePods integration lands; for now we display the class
// index as a stand-in so the column has visible content.
typedef struct {
  dev_type_t  type;
  int         order_index;
  uint32      device_id;
  union {
    ControlPadEventData pad;
    MouseEventData      mouse;
  } data;
} device_t;

// Standard 3DO control-pad button labels + column positions.
// DPAD block, BTNS block (face + start/stop + shoulders).
static const struct {
  uint32      mask;
  const char *label;
  int         x;
} BUTTONS[] = {
  { ControlUp,         "U", 140 },
  { ControlDown,       "D", 152 },
  { ControlLeft,       "L", 164 },
  { ControlRight,      "R", 176 },
  { ControlA,          "A", 200 },
  { ControlB,          "B", 212 },
  { ControlC,          "C", 224 },
  { ControlX,          "X", 236 },
  { ControlStart,      "P", 248 },   // 3DO Start button is labelled "P"
  { ControlLeftShift,  "L", 260 },
  { ControlRightShift, "R", 272 },
};
static const int BUTTON_COUNT = sizeof (BUTTONS) / sizeof (BUTTONS[0]);

// 3-button mouse + buttons live where the pad's BTNS column band is
// so the visual columns stay consistent across rows.
static const struct {
  uint32      mask;
  const char *label;
  int         x;
} MOUSE_BUTTONS[] = {
  { MouseLeft,   "L", 248 },
  { MouseMiddle, "M", 260 },
  { MouseRight,  "R", 272 },
};
static const int MOUSE_BUTTON_COUNT = sizeof (MOUSE_BUTTONS) / sizeof (MOUSE_BUTTONS[0]);

// Screensaver color cycle (matches gcn/gba/pce).
static const uint16 CYCLE_RGB[CYCLE_COUNT] = {
  MakeRGB15 (31,  0,  0),
  MakeRGB15 ( 0, 31,  0),
  MakeRGB15 (31, 31,  0),
  MakeRGB15 ( 0,  0, 31),
  MakeRGB15 (31,  0, 31),
  MakeRGB15 ( 0, 31, 31),
  MakeRGB15 (31, 31, 31),
};

static void
format_hex32 (char *out, uint32 value)
{
  static const char hex[] = "0123456789ABCDEF";
  for (int i = 0; i < 8; i++)
    out[i] = hex[(value >> ((7 - i) * 4)) & 0xF];
  out[8] = 0;
}

static void
format_axis (char *out, int32 value)
{
  if (value < -999) value = -999;
  if (value >  999) value =  999;
  out[0] = (value < 0) ? '-' : '+';
  int v = (value < 0) ? -value : value;
  out[1] = '0' + (v / 100) % 10;
  out[2] = '0' + (v /  10) % 10;
  out[3] = '0' + (v /   1) % 10;
  out[4] = 0;
}

static void
draw_row_chrome (BasicDisplay &display, device_t *dev, int y, const char *type_label)
{
  char buf[8];

  // Player order (1..N).
  buf[0] = '0' + dev->order_index;
  buf[1] = 0;
  display.draw_text8 (20, y, buf);

  // Type label.
  display.draw_text8 (40, y, type_label);

  // Device ID -- placeholder for v0.3. Will be wired to
  // PodDescription.pod_Type once EB_DescribePods is integrated; for
  // now we show the event-utility class index so the column carries
  // something rather than reading as a stub.
  format_hex32 (buf, dev->device_id);
  display.draw_text8 (68, y, buf + 4);   // last 4 hex digits fit the column
}

static void
draw_pad_row (BasicDisplay &display, device_t *dev, int row_idx)
{
  const int y = 68 + row_idx * 14;
  uint32 buttons = dev->data.pad.cped_ButtonBits;

  draw_row_chrome (display, dev, y, "Pad");

  for (int i = 0; i < BUTTON_COUNT; i++)
    {
      const char *label = (buttons & BUTTONS[i].mask) ? BUTTONS[i].label : ".";
      display.draw_text8 (BUTTONS[i].x, y, label);
    }
}

static void
draw_mouse_row (BasicDisplay &display, device_t *dev, int row_idx)
{
  char buf[8];
  const int y = 68 + row_idx * 14;
  MouseEventData *m = &dev->data.mouse;

  draw_row_chrome (display, dev, y, "Mouse");

  // X / Y axes in the DPAD column band.
  display.draw_text8 (140, y, "X");
  format_axis (buf, m->med_HorizPosition);
  display.draw_text8 (152, y, buf);

  display.draw_text8 (188, y, "Y");
  format_axis (buf, m->med_VertPosition);
  display.draw_text8 (200, y, buf);

  // L / M / R buttons in the BTNS column band.
  for (int i = 0; i < MOUSE_BUTTON_COUNT; i++)
    {
      const char *label = (m->med_ButtonBits & MOUSE_BUTTONS[i].mask)
                          ? MOUSE_BUTTONS[i].label : ".";
      display.draw_text8 (MOUSE_BUTTONS[i].x, y, label);
    }
}

static void
draw_main_header (BasicDisplay &display, int detected_count)
{
  char buf[32];

  display.draw_text8 (20, 16, "Joypad Tester - 3DO");
  display.draw_text8 (20, 28, "===================");

  // Group labels above the BTNS letter row so duplicate L/R in DPAD
  // vs Shoulders is contextually distinct.
  display.draw_text8 (140, 44, "DPAD");
  display.draw_text8 (224, 44, "BTNS");

  // Column labels lined up with the per-device row layout.
  display.draw_text8 (20, 56, "P#");
  display.draw_text8 (40, 56, "Type");
  display.draw_text8 (68, 56, "ID");
  for (int i = 0; i < BUTTON_COUNT; i++)
    display.draw_text8 (BUTTONS[i].x, 56, BUTTONS[i].label);

  // Status line when nothing is connected.
  if (detected_count == 0)
    {
      display.draw_text8 (20, 100, "Plug a control pad into the");
      display.draw_text8 (20, 112, "first daisy-chain port.");
    }
  (void)buf;
}

int
main (int argc_, char *argv_)
{
  (void)argc_;
  (void)argv_;

  BasicDisplay display;
  Err err;

  err = InitEventUtility (MAX_PADS, MAX_MICE, 0);
  if (err < 0)
    abort_err (err);

  CCB *logo_ccb = LoadCel ("LogoCel.cel", MEMTYPE_CEL);
  if (logo_ccb == NULL)
    abort_err (-1);

  device_t devices[MAX_DEVICES];
  uint32   prev_input_signature = 0;
  int      idle_count = 0;
  int      ss_color   = 0;
  int      ss_x = 80, ss_y = 80;
  int      ss_dx = 2, ss_dy = 1;
  bool     ss_on = false;

  while (true)
    {
      // Enumerate connected devices into the `devices[]` list in
      // order-of-discovery (pads scanned first, then mice). Player
      // order = position in the list, 1-based.
      int detected = 0;
      uint32 sig = 0;
      ControlPadEventData pad_data;
      MouseEventData      mouse_data;

      for (int i = 0; i < MAX_PADS && detected < MAX_DEVICES; i++)
        {
          Err rc = GetControlPad (i + 1, 0, &pad_data);
          if (rc < 0) continue;
          devices[detected].type        = DEV_PAD;
          devices[detected].order_index = detected + 1;
          devices[detected].device_id   = (uint32)(i + 1);
          devices[detected].data.pad    = pad_data;
          sig ^= pad_data.cped_ButtonBits + (i * 0x101);
          detected++;
        }
      for (int i = 0; i < MAX_MICE && detected < MAX_DEVICES; i++)
        {
          Err rc = GetMouse (i + 1, 0, &mouse_data);
          if (rc < 0) continue;
          devices[detected].type        = DEV_MOUSE;
          devices[detected].order_index = detected + 1;
          devices[detected].device_id   = 0x100u + (uint32)(i + 1);
          devices[detected].data.mouse  = mouse_data;
          sig ^= mouse_data.med_ButtonBits +
                 ((uint32)mouse_data.med_HorizPosition << 3) +
                 ((uint32)mouse_data.med_VertPosition  << 7);
          detected++;
        }

      // Idle gating: any change in any device's state resets the
      // counter. sig is a cheap mixing function over all device data
      // -- not collision-proof but more than enough for "did anything
      // move in the last frame".
      bool active = (sig != prev_input_signature) || (sig != 0);
      prev_input_signature = sig;

      if (active) { idle_count = 0; ss_on = false; }
      else        { idle_count++; if (idle_count >= IDLE_FRAMES) ss_on = true; }

      display.clear ();

      if (ss_on)
        {
          ss_x += ss_dx;
          ss_y += ss_dy;
          if (ss_x <= 0)              { ss_x = 0;              ss_dx = -ss_dx; ss_color = (ss_color + 1) % CYCLE_COUNT; }
          if (ss_x >= SCREEN_W - LOGO_W) { ss_x = SCREEN_W - LOGO_W; ss_dx = -ss_dx; ss_color = (ss_color + 1) % CYCLE_COUNT; }
          if (ss_y <= 0)              { ss_y = 0;              ss_dy = -ss_dy; ss_color = (ss_color + 1) % CYCLE_COUNT; }
          if (ss_y >= SCREEN_H - LOGO_H) { ss_y = SCREEN_H - LOGO_H; ss_dy = -ss_dy; ss_color = (ss_color + 1) % CYCLE_COUNT; }

          logo_ccb->ccb_XPos = (int32)ss_x << 16;
          logo_ccb->ccb_YPos = (int32)ss_y << 16;
          if (logo_ccb->ccb_PLUTPtr)
            ((uint16*)logo_ccb->ccb_PLUTPtr)[1] = CYCLE_RGB[ss_color];

          display.draw_cels (logo_ccb);
        }
      else
        {
          draw_main_header (display, detected);
          for (int i = 0; i < detected; i++)
            {
              switch (devices[i].type)
                {
                case DEV_PAD:   draw_pad_row   (display, &devices[i], i); break;
                case DEV_MOUSE: draw_mouse_row (display, &devices[i], i); break;
                }
            }
        }

      display.display_and_swap ();
      display.waitvbl ();
    }

  KillEventUtility ();
  return 0;
}
