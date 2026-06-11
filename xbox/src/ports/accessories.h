/*
 * accessories.h -- detect & classify Xbox-controller expansion-slot
 * devices (Memory Units, Voice Communicator, Headset, ...).
 *
 * Xbox MUs are documented at https://xboxdevwiki.net/Xbox_Memory_Unit
 * as standard USB Mass Storage devices (class 0x08, subclass 0x42,
 * protocol 0x50) -- not the proprietary protocol I first guessed.
 * Voice Communicator + the wired headset are standard USB Audio
 * Class devices. nxdk's libusbohci_xbox ships usbh_umas and usbh_uac
 * drivers so we can enumerate both directly.
 *
 * v0.1.0 surfaces COUNTS + per-device summary; mapping a USB device
 * back to (Xbox port, expansion slot) requires walking the hub
 * topology (udev->parent chain) and lands in v0.2.0.
 */
#ifndef JT_XBOX_ACCESSORIES_H
#define JT_XBOX_ACCESSORIES_H

#include <stdbool.h>
#include <stdint.h>

/* Bring up usbh_core + register MSC/UAC class drivers. MUST be
 * called BEFORE SDL_Init so the class handlers are in place when
 * SDL's USB enumeration runs -- otherwise pre-attached MUs and
 * headsets miss their class probe and only show up on hot-plug. */
void jt_accessories_register_class_drivers(void);

/* Clear per-port state buffers. Call once after SDL_Init. */
void jt_accessories_init(void);

/* Tick the USB host stack. Must be called every frame for hot-plug
 * events to propagate (SDL only pumps the XID driver). */
void jt_accessories_tick(void);

/* Snapshot counters surfaced in the tester's diagnostic row. */
int jt_accessory_msc_count(void);    /* Mass Storage devices (MUs)   */
int jt_accessory_uac_count(void);    /* Audio Class (Comm / Headset) */
int jt_accessory_usb_total(void);    /* All connected USB devices    */

/* Best-effort textual summary for a per-pad + per-slot cell.
 *   port_idx 0..3       (Xbox chassis port 1..4)
 *   pad_idx  0 = primary, 1 = daisy-chained second pad
 *   slot_idx 0..1       (top / bottom expansion slot on that pad)
 * Returns "MU 8MB", "Headset", "" if empty. The daisy + primary pads
 * share a chassis port but each has its own pair of expansion slots,
 * so the slot label needs to know which pad the device sits in. */
const char *jt_accessory_for_slot(int port_idx, int pad_idx, int slot_idx);

/* Diagnostic: returns a short summary of the first MSC device's
 * port-resolution walk -- "MSC@p1.s0" / "MSC@p0?" / "no MSC" / etc.
 * Used to debug the udev->parent topology mapping. */
const char *jt_accessory_first_msc_debug(void);

/* True if any XID device of XREMOTE bType (DVD-Movie-Playback-Kit
 * IR receiver) is plugged into the given Xbox port. nxdk's SDL XID
 * driver ignores non-gamepad XID classes, so SDL doesn't surface
 * the receiver; we walk usbh_xid_get_device_list directly. */
bool jt_accessory_dvd_remote_at_port(int port_idx);

/* Latest IR button code + time-since-last-press (ms) for a DVD
 * remote receiver on `port_idx`. The 16-bit buttonCode comes from
 * the XREMOTE interrupt-in report bytes 2-3 (little-endian).
 * Returns false if no receiver is plugged in or its first interrupt
 * report hasn't landed yet. */
bool jt_accessory_dvd_remote_state(int port_idx,
                                   uint16_t *button_code,
                                   uint16_t *time_since_ms);

/* Decode a DVD-remote buttonCode into a short human-readable label
 * (e.g. "Play", "Title", "1"). Returns "" for unknown codes. */
const char *jt_accessory_dvd_button_name(uint16_t button_code);

/* ---- USB HID (boot-protocol keyboard / mouse) ------------------- */

/* True if a USB keyboard / mouse is plugged into this Xbox port. */
bool jt_accessory_keyboard_present(int port_idx);
bool jt_accessory_mouse_present(int port_idx);

/* Short label for an XID gamepad type ("Duke", "S Ctlr", "Wheel",
 * "ArcStk", "SteelBtl", "Pad?"). Looked up via the XID descriptor's
 * bType/bSubType, matched against the SDL joystick instance ID
 * (which nxdk pins to xid_dev->uid). */
const char *jt_accessory_xid_type_label(int32_t instance_id);

/* Diagnostic: raw counts of every HID class driver match -- ignores
 * composite-mouse suppression. Lets the tester display "this hub has
 * one kbd-interface but we suppressed it" vs "no kbd-interface at all
 * was enumerated by nxdk's HID driver." */
int jt_accessory_hid_kbd_count_raw(int port_idx);
int jt_accessory_hid_mouse_count_raw(int port_idx);

/* Per-chassis-port counts -- did each class driver see anything on
 * this chassis port? Surfaces detection failure (count=0) vs labelling
 * failure (count>0 but cell still empty). */
int jt_accessory_xid_count_at_port(int port_idx);
int jt_accessory_msc_count_at_port(int port_idx);
int jt_accessory_uac_count_at_port(int port_idx);

/* Latest boot-protocol keyboard report: modifier mask + up to 6
 * simultaneously-held USB-HID scancodes. Returns false if no
 * keyboard is at that port or its first report hasn't landed. */
bool jt_accessory_keyboard_at_port(int port_idx,
                                   uint8_t *modifiers, uint8_t keys[6]);

/* Latest boot-protocol mouse report: 3-bit button mask + signed
 * dx/dy, plus the wheel delta if the device shipped one in the
 * report (byte 3 of the boot report -- most mice include it even
 * though boot protocol officially defines only 3 bytes; *wheel
 * stays 0 if the device didn't send it). */
bool jt_accessory_mouse_at_port(int port_idx,
                                uint8_t *buttons, int8_t *dx, int8_t *dy,
                                int8_t *wheel);

/* Drain the per-mouse motion accumulator since the last call. Unlike
 * the snapshot API above (which returns whatever bytes were in the
 * most recent interrupt report), this sums every dx / dy / wheel delta
 * received between calls -- so an on-screen cursor doesn't lose motion
 * between frames, and a cumulative wheel display ticks up by every
 * scroll click even if the next idle report zeroes the byte. Pass
 * NULL for outputs you don't need. Returns false when no mouse is
 * plugged into `port_idx`. */
bool jt_accessory_mouse_consume_motion(int port_idx,
                                       int32_t *dx_sum, int32_t *dy_sum,
                                       int32_t *wheel_sum);

/* ---- Steel Battalion controller (XID bType=0x80) --------------- */

bool jt_accessory_steel_battalion_at_port(int port_idx);
bool jt_accessory_steel_battalion_state(int port_idx,
                                       uint32_t *buttons_a,
                                       uint32_t *buttons_b,
                                       int8_t *gear);

/* ---- Direct-chassis (no pad) accessories ------------------------
 * MUs / headsets / hubs plugged straight into a controller port with
 * a USB adapter (no pad in between). Same class detection as the
 * in-slot path; just topologically distinct, so the tester can show
 * "Port N Type:MU 8MB" rather than treat them as the slot of a
 * non-existent pad. */
bool jt_accessory_mu_direct_at_port(int port_idx, uint32_t *total_kb);
bool jt_accessory_headset_direct_at_port(int port_idx);
bool jt_accessory_hub_direct_at_port(int port_idx);

/* Find a connected XID controller whose root-hub port isn't claimed
 * yet by `claimed[]` (an array of bools indexed 0..3). Returns the
 * 0-based Xbox port idx for the first match, or -1 if none. Used by
 * ports.c to map a new SDL_GameController to its actual USB port
 * instead of falling back to "first free slot" semantics. */
int jt_accessory_unclaimed_xid_port(const bool claimed[]);

/* Read the 0..255 analog-button pressure bytes off the XID device's
 * most-recent interrupt-in transfer buffer. `instance_id` is
 * SDL_JoystickInstanceID for the controller, which nxdk's SDL pins
 * to xid_dev->uid -- so we can match SDL pads to their underlying
 * XID device cheaply. Returns false if no matching pad is found or
 * the buffer hasn't been populated yet. Output pointers are filled
 * with 0..255 pressures (A, B, X, Y, Black, White). */
bool jt_accessory_xid_pressure(int32_t instance_id,
                               uint8_t *a, uint8_t *b,
                               uint8_t *x, uint8_t *y,
                               uint8_t *black, uint8_t *white);

#endif /* JT_XBOX_ACCESSORIES_H */
