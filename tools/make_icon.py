"""Generate res/app.ico - flat blue rounded square with a white clock face."""
import math
import os
import struct
import sys
import zlib

BG = (0x4A, 0x90, 0xE2)
FG = (255, 255, 255)
SS = 4


def seg_dist(px, py, x0, y0, x1, y1):
    vx, vy = x1 - x0, y1 - y0
    l2 = vx * vx + vy * vy
    if l2 <= 0:
        return math.hypot(px - x0, py - y0)
    t = ((px - x0) * vx + (py - y0) * vy) / l2
    t = max(0.0, min(1.0, t))
    return math.hypot(px - (x0 + vx * t), py - (y0 + vy * t))


def inside_round(x, y, s, r):
    cx = cy = s / 2.0
    qx = abs(x - cx) - (s / 2.0 - r)
    qy = abs(y - cy) - (s / 2.0 - r)
    return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r < 0.0


def render(s):
    r = 0.22 * s
    cx = cy = s / 2.0
    ring_r = 0.285 * s
    ring_t = max(0.085 * s, 1.15)
    hand_t = max(0.075 * s, 1.1)

    out = bytearray(s * s * 4)
    sub = SS * SS
    for j in range(s):
        for i in range(s):
            sa = sr = sg = sb = 0.0
            for sy in range(SS):
                for sx in range(SS):
                    x = i + (sx + 0.5) / SS
                    y = j + (sy + 0.5) / SS
                    if not inside_round(x, y, s, r):
                        continue
                    a = 1.0
                    col = BG
                    d_ring = abs(math.hypot(x - cx, y - cy) - ring_r) - ring_t / 2.0
                    d_h1 = seg_dist(x, y, cx, cy, cx, cy - 0.155 * s) - hand_t / 2.0
                    d_h2 = seg_dist(x, y, cx, cy, cx + 0.125 * s, cy + 0.075 * s) - hand_t / 2.0
                    d_dot = math.hypot(x - cx, y - cy) - 0.042 * s
                    if min(d_ring, d_h1, d_h2, d_dot) < 0.0:
                        col = FG
                    sa += a
                    sr += col[0] * a
                    sg += col[1] * a
                    sb += col[2] * a
            o = (j * s + i) * 4
            if sa > 0:
                out[o + 0] = int(round(sb / sa))
                out[o + 1] = int(round(sg / sa))
                out[o + 2] = int(round(sr / sa))
                out[o + 3] = int(round(255.0 * sa / sub))
            else:
                out[o + 0] = 0
                out[o + 1] = 0
                out[o + 2] = 0
                out[o + 3] = 0
    return bytes(out)


def png(w, h, rgba):
    rows = []
    for y in range(h):
        rows.append(b"\x00" + rgba[y * w * 4:(y + 1) * w * 4])
    raw = b"".join(rows)

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def bmp32(w, h, rgba):
    hdr = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, w * h * 4, 0, 0, 0, 0)
    px = bytearray()
    for y in range(h - 1, -1, -1):
        px += rgba[y * w * 4:(y + 1) * w * 4]
    mask_row = ((w + 31) // 32) * 4
    return hdr + bytes(px) + bytes(mask_row * h)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    dst = os.path.join(root, "res", "app.ico")
    sizes = [16, 24, 32, 48, 128, 256]
    images = []
    for s in sizes:
        rgba = render(s)
        data = png(s, s, rgba) if s >= 128 else bmp32(s, s, rgba)
        images.append((s, data))
        print("size %3d -> %6d bytes" % (s, len(data)))

    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries = b""
    blob = b""
    for s, data in images:
        entries += struct.pack("<BBBBHHII", 0 if s >= 256 else s, 0 if s >= 256 else s,
                               0, 0, 1, 32, len(data), offset)
        offset += len(data)
        blob += data
    with open(dst, "wb") as f:
        f.write(header + entries + blob)
    print("written", dst, os.path.getsize(dst), "bytes")


if __name__ == "__main__":
    sys.exit(main())
