/*
 * Joypad Tester - 3DO
 *
 * Connects directly to the Portfolio Event Broker (rather than going
 * through the higher-level InitEventUtility / GetControlPad polls)
 * so we can detect every device class the PBUS protocol supports,
 * not just Pad + Mouse. The trade-off is more setup boilerplate,
 * but it's the only path to Stick / LightGun / Arcade detection.
 *
 *   1. CreateMsgPort -> our listener port.
 *   2. EB_Configure  -> subscribe to *Update / *DataArrived events
 *                       for every class we render.
 *   3. EB_DescribePods at startup (and on hot-swap events) -> cache
 *                       the pod chain so we know each slot's class
 *                       before any data arrives, and so empty rows
 *                       are simply not drawn.
 *   4. Per-frame:    GetMsg drain -> demux EventFrames by
 *                       ef_EventNumber + ef_PodPosition, updating
 *                       the per-pod state slot.
 *
 * Renderers:
 *   - Pad      (11-button bitfield, dpad + btns groups)
 *   - Mouse    (X / Y axis + L / M / R buttons)
 *   - Stick    (H / V / D axes + 12 buttons)
 *   - LightGun (timing counter + line pulse + trigger)
 *   - Arcade   (0xC0 Silly Control Pad) detected, raw bytes deferred
 *               -- needs EB_ReadPodData, separate path
 *   - Keyboard / Glasses / IR: detected only (no renderer
 *     yet; class label printed in the Type column)
 *
 * Reference protocol documentation:
 *   docs/protocols/3DO_PBUS.md in joypad-ai/joypad-os
 *   3dodev.com PBUS specification
 *
 * Reference 3DO-side examples (Portfolio 1.3 eventbroker tree):
 *   lookie.c -- listener config + DumpEvent frame walk
 *   cpdump.c -- EB_DescribePods request/reply pattern
 *
 * Built against trapexit/3do-devkit; helpers (BasicDisplay,
 * abort_err) come from the devkit's src/ tree.
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
#include "kernel.h"
#include "kernelnodes.h"
#include "mem.h"
#include "msgport.h"
#include "nodes.h"
#include "operror.h"
#include "types.h"

#include <string.h>

#define MAX_DEVICES    8
#define IDLE_FRAMES    (60 * 30)
#define SCREEN_W       320
#define SCREEN_H       240
#define CYCLE_COUNT    7
#define LOGO_W         76
#define LOGO_H         64

// Toggle for the broker-pipeline diagnostic status line. Pads work
// through Portfolio's regular GetControlPad path which is well-tested
// and doesn't need babysitting; non-pad detection is blocked upstream
// on a custom-driver example coming to trapexit/3do-devkit. Set this
// to 1 to surface ACK / POD / EVT / LE / PP / PM in the bottom row
// when iterating on the broker / driverlet path.
#define ENABLE_DIAG_STATUS  1

// Internal device-class enum: covers the PBUS classes the renderer
// knows about, plus a NONE for unpopulated slots and an UNKNOWN
// fall-through for pods the broker reports but we don't decode (yet).
typedef enum {
  DEV_NONE = 0,
  DEV_PAD,
  DEV_MOUSE,
  DEV_STICK,
  DEV_LIGHTGUN,
  DEV_ARCADE,
  DEV_KEYBOARD,
  DEV_GLASSES,
  DEV_IR,
  DEV_SPLITTER,
  DEV_UNKNOWN,
} dev_type_t;

// One pod slot. Populated from EB_DescribePodsReply at startup +
// on every ControlPortChange / DeviceOnline / DeviceOffline event.
// State is updated as EB_EventRecord frames arrive.
typedef struct {
  dev_type_t          type;
  uint8               pod_number;
  uint8               pod_position;     // 1..N, 0 = empty slot
  uint8               generic_number;   // ordinal among its class
  bool                has_state;        // true once non-zero state seen
                                        // (filters out InitEventUtility's
                                        // phantom pad slots that always
                                        // poll-succeed with zero data)
  uint32              pod_type;         // raw PBUS byte / id word
  uint32              pod_flags;        // POD_IsControlPad / IsStick / etc.
  ControlPadEventData pad;
  MouseEventData      mouse;
  StickEventData      stick;
  LightGunEventData   gun;
  KeyboardEventData   kb;
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

// Flightstick buttons. Layout matches the PBUS doc -- FIRE is the
// trigger, A/B/C are face buttons, U/D/L/R are the hat switch,
// X/P are the start-band buttons (Portfolio's headers name bit 22
// "StickStop" but the physical 3DO Joystick FZ-JM1 button cap
// reads "X"), L/R are shoulders.
static const struct {
  uint32      mask;
  const char *label;
  int         x;
} STICK_BUTTONS[] = {
  { StickUp,         "U", 140 },
  { StickDown,       "D", 152 },
  { StickLeft,       "L", 164 },
  { StickRight,      "R", 176 },
  { StickFire,       "F", 188 },
  { StickA,          "A", 200 },
  { StickB,          "B", 212 },
  { StickC,          "C", 224 },
  { StickStop,       "X", 236 },   // physical X button
  { StickPlay,       "P", 248 },
  { StickLeftShift,  "L", 260 },
  { StickRightShift, "R", 272 },
};
static const int STICK_BUTTON_COUNT = sizeof (STICK_BUTTONS) / sizeof (STICK_BUTTONS[0]);

// Screensaver background colour cycle (matches gcn/gba/pce).
// Packed pairs so they can be passed straight to BasicDisplay::clear
// (which uses SetVRAMPages -- writes two 16-bit pixels per uint32).
static const uint32 CYCLE_RGB[CYCLE_COUNT] = {
  MakeRGB15Pair (31,  0,  0),
  MakeRGB15Pair ( 0, 31,  0),
  MakeRGB15Pair (31, 31,  0),
  MakeRGB15Pair ( 0,  0, 31),
  MakeRGB15Pair (31,  0, 31),
  MakeRGB15Pair ( 0, 31, 31),
  MakeRGB15Pair (31, 31, 31),
};

static device_t  g_pods[MAX_DEVICES + 1];   // indexed [1..N], slot 0 unused
static int       g_pod_count = 0;
static Item      g_msgPortItem = -1;
static Item      g_brokerPortItem = -1;
extern Item      g_configMsgItem; /* forward; real definition below */

// Compact diagnostic counters -- rendered as a bottom status line so
// we can see what the broker is actually delivering on real hardware
// (the only place stick / lightgun should fire; Opera's broker is
// hollow). Cheap to keep on while iterating; will be removed once
// hardware testing is locked.
static uint32    g_dbg_events       = 0;
static uint32    g_dbg_frames       = 0;
static uint32    g_dbg_last_evnum   = 0;
static bool      g_dbg_config_acked = false;
static bool      g_dbg_pods_replied = false;
static int       g_dbg_polled_pads  = 0;
static int       g_dbg_polled_mice  = 0;
static int       g_dbg_pod_queries  = 0;   // count of EB_DescribePods we sent
static int32     g_dbg_pod_result   = 1;   // msg_Result of last reply (1=never)
static uint32    g_dbg_pod_flavor   = 0;   // ebh_Flavor of last reply payload

// Keyboard "terminal" line: typed-text buffer + delta-detect state.
// Shared across all keyboard pods on the chain (a single typing
// stream, even if two keyboards were attached). Width is bounded by
// what fits to the right of the prompt on a 320-pixel screen --
// 8 px / char, "> " is 16 px starting at x=40 (Type column), so text
// starts at x=56 and we have 320-56 = 264 px = 33 char cells.
#define KB_TEXT_MAX        33
static char      g_kb_text[KB_TEXT_MAX + 1] = { 0 };
static int       g_kb_textlen = 0;
static uint32    g_kb_prev_matrix[8] = { 0 };
static uint32    g_kb_blink_frame    = 0;

/* Caps Lock toggle state (off / on). Mirrored to the physical LED on
 * the 3DO keyboard via EB_IssuePodCmd. Same approach as OptiDoom: when
 * the user presses Caps Lock we flip the local state and send a
 * GENERIC_KEYBOARD_SetLEDs command to all 8 pod slots (non-keyboard
 * pods reject it silently). */
static bool      g_caps_lock         = false;

static void
kb_send_led_state (void)
{
  struct {
    EventBrokerHeader hdr;
    int32 pd_PodNumber;
    int32 pd_WaitFlag;
    int32 pd_DataByteCount;
    uint8 pd_Data[4];
  } cmd;
  memset (&cmd, 0, sizeof cmd);
  cmd.hdr.ebh_Flavor   = EB_IssuePodCmd;
  cmd.pd_WaitFlag      = 0;
  cmd.pd_DataByteCount = 3;
  cmd.pd_Data[0]       = GENERIC_Keyboard;
  cmd.pd_Data[1]       = GENERIC_KEYBOARD_SetLEDs;
  cmd.pd_Data[2]       = g_caps_lock ? KEYBOARD_LED_CAPSLOCK : 0;
  for (int pod = 1; pod <= 8; pod++)
    {
      cmd.pd_PodNumber = pod;
      SendMsg (g_brokerPortItem, g_configMsgItem, &cmd, sizeof cmd);
    }
}

// PS/2 Set 2 scancode -> ASCII (unshifted). 0 = ignore key (modifier,
// function key, unmapped). 0x08 = backspace (special-case: pop last
// char from buffer). Source: 3DO host driverlet (KeyboardDriver.c)
// decodes PS/2 Set 2 byte stream and stores press state in bit
// `scancode` of ked_KeyMatrix (lower 128 bits = regular keys, upper
// 128 = E0-prefixed extended keys which we ignore for terminal use).
static const char PS2_TO_ASCII[128] = {
  /* 0x00 */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, '`',  0,
  /* 0x10 */ 0,   0,   0,   0,   0, 'q', '1',   0,   0,   0, 'z', 's', 'a', 'w', '2',  0,
  /* 0x20 */ 0, 'c', 'x', 'd', 'e', '4', '3',   0,   0, ' ', 'v', 'f', 't', 'r', '5',  0,
  /* 0x30 */ 0, 'n', 'b', 'h', 'g', 'y', '6',   0,   0,   0, 'm', 'j', 'u', '7', '8',  0,
  /* 0x40 */ 0, ',', 'k', 'i', 'o', '0', '9',   0,   0, '.', '/', 'l', ';', 'p', '-',  0,
  /* 0x50 */ 0,   0,'\'',   0, '[', '=',   0,   0,   0,   0,   0, ']',   0,'\\',   0,  0,
  /* 0x60 */ 0,   0,   0,   0,   0,   0,0x08,   0,   0,   0,   0,   0,   0,   0,   0,  0,
  /* 0x70 */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  0,
};

static void
kb_consume_press (uint8 scancode)
{
  if (scancode >= 128) return;          /* E0 keys -- not handled */
  char c = PS2_TO_ASCII[scancode];
  if (c == 0) return;                   /* unmapped key */
  if (c == 0x08)                        /* backspace */
    {
      if (g_kb_textlen > 0) g_kb_text[--g_kb_textlen] = 0;
      return;
    }
  if (g_kb_textlen < KB_TEXT_MAX)
    {
      g_kb_text[g_kb_textlen++] = c;
      g_kb_text[g_kb_textlen]   = 0;
    }
}


// Two distinct outbound message items: one parks while the broker
// holds it pending its Configure ack, the other handles DescribePods
// query/reply round-trips. A msg can't be re-sent until its previous
// reply has been received, so reusing one item for both would stall
// the DescribePods send while the broker still owns the configure
// message.
/* forward-declared above as extern so kb_send_led_state can use it */
Item             g_configMsgItem = -1;
static Item      g_queryMsgItem  = -1;
static MsgPort  *g_msgPort       = NULL;

static const char *
type_label (dev_type_t t)
{
  switch (t)
    {
    case DEV_PAD:      return "Pad";
    case DEV_MOUSE:    return "Mouse";
    case DEV_STICK:    return "Stick";
    case DEV_LIGHTGUN: return "GUN";
    case DEV_ARCADE:   return "ARCADE";
    case DEV_KEYBOARD: return "Keyb";
    case DEV_GLASSES:  return "Glas";
    case DEV_IR:       return "IR";
    case DEV_SPLITTER: return "Splt";
    default:           return "????";
    }
}

// Canonical PBUS device-class byte per the 3DO_PBUS.md doc:
//   pad         -> ID nibble lives in byte 0 bits 2:0 mixed with d-pad
//                  state; high-nibble-non-zero is the actual fingerprint.
//                  0x80 is the "no buttons pressed" placeholder we use to
//                  surface "pad detected" in the ID column.
//   mouse       -> 0x49
//   lightgun    -> 0x4D
//   arcade      -> 0xC0
//   flightstick -> 0x01 (first byte of the 0x01 0x7B 0x08 signature)
static uint32
pbus_id_byte_for (dev_type_t t)
{
  switch (t)
    {
    case DEV_PAD:      return 0x80;
    case DEV_MOUSE:    return 0x49;
    case DEV_LIGHTGUN: return 0x4D;
    case DEV_ARCADE:   return 0xC0;
    case DEV_STICK:    return 0x01;
    default:           return 0x00;
    }
}

// Map broker pod_Type flag bits to our internal enum. The PBUS doc
// gives the raw PBUS byte IDs (0x49 mouse, 0x4D lightgun, 0xC0 arcade,
// 0x01-0x7B-0x08 stick); the broker translates those into POD_Is*
// flag bits in pod_Type's upper half.
static dev_type_t
type_from_pod_descr (uint32 pod_flags, uint32 pod_type)
{
  // Preferred path: the broker's POD_Is* generic-class flags. These
  // are set when the broker has loaded a driverlet that announced
  // itself with a known class (pad / mouse / lightgun / glasses /
  // etc.).
  if (pod_flags & POD_IsControlPad)  return DEV_PAD;
  if (pod_flags & POD_IsMouse)       return DEV_MOUSE;
  if (pod_flags & POD_IsStick)       return DEV_STICK;
  if (pod_flags & POD_IsLightGun)    return DEV_LIGHTGUN;
  if (pod_flags & POD_IsGun)         return DEV_LIGHTGUN;  // older alias
  if (pod_flags & POD_IsKeyboard)    return DEV_KEYBOARD;
  if (pod_flags & POD_IsGlassesCtlr) return DEV_GLASSES;
  /* POD_IsAudioCtlr is a CAPABILITY, not a device class -- it's
   * also set on pads (volume rocker) and glasses (headphone) and
   * is not a standalone PBUS device. Don't classify by it. */
  if (pod_flags & POD_IsIRController) return DEV_IR;

  // Fallback: identify by the raw PBUS device-class byte that the
  // broker stores in pod_Type. Covers devices whose driverlet either
  // isn't loaded or doesn't announce a POD_Is* class, plus device
  // types that were specified in the PBUS protocol but never had a
  // commercial release (keyboard, IR receiver, splitter variants).
  // PBUS byte IDs collected from
  //   docs/protocols/3DO_PBUS.md  + portfolio_os/src/input/EventBroker.c
  //   BitTable[24] which lists 24 device classes the broker recognizes.
  switch (pod_type & 0xFF)
    {
    case 0x01: return DEV_STICK;     // analog flightstick (0x01 0x7B 0x08)
    case 0x02: return DEV_KEYBOARD;  // keyboard (early ID variant)
    case 0x03: return DEV_IR;        // infrared receiver
    case 0x41: return DEV_GLASSES;   // stereoscopic glasses
    case 0x49: return DEV_MOUSE;
    case 0x4B: return DEV_KEYBOARD;  // keyboard (later ID variant)
    case 0x4D: return DEV_LIGHTGUN;
    case 0x56: return DEV_SPLITTER;  // control-port splitter (arm 1)
    case 0x57: return DEV_SPLITTER;  // control-port splitter (arm 2)
    case 0xC0: return DEV_ARCADE;    // SillyPad / Orbatak arcade
    }
  // Pad bytes always have bits 4-3 != 00 (d-pad up/down can't be
  // 'both pressed'). If we see that pattern in pod_Type, classify
  // as pad even without the POD_IsControlPad flag.
  if (pod_type & 0xFF)
    {
      uint8 b = (uint8)(pod_type & 0xFF);
      if (((b >> 3) & 0x3) != 0) return DEV_PAD;
    }

  return DEV_UNKNOWN;
}

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
format_dec (char *out, uint32 value, int width)
{
  // Right-aligned, space-padded.
  char tmp[12];
  int len = 0;
  if (value == 0) { tmp[len++] = '0'; }
  while (value > 0) { tmp[len++] = '0' + (value % 10); value /= 10; }
  int pad = width - len;
  if (pad < 0) pad = 0;
  int i = 0;
  while (i < pad) { out[i++] = ' '; }
  while (len > 0) { out[i++] = tmp[--len]; }
  out[i] = 0;
}

// ---- broker setup -----------------------------------------------------

static Err
broker_connect (void)
{
  g_brokerPortItem
      = FindNamedItem (MKNODEID (KERNELNODE, MSGPORTNODE), EventPortName);
  if (g_brokerPortItem < 0) return g_brokerPortItem;

  g_msgPortItem = CreateMsgPort ("JoypadPort", 0, 0);
  if (g_msgPortItem < 0) return g_msgPortItem;
  g_msgPort = (MsgPort *)LookupItem (g_msgPortItem);

  g_configMsgItem = CreateMsg (NULL, 0, g_msgPortItem);
  if (g_configMsgItem < 0) return g_configMsgItem;

  // EB_DescribePods replies with a variable-sized PodDescriptionList
  // (header + N PodDescription entries). The official Portfolio docs
  // (Programming the System -> Event Broker -> Other Event Broker
  // Activities) state:
  //   "The message sent to the event broker should be a buffered
  //    message, created with CreateBufferedMsg(), which contains
  //    sufficient room to hold the complete list of connected pods."
  // A non-buffered CreateMsg has no reply-area allocated, so the
  // broker has nowhere to put the reply and we read pod_count = 0
  // even with real devices on the chain. 2048 bytes is plenty for
  // MAX_DEVICES = 8 entries (~32 bytes each + 4-byte header).
  g_queryMsgItem = CreateBufferedMsg (NULL, 0, g_msgPortItem, 2048);
  if (g_queryMsgItem < 0) return g_queryMsgItem;

  ConfigurationRequest config;
  memset (&config, 0, sizeof config);
  config.cr_Header.ebh_Flavor = EB_Configure;
  config.cr_Category = LC_Observer;
  // Subscribe to both state-change events (Pressed / Released / Update
  // / Moved) AND the *DataArrived family. *DataArrived fires every
  // PBUS frame (~60Hz) per device regardless of state change, so it
  // doubles as a "is this device class even alive on the bus?"
  // heartbeat. Without it, a plugged-in stick the user hasn't touched
  // generates no events at all and looks invisible.
  config.cr_TriggerMask[0] = EVENTBIT0_ControlButtonUpdate
                           | EVENTBIT0_ControlButtonPressed
                           | EVENTBIT0_ControlButtonReleased
                           | EVENTBIT0_ControlButtonArrived
                           | EVENTBIT0_MouseUpdate
                           | EVENTBIT0_MouseMoved
                           | EVENTBIT0_MouseButtonPressed
                           | EVENTBIT0_MouseButtonReleased
                           | EVENTBIT0_MouseDataArrived
                           | EVENTBIT0_StickUpdate
                           | EVENTBIT0_StickMoved
                           | EVENTBIT0_StickButtonPressed
                           | EVENTBIT0_StickButtonReleased
                           | EVENTBIT0_StickDataArrived
                           | EVENTBIT0_LightGunUpdate
                           | EVENTBIT0_LightGunButtonPressed
                           | EVENTBIT0_LightGunButtonReleased
                           | EVENTBIT0_LightGunDataArrived
                           | EVENTBIT0_KeyboardKeyPressed
                           | EVENTBIT0_KeyboardKeyReleased
                           | EVENTBIT0_KeyboardUpdate
                           | EVENTBIT0_KeyboardDataArrived;
  config.cr_TriggerMask[2] = EVENTBIT2_ControlPortChange
                           | EVENTBIT2_DeviceOnline
                           | EVENTBIT2_DeviceOffline;
  // Match the reference LG_ConnectEventBroker -- 0 may default to a
  // small/zero-size queue and silently drop events on busy frames.
  config.cr_QueueMax = 10;

  // Fire-and-forget the Configure -- matches the lookie.c reference
  // pattern. The broker eventually replies on g_configMsgItem; the
  // main-loop drain recognises that item and skips ReplyMsg so the
  // protocol stays well-formed. Waiting synchronously here would
  // hang if the broker queues other events ahead of the ack, or if
  // the ack arrives late under the Opera libretro core.
  int32 sent = SendMsg (g_brokerPortItem, g_configMsgItem,
                        &config, sizeof config);
  if (sent < 0) return sent;
  return 0;
}

// Fire-and-forget DescribePods request. The reply lands on
// g_queryMsgItem and is consumed by apply_pod_description() from
// the main-loop drain.
static Err
broker_request_pods (void)
{
  EventBrokerHeader queryHeader;
  queryHeader.ebh_Flavor = EB_DescribePods;
  int32 sent = SendMsg (g_brokerPortItem, g_queryMsgItem,
                        &queryHeader, sizeof queryHeader);
  if (sent >= 0) g_dbg_pod_queries++;
  return (sent < 0) ? (Err)sent : 0;
}

// Consume a DescribePodsReply that arrived on g_queryMsgItem and
// rebuild g_pods[]. Preserves per-slot state across rebuilds so
// renderers don't flicker when the chain hot-swaps.
static void
apply_pod_description (Message *m)
{
  g_dbg_pod_result = (int32)m->msg_Result;
  if ((int32)m->msg_Result < 0) return;
  PodDescriptionList *pdl = (PodDescriptionList *)m->msg_DataPtr;
  if (!pdl) return;
  g_dbg_pod_flavor = (uint32)pdl->pdl_Header.ebh_Flavor;

  int new_count = pdl->pdl_PodCount;
  if (new_count > MAX_DEVICES) new_count = MAX_DEVICES;
  g_pod_count = new_count;

  for (int i = 0; i < new_count; i++)
    {
      PodDescription *pd = &pdl->pdl_Pod[i];
      device_t *slot = &g_pods[i + 1];
      // Try the broker's POD_Is* generic-class flags first; fall
      // back to the raw PBUS byte in pod_Type (covers devices whose
      // driverlet didn't announce a generic class -- observed for
      // flightstick on real hardware).
      dev_type_t new_type = type_from_pod_descr (pd->pod_Flags, pd->pod_Type);

      if (slot->type != new_type || slot->pod_position != pd->pod_Position)
        memset (slot, 0, sizeof *slot);

      slot->type           = new_type;
      slot->pod_number     = pd->pod_Number;
      slot->pod_position   = pd->pod_Position;
      slot->pod_type       = pd->pod_Type;
      slot->pod_flags      = pd->pod_Flags;
      slot->generic_number = pd->pod_GenericNumber[0];
    }
  for (int i = new_count; i < MAX_DEVICES; i++)
    memset (&g_pods[i + 1], 0, sizeof g_pods[0]);
}

// Poll the event utility for control pads and mice and overlay the
// results onto g_pods[]. Required for the Opera libretro core,
// whose event-broker implementation is hollow (DescribePods returns
// pod_count = 0 and no event records are pushed) even though
// GetControlPad / GetMouse work fine. On real hardware these polls
// are redundant with the broker's EventRecord deliveries, but the
// duplication is harmless -- we just overwrite the slot with the
// same state the broker last delivered.
//
// Slot assignment uses synthetic positions in scan order: pads land
// first (positions 1..N), then mice (positions N+1..N+M). If a real
// DescribePodsReply ever arrives, it'll overwrite these slots with
// authoritative pod_Position and pod_Type values.
static void
poll_pads_and_mice (void)
{
  int next_slot = 1;
  g_dbg_polled_pads = 0;
  g_dbg_polled_mice = 0;

  for (int i = 1; i <= MAX_DEVICES && next_slot <= MAX_DEVICES; i++)
    {
      ControlPadEventData data;
      Err rc = GetControlPad (i, 0, &data);
      if (rc < 0) continue;
      device_t *slot = &g_pods[next_slot];
      if (slot->type != DEV_PAD) memset (slot, 0, sizeof *slot);
      slot->type         = DEV_PAD;
      slot->pod_position = (uint8)next_slot;
      slot->pad          = data;
      if (data.cped_ButtonBits != 0) slot->has_state = true;
      next_slot++;
      g_dbg_polled_pads++;
    }
  for (int i = 1; i <= MAX_DEVICES && next_slot <= MAX_DEVICES; i++)
    {
      MouseEventData data;
      Err rc = GetMouse (i, 0, &data);
      if (rc < 0) continue;
      device_t *slot = &g_pods[next_slot];
      if (slot->type != DEV_MOUSE) memset (slot, 0, sizeof *slot);
      slot->type         = DEV_MOUSE;
      slot->pod_position = (uint8)next_slot;
      slot->mouse        = data;
      if (data.med_ButtonBits != 0
          || data.med_HorizPosition != 0
          || data.med_VertPosition  != 0)
        slot->has_state = true;
      next_slot++;
      g_dbg_polled_mice++;
    }
  // Clear any trailing slots that the poll didn't refill -- but only
  // if they were previously polled-class. Slots populated by broker
  // events (stick / lightgun) should persist.
  for (int i = next_slot; i <= MAX_DEVICES; i++)
    {
      device_t *slot = &g_pods[i];
      if (slot->type == DEV_PAD || slot->type == DEV_MOUSE)
        memset (slot, 0, sizeof *slot);
    }
}

// Walk one EventRecord's chain of EventFrames and update per-pod
// state. Lazy-populates pod type from the event-number alone when
// DescribePods hasn't (yet) filled the slot, so we can render input
// even before the DescribePods reply lands.
static int
apply_event_record (EventBrokerHeader *hdr, bool *needs_repodding)
{
  if (hdr->ebh_Flavor != EB_EventRecord) return 0;

  EventFrame *frame = (EventFrame *)(hdr + 1);
  int seen = 0;
  while (frame->ef_ByteCount != 0)
    {
      int pos = frame->ef_PodPosition;
      device_t *slot = (pos >= 1 && pos <= MAX_DEVICES) ? &g_pods[pos] : NULL;
      dev_type_t inferred = DEV_NONE;

      g_dbg_frames++;
      g_dbg_last_evnum = frame->ef_EventNumber;

      switch (frame->ef_EventNumber)
        {
        case EVENTNUM_ControlButtonUpdate:
        case EVENTNUM_ControlButtonPressed:
        case EVENTNUM_ControlButtonReleased:
        case EVENTNUM_ControlButtonArrived:
          inferred = DEV_PAD;
          if (slot)
            slot->pad.cped_ButtonBits = frame->ef_EventData[0];
          break;

        case EVENTNUM_MouseUpdate:
        case EVENTNUM_MouseMoved:
        case EVENTNUM_MouseButtonPressed:
        case EVENTNUM_MouseButtonReleased:
        case EVENTNUM_MouseDataArrived:
          inferred = DEV_MOUSE;
          if (slot)
            slot->mouse = *(MouseEventData *)frame->ef_EventData;
          break;

        case EVENTNUM_StickUpdate:
        case EVENTNUM_StickMoved:
        case EVENTNUM_StickButtonPressed:
        case EVENTNUM_StickButtonReleased:
        case EVENTNUM_StickDataArrived:
          inferred = DEV_STICK;
          if (slot)
            slot->stick = *(StickEventData *)frame->ef_EventData;
          break;

        case EVENTNUM_LightGunUpdate:
        case EVENTNUM_LightGunButtonPressed:
        case EVENTNUM_LightGunButtonReleased:
        case EVENTNUM_LightGunDataArrived:
          inferred = DEV_LIGHTGUN;
          if (slot)
            slot->gun = *(LightGunEventData *)frame->ef_EventData;
          break;

        case EVENTNUM_KeyboardKeyPressed:
        case EVENTNUM_KeyboardKeyReleased:
        case EVENTNUM_KeyboardUpdate:
        case EVENTNUM_KeyboardDataArrived:
          inferred = DEV_KEYBOARD;
          if (slot)
            {
              KeyboardEventData *kbed = (KeyboardEventData *)frame->ef_EventData;
              slot->kb = *kbed;
              /* Terminal feed: detect bits that went 0 -> 1 since the
               * last seen matrix and pipe each into the typed-text
               * buffer. Only the lower 128 bits (regular keys); the
               * upper 128 are E0-extended keys (arrows / ctrl / etc.)
               * which the terminal doesn't consume. */
              for (int w = 0; w < 4; w++)
                {
                  uint32 cur  = kbed->ked_KeyMatrix[w];
                  uint32 down = cur & ~g_kb_prev_matrix[w];
                  while (down)
                    {
                      int bit = 0;
                      uint32 mask = down & (~down + 1);    /* lowest set bit */
                      uint32 tmp  = mask;
                      while (tmp >>= 1) bit++;
                      uint8 sc = (uint8)(w * 32 + bit);
                      if (sc == 0x58)
                        {
                          /* Caps Lock press -- toggle the LED on the
                           * physical keyboard. */
                          g_caps_lock = !g_caps_lock;
                          kb_send_led_state ();
                        }
                      else
                        {
                          kb_consume_press (sc);
                        }
                      down ^= mask;
                    }
                  g_kb_prev_matrix[w] = cur;
                }
            }
          break;

        case EVENTNUM_ControlPortChange:
          *needs_repodding = true;
          break;
        }

      if (slot && slot->type == DEV_NONE && inferred != DEV_NONE)
        {
          slot->type         = inferred;
          slot->pod_position = (uint8)pos;
          // Leave pod_type 0 -- the ID column will fill in once the
          // DescribePods reply lands.
        }
      // Broker-delivered events are by definition real device traffic
      // (the broker doesn't manufacture phantom packets like the event
      // utility's pad polling does), so any frame we receive is enough
      // to flag the slot as carrying real input.
      if (slot && inferred != DEV_NONE)
        slot->has_state = true;

      seen++;
      frame = (EventFrame *)((char *)frame + frame->ef_ByteCount);
    }
  return seen;
}

// ---- rendering --------------------------------------------------------

static void
draw_row_chrome (BasicDisplay &display, device_t *dev, int y)
{
  char buf[16];

  // Player order (position in the daisy chain).
  buf[0] = '0' + (dev->pod_position % 10);
  buf[1] = 0;
  display.draw_text8 (20, y, buf);

  // Type label. For known classes use the short name (Pad / Stick /
  // Mouse / GUN / Arcd / Keyb / Glas / IR / Splt). For anything the
  // PBUS spec doesn't pin down (a future-spec device whose driverlet
  // wasn't loaded, or a non-standard ID), show '0xNN' so the column
  // still carries useful identifying info rather than '????'. This
  // way we automatically detect anything new someone might build,
  // even with no per-class renderer in this app.
  if (dev->type == DEV_UNKNOWN && (dev->pod_type & 0xFF) != 0)
    {
      static const char hex[] = "0123456789ABCDEF";
      uint32 b = dev->pod_type & 0xFF;
      buf[0] = '0'; buf[1] = 'x';
      buf[2] = hex[(b >> 4) & 0xF];
      buf[3] = hex[b & 0xF];
      buf[4] = 0;
      display.draw_text8 (40, y, buf);
    }
  else
    {
      display.draw_text8 (40, y, type_label (dev->type));
    }

  // Device ID. Prefer the broker's pod_Type when DescribePods has
  // populated it (real hardware path, low byte is the PBUS class ID);
  // fall back to the per-type canonical byte from the PBUS doc so the
  // column carries meaningful content during pure-polling operation.
  uint32 id_word = dev->pod_type;
  if ((id_word & 0xFF) == 0)
    id_word = pbus_id_byte_for (dev->type);
  format_hex32 (buf, id_word);
  display.draw_text8 (96, y, buf + 6);   // bottom two hex digits = PBUS byte

}

static void
draw_pad_row (BasicDisplay &display, device_t *dev, int y)
{
  uint32 buttons = dev->pad.cped_ButtonBits;
  draw_row_chrome (display, dev, y);
  for (int i = 0; i < BUTTON_COUNT; i++)
    {
      const char *label = (buttons & BUTTONS[i].mask) ? BUTTONS[i].label : ".";
      display.draw_text8 (BUTTONS[i].x, y, label);
    }
}

static void
draw_mouse_row (BasicDisplay &display, device_t *dev, int y)
{
  char buf[8];
  MouseEventData *m = &dev->mouse;

  draw_row_chrome (display, dev, y);

  display.draw_text8 (140, y, "X");
  format_axis (buf, m->med_HorizPosition);
  display.draw_text8 (152, y, buf);

  display.draw_text8 (188, y, "Y");
  format_axis (buf, m->med_VertPosition);
  display.draw_text8 (200, y, buf);

  for (int i = 0; i < MOUSE_BUTTON_COUNT; i++)
    {
      const char *label = (m->med_ButtonBits & MOUSE_BUTTONS[i].mask)
                          ? MOUSE_BUTTONS[i].label : ".";
      display.draw_text8 (MOUSE_BUTTONS[i].x, y, label);
    }
}

// Sticks span 2 rows. Row 1 carries the digital buttons in the same
// column positions as a pad so the header DPAD UDLR BTNS ABCXPLR
// reads correctly for both classes. Row 2 carries the 3 analog axes,
// indented to align under the column band (same style mouse uses
// for its X / Y values).
static void
draw_stick_row (BasicDisplay &display, device_t *dev, int y)
{
  char buf[8];
  StickEventData *s = &dev->stick;
  uint32 buttons = s->stk_ButtonBits;

  draw_row_chrome (display, dev, y);

  // Row 1: buttons. Columns match pad layout for hat / face / X /
  // start / shoulders so the DPAD UDLR + BTNS ABCXPLR header reads
  // cleanly. StickFire goes in the gap between the DPAD and BTNS
  // bands (x=188) so it doesn't clip the X column.
  //
  // Portfolio's event.h calls bit 22 "StickStop" but on the actual
  // 3DO Joystick (FZ-JM1) hardware the button cap is labelled "X"
  // -- same bit position, just a docs/silkscreen mismatch. We
  // surface it as "X" so the on-screen label matches what's printed
  // on the stick the user is holding. (StickDriver.c:138-140 packs
  // wire byte 8 bit 6 into result bit 22, which event.h:268 names
  // StickStop = 0x00400000. The user-provided wire doc confirms:
  // "byte 8 bit 1 = X button" using MSB-as-bit-0 convention =
  // base[8] bit 6 in StickDriver's read order.)
  static const struct { uint32 mask; const char *l; int x; } BTNS[] = {
    { StickUp,         "U", 140 },
    { StickDown,       "D", 152 },
    { StickLeft,       "L", 164 },
    { StickRight,      "R", 176 },
    { StickFire,       "F", 188 },   // FIRE -- in the inter-column gap
    { StickA,          "A", 200 },
    { StickB,          "B", 212 },
    { StickC,          "C", 224 },
    { StickStop,       "X", 236 },   // physical X (Portfolio calls it Stop)
    { StickPlay,       "P", 248 },
    { StickLeftShift,  "L", 260 },
    { StickRightShift, "R", 272 },
  };
  for (int i = 0; i < (int)(sizeof BTNS / sizeof BTNS[0]); i++)
    display.draw_text8 (BTNS[i].x, y,
                        (buttons & BTNS[i].mask) ? BTNS[i].l : ".");

  // Row 2: H / V / D analog axes, indented under the column band.
  // Spacing mirrors mouse X / Y layout: 12px label + 4-char value
  // (8px label slot + 4*8 = 32px value).
  int y2 = y + 14;
  display.draw_text8 (140, y2, "H");
  format_axis (buf, s->stk_HorizPosition);
  display.draw_text8 (152, y2, buf);

  display.draw_text8 (188, y2, "V");
  format_axis (buf, s->stk_VertPosition);
  display.draw_text8 (200, y2, buf);

  display.draw_text8 (236, y2, "D");
  format_axis (buf, s->stk_DepthPosition);
  display.draw_text8 (248, y2, buf);
}

static void
draw_lightgun_row (BasicDisplay &display, device_t *dev, int y)
{
  char buf[8];
  LightGunEventData *lg = &dev->gun;

  draw_row_chrome (display, dev, y);

  // Counter (20-bit timing value -> approximate X) and line pulse
  // count (5-bit -> approximate Y). Showing the raw broker values
  // rather than converting via XSCANTIME/YSCANTIME constants so the
  // user can see what the hardware actually reports.
  display.draw_text8 (140, y, "C");
  format_dec (buf, lg->lged_Counter, 6);
  display.draw_text8 (152, y, buf);

  display.draw_text8 (200, y, "L");
  format_dec (buf, lg->lged_LinePulseCount, 3);
  display.draw_text8 (212, y, buf);

  display.draw_text8 (248, y,
                      (lg->lged_ButtonBits & LightGunTrigger) ? "T" : ".");
}

static void
draw_generic_row (BasicDisplay &display, device_t *dev, int y)
{
  draw_row_chrome (display, dev, y);
  // No state-feed events for keyboard / audio / IR / glasses yet --
  // mark the data columns as "--" so the row reads as "detected, not
  // decoded" rather than "everything is zero".
  display.draw_text8 (140, y, "(no decoded state -- detected only)");
}

// Keyboard renderer. Shows how many keys are currently down plus the
// first few scancodes (in PS/2 Set 2 hex). With 256 possible keys we
// can't render the entire matrix on one row -- this gives the user
// enough to verify the driver is decoding presses + releases without
// flooding the screen. ked_KeyMatrix is 8 uint32s, bit N set means
// scancode N is currently held.
static void
draw_keyboard_row (BasicDisplay &display, device_t *dev, int y)
{
  char buf[16];
  KeyboardEventData *kb = &dev->kb;
  static const char hex[] = "0123456789ABCDEF";

  draw_row_chrome (display, dev, y);

  int total = 0;
  uint8 scs[4] = {0,0,0,0};
  int nscs = 0;
  for (int w = 0; w < 8; w++)
    {
      uint32 v = kb->ked_KeyMatrix[w];
      while (v)
        {
          int bit;
          for (bit = 0; bit < 32; bit++) if (v & (1u << bit)) break;
          if (bit < 32)
            {
              if (nscs < 4) scs[nscs++] = (uint8)(w * 32 + bit);
              total++;
              v &= ~(1u << bit);
            }
        }
    }

  // "K:NN HH HH HH HH"  (count and up to 4 scancodes in hex).
  // Starts at x=128 to clear the ID column (which ends at x=112 --
  // 96 px label start + 16 px for two hex chars), leaving a 16 px
  // breathing-room gap.
  display.draw_text8 (128, y, "K:");
  buf[0] = '0' + ((total / 10) % 10);
  buf[1] = '0' + (total % 10);
  buf[2] = 0;
  display.draw_text8 (144, y, buf);

  int x = 168;
  for (int i = 0; i < 4; i++)
    {
      if (i < nscs)
        {
          buf[0] = hex[(scs[i] >> 4) & 0xF];
          buf[1] = hex[scs[i] & 0xF];
        }
      else
        {
          buf[0] = '-';
          buf[1] = '-';
        }
      buf[2] = 0;
      display.draw_text8 (x, y, buf);
      x += 24;
    }

  /* Row 2: terminal-style typed-input line. Starts at x=40 to align
   * with the Type column. "> " prompt, then the typed buffer, then a
   * blinking cursor (underscore) at the insertion point. Blink rate
   * ~1 Hz (toggle every 30 frames at 60 fps). */
  int y2 = y + 14;
  display.draw_text8 (40, y2, "> ");
  if (g_kb_textlen > 0)
    display.draw_text8 (56, y2, g_kb_text);
  if (((g_kb_blink_frame / 30) & 1) == 0)
    display.draw_text8 (56 + g_kb_textlen * 8, y2, "_");
}

// SillyPad / Arcade buttons. Our SillyPadDriver decodes byte 1 of
// the 2-byte PBUS frame into the top 5 bits of cped_ButtonBits:
//   0x80000000  P1 Coin
//   0x40000000  P1 Start
//   0x20000000  P2 Coin
//   0x10000000  P2 Start
//   0x08000000  Service
// The remaining bits in byte 1 are reserved (PBUS spec).
static void
draw_arcade_row (BasicDisplay &display, device_t *dev, int y)
{
  uint32 b = dev->pad.cped_ButtonBits;
  draw_row_chrome (display, dev, y);

  static const struct { uint32 mask; const char *l; int x; } BTNS[] = {
    { 0x80000000u, "C1", 140 },   // Player 1 Coin
    { 0x40000000u, "S1", 160 },   // Player 1 Start
    { 0x20000000u, "C2", 180 },   // Player 2 Coin
    { 0x10000000u, "S2", 200 },   // Player 2 Start
    { 0x08000000u, "Sv", 224 },   // Service (maintenance button)
  };
  for (int i = 0; i < (int)(sizeof BTNS / sizeof BTNS[0]); i++)
    {
      const char *label = (b & BTNS[i].mask) ? BTNS[i].l : "..";
      display.draw_text8 (BTNS[i].x, y, label);
    }
}

static void
draw_main_header (BasicDisplay &display, int detected_count)
{
  display.draw_text8 (20, 16, "Joypad Tester - 3DO");
  display.draw_text8 (20, 28, "===================");

  // Per-class identifying columns only -- the rest of the row is
  // device-specific (pad buttons / mouse X-Y / stick axes / gun
  // counter / etc.) and self-labels via its own letter glyphs.
  display.draw_text8 (20, 44, "P#");
  display.draw_text8 (40, 44, "Type");
  display.draw_text8 (96, 44, "ID");

  if (detected_count == 0)
    {
      display.draw_text8 (20, 88, "Plug a control pad into the");
      display.draw_text8 (20, 100, "first daisy-chain port.");
    }
}

// Bottom-row diag readout. Squeezed into one line so it doesn't
// dominate the layout. Field meanings:
//   ACK   - broker accepted our EB_Configure listener registration
//   POD N - DescribePods replied with N pods (real count on
//           hardware; will be 0 under Opera's hollow broker)
//   EVT N - total EventRecord messages drained
//   FRM N - total EventFrame entries inside those records
//   LE N  - last ef_EventNumber decoded (3 = Ctl update, 7 = mouse,
//           23 = LightGun, 28 = Stick, etc.)
//   PP/PM - poll counts for GetControlPad / GetMouse this frame
//           (PP = pads, PM = mice)
static void
draw_debug_status (BasicDisplay &display)
{
  char buf[16];
  const int y = 220;

  display.draw_text8 (20, y, "ACK");
  display.draw_text8 (44, y, g_dbg_config_acked ? "Y" : "N");

  display.draw_text8 (56, y, "POD");
  format_dec (buf, (uint32)g_pod_count, 1);
  display.draw_text8 (80, y, buf);

  display.draw_text8 (96, y, "EVT");
  format_dec (buf, g_dbg_events, 4);
  display.draw_text8 (120, y, buf);

  display.draw_text8 (152, y, "LE");
  format_dec (buf, g_dbg_last_evnum, 3);
  display.draw_text8 (168, y, buf);

  display.draw_text8 (196, y, "PP");
  format_dec (buf, (uint32)g_dbg_polled_pads, 1);
  display.draw_text8 (212, y, buf);

  display.draw_text8 (224, y, "PM");
  format_dec (buf, (uint32)g_dbg_polled_mice, 1);
  display.draw_text8 (240, y, buf);

  // Second diag row: DescribePods round-trip details.
  // RQ  = how many times we have sent EB_DescribePods (incl. periodic
  //       re-queries -- if the daemon lazy-enumerates pods we should
  //       catch the moment they appear).
  // RES = msg_Result of the most recent reply (1 = no reply yet,
  //       0 = success, negative = the broker rejected the query).
  // FLV = ebh_Flavor of the reply payload; should be 28
  //       (EB_DescribePodsReply) when valid.
  const int y2 = 232;
  display.draw_text8 (20, y2, "RQ");
  format_dec (buf, (uint32)g_dbg_pod_queries, 3);
  display.draw_text8 (40, y2, buf);

  display.draw_text8 (72, y2, "RES");
  if (g_dbg_pod_result == 1)
    display.draw_text8 (96, y2, "--------");
  else
    {
      // Print all 8 hex digits so we can decode the full Err code
      // (originator + severity + class + error number).
      format_hex32 (buf, (uint32)g_dbg_pod_result);
      display.draw_text8 (96, y2, buf);
    }

  display.draw_text8 (172, y2, "FLV");
  format_dec (buf, g_dbg_pod_flavor, 3);
  display.draw_text8 (196, y2, buf);
}


// Decide whether a slot is worth rendering. The InitEventUtility
// polling table pre-allocates 8 pad slots on real hardware, and
// GetControlPad(N) returns success for all of them with all-zero
// state even when only a handful of physical pads are connected.
// To avoid 8 ghost rows we require either:
//   - slot 1 (always show the first slot once polled, so a single
//     connected pad gives the user instant feedback), OR
//   - the slot has had any non-zero state since boot, OR
//   - the broker has authoritatively populated pod_type via
//     DescribePods (real device on the chain, even if idle).
static bool
should_render (int slot_idx, const device_t *dev)
{
  if (dev->type == DEV_NONE) return false;
  if (slot_idx == 1) return true;
  if (dev->has_state) return true;
  if (dev->pod_type != 0) return true;
  return false;
}

// Per-mouse on-screen cursor. The broker's med_HorizPosition /
// med_VertPosition fields are running totals of every delta since
// the broker started (see MouseDriver.c -- it accumulates increments
// into pod_PrivateData[2..3], which FillFrame copies into the
// event payload). We wrap modulo screen size so the cursor stays
// visible no matter how far the mouse has been moved. Each mouse
// slot gets its own marker character so multiple mice on the chain
// stay distinguishable.
static void
draw_mouse_cursors (BasicDisplay &display)
{
  int mouse_index = 0;
  for (int i = 1; i <= MAX_DEVICES; i++)
    {
      device_t *dev = &g_pods[i];
      if (dev->type != DEV_MOUSE) continue;
      MouseEventData *m = &dev->mouse;

      // Wrap into screen bounds. Adding SCREEN_W/H before the second
      // mod handles negative remainders (C's '%' is implementation-
      // defined for negatives).
      int cx = (int)(((m->med_HorizPosition % SCREEN_W) + SCREEN_W) % SCREEN_W);
      int cy = (int)(((m->med_VertPosition  % SCREEN_H) + SCREEN_H) % SCREEN_H);

      char marker[2] = { (char)('1' + mouse_index), 0 };
      display.draw_text8 (cx, cy, marker);
      mouse_index++;
    }
}

static int
draw_devices (BasicDisplay &display)
{
  int drawn = 0;
  int y = 56;
  for (int i = 1; i <= MAX_DEVICES; i++)
    {
      device_t *dev = &g_pods[i];
      if (!should_render (i, dev)) continue;
      int rh = 14;
      switch (dev->type)
        {
        case DEV_PAD:      draw_pad_row      (display, dev, y); break;
        case DEV_MOUSE:    draw_mouse_row    (display, dev, y); break;
        // Stick needs two rows (axes + hat on row 1, buttons on row 2).
        case DEV_STICK:    draw_stick_row    (display, dev, y); rh = 28; break;
        case DEV_LIGHTGUN: draw_lightgun_row (display, dev, y); break;
        case DEV_ARCADE:   draw_arcade_row   (display, dev, y); break;
        case DEV_KEYBOARD: draw_keyboard_row (display, dev, y); rh = 28; break;
        default:           draw_generic_row  (display, dev, y); break;
        }
      y += rh;
      drawn++;
    }
  draw_mouse_cursors (display);
  return drawn;
}

// Cheap mixing function over every per-pod state field, used to
// detect any-input-anywhere for idle gating. Not collision-proof
// but more than enough for "did anything change this frame".
static uint32
input_signature (void)
{
  uint32 sig = 0;
  for (int i = 1; i <= MAX_DEVICES; i++)
    {
      device_t *d = &g_pods[i];
      sig ^= ((uint32)d->type << (i & 3));
      sig ^= d->pad.cped_ButtonBits;
      sig ^= d->mouse.med_ButtonBits
           ^ ((uint32)d->mouse.med_HorizPosition << 3)
           ^ ((uint32)d->mouse.med_VertPosition  << 7);
      sig ^= d->stick.stk_ButtonBits
           ^ ((uint32)d->stick.stk_HorizPosition  << 1)
           ^ ((uint32)d->stick.stk_VertPosition   << 5)
           ^ ((uint32)d->stick.stk_DepthPosition  << 9);
      sig ^= d->gun.lged_ButtonBits ^ d->gun.lged_Counter;
      /* Keyboard state -- fold the 256-bit key matrix down into one
       * word so any keypress / release breaks idleness. Without this
       * the screensaver would stay up while someone is typing. */
      const uint32 *km = (const uint32 *)d->kb.ked_KeyMatrix;
      for (int j = 0; j < 8; j++)
        sig ^= km[j];
    }
  return sig;
}

int
main (int argc_, char *argv_)
{
  (void)argc_;
  (void)argv_;

  BasicDisplay display;
  Err err;

  memset (g_pods, 0, sizeof g_pods);

  // Manually load the per-class pod driverlets shipped in the
  // devkit's takeme/System/Drivers/ tree. Each ROM scans the PBUS
  // for its own device ID and publishes EventRecords to the broker
  // when one is present. They are NOT auto-loaded by the broker --
  // applications have to LoadProgram them, otherwise broker
  // DescribePods stays empty and non-pad devices remain invisible
  // even when physically connected.
  //
  // Pad driver is built into the kernel, so no LoadProgram needed
  // for it. SillyPad / Arcade (0xC0) has no shipped driverlet yet;
  // that's the missing-example case trapexit is writing for the
  // devkit.
  // Note: an earlier attempt to manually load /System/Drivers/cport*.rom
  // via LoadProgram fails because PodROM driverlets carry a 32-byte
  // PodROM header (DEADBEEF-prefixed checksum + size + family code)
  // before their AIF body. LoadProgram only understands plain AIF and
  // bails. The 3DO Portfolio SDK has no public PodROM loader; the
  // eventbroker daemon (System/Tasks/eventbroker) contains the loader
  // internally and is supposed to auto-scan System/Drivers/ at boot,
  // but on real-hardware testing the daemon is already running yet
  // EB_DescribePods still reports 0 pods. Adding LoadProgram of the
  // daemon binary doesn't help -- a second instance is refused on
  // hardware. Non-pad detection is parked here until either the
  // upstream trapexit/3do-devkit example lands or an EB_Command
  // opcode for "load driverlet" is identified. See
  // .dev/docs/3do_roadmap.md for the full picture.

  // InitEventUtility starts Portfolio's pod-scanning driverlets and
  // -- crucially on real hardware -- triggers loading of the Mouse /
  // Stick / LightGun driverlets too. Without it, those classes are
  // never enumerated and GetMouse always returns "no such device"
  // even when one is physically plugged in.
  //
  // Initial attempt passed LC_NoSeeUm here under the assumption that
  // we didn't want the utility competing with our own broker observer
  // for events. Hardware testing showed that wasn't right -- non-pad
  // devices never appeared. The fix is to register the utility as an
  // observer too; the broker supports multiple listeners, our own
  // subscription still receives every event, and the utility's
  // driverlet-loading side-effect kicks in.
  err = InitEventUtility (MAX_DEVICES, MAX_DEVICES, LC_Observer);
  if (err < 0) abort_err (err);

  err = broker_connect ();
  if (err < 0) abort_err (err);

  // Kick off the initial pod-chain query. Reply lands asynchronously
  // on g_queryMsgItem; main-loop drain consumes it.
  broker_request_pods ();

  /* Screensaver: background = cycling color via display.clear()
   * (direct VRAM write through SetVRAMPages, bypasses cel engine),
   * sprite = the original LogoCel.cel rendered on top. The sprite
   * keeps whatever color 3it baked into its PLUT (we don't mutate
   * it at runtime -- the PLUT-mutation approach kept hitting PIXC
   * MSB/PPMP edge cases on real hardware). The transparent slot
   * (white -> alpha) lets the cycling background show through
   * around the silhouette, so the sprite still looks like it's
   * being recolored even though only the background actually is. */
  CCB *logo_ccb = LoadCel ("LogoCel.cel", MEMTYPE_CEL);
  if (logo_ccb == NULL) abort_err (-1);

  uint32 prev_sig    = 0;
  int    idle_count  = 0;
  int    ss_color    = 0;
  int    ss_x = 80, ss_y = 80;
  int    ss_dx = 2, ss_dy = 1;
  bool   ss_on = false;

  while (true)
    {
      // Drain any queued broker traffic without blocking, so a frame
      // with no input still ticks the renderer. Three kinds of msgs
      // can show up here:
      //   - g_configMsgItem coming back: the ack of our EB_Configure;
      //     no ReplyMsg (it's a reply TO us, not an event FROM the
      //     broker).
      //   - g_queryMsgItem coming back: a DescribePodsReply; consume
      //     the PodDescriptionList, also no ReplyMsg.
      //   - anything else: an EB_EventRecord delivered by the broker;
      //     we must ReplyMsg so the broker can recycle the message.
      bool needs_repodding = false;
      while (true)
        {
          Item evItem = GetMsg (g_msgPortItem);
          if (evItem <= 0) break;
          Message *m = (Message *)LookupItem (evItem);

          if (evItem == g_configMsgItem)
            {
              g_dbg_config_acked = true;
            }
          else if (evItem == g_queryMsgItem)
            {
              g_dbg_pods_replied = true;
              apply_pod_description (m);
            }
          else
            {
              g_dbg_events++;
              EventBrokerHeader *h = (EventBrokerHeader *)m->msg_DataPtr;
              (void)apply_event_record (h, &needs_repodding);
              ReplyMsg (evItem, 0, NULL, 0);
            }
        }

      if (needs_repodding)
        broker_request_pods ();

      // Periodic DescribePods retry. Theory: the eventbroker daemon
      // may lazy-enumerate pods on a delayed schedule (e.g. after its
      // own initial bus scan completes asynchronously, or only on
      // detected ControlPortChange events that haven't fired yet).
      // Re-asking once per second is cheap and catches that case.
      static int repod_counter = 0;
      if (++repod_counter >= 60)
        {
          repod_counter = 0;
          broker_request_pods ();
        }

      // (Polling fallback removed -- with the freshly-compiled
      // eventbroker daemon from trapexit/portfolio_os now serving
      // the disc's broker, DescribePods enumerates pods correctly
      // and EventRecord events flow for all device classes on real
      // hardware. Polling was fighting the broker: it overwrote
      // slot->pad.cped_ButtonBits each frame with stale data and
      // re-classified non-pad slots back to DEV_PAD because the
      // stick / mouse / etc. also respond to GetControlPad. With
      // the broker working, polling is unnecessary and harmful.)

      uint32 sig = input_signature ();
      bool active = (sig != prev_sig);
      prev_sig = sig;

      if (active) { idle_count = 0; ss_on = false; }
      else        { idle_count++; if (idle_count >= IDLE_FRAMES) ss_on = true; }

      if (ss_on)
        {
          /* Screensaver: black background + cycling-colour logo
           * silhouette bouncing on top. LogoCel.cel is an uncoded
           * 16bpp cel where the inverted source PNG produced two
           * pixel values: 0x7FFF (silhouette) and 0x0000
           * (transparent, with CCB_BGND clear). Each frame we
           * walk the pixel buffer and rewrite every nonzero
           * pixel to the current cycle colour. Transparent
           * pixels stay 0x0000. After the first frame the
           * silhouette pixels hold whatever cycle colour we
           * last wrote -- still nonzero, so the test keeps
           * matching and we keep overwriting them in place. */
          ss_x += ss_dx;
          ss_y += ss_dy;
          if (ss_x <= 0)                 { ss_x = 0;                 ss_dx = -ss_dx; ss_color = (ss_color + 1) % CYCLE_COUNT; }
          if (ss_x >= SCREEN_W - LOGO_W) { ss_x = SCREEN_W - LOGO_W; ss_dx = -ss_dx; ss_color = (ss_color + 1) % CYCLE_COUNT; }
          if (ss_y <= 0)                 { ss_y = 0;                 ss_dy = -ss_dy; ss_color = (ss_color + 1) % CYCLE_COUNT; }
          if (ss_y >= SCREEN_H - LOGO_H) { ss_y = SCREEN_H - LOGO_H; ss_dy = -ss_dy; ss_color = (ss_color + 1) % CYCLE_COUNT; }

          uint16 sprite_color = 0x8000u | (uint16)CYCLE_RGB[ss_color];
          uint16 *src = (uint16 *)logo_ccb->ccb_SourcePtr;
          int npx = LOGO_W * LOGO_H;
          for (int i = 0; i < npx; i++)
            if (src[i]) src[i] = sprite_color;

          display.clear ();
          logo_ccb->ccb_XPos = (int32)ss_x << 16;
          logo_ccb->ccb_YPos = (int32)ss_y << 16;
          display.draw_cels (logo_ccb);
        }
      else
        {
          display.clear ();
          int drawn = draw_devices (display);
          draw_main_header (display, drawn);
#if ENABLE_DIAG_STATUS
          draw_debug_status (display);
#endif
        }

      display.display_and_swap ();
      display.waitvbl ();
      g_kb_blink_frame++;   /* drives the terminal-cursor blink */
    }

  // Unreachable in practice; cleanup is left to the OS on reset.
  return 0;
}
