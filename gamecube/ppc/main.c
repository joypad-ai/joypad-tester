#include <stdio.h>
#include <stdlib.h>

#include <gccore.h>

#include "../common/common_utils.h"
#include "n64.h"
#include "ppc_utils.h"

#define CONSOLE_START_POS 20

static void *xfb = NULL;
static GXRModeObj *rMode = NULL;

typedef enum {
  STYLE_NONE,
  STYLE_N64,
  STYLE_GCN,
  STYLE_MOUSE,
  STYLE_KEYBOARD,
} pad_style_t;

static const char *format_style(pad_style_t s) {
  switch (s) {
  case STYLE_N64:      return "N64     ";
  case STYLE_GCN:      return "GCN     ";
  case STYLE_MOUSE:    return "Mouse   ";
  case STYLE_KEYBOARD: return "Keyboard";
  default:             return "None    ";
  }
}

typedef enum {
  PAK_NONE,
  PAK_UNKNOWN,
  PAK_MEMORY,
  PAK_RUMBLE,
  PAK_TRANSFER,
  PAK_BIO_SENSOR,
  PAK_SNAP_STATION,
} pad_pak_t;

// Unified per-port input snapshot. Fields the source controller doesn't have
// stay at zero (matches the JoypadTest-N64 reference layout).
typedef struct {
  pad_style_t style;
  pad_pak_t pak;
  bool rumble_supported;
  bool rumble_active;
  s8 stick_x, stick_y;
  s8 cstick_x, cstick_y;
  u8 analog_l, analog_r;
  bool a, b, x, y, l, r, z, start;
  bool d_up, d_down, d_left, d_right;
  bool c_up, c_down, c_left, c_right;
} pad_snap_t;

static const char *format_pak(pad_pak_t p) {
  switch (p) {
  case PAK_MEMORY:       return "Memory      ";
  case PAK_RUMBLE:       return "Rumble Pak  ";
  case PAK_TRANSFER:     return "Transfer Pak";
  case PAK_BIO_SENSOR:   return "Bio Sensor  ";
  case PAK_SNAP_STATION: return "Snap Station";
  case PAK_UNKNOWN:      return "Unknown     ";
  default:               return "None        ";
  }
}

static const char *format_rumble(bool supported, bool active) {
  if (!supported) return "Unavailable";
  return active ? "Active" : "Idle";
}

static void snap_n64(pad_snap_t *out, const N64State *s) {
  out->style = (s->kind == N64_KIND_MOUSE) ? STYLE_MOUSE : STYLE_N64;
  switch (s->pak) {
  case N64_PAK_MEMORY:       out->pak = PAK_MEMORY;       break;
  case N64_PAK_RUMBLE:       out->pak = PAK_RUMBLE;       break;
  case N64_PAK_TRANSFER:     out->pak = PAK_TRANSFER;     break;
  case N64_PAK_BIO_SENSOR:   out->pak = PAK_BIO_SENSOR;   break;
  case N64_PAK_SNAP_STATION: out->pak = PAK_SNAP_STATION; break;
  case N64_PAK_UNKNOWN:      out->pak = PAK_UNKNOWN;      break;
  default:                   out->pak = PAK_NONE;         break;
  }
  out->rumble_supported = (s->pak == N64_PAK_RUMBLE);
  out->rumble_active = s->rumble_active;
  out->stick_x = s->stick_x;
  out->stick_y = s->stick_y;
  out->a       = !!(s->buttons & N64_BTN_A);
  out->b       = !!(s->buttons & N64_BTN_B);
  out->z       = !!(s->buttons & N64_BTN_Z);
  out->start   = !!(s->buttons & N64_BTN_START);
  out->l       = !!(s->buttons & N64_BTN_L);
  out->r       = !!(s->buttons & N64_BTN_R);
  out->d_up    = !!(s->buttons & N64_DPAD_UP);
  out->d_down  = !!(s->buttons & N64_DPAD_DOWN);
  out->d_left  = !!(s->buttons & N64_DPAD_LEFT);
  out->d_right = !!(s->buttons & N64_DPAD_RIGHT);
  out->c_up    = !!(s->buttons & N64_BTN_CUP);
  out->c_down  = !!(s->buttons & N64_BTN_CDOWN);
  out->c_left  = !!(s->buttons & N64_BTN_CLEFT);
  out->c_right = !!(s->buttons & N64_BTN_CRIGHT);
}

static void snap_gc(pad_snap_t *out, int p, u16 buttons) {
  out->style = STYLE_GCN;
  out->rumble_supported = true;  // GC controllers have built-in rumble motor
  out->stick_x = PAD_StickX(p);
  out->stick_y = PAD_StickY(p);
  out->cstick_x = PAD_SubStickX(p);
  out->cstick_y = PAD_SubStickY(p);
  out->analog_l = PAD_TriggerL(p);
  out->analog_r = PAD_TriggerR(p);
  out->a       = !!(buttons & PAD_BUTTON_A);
  out->b       = !!(buttons & PAD_BUTTON_B);
  out->x       = !!(buttons & PAD_BUTTON_X);
  out->y       = !!(buttons & PAD_BUTTON_Y);
  out->z       = !!(buttons & PAD_TRIGGER_Z);
  out->l       = !!(buttons & PAD_TRIGGER_L);
  out->r       = !!(buttons & PAD_TRIGGER_R);
  out->start   = !!(buttons & PAD_BUTTON_START);
  out->d_up    = !!(buttons & PAD_BUTTON_UP);
  out->d_down  = !!(buttons & PAD_BUTTON_DOWN);
  out->d_left  = !!(buttons & PAD_BUTTON_LEFT);
  out->d_right = !!(buttons & PAD_BUTTON_RIGHT);
}

static void print_port(int p, const pad_snap_t *s) {
  SetFgColor(2, 2);
  printf("Port %d ", p + 1);
  SetFgColor(3, 2);
  printf("Style: %s ", format_style(s->style));
  printf("Pak: %s ", format_pak(s->pak));
  printf("Rumble: %-11s\n", format_rumble(s->rumble_supported, s->rumble_active));
  SetFgColor(7, 2);
  printf("Stick: %+04d,%+04d C-Stick: %+04d,%+04d L-Trig:%03u R-Trig:%03u\n",
         s->stick_x, s->stick_y, s->cstick_x, s->cstick_y, s->analog_l,
         s->analog_r);
  printf("A:%d B:%d X:%d Y:%d L:%d R:%d Z:%d Start:%d\n",
         s->a, s->b, s->x, s->y, s->l, s->r, s->z, s->start);
  printf("D-U:%d D-D:%d D-L:%d D-R:%d C-U:%d C-D:%d C-L:%d C-R:%d\n\n",
         s->d_up, s->d_down, s->d_left, s->d_right, s->c_up, s->c_down,
         s->c_left, s->c_right);
}

int main(int argc, char **argv) {
  // Init
  VIDEO_Init();
  PAD_Init();
  rMode = VIDEO_GetPreferredMode(NULL);
  xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rMode));
  console_init(xfb, CONSOLE_START_POS, CONSOLE_START_POS, rMode->fbWidth,
               rMode->xfbHeight, rMode->fbWidth * VI_DISPLAY_PIX_SZ);
  VIDEO_Configure(rMode);
  VIDEO_SetNextFramebuffer(xfb);
  VIDEO_SetBlack(FALSE);
  VIDEO_Flush();

  VIDEO_WaitVSync();
  if (rMode->viTVMode & VI_NON_INTERLACE)
    VIDEO_WaitVSync();

  // Title row sits below TV overscan.
  SetPosition(0, 3);
  SetFgColor(2, 2);
  printf("Joypad Test Suite");
  SetFgColor(7, 2);

  u16 keysHeld[4] = {0, 0, 0, 0};
#ifdef __WII__
  u16 keysHeldOld[4] = {0, 0, 0, 0};
#endif
  N64State n64[SI_MAX_CHAN] = {0};

  while (1) {
    PAD_ScanPads();
    N64_Scan(n64);
    for (int i = 0; i < 4; i++) {
      keysHeld[i] = PAD_ButtonsHeld(i);
    }

    // Rumble while A is held (matches libdragon's JoypadTest reference).
    for (int i = 0; i < 4; i++) {
      if (n64[i].present) {
        N64_SetRumble(i, !!(n64[i].buttons & N64_BTN_A));
      } else if ((SI_GetType(i) & SI_TYPE_MASK) == SI_TYPE_GC) {
        PAD_ControlMotor(i, (keysHeld[i] & PAD_BUTTON_A) ? 1 : 0);
      }
    }

    // Repaint each port at a fixed row (5 rows: header + 3 data + blank).
    int base_row = 5;
    for (int i = 0; i < 4; i++) {
      pad_snap_t snap = {0};
      u32 raw_type = SI_GetType(i);
      if (n64[i].present) {
        snap_n64(&snap, &n64[i]);
      } else if ((raw_type & ~0x001F0000) == SI_GC_KEYBOARD) {
        // GC ASCII keyboard — detection only; full key polling (cmd 0x54
        // with 8-byte response carrying 3 simultaneous keycodes) is TODO,
        // requires hardware to verify scancode→label mapping.
        snap.style = STYLE_KEYBOARD;
      } else if ((raw_type & SI_TYPE_MASK) == SI_TYPE_GC) {
        snap_gc(&snap, i, keysHeld[i]);
      }
      // STYLE_NONE leaves all zeros, including style="None"
      SetPosition(0, base_row + i * 5);
      print_port(i, &snap);
    }

#ifdef __WII__
    // L+R together on any GC port returns to the loader.
    for (int i = 0; i < 4; i++) {
      if (keysHeld[i] & PAD_TRIGGER_L && keysHeldOld[i] & PAD_TRIGGER_R) {
        exit(0);
      }
      keysHeldOld[i] = keysHeld[i];
    }
#endif

    LongWait(2);
  }
  exit(0);
}
