// display.c — OLED-compatible display API on the GBA.
//
// Renders eyes_anim at native GBA resolution (240×128 logical) into a
// Mode-4 back page directly. eyes_anim writes pixel classes (0=BG,
// 1=FG, 2=PUPIL) into a shadow buffer; display_present() then copies
// the shadow into VRAM with the appropriate palette index per pixel.
// No upscale, no AA — at native resolution curves are already smooth
// enough.
//
// Mode 4: 240×160, 8-bit indexed, two pages at 0x06000000 / 0x0600A000.
// DISPCNT bit 4 = page select; flipped from the VBlank IRQ so completed
// frames swap atomically during scanout-off.

#include "display.h"
#include <gba_base.h>
#include <gba_video.h>
#include <string.h>

#define VRAM_PAGE0 ((volatile uint8_t*)0x06000000)
#define VRAM_PAGE1 ((volatile uint8_t*)0x0600A000)
#define GBA_W 240
#define GBA_H 160

// 1:1 native rendering. Shadow rows (240) match VRAM rows. Vertical
// centering: 240×128 shadow inside 240×160 VRAM with 16 px top + 16 px
// bottom margin.
#define OFFSET_Y ((GBA_H - DISPLAY_HEIGHT) / 2)  // = 16

#define COLOR_BG     0   // background (default black)
#define COLOR_EDGE   1   // anti-aliased boundary tone between bg and fg
#define COLOR_FG     2   // eye whites
#define COLOR_PUPIL  3   // pupil dot inside eye whites
#define COLOR_PEDGE  4   // anti-aliased boundary tone between fg and pupil

#define RGB5(r,g,b) (uint16_t)((r) | ((g) << 5) | ((b) << 10))

// 240×128 = 30 KB shadow buffer. As an uninitialised static it lands in
// .bss, which devkitARM's gba_mb.ld puts in IWRAM (32-bit bus, 1 cycle
// per access) — vs EWRAM's 16-bit bus at ~3 cycles. The eyes-anim render
// is dominated by 30 K+ bytes of memset row-fills, so the bus speedup
// roughly halves render time without bloating the multiboot image.
// (Don't add IWRAM_DATA: that forces initialised data into the ROM
// image, ballooning the upload by the buffer size.)
static uint8_t shadow[DISPLAY_WIDTH * DISPLAY_HEIGHT];

// Currently-active back page in VRAM (where present writes pixels).
static volatile uint8_t* back_page;

// JOYTR is refreshed by the VBlank IRQ handler in main.c at the 60 Hz
// VBlank rate. That cadence is sufficient for the host's joybus polling
// and saves us from per-pixel MMIO taps that were adding ~8 ms to every
// render at native resolution.
//
// Address note: REG_JOYTR is at 0x4000154 — NOT 0x4000150. 0x4000150 is
// REG_JOYRE (receive). Earlier revisions of this file pointed at the
// wrong address; the writes were silent no-ops on the wire. libgba
// gba_sio.h is authoritative if in doubt.
#define REG_JOYTR_      (*(volatile uint32_t*)0x04000154)
#define REG_KEYINPUT_   (*(volatile uint16_t*)0x04000130)

void display_init(void)
{
    // Palette: black background, edge midtone, eye-white foreground,
    // pupil dot. Defaults can be overridden via display_set_fg_color()
    // and display_set_pupil_color().
    BG_PALETTE[COLOR_BG]    = RGB5( 0,  0,  0);
    BG_PALETTE[COLOR_EDGE]  = RGB5(14, 15, 15);
    BG_PALETTE[COLOR_FG]    = RGB5(28, 30, 31);
    BG_PALETTE[COLOR_PUPIL] = RGB5( 4,  4,  6);   // near-black w/ slight blue
    BG_PALETTE[COLOR_PEDGE] = RGB5(16, 17, 18);   // midtone fg<->pupil

    // Mode 4 + BG2. Bit 4 cleared = page 0 visible.
    REG_DISPCNT = 0x0004 /* MODE_4 */ | 0x0400 /* BG2_ENABLE */;
    back_page = VRAM_PAGE1;

    // Clear shadow + both VRAM pages so we never flash garbage.
    for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) shadow[i] = 0;
    volatile uint32_t* p0 = (volatile uint32_t*)VRAM_PAGE0;
    volatile uint32_t* p1 = (volatile uint32_t*)VRAM_PAGE1;
    for (int i = 0; i < (GBA_W * GBA_H) / 4; i++) { p0[i] = 0; p1[i] = 0; }
}

void display_set_fg_color(uint16_t color)
{
    BG_PALETTE[COLOR_FG] = color;
    // Auto-derive a half-bright edge tone from the same hue so anti-
    // aliased curve edges blend cleanly into the black background.
    uint16_t r = (color >>  0) & 0x1F;
    uint16_t g = (color >>  5) & 0x1F;
    uint16_t b = (color >> 10) & 0x1F;
    BG_PALETTE[COLOR_EDGE] = (r >> 1) | ((g >> 1) << 5) | ((b >> 1) << 10);

    // Auto-derive pupil-edge midtone from current FG and PUPIL palettes.
    uint16_t pup = BG_PALETTE[COLOR_PUPIL];
    uint16_t pr = (((pup >>  0) & 0x1F) + r) >> 1;
    uint16_t pg = (((pup >>  5) & 0x1F) + g) >> 1;
    uint16_t pb = (((pup >> 10) & 0x1F) + b) >> 1;
    BG_PALETTE[COLOR_PEDGE] = pr | (pg << 5) | (pb << 10);
}

void display_set_pupil_color(uint16_t color)
{
    BG_PALETTE[COLOR_PUPIL] = color;
    // Recompute the pupil-edge midtone (avg of pupil and current fg).
    uint16_t fg = BG_PALETTE[COLOR_FG];
    uint16_t fr = (fg >>  0) & 0x1F;
    uint16_t fg2= (fg >>  5) & 0x1F;
    uint16_t fb = (fg >> 10) & 0x1F;
    uint16_t pr = (color >>  0) & 0x1F;
    uint16_t pg = (color >>  5) & 0x1F;
    uint16_t pb = (color >> 10) & 0x1F;
    BG_PALETTE[COLOR_PEDGE] = ((fr + pr) >> 1)
                            | (((fg2 + pg) >> 1) << 5)
                            | (((fb + pb) >> 1) << 10);
}

void display_flip_page(void)
{
    REG_DISPCNT ^= 0x0010;
    back_page = (REG_DISPCNT & 0x0010) ? VRAM_PAGE0 : VRAM_PAGE1;
}

void display_clear(void)
{
    // Block-fill the shadow with the background palette index. memset
    // compiles to a tight 32-bit loop on ARM, much faster than a
    // per-byte C loop.
    memset((void*)shadow, COLOR_BG, DISPLAY_WIDTH * DISPLAY_HEIGHT);
}

void display_pixel(int x, int y, bool on)
{
    if ((unsigned)x >= DISPLAY_WIDTH || (unsigned)y >= DISPLAY_HEIGHT) return;
    // Store the actual palette index in the shadow so display_present can
    // DMA the buffer straight to VRAM with zero per-pixel processing.
    shadow[y * DISPLAY_WIDTH + x] = on ? COLOR_FG : COLOR_BG;
}

void display_pupil_pixel(int x, int y)
{
    if ((unsigned)x >= DISPLAY_WIDTH || (unsigned)y >= DISPLAY_HEIGHT) return;
    // Clip to eye whites: only paint where the underlying pixel is FG.
    // Stops the pupil from poking outside the elliptical eye boundary or
    // through smile/brow cuts when gaze pushes it toward the edge.
    uint8_t* p = &shadow[y * DISPLAY_WIDTH + x];
    if (*p == COLOR_FG) *p = COLOR_PUPIL;
}

void display_hspan(int x, int y, int w, bool on)
{
    if ((unsigned)y >= DISPLAY_HEIGHT) return;
    if (x < 0) { w += x; x = 0; }
    if (x >= DISPLAY_WIDTH) return;
    if (x + w > DISPLAY_WIDTH) w = DISPLAY_WIDTH - x;
    if (w <= 0) return;
    memset(&shadow[y * DISPLAY_WIDTH + x], on ? COLOR_FG : COLOR_BG, w);
}

void display_hspan_pupil(int x, int y, int w)
{
    if ((unsigned)y >= DISPLAY_HEIGHT) return;
    if (x < 0) { w += x; x = 0; }
    if (x >= DISPLAY_WIDTH) return;
    if (x + w > DISPLAY_WIDTH) w = DISPLAY_WIDTH - x;
    if (w <= 0) return;
    // Clipped fill: only overwrite eye-white (FG) pixels. The pupil
    // ellipse may extend past the eye when gaze pushes it to the edge,
    // and the smile/brow cuts can chop into the eye after the pupil's
    // bbox was already constrained — so an unclipped fill would leak
    // pupil dots into the background. Walk the row and skip non-FG.
    uint8_t* row = &shadow[y * DISPLAY_WIDTH + x];
    for (int i = 0; i < w; i++) {
        if (row[i] == COLOR_FG) row[i] = COLOR_PUPIL;
    }
}

// Pick the GBA palette index for one upscaled output pixel given the
// 4 source-shadow neighbors that contribute to it.
//   self  = the source pixel directly under this output corner
//   right = horizontal neighbor (the OTHER corner in the 2-pixel pair)
//   below = the vertical neighbor (in the next scanline of the 2x2 block)
//   diag  = the diagonal neighbor
// Each of these is 0=BG, 1=FG, 2=PUPIL. We pick:
//   - same class on all 4 → that class's solid color
//   - mixed → midtone between the two classes that share the boundary
//             (BG↔FG = EDGE, FG↔PUPIL = PEDGE)
static inline uint8_t display_class_to_color(int self, int right, int below, int diag, int unused)
{
    (void)unused;
    int min = self, max = self;
    if (right < min) min = right; else if (right > max) max = right;
    if (below < min) min = below; else if (below > max) max = below;
    if (diag  < min) min = diag;  else if (diag  > max) max = diag;
    if (min == max) {
        // Solid: all four corners the same class.
        switch (self) {
            case 0:  return COLOR_BG;
            case 1:  return COLOR_FG;
            case 2:  return COLOR_PUPIL;
            default: return COLOR_BG;
        }
    }
    // Mixed: which boundary?
    if (max <= 1)            return self ? COLOR_FG : COLOR_EDGE;   // BG/FG
    if (min >= 1 && max >= 2) return self == 2 ? COLOR_PUPIL : COLOR_PEDGE;   // FG/PUPIL
    // BG and PUPIL share a boundary only if the eye is impossibly thin —
    // pick PUPIL for self=2, EDGE otherwise.
    return self == 2 ? COLOR_PUPIL : (self == 1 ? COLOR_FG : COLOR_BG);
}

// Present: DMA the shadow buffer straight to the Mode-4 back page.
// The shadow already stores palette indices per pixel (set by
// display_pixel and display_pupil_pixel) so no per-pixel mapping is
// needed here. DMA3 handles the entire 240×128 (= 15360 16-bit units)
// copy in roughly one VBlank's worth of bus cycles, vs ~9 ms with the
// CPU-loop version this replaces.
//
// Source must be 16-bit aligned (shadow is uint8_t but DISPLAY_WIDTH is
// even); destination skips OFFSET_Y rows so the eyes are vertically
// centered in the 160-row physical screen.
#define REG_DMA3SAD  (*(volatile const void**)0x040000D4)
#define REG_DMA3DAD  (*(volatile void**)0x040000D8)
#define REG_DMA3CNT  (*(volatile uint32_t*)0x040000DC)
#define DMA_ENABLE   (1u << 31)
#define DMA_16BIT    (0u << 26)
#define DMA_INC_SD   (0u << 23)  // src/dest both increment

void display_present(void)
{
    REG_JOYTR_ = REG_KEYINPUT_;
    REG_DMA3SAD = (const void*)shadow;
    REG_DMA3DAD = (void*)(back_page + OFFSET_Y * GBA_W);
    // Count = 15360 halfwords (240*128 bytes / 2). DMA enable + 16-bit + auto inc.
    REG_DMA3CNT = (DISPLAY_WIDTH * DISPLAY_HEIGHT / 2) | DMA_ENABLE | DMA_16BIT;
    REG_JOYTR_ = REG_KEYINPUT_;
}
