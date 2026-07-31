#!/usr/bin/env python3
"""Generates a minimal VobSub (.idx + .sub) subtitle pair with one bitmap cue.

The cue is a solid rectangle displayed from 0.2s to 1.8s. Only the Python
standard library is used. The output is meant to be muxed into a Matroska
container by ffmpeg (vobsub demuxer reads the .idx and finds the .sub next
to it).
"""
import struct
import sys

# Display canvas and rectangle geometry (DVD-style coordinates).
CANVAS_W, CANVAS_H = 720, 480
RECT_X, RECT_Y = 300, 400
RECT_W, RECT_H = 120, 40

START_PTS = int(0.2 * 90000)  # presentation start, 90 kHz clock
DURATION_TICKS = int(1.6 * 90000 / 1024)  # display duration, SPU delay units


def rle_line(color):
    """One scanline: a single run-to-end-of-line of `color` (2 bytes)."""
    return bytes([0x00, color])


def build_spu():
    # RLE data: interlaced even/odd fields, each covering half the lines.
    even = b"".join(rle_line(1) for _ in range((RECT_H + 1) // 2))
    odd = b"".join(rle_line(1) for _ in range(RECT_H // 2))
    rle = even + odd
    rle_start = 4  # after the 2+2 byte SPU header
    even_ofs = rle_start
    odd_ofs = rle_start + len(even)

    ctrl_ofs = rle_start + len(rle)
    x2, y2 = RECT_X + RECT_W - 1, RECT_Y + RECT_H - 1

    # Control sequence 1 (at t=0): set colors/alpha/area/data offsets, start.
    seq1 = bytearray()
    seq1 += struct.pack(">H", 0)  # delay
    seq1 += b"\x00\x00"  # placeholder for next-seq offset
    seq1 += bytes([0x03, 0x01, 0x23])  # SET_COLOR: palette idx per color
    seq1 += bytes([0x04, 0xFF, 0xF0])  # SET_CONTR: alpha (colors 3,2,1 opaque)
    seq1 += bytes(
        [
            0x05,
            RECT_X >> 4,
            ((RECT_X & 0xF) << 4) | (x2 >> 8),
            x2 & 0xFF,
            RECT_Y >> 4,
            ((RECT_Y & 0xF) << 4) | (y2 >> 8),
            y2 & 0xFF,
        ]
    )  # SET_DAREA
    seq1 += bytes([0x06]) + struct.pack(">HH", even_ofs, odd_ofs)  # SET_DSPXA
    seq1 += b"\x01"  # START_DISPLAY
    seq1 += b"\xff"  # end of sequence
    if len(seq1) % 2:
        seq1 += b"\xff"

    seq2_ofs = ctrl_ofs + len(seq1)
    # Control sequence 2 (at t=duration): stop display. Points at itself.
    seq2 = struct.pack(">H", DURATION_TICKS) + struct.pack(">H", seq2_ofs)
    seq2 += b"\x02\xff"  # STOP_DISPLAY, end

    struct.pack_into(">H", seq1, 2, seq2_ofs)

    total = 4 + len(rle) + len(seq1) + len(seq2)
    spu = struct.pack(">HH", total, ctrl_ofs) + rle + bytes(seq1) + seq2
    return spu


def pes_packets(spu, pts):
    """Wraps the SPU into MPEG-2 PS pack + PES (private stream 1, substream 0x20)."""
    out = b""
    first = True
    pos = 0
    while pos < len(spu):
        chunk = spu[pos : pos + 2010]
        pos += len(chunk)
        # PS pack header (MPEG-2 style, SCR=0, mux rate arbitrary).
        pack = bytes.fromhex("000001ba44000400040104808c63f8")
        if first:
            flags, hdrlen = 0x80, 5  # PTS only
            pts_val = pts
            pes_hdr = bytes(
                [
                    0x21 | ((pts_val >> 29) & 0x0E),
                    (pts_val >> 22) & 0xFF,
                    0x01 | ((pts_val >> 14) & 0xFE),
                    (pts_val >> 7) & 0xFF,
                    0x01 | ((pts_val << 1) & 0xFE),
                ]
            )
        else:
            flags, hdrlen = 0x00, 0
            pes_hdr = b""
        payload = bytes([0x20]) + chunk  # substream id + data
        pes_len = 3 + hdrlen + len(payload)
        pes = (
            b"\x00\x00\x01\xbd"
            + struct.pack(">H", pes_len)
            + bytes([0x81, flags, hdrlen])
            + pes_hdr
            + payload
        )
        out += pack + pes
        first = False
    return out


def main(base):
    spu = build_spu()
    sub = pes_packets(spu, START_PTS)
    with open(base + ".sub", "wb") as f:
        f.write(sub)
    idx = f"""# VobSub index file, v7 (do not modify this line!)
size: {CANVAS_W}x{CANVAS_H}
org: 0, 0
scale: 100%, 100%
alpha: 100%
smooth: OFF
fadein/out: 0, 0
align: OFF at LEFT TOP
time offset: 0
forced subs: OFF
palette: 000000, ffffff, ff0000, 00ff00, 0000ff, ffff00, 00ffff, ff00ff, 808080, c0c0c0, 400040, 004040, 404000, 804080, 408040, 808040
custom colors: OFF, tridx: 0000, colors: 000000, 000000, 000000, 000000
langidx: 0
id: en, index: 0
timestamp: 00:00:00:200, filepos: 000000000
"""
    with open(base + ".idx", "w") as f:
        f.write(idx)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "vobsub")
