"""Generate the spacecraft textures in assets/textures/ship/ (stdlib only).

All textures tile seamlessly. Light, neutral base colors are used where the generator
tints materials (hull, cargo) through the material's diffuse color.
"""

import math
import random
import struct
import zlib
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "assets" / "textures" / "ship"


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


def split_rects(rnd, x0, y0, x1, y1, min_size, out, depth=0):
    """Recursively subdivide a rectangle into panels."""
    w, h = x1 - x0, y1 - y0
    can_x = w >= 2 * min_size
    can_y = h >= 2 * min_size
    if (not can_x and not can_y) or (depth > 2 and rnd.random() < 0.25):
        out.append((x0, y0, x1, y1))
        return
    if can_x and (not can_y or (w > h if rnd.random() < 0.75 else rnd.random() < 0.5)):
        cut = x0 + min_size * rnd.randint(1, w // min_size - 1)
        split_rects(rnd, x0, y0, cut, y1, min_size, out, depth + 1)
        split_rects(rnd, cut, y0, x1, y1, min_size, out, depth + 1)
    else:
        cut = y0 + min_size * rnd.randint(1, h // min_size - 1)
        split_rects(rnd, x0, y0, x1, cut, min_size, out, depth + 1)
        split_rects(rnd, x0, cut, x1, y1, min_size, out, depth + 1)


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


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    hull().save(OUT / "hull.png")
    greeble().save(OUT / "greeble.png")
    cargo().save(OUT / "cargo.png")
    truss().save(OUT / "truss.png")
    nozzle().save(OUT / "nozzle.png")
    d, e = windows()
    d.save(OUT / "windows.png")
    e.save(OUT / "windows_emit.png")
    solar().save(OUT / "solar.png")
    greenhouse().save(OUT / "greenhouse.png")


if __name__ == "__main__":
    main()
