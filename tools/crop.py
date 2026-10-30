"""Crop / downscale a 32bpp BMP screenshot so it can be inspected quickly."""
import struct
import sys


def load(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:2] == b"BM", "not a BMP"
    off = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    assert bpp == 32, "expect 32bpp"
    topdown = h < 0
    h = abs(h)
    rows = []
    stride = w * 4
    for y in range(h):
        start = off + y * stride
        rows.append(data[start:start + stride])
    if not topdown:
        rows.reverse()
    return w, h, rows


def save(path, w, h, rows):
    hdr = bytearray(54)
    hdr[0:2] = b"BM"
    struct.pack_into("<I", hdr, 2, 54 + w * h * 4)
    struct.pack_into("<I", hdr, 10, 54)
    struct.pack_into("<I", hdr, 14, 40)
    struct.pack_into("<i", hdr, 18, w)
    struct.pack_into("<i", hdr, 22, -h)
    struct.pack_into("<H", hdr, 26, 1)
    struct.pack_into("<H", hdr, 28, 32)
    struct.pack_into("<I", hdr, 34, w * h * 4)
    with open(path, "wb") as f:
        f.write(hdr)
        for r in rows:
            f.write(r)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    x0, y0, x1, y1 = (int(v) for v in sys.argv[3:7])
    step = int(sys.argv[7]) if len(sys.argv) > 7 else 1
    w, h, rows = load(src)
    x1 = min(x1, w)
    y1 = min(y1, h)
    out = []
    for y in range(y0, y1, step):
        src_row = rows[y]
        row = bytearray()
        for x in range(x0, x1, step):
            # average step x step block
            r = g = b = 0
            n = 0
            for yy in range(y, min(y + step, y1)):
                sr = rows[yy]
                for xx in range(x, min(x + step, x1)):
                    i = xx * 4
                    b += sr[i]
                    g += sr[i + 1]
                    r += sr[i + 2]
                    n += 1
            row += bytes((b // n, g // n, r // n, 255))
        out.append(bytes(row))
    ow = (x1 - x0 + step - 1) // step
    oh = (y1 - y0 + step - 1) // step
    save(dst, ow, oh, out)
    print("wrote %s %dx%d" % (dst, ow, oh))


if __name__ == "__main__":
    main()
