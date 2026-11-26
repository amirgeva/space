"""Generate the spacecraft textures in assets/textures/ship/ (stdlib only).

All textures tile seamlessly. Light, neutral base colors are used where the generator
tints materials (hull, cargo) through the material's diffuse color.

    python tools/gen_ship_textures.py                 # all textures
    python tools/gen_ship_textures.py hazard hull     # only these
"""

import math
import random
import struct
import sys
import time
import zlib
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "assets" / "textures" / "ship"
OUT_EFFECTS = OUT.parent / "effects"


# ---------------------------------------------------------------------------
# Tiny raster helpers
# ---------------------------------------------------------------------------

class Image:
    def __init__(self, w, h, color=(0, 0, 0)):
        self.w, self.h = w, h
        self.px = [[list(color) for _ in range(w)] for _ in range(h)]

    def get(self, x, y):
        return self.px[y % self.h][x % self.w]

    def set(self, x, y, c):
        self.px[y % self.h][x % self.w] = [int(max(0, min(255, v))) for v in c]

    def add(self, x, y, d):
        p = self.get(x, y)
        self.set(x, y, (p[0] + d, p[1] + d, p[2] + d))

    def scale(self, x, y, f):
        p = self.get(x, y)
        self.set(x, y, (p[0] * f, p[1] * f, p[2] * f))

    def rect(self, x0, y0, x1, y1, c):
        for y in range(y0, y1):
            for x in range(x0, x1):
                self.set(x, y, c)

    def rect_add(self, x0, y0, x1, y1, d):
        for y in range(y0, y1):
            for x in range(x0, x1):
                self.add(x, y, d)

    def bevel(self, x0, y0, x1, y1, light=18, dark=-28):
        """Outline a rectangle: light top/left edge, dark bottom/right edge."""
        for x in range(x0, x1):
            self.add(x, y0, light)
            self.add(x, y1 - 1, dark)
        for y in range(y0, y1):
            self.add(x0, y, light)
            self.add(x1 - 1, y, dark)

    def grain(self, amount, rnd):
        for row in self.px:
            for p in row:
                d = rnd.uniform(-amount, amount)
                p[0] = int(max(0, min(255, p[0] + d)))
                p[1] = int(max(0, min(255, p[1] + d)))
                p[2] = int(max(0, min(255, p[2] + d)))

    def save(self, path):
        raw = b"".join(b"\x00" + bytes(v for p in row for v in p) for row in self.px)

        def chunk(tag, data):
            return (struct.pack(">I", len(data)) + tag + data
                    + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

        png = b"\x89PNG\r\n\x1a\n"
        png += chunk(b"IHDR", struct.pack(">IIBBBBB", self.w, self.h, 8, 2, 0, 0, 0))
        png += chunk(b"IDAT", zlib.compress(raw, 9))
        png += chunk(b"IEND", b"")
        path.write_bytes(png)
        print(f"Wrote {path}")


class Gray:
    """Grayscale canvas for the large textures seen from close by. It is much faster than Image
    because it works on whole row slices. Values stay unclamped floats until saving, and every
    operation wraps around the edges so the texture tiles."""

    def __init__(self, size, value):
        self.w = self.h = size
        self.rows = [[value] * size for _ in range(size)]

    def _spans(self, x0, x1):
        """Split the columns [x0, x1) into pieces inside the image, wrapping around."""
        x0, x1 = int(x0), int(x1)
        if x1 <= x0:
            return []
        if x1 - x0 >= self.w:
            return [(0, self.w)]
        a = x0 % self.w
        b = a + (x1 - x0)
        return [(a, b)] if b <= self.w else [(a, self.w), (0, b - self.w)]

    def add_rect(self, x0, y0, x1, y1, d):
        spans = self._spans(x0, x1)
        for y in range(int(y0), int(y1)):
            row = self.rows[y % self.h]
            for a, b in spans:
                row[a:b] = [v + d for v in row[a:b]]

    def set_rect(self, x0, y0, x1, y1, value):
        spans = self._spans(x0, x1)
        for y in range(int(y0), int(y1)):
            row = self.rows[y % self.h]
            for a, b in spans:
                row[a:b] = [value] * (b - a)

    def pattern_rect(self, x0, y0, x1, y1, tile):
        """Add a repeating pattern of deltas (a list of rows) over a rectangle."""
        th, tw = len(tile), len(tile[0])
        for y in range(int(y0), int(y1)):
            row, prow = self.rows[y % self.h], tile[y % th]
            for a, b in self._spans(x0, x1):
                row[a:b] = [v + prow[x % tw] for x, v in zip(range(a, b), row[a:b])]

    def add(self, x, y, d):
        self.rows[int(y) % self.h][int(x) % self.w] += d

    def bevel(self, x0, y0, x1, y1, light, dark, width=1):
        """Raised edge: light top/left, dark bottom/right. Swap the signs for a recess."""
        for k in range(width):
            self.add_rect(x0 + k, y0 + k, x1 - k, y0 + k + 1, light)
            self.add_rect(x0 + k, y1 - k - 1, x1 - k, y1 - k, dark)
            self.add_rect(x0 + k, y0 + k + 1, x0 + k + 1, y1 - k - 1, light)
            self.add_rect(x1 - k - 1, y0 + k + 1, x1 - k, y1 - k - 1, dark)

    def bump(self, cx, cy, r, height):
        """Round bump such as a rivet or bolt head, lit from the top left. Negative height dimples."""
        ri = int(r) + 1
        for dy in range(-ri, ri + 1):
            for dx in range(-ri, ri + 1):
                d2 = dx * dx + dy * dy
                if d2 > r * r:
                    continue
                v = -height * (dx + dy) / (r * 1.5)
                if d2 > (r - 1.2) ** 2:
                    v -= abs(height) * 0.5  # dark rim
                self.add(cx + dx, cy + dy, v)

    def disc(self, cx, cy, r, d, soft=False):
        """Add d inside a circle; with soft, fading out towards the edge."""
        ri = int(r) + 1
        for dy in range(-ri, ri + 1):
            for dx in range(-ri, ri + 1):
                d2 = dx * dx + dy * dy
                if d2 <= r * r:
                    self.add(cx + dx, cy + dy, d * (1 - d2 / (r * r)) if soft else d)

    def line(self, x0, y0, x1, y1, d):
        steps = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for i in range(steps):
            t = i / max(1, steps - 1)
            self.add(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, d)

    def smooth_noise(self, cells, amount, rnd):
        """Low-frequency value noise: a cells x cells grid of random offsets, smoothly interpolated."""
        grid = [[rnd.uniform(-amount, amount) for _ in range(cells)] for _ in range(cells)]
        step = self.w / cells

        def weights(n):
            out = []
            for p in range(n):
                f = p / step
                i = int(f)
                t = f - i
                out.append((i % cells, (i + 1) % cells, t * t * (3 - 2 * t)))
            return out

        wx = weights(self.w)
        up = [[r[i] + (r[j] - r[i]) * t for i, j, t in wx] for r in grid]
        for y, (i, j, t) in enumerate(weights(self.h)):
            ra, rb, row = up[i], up[j], self.rows[y]
            row[:] = [v + a + (b - a) * t for v, a, b in zip(row, ra, rb)]

    def grain(self, amount, rnd):
        lut = [(n / 127.5 - 1.0) * amount for n in range(256)]
        for row in self.rows:
            row[:] = [v + lut[n] for v, n in zip(row, rnd.randbytes(self.w))]

    def save(self, path, ramp=None):
        """Save as grayscale, or as color through ramp: a function from 0..255 to an (r, g, b) tuple."""
        lut = [bytes(int(max(0, min(255, c))) for c in ramp(i)) for i in range(256)] if ramp else None
        raw = bytearray()
        for row in self.rows:
            raw.append(0)
            values = [0 if v < 0 else 255 if v > 255 else int(v + 0.5) for v in row]
            raw.extend(b"".join(lut[v] for v in values) if lut else bytes(values))

        def chunk(tag, data):
            return (struct.pack(">I", len(data)) + tag + data
                    + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

        png = b"\x89PNG\r\n\x1a\n"
        png += chunk(b"IHDR", struct.pack(">IIBBBBB", self.w, self.h, 8, 2 if lut else 0, 0, 0, 0))
        png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        png += chunk(b"IEND", b"")
        path.write_bytes(png)
        print(f"Wrote {path}")


def split_rects(rnd, x0, y0, x1, y1, min_size, out, depth=0, max_size=None):
    """Recursively subdivide a rectangle into panels, none larger than max_size if given."""
    w, h = x1 - x0, y1 - y0
    can_x = w >= 2 * min_size
    can_y = h >= 2 * min_size
    too_big = max_size is not None and max(w, h) > max_size
    if (not can_x and not can_y) or (not too_big and depth > 2 and rnd.random() < 0.25):
        out.append((x0, y0, x1, y1))
        return
    if can_x and (not can_y or (w > h if rnd.random() < 0.75 else rnd.random() < 0.5)):
        cut = x0 + min_size * rnd.randint(1, w // min_size - 1)
        split_rects(rnd, x0, y0, cut, y1, min_size, out, depth + 1, max_size)
        split_rects(rnd, cut, y0, x1, y1, min_size, out, depth + 1, max_size)
    else:
        cut = y0 + min_size * rnd.randint(1, h // min_size - 1)
        split_rects(rnd, x0, y0, x1, cut, min_size, out, depth + 1, max_size)
        split_rects(rnd, x0, cut, x1, y1, min_size, out, depth + 1, max_size)


# ---------------------------------------------------------------------------
# Textures
# ---------------------------------------------------------------------------

def hull(size=512):
    """Light hull plating: panels of slightly varying tone, seams, rivets and hatches."""
    rnd = random.Random(11)
    img = Image(size, size, (182, 184, 188))
    panels = []
    split_rects(rnd, 0, 0, size, size, 32, panels)
    for (x0, y0, x1, y1) in panels:
        tone = rnd.uniform(-12, 10)
        img.rect_add(x0, y0, x1, y1, tone)
        # Seam on the panel's top/left edge (wraps, so the tile stays seamless)
        for x in range(x0, x1):
            img.add(x, y0, -60)
            img.add(x, y0 + 1, 14)
        for y in range(y0, y1):
            img.add(x0, y, -60)
            img.add(x0 + 1, y, 14)

        w, h = x1 - x0, y1 - y0
        r = rnd.random()
        if r < 0.25 and w >= 64:
            # Rivet rows along the top and bottom of the panel
            for x in range(x0 + 6, x1 - 4, 10):
                for y in (y0 + 5, y1 - 5):
                    img.add(x, y, -35)
                    img.add(x + 1, y, -20)
        elif r < 0.40 and w >= 48 and h >= 48:
            # Small recessed hatch
            hx0, hy0 = x0 + w // 4, y0 + h // 4
            hx1, hy1 = x1 - w // 4, y1 - h // 4
            img.rect_add(hx0, hy0, hx1, hy1, -14)
            img.bevel(hx0, hy0, hx1, hy1, light=-30, dark=20)
        elif r < 0.50 and h >= 40:
            # Vent slots
            for y in range(y0 + 8, y1 - 6, 6):
                img.rect_add(x0 + 8, y, x1 - 8, y + 2, -45)
        elif r < 0.56:
            # Grime streak
            for y in range(y0, y1):
                for x in range(x0, x1):
                    img.add(x, y, -10 * (1 - (y - y0) / max(1, h)))
    img.grain(4, rnd)
    return img


def greeble(size=256):
    """Dense machinery: boxes, vents, pipes and status lights on a dark base."""
    rnd = random.Random(22)
    img = Image(size, size, (82, 84, 90))
    for _ in range(140):
        w, h = rnd.randint(6, 40), rnd.randint(6, 40)
        x0, y0 = rnd.randrange(size), rnd.randrange(size)
        g = rnd.randint(60, 150)
        img.rect(x0, y0, x0 + w, y0 + h, (g, g + 2, g + 6))
        img.bevel(x0, y0, x0 + w, y0 + h, light=30, dark=-40)
        kind = rnd.random()
        if kind < 0.25 and h > 10:
            for y in range(y0 + 3, y0 + h - 2, 4):
                img.rect_add(x0 + 2, y, x0 + w - 2, y + 1, -40)
        elif kind < 0.35 and w > 12 and h > 12:
            cx, cy, r = x0 + w // 2, y0 + h // 2, min(w, h) // 3
            for y in range(cy - r, cy + r + 1):
                for x in range(cx - r, cx + r + 1):
                    if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                        img.add(x, y, -50 + 8 * (x - cx))
        elif kind < 0.40:
            c = rnd.choice([(230, 120, 30), (220, 40, 40), (60, 200, 90)])
            img.rect(x0 + 2, y0 + 2, x0 + 5, y0 + 5, c)
    # Pipes
    for _ in range(14):
        horizontal = rnd.random() < 0.5
        p, t = rnd.randrange(size), rnd.randint(3, 6)
        a, b = rnd.randrange(size), rnd.randint(40, 200)
        for s in range(a, a + b):
            for k in range(t):
                shade = 120 + int(60 * math.sin(math.pi * (k + 0.5) / t)) - 40
                x, y = (s, p + k) if horizontal else (p + k, s)
                img.set(x, y, (shade, shade, shade + 4))
    img.grain(5, rnd)
    return img


def cargo(size=512):
    """Corrugated container side with frame, stencil blocks and a hazard band (tint via Kd)."""
    rnd = random.Random(33)
    img = Image(size, size, (225, 225, 225))
    for y in range(size):
        for x in range(size):
            c = 225 + 14 * math.sin(2 * math.pi * x / 16)
            img.set(x, y, (c, c, c))
    # Frame bands at the tile edges (v) and corner posts (u)
    img.rect_add(0, 0, size, 14, -55)
    img.rect_add(0, size - 14, size, size, -55)
    img.rect_add(0, 0, 10, size, -40)
    img.bevel(0, 14, size, size - 14, light=20, dark=-30)
    # Stencil-like marking blocks
    x = 40
    y = 60
    for _ in range(7):
        w, h = rnd.choice([(14, 40), (28, 14), (14, 14), (28, 40)])
        img.rect(x, y, x + w, y + h, (40, 40, 44))
        x += w + 8
    for i in range(12):
        img.rect(40 + i * 14, 120, 48 + i * 14, 128, (40, 40, 44))
    # Hazard band
    for y in range(size - 70, size - 40):
        for x in range(size):
            if ((x + y) // 12) % 2 == 0:
                img.set(x, y, (235, 175, 40))
            else:
                img.set(x, y, (35, 35, 35))
    img.grain(5, rnd)
    return img


def truss(size=128):
    """Brushed structural metal with lightening holes."""
    rnd = random.Random(44)
    img = Image(size, size, (128, 130, 134))
    for x in range(size):
        streak = rnd.uniform(-10, 10)
        for y in range(size):
            img.add(x, y, streak)
    for cy in range(16, size, 32):
        for y in range(cy - 6, cy + 7):
            for x in range(size // 2 - 6, size // 2 + 7):
                d = (x - size // 2) ** 2 + (y - cy) ** 2
                if d <= 36:
                    img.set(x, y, (30, 30, 34))
                elif d <= 52:
                    img.add(x, y, 25)
    img.rect_add(0, 0, 4, size, -35)
    img.rect_add(size - 4, 0, size, size, -35)
    img.grain(4, rnd)
    return img


def nozzle(size=256):
    """Engine bell interior: v runs from the exit (0) into the throat (1), getting hotter."""
    rnd = random.Random(55)
    img = Image(size, size)
    for y in range(size):
        t = y / (size - 1)
        # Steel -> bronze -> blue-violet heat tint
        if t < 0.5:
            k = t / 0.5
            base = (90 + 40 * k, 92 + 20 * k, 98 - 20 * k)
        else:
            k = (t - 0.5) / 0.5
            base = (130 - 60 * k, 112 - 50 * k, 78 + 80 * k)
        ring = -40 if y % 32 < 3 else 0
        for x in range(size):
            img.set(x, y, tuple(c + ring for c in base))
    for x in range(0, size, 8):
        for y in range(size):
            img.add(x, y, -12)
    img.grain(5, rnd)
    return img


def windows(w=256, h=64):
    """Window band: diffuse frame texture plus a matching emissive map of lit windows."""
    rnd = random.Random(66)
    diffuse = Image(w, h, (58, 60, 66))
    emit = Image(w, h, (0, 0, 0))
    cell = 16
    for cx in range(0, w, cell):
        lit = rnd.random() < 0.75
        warm = rnd.random() < 0.7
        level = rnd.uniform(0.55, 1.0)
        color = (255, 214, 150) if warm else (190, 225, 255)
        for y in range(14, h - 14):
            for x in range(cx + 3, cx + cell - 3):
                diffuse.set(x, y, (24, 30, 40))
                if lit:
                    emit.set(x, y, tuple(c * level for c in color))
        diffuse.bevel(cx + 3, 14, cx + cell - 3, h - 14, light=-20, dark=30)
    diffuse.rect_add(0, 0, w, 4, 30)
    diffuse.rect_add(0, h - 4, w, h, -25)
    diffuse.grain(3, rnd)
    return diffuse, emit


def solar(size=256):
    """Photovoltaic panel: dark blue cells with silver bus bars, 4x4 cells per tile."""
    rnd = random.Random(77)
    img = Image(size, size, (190, 192, 198))
    cell = size // 4
    for cy in range(0, size, cell):
        for cx in range(0, size, cell):
            tone = rnd.uniform(-6, 6)
            for y in range(cy + 3, cy + cell - 3):
                for x in range(cx + 3, cx + cell - 3):
                    sheen = 18 * (1 - ((x - cx) + (y - cy)) / (2 * cell))
                    img.set(x, y, (18 + tone + sheen, 32 + tone + sheen, 78 + tone + sheen * 1.5))
            # Thin vertical bus lines inside each cell
            for x in range(cx + cell // 4, cx + cell - 3, cell // 4):
                for y in range(cy + 3, cy + cell - 3):
                    img.set(x, y, (120, 125, 140))
    img.grain(3, rnd)
    return img


def greenhouse(size=256):
    """Greenhouse glazing: mullion grid over lit, mottled plant beds (also used as emissive map)."""
    rnd = random.Random(88)
    img = Image(size, size, (60, 64, 70))
    pane = size // 4
    blobs = [(rnd.randrange(size), rnd.randrange(size), rnd.randint(6, 22)) for _ in range(160)]
    for y in range(size):
        for x in range(size):
            if x % pane < 4 or y % pane < 4:
                continue
            g = 0.35
            for bx, by, br in blobs:
                dx = min(abs(x - bx), size - abs(x - bx))
                dy = min(abs(y - by), size - abs(y - by))
                if dx * dx + dy * dy < br * br:
                    g += 0.25
            g = min(g, 1.0)
            img.set(x, y, (40 + 60 * g, 110 + 130 * g, 50 + 50 * g))
    img.grain(4, rnd)
    return img


# ---------------------------------------------------------------------------
# High-detail station hub textures (16 world units per tile, 128 px per unit)
# ---------------------------------------------------------------------------

# 3x5 stencil font for the painted markings
FONT = {
    "0": "111101101101111", "1": "010110010010111", "2": "111001111100111", "3": "111001111001111",
    "4": "101101111001001", "5": "111100111001111", "6": "111100111101111", "7": "111001001001001",
    "8": "111101111101111", "9": "111101111001111", "A": "111101111101101", "B": "110101110101110",
    "C": "111100100100111", "D": "110101101101110", "E": "111100111100111", "H": "101101111101101",
    "I": "111010010010111", "K": "101101110101101", "L": "100100100100111", "N": "111101101101101",
    "O": "111101101101111", "P": "111101111100100", "R": "111101110101101", "S": "111100111001111",
    "T": "111010010010010", "V": "101101101101010", "W": "101101111111101", "X": "101101010101101",
    "Y": "101101010010010", "-": "000000111000000",
}
MARKINGS = ["NO STEP", "AIRLOCK", "DECK", "PWR", "H2O", "LOX", "VENT", "SEC", "BAY", "KEEP CLEAR",
            "ACCESS", "LOCK", "HI-V", "N2"]


def progress(name, step):
    print(f"  {name}: {step}", flush=True)


def marking(rnd):
    text = rnd.choice(MARKINGS)
    if rnd.random() < 0.6:
        text += " " + rnd.choice(["", "", "A", "B", "C"]) + str(rnd.randint(1, 99))
    return text


def stencil(img, x, y, text, gs, d):
    """Paint text in the stencil font; gs is the size of one font cell in pixels."""
    for ch in text:
        bits = FONT.get(ch)
        if bits:
            for i, b in enumerate(bits):
                if b == "1":
                    cx, cy = i % 3, i // 3
                    img.add_rect(x + cx * gs, y + cy * gs, x + (cx + 1) * gs, y + (cy + 1) * gs, d)
        x += 4 * gs


def corner_screws(img, x0, y0, x1, y1, inset, r):
    for x, y in ((x0 + inset, y0 + inset), (x1 - inset, y0 + inset),
                 (x0 + inset, y1 - inset), (x1 - inset, y1 - inset)):
        img.bump(x, y, r, 22)
        img.add_rect(x - r + 1, y, x + r, y + 1, -30)  # screw slot


def seam(img, x0, y0, x1, y1, profile):
    """Groove along a panel's top and left edges, which it shares with its neighbors."""
    n = len(profile)
    for k, d in enumerate(profile):
        img.add_rect(x0, y0 + k, x1, y0 + k + 1, d)
        img.add_rect(x0 + k, y0 + n, x0 + k + 1, y1, d)


def wear(img, rnd, size, streaks, scratches, scuffs):
    """Grime streaks running down, scratches and soft scuffs."""
    for _ in range(streaks):
        x, y = rnd.randrange(size), rnd.randrange(size)
        length, half, amount = rnd.randint(60, 360), rnd.uniform(3, 12), rnd.uniform(3, 7)
        for dx in range(-int(half), int(half) + 1):
            across = (1 - abs(dx) / (half + 1)) ** 2
            for k in range(length):
                img.add(x + dx, y + k, -amount * across * (1 - k / length))
    for _ in range(scratches):
        x, y = rnd.randrange(size), rnd.randrange(size)
        a, length = rnd.uniform(0, math.tau), rnd.randint(8, 70)
        img.line(x, y, x + math.cos(a) * length, y + math.sin(a) * length, rnd.choice([-14, -9, 10, 16]))
    for _ in range(scuffs):
        img.disc(rnd.randrange(size), rnd.randrange(size), rnd.randint(10, 40), -6, soft=True)


def hub_plating(size=2048):
    """Station hub plating, seen from close by: plates split into panels with grooved seams, and
    rivet rows, hatches, vent grilles, access covers, stencilled markings, warning stripes, weld
    lines, scratches and grime."""
    rnd = random.Random(101)
    img = Gray(size, 170)
    progress("hub_plating", "base")
    img.smooth_noise(4, 7, rnd)
    img.smooth_noise(16, 4, rnd)

    plates = []
    split_rects(rnd, 0, 0, size, size, 256, plates, max_size=768)
    panels = []
    for p in plates:
        if rnd.random() < 0.5:
            split_rects(rnd, *p, 128, panels)
        else:
            panels.append(p)

    progress("hub_plating", f"{len(panels)} panels")
    for (x0, y0, x1, y1) in panels:
        img.add_rect(x0, y0, x1, y1, rnd.uniform(-12, 9))
        seam(img, x0, y0, x1, y1, (-50, -60, -45, -20, 14, 6))
        x0, y0 = x0 + 6, y0 + 6  # the panel's face, inside the seam
        w, h = x1 - x0, y1 - y0
        r = rnd.random()
        if r < 0.28 and min(w, h) >= 96:
            # Rivet rows along the edges
            sp = rnd.choice([18, 22, 26])
            for x in range(x0 + 10, x1 - 8, sp):
                img.bump(x, y0 + 10, 3.2, 26)
                img.bump(x, y1 - 10, 3.2, 26)
            for y in range(y0 + 10 + sp, y1 - 8 - sp // 2, sp):
                img.bump(x0 + 10, y, 3.2, 26)
                img.bump(x1 - 10, y, 3.2, 26)
            continue
        if r < 0.40 and w >= 160 and h >= 160:
            # Recessed hatch with a raised frame, hinges and a handle
            mx, my = int(w * rnd.uniform(0.18, 0.28)), int(h * rnd.uniform(0.18, 0.28))
            hx0, hy0, hx1, hy1 = x0 + mx, y0 + my, x1 - mx, y1 - my
            img.bevel(hx0 - 7, hy0 - 7, hx1 + 7, hy1 + 7, 16, -22, 2)
            img.add_rect(hx0, hy0, hx1, hy1, -9)
            img.bevel(hx0, hy0, hx1, hy1, -32, 22, 3)
            for hy in (hy0 + (hy1 - hy0) // 4, hy1 - (hy1 - hy0) // 4):
                img.add_rect(hx0 - 10, hy - 14, hx0 + 8, hy + 14, 18)
                img.bevel(hx0 - 10, hy - 14, hx0 + 8, hy + 14, 20, -30)
            cy = (hy0 + hy1) // 2
            img.add_rect(hx1 - 30, cy - 20, hx1 - 16, cy + 20, -40)
            img.bevel(hx1 - 30, cy - 20, hx1 - 16, cy + 20, -25, 20)
            if rnd.random() < 0.5 and hx1 - hx0 > 140:
                stencil(img, hx0 + 18, hy0 + 16, marking(rnd)[:(hx1 - hx0 - 60) // 16], 4, -70)
        elif r < 0.50 and min(w, h) >= 96:
            # Vent grille
            gx0, gx1, gy0, gy1 = x0 + 20, x1 - 20, y0 + 20, min(y1 - 20, y0 + 20 + rnd.randint(60, 200))
            img.bevel(gx0 - 5, gy0 - 5, gx1 + 5, gy1 + 5, 18, -26, 2)
            for y in range(gy0, gy1 - 8, 14):
                img.add_rect(gx0, y, gx1, y + 7, -70)
                img.add_rect(gx0, y + 7, gx1, y + 9, 20)
        elif r < 0.60 and w >= 128 and h >= 96:
            # A row of small access covers, each held by four screws
            cs = rnd.choice([48, 56, 64])
            n = max(1, (w - 32) // (cs + 16))
            cy0 = y0 + (h - cs) // 2
            for i in range(n):
                cx0 = x0 + 16 + i * (cs + 16)
                img.add_rect(cx0, cy0, cx0 + cs, cy0 + cs, 6)
                img.bevel(cx0, cy0, cx0 + cs, cy0 + cs, 22, -30, 2)
                corner_screws(img, cx0, cy0, cx0 + cs, cy0 + cs, 9, 3)
        elif r < 0.70 and w >= 160 and h >= 64:
            # Stencilled marking, sometimes on a painted plate
            gs = rnd.choice([4, 5, 6])
            text = marking(rnd)
            tw, th = len(text) * 4 * gs, 5 * gs
            if tw + 48 < w and th + 40 < h:
                tx = x0 + rnd.randint(20, w - tw - 24)
                ty = y0 + rnd.randint(20, h - th - 20)
                if rnd.random() < 0.5:
                    img.add_rect(tx - 10, ty - 10, tx + tw + 6, ty + th + 10, 45)
                    img.bevel(tx - 10, ty - 10, tx + tw + 6, ty + th + 10, 10, -20)
                    stencil(img, tx, ty, text, gs, -110)
                else:
                    stencil(img, tx, ty, text, gs, rnd.choice([-70, 55]))
        elif r < 0.76 and w >= 128 and h >= 80:
            # Warning stripes along the bottom edge
            by = y1 - 50
            for y in range(by, by + 36):
                for x in range(x0 + 12, x1 - 12):
                    img.add(x, y, -55 if ((x - y) // 16) % 2 == 0 else 25)
        elif r < 0.82 and w >= 96:
            # Weld bead across the panel
            y = y0 + rnd.randint(20, max(20, h - 20))
            img.add_rect(x0 + 6, y - 6, x1 - 6, y - 4, -12)
            img.add_rect(x0 + 6, y + 5, x1 - 6, y + 7, -12)
            for x in range(x0 + 8, x1 - 8, 3):
                img.disc(x, y + rnd.uniform(-1, 1), 3.5, rnd.uniform(5, 10), soft=True)
        if min(w, h) >= 64 and rnd.random() < 0.5:
            corner_screws(img, x0, y0, x1, y1, 10, 4)

    progress("hub_plating", "wear")
    wear(img, rnd, size, streaks=90, scratches=500, scuffs=80)
    img.grain(3, rnd)
    return img


def tread_tile(cell=32):
    """Tread (diamond) plate: raised lozenges, turning direction in a checkerboard, lit from the top left."""
    n = cell * 2
    tile = [[0.0] * n for _ in range(n)]
    for y in range(n):
        for x in range(n):
            u = x - (x // cell) * cell - cell / 2 + 0.5
            v = y - (y // cell) * cell - cell / 2 + 0.5
            along, across = (u + v) * 0.7071, (u - v) * 0.7071
            if ((x // cell) + (y // cell)) % 2:
                along, across = across, along
            if (along / (cell * 0.36)) ** 2 + (across / (cell * 0.1)) ** 2 <= 1:
                tile[y][x] = 12 - 44 * (u + v) / cell
    return tile


def tie_down(img, cx, cy):
    """Recessed tie-down cup with a bar across it."""
    img.disc(cx, cy, 20, -38)
    for a in range(0, 360, 3):
        t = math.radians(a)
        shade = 18 * math.cos(t - math.radians(45))  # dark top-left rim, lit bottom-right
        img.add(cx + math.cos(t) * 20.5, cy + math.sin(t) * 20.5, shade)
    img.add_rect(cx - 14, cy - 3, cx + 14, cy + 4, 34)
    img.bevel(cx - 14, cy - 3, cx + 14, cy + 4, 16, -24)


def hangar_deck(size=2048):
    """Hangar deck plating: a regular grid of heavy plates, some of them tread plate, with bolts,
    tie-down points, drain strips, painted guide lines, tyre marks, scorch marks and scratches."""
    rnd = random.Random(202)
    img = Gray(size, 128)
    progress("hangar_deck", "base")
    img.smooth_noise(4, 8, rnd)
    img.smooth_noise(16, 5, rnd)
    tread = tread_tile()
    plate = size // 4  # 4 world units

    progress("hangar_deck", "plates")
    for py in range(0, size, plate):
        for px in range(0, size, plate):
            img.add_rect(px, py, px + plate, py + plate, rnd.uniform(-10, 10))
            split = rnd.random() < 0.35
            if split:
                half = plate // 2
                subs = [(px + i * half, py + j * half, px + (i + 1) * half, py + (j + 1) * half)
                        for i in (0, 1) for j in (0, 1)]
            else:
                subs = [(px, py, px + plate, py + plate)]
            for (x0, y0, x1, y1) in subs:
                img.add_rect(x0, y0, x1, y1, rnd.uniform(-6, 6))
                seam(img, x0, y0, x1, y1, (-55, -70, -70, -50, -25, 16, 8))
                if rnd.random() < 0.3:
                    img.pattern_rect(x0 + 30, y0 + 30, x1 - 22, y1 - 22, tread)
                    img.bevel(x0 + 26, y0 + 26, x1 - 18, y1 - 18, 18, -26, 2)
                for x in range(x0 + 24, x1 - 12, 40):
                    img.bump(x, y0 + 16, 4, -18)
                for y in range(y0 + 56, y1 - 12, 40):
                    img.bump(x0 + 16, y, 4, -18)
            if not split and rnd.random() < 0.7:
                tie_down(img, px + plate // 2, py + plate // 2)

    progress("hangar_deck", "drains, paint and marks")
    # Drain strips along some of the plate seams
    for _ in range(5):
        x = rnd.randrange(4) * plate + 8
        y0 = rnd.randrange(4) * plate
        for y in range(y0, y0 + plate * rnd.randint(1, 3), 12):
            img.add_rect(x, y + 3, x + 14, y + 9, -60)
            img.add_rect(x, y + 9, x + 14, y + 10, 16)

    # Painted guide lines, worn
    for _ in range(2):
        horizontal = rnd.random() < 0.5
        pos = rnd.randrange(4) * plate + rnd.choice([40, plate // 2])
        start, length = rnd.randrange(size), plate * rnd.randint(1, 3)
        x0, y0, x1, y1 = (start, pos, start + length, pos + 28) if horizontal else (pos, start, pos + 28, start + length)
        img.add_rect(x0, y0, x1, y1, 55)
        for _ in range(length // 4):
            t = rnd.randrange(length)
            x, y = (x0 + t, y0 + rnd.randrange(28)) if horizontal else (x0 + rnd.randrange(28), y0 + t)
            img.disc(x, y, rnd.uniform(1.5, 5), -60, soft=True)

    # Tyre marks and scorch marks
    for _ in range(24):
        x, y = rnd.uniform(0, size), rnd.uniform(0, size)
        a, turn = rnd.uniform(0, math.tau), rnd.uniform(-0.004, 0.004)
        r = rnd.uniform(10, 18)
        for _ in range(rnd.randint(30, 110)):
            img.disc(x, y, r, -1.6, soft=True)
            x, y, a = x + math.cos(a) * 4, y + math.sin(a) * 4, a + turn
    for _ in range(6):
        img.disc(rnd.randrange(size), rnd.randrange(size), rnd.randint(80, 200), -18, soft=True)

    progress("hangar_deck", "wear")
    wear(img, rnd, size, streaks=0, scratches=700, scuffs=120)
    img.grain(3, rnd)
    return img


def hazard(w=256, h=64):
    """Yellow and black diagonal warning stripes with worn spots (drawn in color, used untinted)."""
    rnd = random.Random(303)
    img = Image(w, h)
    for y in range(h):
        for x in range(w):
            img.set(x, y, (232, 178, 28) if ((x + y) // 32) % 2 == 0 else (34, 34, 36))
    for _ in range(90):
        cx, cy, r = rnd.randrange(w), rnd.randrange(h), rnd.randint(1, 4)
        for y in range(cy - r, cy + r + 1):
            for x in range(cx - r, cx + r + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r and rnd.random() < 0.7:
                    img.set(x, y, (118, 120, 124))
    img.rect_add(0, 0, w, 3, 25)
    img.rect_add(0, h - 3, w, h, -35)
    img.grain(6, rnd)
    return img


# ---------------------------------------------------------------------------
# Effects
# ---------------------------------------------------------------------------

def forcefield(size=512):
    """Force field energy: ridged noise, so the bright parts are thin wandering filaments over a
    faint haze. Black means transparent, since the field is blended additively."""
    rnd = random.Random(404)
    img = Gray(size, 0)
    for cells, amount in ((8, 50), (16, 38), (32, 20), (64, 10)):
        img.smooth_noise(cells, amount, rnd)
    haze = Gray(size, 0)
    haze.smooth_noise(8, 1, rnd)
    for row, hrow in zip(img.rows, haze.rows):
        row[:] = [255 * (0.12 * (h + 1) * 0.5 + 0.88 * max(0.0, 1 - abs(v) / 28) ** 2.5)
                  for v, h in zip(row, hrow)]
    img.grain(6, rnd)
    return img


def forcefield_ramp(v):
    """Black through deep blue and cyan to white."""
    t = v / 255
    return (30 * t + 225 * t ** 4, 70 * t + 185 * t ** 2.5, 255 * min(1.0, 1.5 * t))


# ---------------------------------------------------------------------------

def save_windows():
    d, e = windows()
    d.save(OUT / "windows.png")
    e.save(OUT / "windows_emit.png")


TEXTURES = {
    "hull": lambda: hull().save(OUT / "hull.png"),
    "greeble": lambda: greeble().save(OUT / "greeble.png"),
    "cargo": lambda: cargo().save(OUT / "cargo.png"),
    "truss": lambda: truss().save(OUT / "truss.png"),
    "nozzle": lambda: nozzle().save(OUT / "nozzle.png"),
    "windows": save_windows,
    "solar": lambda: solar().save(OUT / "solar.png"),
    "greenhouse": lambda: greenhouse().save(OUT / "greenhouse.png"),
    "hub_plating": lambda: hub_plating().save(OUT / "hub_plating.png"),
    "hangar_deck": lambda: hangar_deck().save(OUT / "hangar_deck.png"),
    "hazard": lambda: hazard().save(OUT / "hazard.png"),
    "forcefield": lambda: forcefield().save(OUT_EFFECTS / "forcefield.png", forcefield_ramp),
}


def main():
    """Generate all textures, or only the ones named on the command line."""
    names = sys.argv[1:] or list(TEXTURES)
    unknown = [n for n in names if n not in TEXTURES]
    if unknown:
        sys.exit(f"Unknown texture(s): {', '.join(unknown)}. Choose from: {', '.join(TEXTURES)}")
    OUT.mkdir(parents=True, exist_ok=True)
    OUT_EFFECTS.mkdir(parents=True, exist_ok=True)
    for name in names:
        start = time.time()
        TEXTURES[name]()
        print(f"  {name} took {time.time() - start:.1f} s", flush=True)


if __name__ == "__main__":
    main()
