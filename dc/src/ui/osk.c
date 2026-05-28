/*
 * osk.c — modal on-screen keyboard.
 *
 * Layout: 6 rows x 10 cols of cells. Top 4 rows are the alphanumeric
 * grid (changes per layer). Row 4 has Shift / Layer / Space (wide)
 * / Backspace / Done. Row 5 is reserved for a status line.
 *
 * Layers: 0 = lowercase, 1 = UPPERCASE, 2 = symbols + numbers.
 *
 * Highlight follows the active input source (cursor when over a key,
 * D-pad nav otherwise, last-pressed key from maple kbd as feedback).
 */
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/maple/keyboard.h>
#include <string.h>
#include <stdio.h>

#include "osk.h"
#include "bfont_util.h"
#include "../ports/ports.h"
#include "../input/cursor.h"

#define KEY_W   48
#define KEY_H   36
#define COLS    10
#define ROWS    5
#define GRID_X  40
#define GRID_Y  150

/* Three layers, 10x4 keys each (40 keys/layer for letters/digits/etc). */
static const char *layers[3][4] = {
    {
        "qwertyuiop",
        "asdfghjkl-",
        "zxcvbnm,. ",
        "1234567890",
    },
    {
        "QWERTYUIOP",
        "ASDFGHJKL_",
        "ZXCVBNM<> ",
        "!@#$%^&*()",
    },
    {
        "1234567890",
        "+-*/=()[]{",
        "<>?:;'\"|\\}",
        " ~`@#$%^&*",
    }
};

static bool   visible = false;
static char   label_buf[32];
static char   text_buf[JT_OSK_MAX_LEN + 1];
static size_t text_len = 0;
static size_t text_cap = JT_OSK_MAX_LEN;
static int    layer = 0;
static int    hover_row = 0;
static int    hover_col = 0;
static bool   completed = false;       /* user pressed Done */
static bool   has_pending_consume = false;
static char   pending_text[JT_OSK_MAX_LEN + 1];
static uint32_t last_btns = 0;

static uint32_t aggregate_pad_buttons(void);

/* Redraw is expensive (~50 bfont calls per frame). Track state so we
 * only paint on actual change — open / close, layer flip, hover move,
 * text-buffer change. Otherwise the beam catches mid-draw and the
 * panel flickers. */
static bool   needs_redraw = true;
static int    last_drawn_layer = -1;
static int    last_drawn_hover_row = -1;
static int    last_drawn_hover_col = -1;
static size_t last_drawn_text_len = (size_t)-1;

void jt_osk_begin(const char *label, const char *initial, size_t max_len)
{
    visible = true;
    completed = false;
    has_pending_consume = false;
    layer = 0;
    hover_row = 0;
    hover_col = 0;
    /* Seed last_btns with the buttons currently held so the press that
     * opened the OSK (typically A on a picker / button) doesn't appear
     * as a fresh edge on the next frame and immediately type the
     * hovered key. */
    last_btns = aggregate_pad_buttons();
    strncpy(label_buf, label ? label : "", sizeof(label_buf) - 1);
    label_buf[sizeof(label_buf) - 1] = '\0';
    text_cap = (max_len > 0 && max_len <= JT_OSK_MAX_LEN) ? max_len : JT_OSK_MAX_LEN;
    text_len = 0;
    if (initial) {
        size_t n = strlen(initial);
        if (n > text_cap) n = text_cap;
        memcpy(text_buf, initial, n);
        text_len = n;
    }
    text_buf[text_len] = '\0';

    /* Force a fresh full redraw on open. */
    needs_redraw = true;
    last_drawn_layer = -1;
    last_drawn_hover_row = -1;
    last_drawn_hover_col = -1;
    last_drawn_text_len = (size_t)-1;
}

bool jt_osk_visible(void) { return visible; }

bool jt_osk_consume_text(char *out, size_t cap)
{
    if (!has_pending_consume) return false;
    if (out && cap > 0) {
        size_t n = strlen(pending_text);
        if (n >= cap) n = cap - 1;
        memcpy(out, pending_text, n);
        out[n] = '\0';
    }
    has_pending_consume = false;
    return true;
}

static void commit(void)
{
    memcpy(pending_text, text_buf, text_len);
    pending_text[text_len] = '\0';
    has_pending_consume = true;
    completed = true;
    visible = false;
}

void jt_osk_input(int ascii)
{
    if (!visible) return;
    if (ascii == '\b') {
        /* Backspace. */
        if (text_len > 0) {
            text_len--;
            text_buf[text_len] = '\0';
        }
        return;
    }
    if (ascii == '\r' || ascii == '\n') {
        commit();
        return;
    }
    if (ascii == 0x1B) {
        /* Cancel — toss the text. */
        visible = false;
        completed = false;
        has_pending_consume = false;
        return;
    }
    if (ascii >= 0x20 && ascii < 0x7F && text_len < text_cap) {
        text_buf[text_len++] = (char)ascii;
        text_buf[text_len] = '\0';
    }
}

static uint32_t aggregate_pad_buttons(void)
{
    uint32_t b = 0;
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        if (jt_ports[p].present && jt_ports[p].style == JT_STYLE_PAD) {
            b |= jt_ports[p].pad.buttons;
        }
    }
    return b;
}

/* Translate row/col of the OSK grid into the ASCII that key produces. */
static char key_at(int row, int col)
{
    if (row < 0 || row >= 4 || col < 0 || col >= 10) return 0;
    const char *s = layers[layer][row];
    return s[col];
}

/* Row 4 is the function bar. col 0=Shift, 1-2=Layer, 3-7=Space (5 cells),
 * 8=Bksp, 9=Done. Returns the action code: 1=shift, 2=layer next,
 * 3=space, 4=backspace, 5=done, 0=none. */
static int func_at(int col)
{
    if (col == 0) return 1;
    if (col >= 1 && col <= 2) return 2;
    if (col >= 3 && col <= 7) return 3;
    if (col == 8) return 4;
    if (col == 9) return 5;
    return 0;
}

static void apply_func(int code)
{
    switch (code) {
        case 1: layer = (layer == 1) ? 0 : 1; break;
        case 2: layer = (layer + 1) % 3; break;
        case 3: jt_osk_input(' '); break;
        case 4: jt_osk_input('\b'); break;
        case 5: jt_osk_input('\r'); break;
    }
}

void jt_osk_update(float dt)
{
    (void)dt;
    if (!visible) return;

    /* Pull any maple keyboard input first — feeds directly into
     * jt_osk_input via the kbd queue. */
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        maple_device_t *dev = maple_enum_dev(p, 0);
        if (!dev || !dev->valid) continue;
        if (!(dev->info.functions & MAPLE_FUNC_KEYBOARD)) continue;
        int k;
        while ((k = kbd_queue_pop(dev, 1)) >= 0) {
            if (k > 0) jt_osk_input(k);
        }
    }

    /* Pad nav + activation. */
    uint32_t btns = aggregate_pad_buttons();
    uint32_t edges = btns & ~last_btns;

    /* Hover bounds: rows 0..3 are QWERTY layers, row 4 is the
     * function bar -> max valid row index is ROWS - 1 (= 4). */
    if (edges & CONT_DPAD_UP)    if (hover_row > 0) hover_row--;
    if (edges & CONT_DPAD_DOWN)  if (hover_row < ROWS - 1) hover_row++;
    if (edges & CONT_DPAD_LEFT)  if (hover_col > 0) hover_col--;
    if (edges & CONT_DPAD_RIGHT) if (hover_col < COLS - 1) hover_col++;

    /* B = backspace-equivalent quick action; Y = layer cycle; Start = done. */
    if (edges & CONT_B)     jt_osk_input('\b');
    if (edges & CONT_Y)     layer = (layer + 1) % 3;
    if (edges & CONT_START) jt_osk_input('\r');

    /* A activates the currently-hovered cell. */
    if (edges & CONT_A) {
        if (hover_row < 4) {
            char c = key_at(hover_row, hover_col);
            if (c) jt_osk_input(c);
        } else {
            apply_func(func_at(hover_col));
        }
    }

    /* Mouse-cursor click maps to A on whichever cell the cursor is over.
     * Translate cursor screen coords to a cell. */
    if (jt_cursor.button_a && jt_cursor.source == JT_CURSOR_SRC_MOUSE) {
        int cx = (jt_cursor.x - GRID_X) / KEY_W;
        int cy = (jt_cursor.y - GRID_Y) / KEY_H;
        if (cx >= 0 && cx < COLS && cy >= 0 && cy < ROWS) {
            hover_col = cx; hover_row = cy;
            if (hover_row < 4) {
                char c = key_at(hover_row, hover_col);
                if (c) jt_osk_input(c);
            } else {
                apply_func(func_at(hover_col));
            }
        }
    }

    last_btns = btns;
}

/* Tiny rect filler — same pattern as the editor's. */
extern void jt_fill_rect(int x, int y, int w, int h, uint16_t color);  /* forward */
#include <dc/video.h>
static void fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0 || y < 0 || x + w > 640 || y + h > 480) return;
    for (int j = 0; j < h; j++) {
        uint16_t *row = vram_s + (y + j) * 640 + x;
        for (int i = 0; i < w; i++) row[i] = color;
    }
}

static const char *func_label(int col)
{
    switch (func_at(col)) {
        case 1: return (layer == 1) ? "shft*" : "shft";
        case 2: return "layr";
        case 3: return (col == 5) ? "space" : "";
        case 4: return "bksp";
        case 5: return "DONE";
    }
    return "";
}

void jt_osk_draw(void)
{
    if (!visible) return;

    /* Full redraw every frame -- double buffering means no beam race,
     * so the old dirty-flag gating is unnecessary. */

    /* Backdrop panel: 580 x 280, centered. */
    fill_rect(30, 90, 580, 350, JT_COL_BLACK);
    /* 2px yellow border. */
    fill_rect(30, 90, 580, 2, JT_COL_YELLOW);
    fill_rect(30, 438, 580, 2, JT_COL_YELLOW);
    fill_rect(30, 90, 2, 350, JT_COL_YELLOW);
    fill_rect(608, 90, 2, 350, JT_COL_YELLOW);

    jt_text(40, 100, JT_COL_YELLOW, JT_COL_BLACK, "%s", label_buf);
    /* Text box. */
    fill_rect(40, 124, 560, 22, JT_RGB565(40, 40, 40));
    jt_text(44, 126, JT_COL_WHITE, JT_RGB565(40, 40, 40), "%s", text_buf);
    /* Length indicator. */
    char count[16];
    snprintf(count, sizeof(count), "%zu/%zu", text_len, text_cap);
    jt_text(540, 100, JT_COL_GREY, JT_COL_BLACK, "%s", count);

    /* Key grid. */
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int x = GRID_X + c * KEY_W;
            int y = GRID_Y + r * KEY_H;
            bool is_hover = (r == hover_row && c == hover_col);
            uint16_t bg = is_hover ? JT_COL_YELLOW : JT_RGB565(60, 60, 60);
            uint16_t fg = is_hover ? JT_COL_BLACK : JT_COL_WHITE;
            fill_rect(x + 2, y + 2, KEY_W - 4, KEY_H - 4, bg);
            char glyph[8] = {0};
            if (r < 4) {
                char c2 = key_at(r, c);
                if (c2) glyph[0] = c2;
            } else {
                const char *fl = func_label(c);
                strncpy(glyph, fl, sizeof(glyph) - 1);
            }
            int tx = x + (KEY_W - (int)strlen(glyph) * 12) / 2;
            jt_text(tx, y + (KEY_H - 24) / 2 + 2, fg, bg, "%s", glyph);
        }
    }
    jt_text(40, 410, JT_COL_GREY, JT_COL_BLACK,
            "D-pad nav  A select  B bksp  Y layer  Start DONE");
}
