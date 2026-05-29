#!/usr/bin/env python3
"""Generate assets/boot_logo/joypad_boot.mr: the custom license-screen
("boot") logo embedded into IP.BIN by mkdcdisc (-i / --image).

The Dreamcast bootstrap shows an "MR" format image on the license screen.
Format (little-endian), reverse-engineered from KOS's sample MR images
(mruby.mr) and mkdcdisc's assets/default-mr.mr:

    0x00  char[2]  'M','R'
    0x02  u32      total file size
    0x06  u32      0 (reserved)
    0x0A  u32      offset to bitmap data (= 0x1E + ncolors*4)
    0x0E  u32      width  (px)
    0x12  u32      height (px)
    0x16  u32      0 (reserved)
    0x1A  u32      ncolors (palette entry count)
    0x1E  ncolors x [B, G, R, 0]      (palette, BGR + pad byte)
    ...   RLE bitmap of palette indices

RLE: process bytes left to right with a count accumulator.
  - byte >= 0x80: a base-128 count digit, MSB first:  acc = (acc<<7)|(b&0x7f)
  - byte <  0x80: a palette index. Emit it `acc` times (or once if acc==0),
                  then reset acc. So a run of length R of index v encodes as
                  the base-128 digits of R (each OR'd with 0x80) followed by v;
                  a length-1 run is just the bare index byte.

The license logo is rendered black-on-white at 320x90 (the size the BIOS
sample images use). Output stays well under mkdcdisc's 8192-byte cap.

Source: dc/assets/screensaver-logo.png (preferred) or dc/assets/logo.png.
"""
import struct
import sys
from pathlib import Path
from PIL import Image

CANVAS_W = 320
CANVAS_H = 90
MAX_LOGO_W = 300   # leave a little horizontal margin
MAX_LOGO_H = 80    # leave a little vertical margin
RIGHT_MARGIN = 10  # gap between the logo and the canvas's right edge

HERE = Path(__file__).parent
PROJECT = HERE.parent
OUT = PROJECT / 'assets' / 'boot_logo' / 'joypad_boot.mr'
CANDIDATES = [
    PROJECT / 'assets' / 'screensaver-logo.png',
    PROJECT / 'assets' / 'logo.png',
]

# index 0 = background, index 1 = black (logo ink). Stored B,G,R,pad.
# The background matches the Dreamcast license screen's light grey (the
# value mkdcdisc's stock default-mr uses, RGB ~190,192,189) so the logo's
# field blends into the surrounding screen instead of a stark white box.
PALETTE = [(189, 192, 190, 0), (0, 0, 0, 0)]


def find_source() -> Path:
    for p in CANDIDATES:
        if p.exists():
            return p
    sys.exit('No source PNG found; tried: ' +
             ', '.join(str(p) for p in CANDIDATES))


def build_indices(src_path: Path):
    """Return a CANVAS_W*CANVAS_H list of palette indices (0=white,1=black)."""
    img = Image.open(src_path).convert('RGBA')
    bg = Image.new('RGBA', img.size, (255, 255, 255, 255))
    flat = Image.alpha_composite(bg, img).convert('L')
    # Silhouette = dark pixels (same rule as make_logo.py).
    mask = flat.point(lambda v: 255 if v < 128 else 0, 'L')
    bbox = mask.getbbox()
    if bbox:
        mask = mask.crop(bbox)
    sw, sh = mask.size
    scale = min(MAX_LOGO_W / sw, MAX_LOGO_H / sh)
    w = max(1, int(round(sw * scale)))
    h = max(1, int(round(sh * scale)))
    mask = mask.resize((w, h), Image.LANCZOS)

    px = [0] * (CANVAS_W * CANVAS_H)          # all background
    ox = CANVAS_W - w - RIGHT_MARGIN          # right-aligned
    oy = (CANVAS_H - h) // 2                   # vertically centered
    for y in range(h):
        for x in range(w):
            if mask.getpixel((x, y)) >= 128:   # ink
                px[(oy + y) * CANVAS_W + (ox + x)] = 1
    return px


def encode_rle(px):
    out = bytearray()
    i, n = 0, len(px)
    while i < n:
        v = px[i]
        j = i
        while j < n and px[j] == v:
            j += 1
        run = j - i
        if run == 1:
            out.append(v)
        else:
            digits = []
            x = run
            while x > 0:
                digits.append(x & 0x7f)
                x >>= 7
            for d in reversed(digits):
                out.append(0x80 | d)
            out.append(v)
        i = j
    return bytes(out)


def decode_rle(data):
    out = []
    acc = 0
    have = False
    for b in data:
        if b >= 0x80:
            acc = (acc << 7) | (b & 0x7f)
            have = True
        else:
            out.extend([b] * (acc if have else 1))
            acc = 0
            have = False
    return out


def build_mr(px):
    ncol = len(PALETTE)
    bitmap = encode_rle(px)
    # Self-test: the BIOS RLE must round-trip to exactly the image.
    dec = decode_rle(bitmap)
    assert dec == px, f'RLE round-trip failed ({len(dec)} vs {len(px)} px)'
    dataoff = 0x1E + ncol * 4
    filesize = dataoff + len(bitmap)
    hdr = b'MR' + struct.pack('<IIIIII', filesize, 0, dataoff,
                              CANVAS_W, CANVAS_H, 0) + struct.pack('<I', ncol)
    pal = b''.join(struct.pack('<BBBB', *c) for c in PALETTE)
    blob = hdr + pal + bitmap
    assert len(blob) == filesize
    return blob


def main():
    src = find_source()
    px = build_indices(src)
    blob = build_mr(px)
    if len(blob) > 8192:
        sys.exit(f'MR is {len(blob)} bytes, exceeds mkdcdisc 8192 cap')
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(blob)
    ink = sum(1 for v in px if v)
    print(f'wrote {OUT.relative_to(PROJECT)} '
          f'({CANVAS_W}x{CANVAS_H}, {ink} ink px, {len(blob)} bytes, '
          f'source {src.relative_to(PROJECT)})')


if __name__ == '__main__':
    main()
