"""Generate assets/textures/crate.png: a sci-fi cargo crate panel (stdlib only)."""

import math
import random
import struct
import zlib
from pathlib import Path

SIZE = 256
BORDER = 22
RIVET_R = 5

random.seed(1234)


def clamp(x):
    return max(0, min(255, int(x)))


def write_png(path, width, height, rows):
    raw = b"".join(b"\x00" + bytes(row) for row in rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def pixel(x, y):
    base = (70, 78, 92)          # dark blue-grey metal
    frame = (150, 155, 165)      # lighter frame
    brace = (110, 116, 128)
    hazard = (215, 160, 40)

    noise = random.uniform(-6, 6)
    c = base

    # Horizontal brushed-metal streaks
    c = tuple(v + 4 * math.sin(y * 0.9 + math.sin(x * 0.05) * 3) for v in c)

    inner = BORDER <= x < SIZE - BORDER and BORDER <= y < SIZE - BORDER

    # Diagonal braces (X) inside the frame
    if inner:
        d1 = abs(x - y)
        d2 = abs(x + y - (SIZE - 1))
        if d1 < 9 or d2 < 9:
            c = brace
            if d1 in (8, 7) or d2 in (8, 7):
                c = tuple(v * 0.6 for v in brace)

        # Hazard stripe band across the middle
        if abs(y - SIZE // 2) < 10 and abs(x - SIZE // 2) > 30:
            c = hazard if ((x + y) // 8) % 2 == 0 else (30, 30, 30)
    else:
        c = frame
        # Bevel: light on top/left edges, dark on bottom/right
        if x < 3 or y < 3:
            c = tuple(v * 1.25 for v in frame)
        elif x >= SIZE - 3 or y >= SIZE - 3:
            c = tuple(v * 0.55 for v in frame)
        elif (BORDER - 3 <= x < BORDER or BORDER - 3 <= y < BORDER) and not (
            x >= SIZE - BORDER or y >= SIZE - BORDER
        ):
            c = tuple(v * 0.55 for v in frame)
        elif SIZE - BORDER <= x < SIZE - BORDER + 3 or SIZE - BORDER <= y < SIZE - BORDER + 3:
            c = tuple(v * 1.2 for v in frame)

    # Rivets at the frame corners and edge midpoints
    m = BORDER // 2
    for rx in (m, SIZE // 2, SIZE - 1 - m):
        for ry in (m, SIZE // 2, SIZE - 1 - m):
            if rx == ry == SIZE // 2:
                continue
            if not (rx in (m, SIZE - 1 - m) or ry in (m, SIZE - 1 - m)):
                continue
            dx, dy = x - rx, y - ry
            r2 = dx * dx + dy * dy
            if r2 <= RIVET_R * RIVET_R:
                shade = 1.3 - 0.12 * (dx + dy)
                c = tuple(v * shade for v in (120, 124, 132))

    return tuple(clamp(v + noise) for v in c)


def main():
    out = Path(__file__).resolve().parent.parent / "assets" / "textures" / "crate.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            row.extend(pixel(x, y))
        rows.append(row)
    write_png(out, SIZE, SIZE, rows)
    print(f"Wrote {out}")


if __name__ == "__main__":
    main()
