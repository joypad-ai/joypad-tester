/*
 * accessories.c -- USB-class-driven detection of Xbox-controller
 * expansion-slot devices.
 *
 * We treat the question "what's in this slot?" as "what USB device
 * is attached at this hub port?" and look it up via nxdk's class
 * drivers rather than chasing Xbox-specific kernel device names:
 *   - usbh_umas / msc_dev_t -> Memory Unit (or any FATX-able USB MSC)
 *   - usbh_uac  / uac_dev_t -> Voice Communicator / headset
 *
 * usbh_pooling_hubs ticks the underlying OHCI scheduler so add/remove
 * events propagate. SDL only pumps it for the XID driver, so we
 * call it again per frame from jt_accessories_tick.
 */
#include "accessories.h"

#include <usbh_lib.h>
#include <usbh_msc.h>
#include <usbh_uac.h>
#include <usbh_hid.h>
#include <hub.h>
#include <xid_driver.h>
#include <xboxkrnl/xboxkrnl.h>
#include <windows.h>   /* Sleep() */
#include <stdio.h>
#include <string.h>

#include "ports.h"

static int msc_count;
static int uac_count;
static int usb_total;

/* Per-pad-per-slot label table. The first axis is the chassis port
 * (0..3); the second is which pad on that port -- 0 = primary,
 * 1 = daisy-chained; the third is the expansion-slot index (0..1).
 * Daisy + primary share a chassis port but each has its own slot
 * pair, so a mic in the daisy pad's slot 1 must not bleed into the
 * primary pad's slot 1. */
#define PAD_PRIMARY  0
#define PAD_DAISY    1
#define PAD_SLOTS    2
static char port_slot_label[JT_NUM_PORTS][PAD_SLOTS][JT_NUM_SLOTS][24];

/* Forward decls -- definitions further down. */
static UTR_T *xid_find_in_utr(xid_dev_t *xd);
static int    udev_root_port(UDEV_T *udev);

static char first_msc_debug[48];

const char *jt_accessory_first_msc_debug(void) { return first_msc_debug; }

/* ---------- Xbox Communicator / Headset detection -----------------
 *
 * The Xbox Communicator (xboxdevwiki.net/Xbox_Live_Communicator) uses
 * bInterfaceClass = 0x78 (Xbox-specific) rather than the standard
 * USB Audio Class (0x01), so nxdk's usbh_uac probe rejects it.
 *
 * Register a tiny class driver that matches 0x78 and keeps its own
 * device list -- enough to detect presence + map to (port, slot)
 * for the tester display. We don't actually stream audio. */
#define XBOX_AUDIO_INTERFACE_CLASS 0x78

typedef struct xbox_audio_dev {
    IFACE_T *iface;
    struct xbox_audio_dev *next;
} xbox_audio_dev_t;

static xbox_audio_dev_t *xbox_audio_list;

static int xbox_audio_probe(IFACE_T *iface)
{
    if (!iface || !iface->aif || !iface->aif->ifd) return USBH_ERR_NOT_MATCHED;
    if (iface->aif->ifd->bInterfaceClass != XBOX_AUDIO_INTERFACE_CLASS)
        return USBH_ERR_NOT_MATCHED;
    /* Avoid double-add if multiple interfaces from the same device
     * land here. */
    for (xbox_audio_dev_t *d = xbox_audio_list; d; d = d->next) {
        if (d->iface && d->iface->udev == iface->udev) return 0;
    }
    xbox_audio_dev_t *node = (xbox_audio_dev_t *)usbh_alloc_mem(sizeof(*node));
    if (!node) return USBH_ERR_MEMORY_OUT;
    node->iface = iface;
    node->next = xbox_audio_list;
    xbox_audio_list = node;
    return 0;
}

static void xbox_audio_disconnect(IFACE_T *iface)
{
    xbox_audio_dev_t **pp = &xbox_audio_list;
    while (*pp) {
        if ((*pp)->iface == iface ||
            ((*pp)->iface && (*pp)->iface->udev == iface->udev)) {
            xbox_audio_dev_t *gone = *pp;
            *pp = gone->next;
            usbh_free_mem(gone, sizeof(*gone));
        } else {
            pp = &(*pp)->next;
        }
    }
}

static UDEV_DRV_T xbox_audio_driver = {
    xbox_audio_probe,
    xbox_audio_disconnect,
    NULL,   /* suspend */
    NULL,   /* resume  */
};

/* ---------- USB HID (keyboard + mouse) tracking -------------------
 *
 * nxdk's HID driver is a polling class driver: applications call
 * usbh_hid_start_int_read on each device to get an interrupt-read
 * loop, with a callback that delivers each report. We cache the
 * latest report per HID device uid so the tester can read it on
 * demand without coordinating callbacks with the render path.
 *
 * boot-protocol layouts:
 *   keyboard (subclass 1, protocol 1) -- 8 bytes:
 *     [0] modifier mask (bit0=LCtrl, bit1=LShift, bit2=LAlt,
 *         bit3=LGUI, bit4=RCtrl, bit5=RShift, bit6=RAlt, bit7=RGUI)
 *     [1] reserved
 *     [2..7] up to 6 simultaneous USB-HID scancodes
 *   mouse (subclass 1, protocol 2) -- 3 bytes:
 *     [0] button mask (bit0=left, bit1=right, bit2=middle)
 *     [1] dx signed int8
 *     [2] dy signed int8
 */
#define HID_REPORT_BUF 16
#define HID_TRACKED    8

typedef struct {
    uint32_t uid;
    uint8_t  data[HID_REPORT_BUF];
    uint32_t len;
} hid_report_t;

static hid_report_t hid_reports[HID_TRACKED];

/* Per-mouse motion accumulator. The snapshot in hid_reports[] only
 * keeps the LAST interrupt report, so reading dx/dy directly there
 * drops motion that happened between frames (or between two reports
 * inside a single frame). For the on-screen cursor we need every
 * delta integrated; for wheel display we need totals so the user
 * sees scroll-byte presence even when the byte returns to 0 between
 * ticks. Sum here in the callback, drain in jt_accessory_mouse_consume_motion. */
typedef struct {
    uint32_t uid;
    int32_t  dx_sum;
    int32_t  dy_sum;
    int32_t  wheel_sum;
} mouse_accum_t;
static mouse_accum_t mouse_accum[HID_TRACKED];

/* Per-mouse parsed report layout. Populated once at probe time by
 * fetching the HID report descriptor and scanning for a REPORT_ID
 * global item. When present, every report from this device starts
 * with a 1-byte ID prefix, so the [btns][dx][dy][wheel] layout
 * starts at offset 1 instead of 0. We use this for BOTH the snapshot
 * read API and the accumulator path in the callback. */
typedef struct {
    uint32_t uid;
    bool     valid;
    bool     report_id_present;
} mouse_layout_t;
static mouse_layout_t mouse_layouts[HID_TRACKED];

static mouse_layout_t *mouse_layout_for(uint32_t uid)
{
    for (int i = 0; i < HID_TRACKED; i++) {
        if (mouse_layouts[i].uid == uid) return &mouse_layouts[i];
    }
    return NULL;
}

static bool hid_is_mouse(HID_DEV_T *h);   /* forward decl */

static void hid_int_read_cb(HID_DEV_T *hdev, uint16_t ep_addr, int status,
                            uint8_t *rdata, uint32_t data_len)
{
    (void)ep_addr;
    if (status < 0 || !rdata || !hdev) return;
    int free_slot = -1;
    int slot = -1;
    for (int i = 0; i < HID_TRACKED; i++) {
        if (hid_reports[i].uid == hdev->uid) { slot = i; break; }
        if (hid_reports[i].uid == 0 && free_slot < 0) free_slot = i;
    }
    if (slot < 0) slot = free_slot;
    if (slot < 0) return;
    hid_reports[slot].uid = hdev->uid;
    hid_reports[slot].len = (data_len > HID_REPORT_BUF) ? HID_REPORT_BUF : data_len;
    memcpy(hid_reports[slot].data, rdata, hid_reports[slot].len);

    if (hid_is_mouse(hdev)) {
        mouse_layout_t *ml = mouse_layout_for(hdev->uid);
        int off = (ml && ml->valid && ml->report_id_present) ? 1 : 0;
        if (data_len < (uint32_t)(off + 3)) return;
        int8_t dx = (int8_t)rdata[off + 1];
        int8_t dy = (int8_t)rdata[off + 2];
        int8_t wheel = (data_len >= (uint32_t)(off + 4)) ? (int8_t)rdata[off + 3] : 0;
        int aslot = -1, afree = -1;
        for (int i = 0; i < HID_TRACKED; i++) {
            if (mouse_accum[i].uid == hdev->uid) { aslot = i; break; }
            if (mouse_accum[i].uid == 0 && afree < 0) afree = i;
        }
        if (aslot < 0) aslot = afree;
        if (aslot >= 0) {
            mouse_accum[aslot].uid        = hdev->uid;
            mouse_accum[aslot].dx_sum    += dx;
            mouse_accum[aslot].dy_sum    += dy;
            mouse_accum[aslot].wheel_sum += wheel;
        }
    }
}

static const hid_report_t *hid_report_for(uint32_t uid)
{
    for (int i = 0; i < HID_TRACKED; i++) {
        if (hid_reports[i].uid == uid) return &hid_reports[i];
    }
    return NULL;
}

static int hid_root_port(HID_DEV_T *h)
{
    if (!h) return -1;
    IFACE_T *iface = (IFACE_T *)h->iface;
    if (!iface || !iface->udev) return -1;
    return udev_root_port(iface->udev);
}

static bool hid_is_kbd(HID_DEV_T *h)
{
    return h && h->bSubClassCode == 1 && h->bProtocolCode == 1;
}

static bool hid_is_mouse(HID_DEV_T *h)
{
    return h && h->bSubClassCode == 1 && h->bProtocolCode == 2;
}

/* Track which HID devices we've already pushed into boot protocol.
 * usbh_hid_set_protocol issues a control transfer, so we only call
 * it once per device. The OG Xbox kernel's behaviour after enumeration
 * leaves HID devices in report-protocol mode -- with a custom report
 * descriptor most mice prefix their reports with a report-ID byte,
 * which threw off our [buttons][dx][dy] parsing and made clicks look
 * like X-axis motion. Boot protocol guarantees the simple 3-byte
 * mouse / 8-byte keyboard layouts. */
static uint32_t hid_boot_set_uids[HID_TRACKED];

static bool hid_boot_already_set(uint32_t uid)
{
    for (int i = 0; i < HID_TRACKED; i++) {
        if (hid_boot_set_uids[i] == uid) return true;
    }
    return false;
}

static void hid_boot_mark_set(uint32_t uid)
{
    for (int i = 0; i < HID_TRACKED; i++) {
        if (hid_boot_set_uids[i] == 0) {
            hid_boot_set_uids[i] = uid;
            return;
        }
    }
}

/* Fetch the HID report descriptor for a mouse and scan for the
 * REPORT_ID global item (short-item prefix 0x84..0x87, i.e.
 * bTag=8, bType=01 global, any bSize). When present, every report
 * from this device starts with a 1-byte report ID prefix -- and
 * the wheel byte only exists in REPORT protocol (not BOOT). So:
 *   has REPORT_ID  -> use report protocol, parse with offset=1
 *   no REPORT_ID   -> use boot protocol (3-byte spec) OR report
 *                     protocol (4-byte with wheel) -- we pick report
 *                     because every USB mouse we've tested ships a
 *                     4-byte layout there and dropping back to boot
 *                     loses the wheel byte entirely.
 * Records the result in mouse_layouts[] so the callback + snapshot
 * read can use the right offset. */
static void mouse_probe_layout(HID_DEV_T *h)
{
    if (mouse_layout_for(h->uid)) return;
    int free_slot = -1;
    for (int i = 0; i < HID_TRACKED; i++) {
        if (mouse_layouts[i].uid == 0) { free_slot = i; break; }
    }
    if (free_slot < 0) return;
    mouse_layout_t *ml = &mouse_layouts[free_slot];
    ml->uid = h->uid;

    uint8_t desc[256];
    int n = usbh_hid_get_report_descriptor(h, desc, sizeof(desc));
    if (n <= 0) {
        /* Descriptor fetch failed -- fall back to boot protocol so
         * dx/dy still work even though wheel is lost. */
        usbh_hid_set_protocol(h, 0);
        ml->valid = true;
        ml->report_id_present = false;
        return;
    }
    bool has_report_id = false;
    int i = 0;
    while (i < n) {
        uint8_t prefix = desc[i];
        if (prefix == 0xFE) {
            /* Long-item form: bSize byte + bTag byte + bSize data bytes. */
            if (i + 2 >= n) break;
            int sz = desc[i + 1];
            i += 3 + sz;
            continue;
        }
        if ((prefix & 0xFC) == 0x84) { has_report_id = true; break; }
        uint8_t bsize = prefix & 0x03;
        int dsize = (bsize == 3) ? 4 : bsize;
        i += 1 + dsize;
    }
    ml->valid = true;
    ml->report_id_present = has_report_id;
    /* Always use REPORT protocol for mice now -- BOOT is 3 bytes and
     * strips the wheel byte. The descriptor tells us where btns/dx/
     * dy/wheel sit within each report (offset 0 or 1 depending on
     * report-ID prefix). */
    usbh_hid_set_protocol(h, 1);
}

/* Start polling reads on every HID device we haven't already started.
 * usbh_hid_start_int_read returns HID_RET_XFER_IS_RUNNING when the
 * pipe is already active, so re-calling is idempotent. Keyboards
 * stay in boot protocol (8-byte fixed layout) since we don't need
 * extra keys; mice get the descriptor-probe path so wheel works. */
static void hid_start_polling_all(void)
{
    for (HID_DEV_T *h = usbh_hid_get_device_list(); h; h = h->next) {
        if (hid_is_kbd(h)) {
            if (!hid_boot_already_set(h->uid)) {
                usbh_hid_set_protocol(h, 0);   /* 0 = boot, 1 = report */
                hid_boot_mark_set(h->uid);
            }
        } else if (hid_is_mouse(h)) {
            mouse_probe_layout(h);
        } else {
            continue;
        }
        usbh_hid_start_int_read(h, 0, hid_int_read_cb);
    }
}

bool jt_accessory_keyboard_at_port(int port_idx,
                                   uint8_t *modifiers, uint8_t keys[6])
{
    for (HID_DEV_T *h = usbh_hid_get_device_list(); h; h = h->next) {
        if (!hid_is_kbd(h)) continue;
        if (hid_root_port(h) != port_idx) continue;
        const hid_report_t *r = hid_report_for(h->uid);
        if (!r || r->len < 8) return false;
        if (modifiers) *modifiers = r->data[0];
        if (keys) memcpy(keys, &r->data[2], 6);
        return true;
    }
    return false;
}

bool jt_accessory_mouse_at_port(int port_idx,
                                uint8_t *buttons, int8_t *dx, int8_t *dy,
                                int8_t *wheel)
{
    for (HID_DEV_T *h = usbh_hid_get_device_list(); h; h = h->next) {
        if (!hid_is_mouse(h)) continue;
        if (hid_root_port(h) != port_idx) continue;
        const hid_report_t *r = hid_report_for(h->uid);
        if (!r) return false;
        const mouse_layout_t *ml = mouse_layout_for(h->uid);
        int off = (ml && ml->valid && ml->report_id_present) ? 1 : 0;
        if (r->len < (uint32_t)(off + 3)) return false;
        if (buttons) *buttons = r->data[off + 0];
        if (dx)      *dx      = (int8_t)r->data[off + 1];
        if (dy)      *dy      = (int8_t)r->data[off + 2];
        /* Wheel: byte after dy if the report carries it. Report
         * protocol always includes it for any mouse with a scroll
         * wheel; descriptor parsing sets the offset above. */
        if (wheel)   *wheel   = (r->len >= (uint32_t)(off + 4))
                              ? (int8_t)r->data[off + 3] : 0;
        return true;
    }
    return false;
}

bool jt_accessory_mouse_consume_motion(int port_idx,
                                       int32_t *dx_sum, int32_t *dy_sum,
                                       int32_t *wheel_sum)
{
    for (HID_DEV_T *h = usbh_hid_get_device_list(); h; h = h->next) {
        if (!hid_is_mouse(h)) continue;
        if (hid_root_port(h) != port_idx) continue;
        for (int i = 0; i < HID_TRACKED; i++) {
            if (mouse_accum[i].uid != h->uid) continue;
            if (dx_sum)    *dx_sum    = mouse_accum[i].dx_sum;
            if (dy_sum)    *dy_sum    = mouse_accum[i].dy_sum;
            if (wheel_sum) *wheel_sum = mouse_accum[i].wheel_sum;
            mouse_accum[i].dx_sum = 0;
            mouse_accum[i].dy_sum = 0;
            mouse_accum[i].wheel_sum = 0;
            return true;
        }
        return false;
    }
    return false;
}

/* True if `kbd` is the keyboard half of a composite mouse: i.e. its
 * underlying UDEV_T (the physical USB device) ALSO exposes a HID
 * mouse interface. Gaming / multibutton mice advertise both classes
 * so media keys + scroll wheels can ride alongside the mouse
 * reports; matching on subclass+protocol alone double-renders them.
 *
 * Even when the phantom keyboard interface never sends a real key,
 * its idle-report stays a valid empty report (all zeros), so a
 * "report-received" filter doesn't catch it. Walking the device
 * tree for a sibling mouse interface does. */
static bool hid_is_composite_mouse_kbd(HID_DEV_T *kbd)
{
    if (!kbd || !hid_is_kbd(kbd)) return false;
    IFACE_T *kbd_iface = (IFACE_T *)kbd->iface;
    if (!kbd_iface || !kbd_iface->udev) return false;
    for (HID_DEV_T *m = usbh_hid_get_device_list(); m; m = m->next) {
        if (!hid_is_mouse(m)) continue;
        IFACE_T *m_iface = (IFACE_T *)m->iface;
        if (m_iface && m_iface->udev == kbd_iface->udev) return true;
    }
    return false;
}

const char *jt_accessory_xid_type_label(int32_t instance_id)
{
    for (xid_dev_t *x = usbh_xid_get_device_list(); x; x = x->next) {
        if ((int32_t)x->uid != instance_id) continue;
        /* xid_type encodes bType<<8 | bSubType. Match the canonical
         * Microsoft variants (see xid_driver.h enum); fall back to
         * "Pad?" for unrecognised gamepad subtypes. */
        switch (usbh_xid_get_type(x)) {
            case GAMECONTROLLER_S:           return "S Ctlr";
            case GAMECONTROLLER_DUKE:        return "Duke";
            case GAMECONTROLLER_WHEEL:       return "Wheel";
            case GAMECONTROLLER_ARCADESTICK: return "ArcStk";
            case STEEL_BATTALION:            return "SteelBtl";
            default:                         return "Pad?";
        }
    }
    return "Pad?";
}

bool jt_accessory_keyboard_present(int port_idx)
{
    for (HID_DEV_T *h = usbh_hid_get_device_list(); h; h = h->next) {
        if (!hid_is_kbd(h)) continue;
        if (hid_root_port(h) != port_idx) continue;
        if (hid_is_composite_mouse_kbd(h)) continue;
        return true;
    }
    return false;
}

bool jt_accessory_mouse_present(int port_idx)
{
    for (HID_DEV_T *h = usbh_hid_get_device_list(); h; h = h->next) {
        if (!hid_is_mouse(h)) continue;
        if (hid_root_port(h) != port_idx) continue;
        return true;
    }
    return false;
}

int jt_accessory_hid_kbd_count_raw(int port_idx)
{
    int n = 0;
    for (HID_DEV_T *h = usbh_hid_get_device_list(); h; h = h->next) {
        if (hid_is_kbd(h) && hid_root_port(h) == port_idx) n++;
    }
    return n;
}

int jt_accessory_hid_mouse_count_raw(int port_idx)
{
    int n = 0;
    for (HID_DEV_T *h = usbh_hid_get_device_list(); h; h = h->next) {
        if (hid_is_mouse(h) && hid_root_port(h) == port_idx) n++;
    }
    return n;
}

int jt_accessory_xid_count_at_port(int port_idx)
{
    int n = 0;
    for (xid_dev_t *x = usbh_xid_get_device_list(); x; x = x->next) {
        if (!x->iface || !x->iface->udev) continue;
        if (udev_root_port(x->iface->udev) == port_idx) n++;
    }
    return n;
}

int jt_accessory_msc_count_at_port(int port_idx)
{
    int n = 0;
    for (MSC_T *m = usbh_msc_get_device_list(); m; m = m->next) {
        if (!m->iface || !m->iface->udev) continue;
        if (udev_root_port(m->iface->udev) == port_idx) n++;
    }
    return n;
}

int jt_accessory_uac_count_at_port(int port_idx)
{
    int n = 0;
    for (UAC_DEV_T *a = usbh_uac_get_device_list(); a; a = a->next) {
        if (!a->udev) continue;
        if (udev_root_port(a->udev) == port_idx) n++;
    }
    for (xbox_audio_dev_t *d = xbox_audio_list; d; d = d->next) {
        if (!d->iface || !d->iface->udev) continue;
        if (udev_root_port(d->iface->udev) == port_idx) n++;
    }
    return n;
}

/* True if `u` is plugged directly into the Xbox chassis (no
 * intermediate hub / controller). On Xbox, "direct on chassis"
 * means the device's parent hub IS the internal USB hub that sits
 * between the OHCI root and the four chassis ports -- nxdk
 * represents that internal hub as a UDEV_T whose own `parent` is
 * NULL. So if our device's parent hub-udev has NULL parent, we're
 * a direct child of the chassis. */
static bool udev_is_direct_on_chassis(UDEV_T *u)
{
    if (!u || !u->parent) return false;
    UDEV_T *parent_udev =
        u->parent->iface ? u->parent->iface->udev : NULL;
    if (!parent_udev) return false;
    return parent_udev->parent == NULL;
}

/* True if `target` has an XID-class interface (bInterfaceClass 0x58,
 * subclass 0x42) -- i.e. the device is an Xbox controller / remote /
 * Steel Battalion. We check the device's own interface descriptors
 * rather than walking usbh_xid_get_device_list and comparing UDEV_T
 * pointers, because for compound devices a hub interface and an XID
 * interface can have different IFACE_T->udev pointers in nxdk even
 * though they belong to the same physical device. The interface
 * descriptor check works regardless. */
static bool udev_is_xid_controller(UDEV_T *target)
{
    if (!target) return false;
    for (IFACE_T *iface = target->iface_list; iface; iface = iface->next) {
        DESC_IF_T *ifd = iface->aif ? iface->aif->ifd : NULL;
        if (!ifd) continue;
        if (ifd->bInterfaceClass == XID_INTERFACE_CLASS &&
            ifd->bInterfaceSubClass == XID_INTERFACE_SUBCLASS) return true;
    }
    return false;
}

/* Count UDEV_T hops from `u` up the hub chain to the top (where
 * `parent->iface->udev` resolves to NULL -- the internal Xbox hub
 * sits there). Returns the hop count, capped at 8 for safety. */
static int udev_hops_to_top(UDEV_T *u)
{
    if (!u) return -1;
    int hops = 0;
    UDEV_T *cursor = u;
    while (cursor && cursor->parent && hops < 8) {
        UDEV_T *parent_udev =
            cursor->parent->iface ? cursor->parent->iface->udev : NULL;
        if (!parent_udev) break;
        cursor = parent_udev;
        hops++;
    }
    return hops;
}

/* Resolve a UDEV to its (pad_idx, slot) position by walking up the
 * tree looking for a level at slot-depth.
 *
 * For simple devices (mic, MU, kbd) the iface's UDEV is already at
 * slot-depth (2 hops above the internal Xbox hub for a primary
 * pad's slot, 3 hops for a daisy's slot) and its port_num gives a
 * valid slot index (2 or 3 -> slot 0 or 1).
 *
 * For compound devices (an Xbox controller has separate HID + hub
 * interface UDEVs in nxdk) the XID-iface UDEV is too deep and its
 * port_num is the sub-iface index (1) which gives an invalid slot.
 * We walk up one level (to the outer device UDEV) and try again.
 *
 * Returns true on success and writes *out_pad_idx + *out_slot. */
static bool udev_resolve_slot(UDEV_T *u, int *out_pad_idx, int *out_slot)
{
    while (u) {
        int hops = udev_hops_to_top(u);
        if (hops < 2) return false;       /* at or above the chassis */
        if (hops <= 3) {
            int slot = u->port_num - 2;
            if (slot >= 0 && slot < JT_NUM_SLOTS) {
                *out_pad_idx = (hops == 2) ? PAD_PRIMARY : PAD_DAISY;
                *out_slot = slot;
                return true;
            }
        }
        /* Either too deep (compound device) or this UDEV's port_num
         * is the sub-iface number -- walk one level up and retry. */
        if (!u->parent || !u->parent->iface) return false;
        u = u->parent->iface->udev;
    }
    return false;
}

bool jt_accessory_mu_direct_at_port(int port_idx, uint32_t *total_kb)
{
    for (MSC_T *m = usbh_msc_get_device_list(); m; m = m->next) {
        if (!m->iface || !m->iface->udev) continue;
        UDEV_T *u = m->iface->udev;
        if (!udev_is_direct_on_chassis(u)) continue;
        if (udev_root_port(u) != port_idx) continue;
        if (total_kb) {
            uint64_t total = (uint64_t)m->uTotalSectorN *
                             (uint64_t)m->nSectorSize;
            *total_kb = (uint32_t)(total / 1024u);
        }
        return true;
    }
    return false;
}

bool jt_accessory_headset_direct_at_port(int port_idx)
{
    for (UAC_DEV_T *a = usbh_uac_get_device_list(); a; a = a->next) {
        if (!a->udev) continue;
        if (!udev_is_direct_on_chassis(a->udev)) continue;
        if (udev_root_port(a->udev) == port_idx) return true;
    }
    for (xbox_audio_dev_t *d = xbox_audio_list; d; d = d->next) {
        if (!d->iface || !d->iface->udev) continue;
        if (!udev_is_direct_on_chassis(d->iface->udev)) continue;
        if (udev_root_port(d->iface->udev) == port_idx) return true;
    }
    return false;
}

bool jt_accessory_hub_direct_at_port(int port_idx)
{
    extern UDEV_T *g_udev_list;
    for (UDEV_T *u = g_udev_list; u; u = u->next) {
        if (u->descriptor.bDeviceClass != 0x09) continue;
        /* The pad itself is a compound USB hub (class 0x09 + HID
         * interfaces for the controller). Skip every device that's
         * already in the XID list -- "Hub" should only label real
         * standalone USB hubs (or generic-class hub adapters), not
         * the controller's own hub functionality. */
        if (udev_is_xid_controller(u)) continue;
        if (!udev_is_direct_on_chassis(u)) continue;
        if (udev_root_port(u) == port_idx) return true;
    }
    return false;
}

/* ---------- Steel Battalion controller --------------------------
 *
 * The cockpit controller (one game: Steel Battalion + Steel Battalion
 * Line of Contact) shows up as XID bType=XID_TYPE_STEELBATTALION
 * (0x80). nxdk's SDL XID driver skips it the same way it skips the
 * DVD-Movie remote, so SDL never sees it; we walk the raw XID list.
 *
 * Report layout (xboxdevwiki.net/Steel_Battalion_Controller): 26
 * bytes with three button groups (toes/cockpit + radar/secondary +
 * weapons), aiming stick X/Y, sight changer, gear lever -2..5, and
 * four pedal axes. v1.0.0 surfaces detection + button bitmask + gear
 * lever -- enough to confirm the controller is recognised; deep
 * decode lands in a follow-up if anyone actually owns one to test.
 */
bool jt_accessory_steel_battalion_at_port(int port_idx)
{
    for (xid_dev_t *x = usbh_xid_get_device_list(); x; x = x->next) {
        if (x->xid_desc.bType != XID_TYPE_STEELBATTALION) continue;
        if (!x->iface || !x->iface->udev) continue;
        if (udev_root_port(x->iface->udev) == port_idx) return true;
    }
    return false;
}

bool jt_accessory_steel_battalion_state(int port_idx,
                                       uint32_t *buttons_a, uint32_t *buttons_b,
                                       int8_t *gear)
{
    for (xid_dev_t *x = usbh_xid_get_device_list(); x; x = x->next) {
        if (x->xid_desc.bType != XID_TYPE_STEELBATTALION) continue;
        if (!x->iface || !x->iface->udev) continue;
        if (udev_root_port(x->iface->udev) != port_idx) continue;
        UTR_T *utr = xid_find_in_utr(x);
        if (!utr || !utr->buff) return false;
        const uint8_t *b = utr->buff;
        /* Report layout per xboxdevwiki:
         *   [0]   startByte
         *   [1]   bLength (== 0x1A)
         *   [2-5] button group A (32 bits)
         *   [6-9] button group B (32 bits)
         *   [22]  gear lever (signed -2..5)
         * We surface enough to confirm the device, not all of it. */
        if (buttons_a)
            *buttons_a = (uint32_t)b[2] |
                         ((uint32_t)b[3] << 8) |
                         ((uint32_t)b[4] << 16) |
                         ((uint32_t)b[5] << 24);
        if (buttons_b)
            *buttons_b = (uint32_t)b[6] |
                         ((uint32_t)b[7] << 8) |
                         ((uint32_t)b[8] << 16) |
                         ((uint32_t)b[9] << 24);
        if (gear) *gear = (int8_t)b[22];
        return true;
    }
    return false;
}

void jt_accessories_register_class_drivers(void)
{
    /* Bring up the USB core ourselves so we can register the MSC
     * and UAC class drivers BEFORE SDL_Init kicks off its XID setup
     * + 500ms enumeration warmup. usbh_core_init is idempotent
     * (guarded by an internal flag), so SDL's later call is a no-op
     * and its warmup probes every device against every registered
     * driver -- pre-attached MUs / headsets get matched without us
     * needing an extra sleep loop.
     *
     * Order matters: if we leave this to jt_accessories_init() (which
     * runs after SDL_Init), the MSC driver is registered too late --
     * a pre-attached MU is already enumerated as a "no class match"
     * device and only appears if the user unplugs + re-plugs it. */
    usbh_core_init();
    usbh_umas_init();
    usbh_uac_init();
    usbh_hid_init();
    /* Custom probe for the Xbox Communicator / wired headset, which
     * advertises bInterfaceClass=0x78 instead of the standard USB
     * Audio Class 0x01 and is therefore invisible to usbh_uac. */
    usbh_register_driver(&xbox_audio_driver);
}

void jt_accessories_init(void)
{
    msc_count = uac_count = usb_total = 0;
    for (int p = 0; p < JT_NUM_PORTS; p++)
        for (int pi = 0; pi < PAD_SLOTS; pi++)
            for (int s = 0; s < JT_NUM_SLOTS; s++)
                port_slot_label[p][pi][s][0] = '\0';
}

/* Resolve a USB device to a 0-based Xbox port index (0..3).
 *
 * Mirrors nxdk's SDL2 SDL_xboxjoystick.c xid_get_device_port:
 *
 *   - Walk up the hub chain. There are two stop conditions:
 *       * Newer Xbox revisions have an internal USB hub layer
 *         (XBOX_HW_FLAG_INTERNAL_USB_HUB) -- stop when the parent
 *         device's *grandparent* is NULL (one level up from the
 *         OHCI root).
 *       * Older revisions wire ports directly -- stop when the
 *         current device's parent is NULL.
 *   - Then apply the OG Xbox's port-permutation table: the OHCI
 *     numbering doesn't match the chassis labels, so port_num
 *     3 -> port 1, 4 -> port 2, 1 -> port 3, 2 -> port 4.
 *
 * Returns -1 if the walk falls off the end (device disconnected or
 * not on a numbered chassis port). */
static int udev_root_port(UDEV_T *udev)
{
    if (!udev) return -1;
    ULONG has_internal_hub =
        XboxHardwareInfo.Flags & XBOX_HW_FLAG_INTERNAL_USB_HUB;
    UDEV_T *u = udev;
    while (u != NULL) {
        UDEV_T *parent_udev = NULL;
        if (u->parent != NULL && u->parent->iface != NULL) {
            parent_udev = u->parent->iface->udev;
        }
        bool stop;
        if (has_internal_hub) {
            stop = (parent_udev != NULL && parent_udev->parent == NULL);
        } else {
            stop = (u->parent == NULL);
        }
        if (stop) {
            switch (u->port_num) {
                case 3: return 0;   /* chassis port 1 */
                case 4: return 1;   /* chassis port 2 */
                case 1: return 2;   /* chassis port 3 */
                case 2: return 3;   /* chassis port 4 */
                default: return -1;
            }
        }
        u = parent_udev;
    }
    return -1;
}

static void format_msc(char *dst, size_t cap, MSC_T *msc)
{
    if (!msc) { snprintf(dst, cap, "MU?"); return; }
    /* Total bytes = sectors * sector_size; surface as MB so the
     * standard 8 MB MU shows up as a meaningful "MU 8MB" rather
     * than "MU 16384K". */
    uint64_t total = (uint64_t)msc->uTotalSectorN * (uint64_t)msc->nSectorSize;
    unsigned mb = (unsigned)(total / (1024u * 1024u));
    if (mb > 0) snprintf(dst, cap, "MU %uMB", mb);
    else        snprintf(dst, cap, "MU");
}

void jt_accessories_tick(void)
{
    usbh_pooling_hubs();
    hid_start_polling_all();

    /* Refresh per-port-per-slot labels from scratch each tick. */
    for (int p = 0; p < JT_NUM_PORTS; p++)
        for (int pi = 0; pi < PAD_SLOTS; pi++)
            for (int s = 0; s < JT_NUM_SLOTS; s++)
                port_slot_label[p][pi][s][0] = '\0';

    msc_count = 0;
    first_msc_debug[0] = '\0';
    for (MSC_T *m = usbh_msc_get_device_list(); m; m = m->next) {
        msc_count++;
        if (!m->iface || !m->iface->udev) {
            if (first_msc_debug[0] == '\0')
                snprintf(first_msc_debug, sizeof(first_msc_debug),
                         "MSC no-udev");
            continue;
        }
        UDEV_T *u = m->iface->udev;
        if (first_msc_debug[0] == '\0') {
            UDEV_T *p1 = u;
            int hops = 0;
            int leaf_port = u->port_num;
            int last_port = leaf_port;
            while (p1->parent && hops < 8) {
                HUB_DEV_T *ph = p1->parent;
                UDEV_T *hud = (ph->iface) ? ph->iface->udev : NULL;
                if (!hud) break;
                last_port = hud->port_num;
                p1 = hud;
                hops++;
            }
            snprintf(first_msc_debug, sizeof(first_msc_debug),
                     "MSC pn=%d hops=%d top_pn=%d",
                     leaf_port, hops, last_port);
        }
        int port = udev_root_port(u);
        int pad_idx, slot;
        if (port >= 0 && udev_resolve_slot(u, &pad_idx, &slot) &&
            !port_slot_label[port][pad_idx][slot][0]) {
            format_msc(port_slot_label[port][pad_idx][slot],
                       sizeof(port_slot_label[port][pad_idx][slot]), m);
        }
    }

    uac_count = 0;
    for (UAC_DEV_T *a = usbh_uac_get_device_list(); a; a = a->next) {
        uac_count++;
        if (!a->udev) continue;
        int port = udev_root_port(a->udev);
        int pad_idx, slot;
        if (port >= 0 && udev_resolve_slot(a->udev, &pad_idx, &slot) &&
            !port_slot_label[port][pad_idx][slot][0]) {
            snprintf(port_slot_label[port][pad_idx][slot],
                     sizeof(port_slot_label[port][pad_idx][slot]),
                     "Headset");
        }
    }

    for (xbox_audio_dev_t *d = xbox_audio_list; d; d = d->next) {
        if (!d->iface || !d->iface->udev) continue;
        uac_count++;
        int port = udev_root_port(d->iface->udev);
        int pad_idx, slot;
        if (port >= 0 && udev_resolve_slot(d->iface->udev, &pad_idx, &slot) &&
            !port_slot_label[port][pad_idx][slot][0]) {
            snprintf(port_slot_label[port][pad_idx][slot],
                     sizeof(port_slot_label[port][pad_idx][slot]),
                     "Mic");
        }
    }

    for (HID_DEV_T *h = usbh_hid_get_device_list(); h; h = h->next) {
        if (!h->iface) continue;
        IFACE_T *iface = (IFACE_T *)h->iface;
        if (!iface->udev) continue;
        int port = udev_root_port(iface->udev);
        int pad_idx, slot;
        if (port < 0 || !udev_resolve_slot(iface->udev, &pad_idx, &slot)) continue;
        if (port_slot_label[port][pad_idx][slot][0]) continue;
        const char *label = NULL;
        if (hid_is_kbd(h) && !hid_is_composite_mouse_kbd(h)) label = "Keyboard";
        else if (hid_is_mouse(h))                            label = "Mouse";
        if (label) snprintf(port_slot_label[port][pad_idx][slot],
                            sizeof(port_slot_label[port][pad_idx][slot]),
                            "%s", label);
    }

    for (xid_dev_t *x = usbh_xid_get_device_list(); x; x = x->next) {
        if (!x->iface || !x->iface->udev) continue;
        int port = udev_root_port(x->iface->udev);
        int pad_idx, slot;
        if (port < 0 || !udev_resolve_slot(x->iface->udev, &pad_idx, &slot)) continue;
        if (port_slot_label[port][pad_idx][slot][0]) continue;
        const char *label = NULL;
        switch (x->xid_desc.bType) {
            case XID_TYPE_GAMECONTROLLER:  label = "Pad";    break;
            case XID_TYPE_XREMOTE:         label = "Remote"; break;
            case XID_TYPE_STEELBATTALION:  label = "Steel";  break;
        }
        if (label) snprintf(port_slot_label[port][pad_idx][slot],
                            sizeof(port_slot_label[port][pad_idx][slot]),
                            "%s", label);
    }

    /* Earlier attempts also did a fallback g_udev_list walk that
     * labelled unknown-class devices ("Hub" / "HID" / "Storage" /
     * "Audio" / "Other") in the slot column. Removed: the OG Xbox
     * controller is itself a USB hub (compound device with class
     * 0x09 + HID interfaces for the pad), and walking the global
     * device list misclassified the pad-as-hub-itself as "Hub in
     * slot 1" -- and the label stuck even when nothing was actually
     * in the slot. Only the specific class-driver lists above are
     * trusted now; slots show "-------" for anything we can't
     * positively identify, which is more honest than a wrong label. */

    /* USB total: we don't have a direct API for "every connected
     * device" so we approximate as MSC + UAC + XID. Useful for the
     * diagnostic row as a sanity check that pooling is alive. */
    int xid = 0;
    for (xid_dev_t *x = usbh_xid_get_device_list(); x; x = x->next) xid++;
    usb_total = msc_count + uac_count + xid;
}

int jt_accessory_msc_count(void)  { return msc_count; }
int jt_accessory_uac_count(void)  { return uac_count; }
int jt_accessory_usb_total(void)  { return usb_total; }

const char *jt_accessory_for_slot(int port_idx, int pad_idx, int slot_idx)
{
    if (port_idx < 0 || port_idx >= JT_NUM_PORTS) return "";
    if (pad_idx  < 0 || pad_idx  >= PAD_SLOTS)    return "";
    if (slot_idx < 0 || slot_idx >= JT_NUM_SLOTS) return "";
    return port_slot_label[port_idx][pad_idx][slot_idx];
}

bool jt_accessory_dvd_remote_at_port(int port_idx)
{
    if (port_idx < 0 || port_idx >= JT_NUM_PORTS) return false;
    for (xid_dev_t *x = usbh_xid_get_device_list(); x; x = x->next) {
        if (x->xid_desc.bType != XID_TYPE_XREMOTE) continue;
        if (!x->iface || !x->iface->udev) continue;
        if (udev_root_port(x->iface->udev) == port_idx) return true;
    }
    return false;
}

/* xid_xremote_in layout (xid_driver.h):
 *   [0]   startByte
 *   [1]   bLength
 *   [2-3] buttonCode (little-endian uint16)
 *   [4-5] timeElapsed (ms since last button press)
 *
 * Like jt_accessory_xid_pressure, the latest interrupt-in transfer
 * lands in utr_list[0]->buff. SDL ignores XREMOTE entirely so the
 * buffer never gets re-queued by the joystick driver -- but nxdk's
 * core USB stack keeps issuing the interrupt reads, so the buffer
 * stays fresh. */
bool jt_accessory_dvd_remote_state(int port_idx,
                                   uint16_t *button_code,
                                   uint16_t *time_since_ms)
{
    if (port_idx < 0 || port_idx >= JT_NUM_PORTS) return false;
    for (xid_dev_t *x = usbh_xid_get_device_list(); x; x = x->next) {
        if (x->xid_desc.bType != XID_TYPE_XREMOTE) continue;
        if (!x->iface || !x->iface->udev) continue;
        if (udev_root_port(x->iface->udev) != port_idx) continue;
        UTR_T *utr = xid_find_in_utr(x);
        /* xfer_len is reset to 0 by every consumer's read callback;
         * the buffer itself stays valid. */
        if (!utr || !utr->buff) return false;
        const uint8_t *b = utr->buff;
        if (button_code)   *button_code   = (uint16_t)(b[2] | (b[3] << 8));
        if (time_since_ms) *time_since_ms = (uint16_t)(b[4] | (b[5] << 8));
        return true;
    }
    return false;
}

/* DVD Movie Playback Kit IR button codes -- documented at
 * xboxdevwiki.net/Xbox_DVD_Movie_Playback_Kit. data_high (the upper
 * nibble) is always 0x0A for genuine Microsoft remotes, but the
 * codes here are stored full-width since some clone remotes vary. */
const char *jt_accessory_dvd_button_name(uint16_t code)
{
    switch (code) {
        case 0x0AEA: return "Play";
        case 0x0AE6: return "Pause";
        case 0x0AE0: return "Stop";
        case 0x0AE3: return "Forward";
        case 0x0AE2: return "Reverse";
        case 0x0ADF: return "Skip+";
        case 0x0ADD: return "Skip-";
        case 0x0AE5: return "Title";
        case 0x0AD5: return "Display";
        case 0x0AD8: return "Back";
        case 0x0AF7: return "Menu";
        case 0x0A0B: return "Select";
        case 0x0AA6: return "Up";
        case 0x0AA7: return "Down";
        case 0x0AA9: return "Left";
        case 0x0AA8: return "Right";
        case 0x0ACF: return "0";
        case 0x0ACD: return "1";
        case 0x0ACC: return "2";
        case 0x0ACB: return "3";
        case 0x0ACA: return "4";
        case 0x0AC9: return "5";
        case 0x0AC8: return "6";
        case 0x0AC7: return "7";
        case 0x0AC6: return "8";
        case 0x0AC3: return "9";
        default:     return "";
    }
}

int jt_accessory_unclaimed_xid_port(const bool claimed[])
{
    for (xid_dev_t *x = usbh_xid_get_device_list(); x; x = x->next) {
        if (!x->iface || !x->iface->udev) continue;
        int p = udev_root_port(x->iface->udev);
        if (p < 0 || p >= JT_NUM_PORTS) continue;
        if (!claimed[p]) return p;
    }
    return -1;
}

/* The XID driver's utr_list[] holds both IN (interrupt-in for the
 * input report) and OUT (interrupt-out for rumble) UTRs in
 * insertion order -- whichever was queued first lands at index 0,
 * which on a rumble-active pad is usually the OUT one. Walk the
 * list and return the UTR whose endpoint has the IN direction bit
 * (bit 7) set; that's the one whose buffer holds the latest input
 * report. */
static UTR_T *xid_find_in_utr(xid_dev_t *xd)
{
    for (int i = 0; i < XID_MAX_TRANSFER_QUEUE; i++) {
        UTR_T *u = xd->utr_list[i];
        if (!u || !u->ep) continue;
        if (u->ep->bEndpointAddress & 0x80) return u;
    }
    return NULL;
}

/* xid_gamepad_in layout (xboxdevwiki.net/Xbox_Input_Devices):
 *   [0]  startByte
 *   [1]  bLength
 *   [2-3] dButtons (digital)
 *   [4]  a pressure        0..255
 *   [5]  b pressure
 *   [6]  x pressure
 *   [7]  y pressure
 *   [8]  black pressure
 *   [9]  white pressure
 *   [10] left trigger
 *   [11] right trigger
 *   [12-19] sticks
 *
 * nxdk's SDL throws the analog button bytes away (XINPUT_GAMEPAD has
 * only triggers + sticks + digital wButtons), but the raw report
 * still lands in xid_dev->utr_list[0]->buff every USB interrupt
 * read. Read directly from there. */
bool jt_accessory_xid_pressure(int32_t instance_id,
                               uint8_t *a, uint8_t *b,
                               uint8_t *x, uint8_t *y,
                               uint8_t *black, uint8_t *white)
{
    for (xid_dev_t *xd = usbh_xid_get_device_list(); xd; xd = xd->next) {
        if ((int32_t)xd->uid != instance_id) continue;
        UTR_T *utr = xid_find_in_utr(xd);
        /* Don't gate on xfer_len: SDL's int_read_callback resets it
         * to 0 after each successful read (and immediately re-queues
         * the IN transfer), so the only window where xfer_len > 0
         * is the few microseconds between transfer complete and
         * SDL's memcpy. The buffer itself isn't cleared -- SDL only
         * memcpys *out* of it -- so the last full report is always
         * sitting in utr->buff. */
        if (!utr || !utr->buff) return false;
        const uint8_t *buf = utr->buff;
        if (a)     *a     = buf[4];
        if (b)     *b     = buf[5];
        if (x)     *x     = buf[6];
        if (y)     *y     = buf[7];
        if (black) *black = buf[8];
        if (white) *white = buf[9];
        return true;
    }
    return false;
}
